#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "errno.h"
#include "fuzzy_match.h"
#include "meta.h"
#include "migrate.h"
#include "sqlh.h"
#include "sqlite3.h"
#include "utf8.h"

typedef struct {
  size_t length;
  const uint32_t *runes;
} query_t;

static void match_impl(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
#if 0
  if (argc != 3) {
    sqlite3_result_error(context,
                         "wrong number of arguments to function match()", -1);
    return;
  }

  if (sqlite3_value_type(argv[0]) != SQLITE_BLOB ||
      sqlite3_value_type(argv[1]) != SQLITE_INTEGER ||
      !sqlite3_value_frombind(argv[2])) {
    sqlite3_result_error(context, "incorrect arguments to function match()",
                         -1);
    return;
  }
#endif

  const size_t dir_length = sqlite3_value_bytes(argv[0]) / sizeof(uint32_t);
  // should be aligned since sqlite requires memory allocations to have
  // at least 4 byte alignment
  const uint32_t *dir = sqlite3_value_blob(argv[0]);

  const int frecency = sqlite3_value_int(argv[1]);

  const query_t *query = sqlite3_value_pointer(argv[2], "");

  sqlite3_result_int(context,
                     fuzzy_match(dir, dir_length, query->runes, query->length) +
                         frecency);
}

static const char *const cache_dir = "/.cache";
static const char *const cache_file = "/zsql.db";

static int zsql_open(sqlite3 **db) {
  const char *base = getenv("XDG_CACHE_HOME");
  int is_home = 0;

  if (base == NULL) {
    is_home = 1;
    base = getenv("HOME");

    if (base == NULL) {
      // XDG_CACHE_HOME or HOME must be specified
      return ZSQL_ERROR;
    }
  }

  const size_t base_chars = sizeof(*base) * strlen(base);
  const size_t cache_dir_chars = sizeof(*cache_dir) * strlen(cache_dir);
  const size_t cache_file_chars = sizeof(*cache_file) * strlen(cache_file);
  const size_t null_chars = sizeof(char) * 1;
  const size_t path_length =
      base_chars + is_home * cache_dir_chars + cache_file_chars + null_chars;

  char *path = malloc(path_length);
  if (path == NULL) {
    return ZSQL_ERROR;
  }
  size_t offset = 0;

  memcpy(path + offset, base, base_chars);
  offset += base_chars;

  if (is_home) {
    memcpy(path + offset, cache_dir, cache_dir_chars);
    offset += cache_dir_chars;
  }

  memcpy(path + offset, cache_file, cache_file_chars);
  offset += cache_file_chars;

  path[offset] = 0;

  const int status = sqlite3_open_v2(
      path, db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);

  free(path);

  if (status != SQLITE_OK) {
    return ZSQL_ERROR;
  }

  sqlite3_create_function(
      *db, "match", 3, SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_DIRECTONLY,
      NULL, match_impl, NULL, NULL);

  return ZSQL_OK;
}

static int zsql_match(sqlite3 *db, const char *search) {
  int result = ZSQL_OK;

  uint32_t runes_short[64];
  uint32_t *runes;
  const size_t search_length = strlen(search);
  size_t runes_bytes = search_length * sizeof(*runes);

  int runes_allocated = runes_bytes > sizeof(runes_short);
  if (runes_allocated) {
    runes = malloc(runes_bytes);
    if (runes == NULL) {
      result = ZSQL_ERROR;
      goto exit;
    }
  } else {
    runes = runes_short;
    runes_bytes = sizeof(runes_short);
  }

  sqlite3_stmt *stmt;
  if (sqlh_prepare(db,
                   "CREATE TEMP TABLE vdirs AS "
                   "SELECT dir,match(dir,frecency,?1)quality FROM dirs",
                   &stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
    sqlh_finalize(stmt);
    goto exit;
  }

  const size_t runes_length = utf8_to_utf32(runes, search, search_length);
  query_t query = {.length = runes_length, .runes = runes};
  if (sqlite3_bind_pointer(stmt, 1, &query, "", NULL) != SQLITE_OK) {
    result = ZSQL_ERROR;
    goto cleanup_sql;
  }

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    result = ZSQL_ERROR;
    goto cleanup_sql;
  }

  if (sqlh_finalize(stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
    goto cleanup_mem;
  }

  if (sqlh_prepare(db,
                   "SELECT dir FROM vdirs WHERE quality>=0 "
                   "ORDER BY quality DESC",
                   &stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
    goto cleanup_mem;
  }

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    printf("no result\n");
    result = ZSQL_ERROR;
    goto cleanup_sql;
  }

  // we need as many bytes in the char* as there are bytes in runes because
  // each character in the uint32_t may expand into 4 utf-8 bytes
  const size_t result_bytes = sqlite3_column_bytes(stmt, 0);
  // should be aligned since sqlite requires memory allocations to have
  // at least 4 byte alignment
  const uint32_t *result_runes = sqlite3_column_blob(stmt, 0);
  char *str;

  int str_allocated = result_bytes > runes_bytes;
  if (str_allocated) {
    str = malloc(result_bytes);
    if (str == NULL) {
      result = ZSQL_ERROR;
      goto cleanup_sql;
    }
  } else {
    str = (char *)runes;
  }

  const size_t written =
      utf32_to_utf8(str, result_runes, result_bytes / sizeof(*result_runes));
  fwrite(str, 1, written, stdout);
  putc('\n', stdout);

  if (str_allocated) {
    free(str);
  }

cleanup_sql:
  if (sqlh_finalize(stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
  }
  if (sqlh_exec(db, "DROP TABLE vdirs") != ZSQL_OK) {
    result = ZSQL_ERROR;
  }
cleanup_mem:
  if (runes_allocated) {
    free(runes);
  }
exit:
  return result;
}

int main(int argc, char **argv) {
  int result = 0;

  if (argc >= 1) {
    ARGC = argc;
    ARGV = (const char *const *)argv;
  }

  sqlite3 *db;
  if (zsql_open(&db) != ZSQL_OK) {
    result = 1;
    goto exit;
  }
  if (zsql_migrate(db) != ZSQL_OK) {
    result = 1;
    goto cleanup;
  }

  if (ARGC < 2) {
    /* argv[1] = malloc(1024); */
    /* fgets(argv[1], 1024, stdin); */
    /* argc = 2; */
    result = 1;
    goto cleanup;
  }

  if (zsql_match(db, ARGV[1]) != ZSQL_OK) {
    result = 1;
    goto cleanup;
  }

cleanup:
  sqlite3_close(db);
exit:
  return result;
}
