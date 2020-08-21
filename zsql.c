#include <stddef.h>
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
  const size_t length;
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

  const size_t dir_length =
      (size_t)sqlite3_value_bytes(argv[0]) / sizeof(uint32_t);
  // should be aligned since sqlite requires memory allocations to have
  // at least 4 byte alignment
  const uint32_t *dir = sqlite3_value_blob(argv[0]);

  const int frecency = sqlite3_value_int(argv[1]);

  const query_t *query = sqlite3_value_pointer(argv[2], "");

  sqlite3_result_int(context,
                     fuzzy_match(dir, dir_length, query->runes, query->length) +
                         frecency);
}

static const char *const cache_dir =
#ifdef _WIN32
    "/zsql";
#else
    "/.cache";
#endif
static const char *const cache_file = "/zsql.db";

static const char *const cache_env =
#ifdef _WIN32
    "LocalAppData";
#else
    "XDG_CACHE_HOME";
#endif
static const char *const cache_fallback_env =
#ifdef _WIN32
    "AppData";
#else
    "HOME";
#endif

static const int force_cache_dir =
#ifdef _WIN32
    1;
#else
    0;
#endif

static int zsql_open(sqlite3 **db) {
  const char *base = getenv(cache_env);
  unsigned int is_home = 0;

  if (base == NULL) {
    is_home = 1;
    base = getenv(cache_fallback_env);

    if (base == NULL) {
      // XDG_CACHE_HOME or HOME must be specified
      return ZSQL_ERROR;
    }
  }

  const size_t base_bytes = sizeof(*base) * strlen(base);
  const size_t cache_dir_bytes = sizeof(*cache_dir) * strlen(cache_dir);
  const size_t cache_file_bytes = sizeof(*cache_file) * strlen(cache_file);
  const size_t null_bytes = sizeof(char) * 1;
  const size_t path_bytes = base_bytes +
                            (force_cache_dir || is_home) * cache_dir_bytes +
                            cache_file_bytes + null_bytes;

  char path_short[256];
  char *path = path_short;

  const int path_allocated = path_bytes > sizeof(path_short);
  if (path_allocated) {
    path = malloc(path_bytes);
    if (path == NULL) {
      return ZSQL_ERROR;
    }
  }

  size_t offset = 0;

  memcpy(path + offset, base, base_bytes);
  offset += base_bytes;

  if (force_cache_dir || is_home) {
    memcpy(path + offset, cache_dir, cache_dir_bytes);
    offset += cache_dir_bytes;
  }

  memcpy(path + offset, cache_file, cache_file_bytes);
  offset += cache_file_bytes;

  path[offset] = 0;

  const int status = sqlite3_open_v2(
      path, db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);

  if (path_allocated) {
    free(path);
  }

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

  const int runes_allocated = runes_bytes > sizeof(runes_short);
  if (!runes_allocated) {
    runes = runes_short;
    runes_bytes = sizeof(runes_short);
  } else {
    runes = malloc(runes_bytes);
    if (runes == NULL) {
      result = ZSQL_ERROR;
      goto exit;
    }
  }

  sqlite3_stmt *stmt;
  if (sqlh_prepare_static(
          db,
          "SELECT dir FROM dirs WHERE match(dir,frecency,?1)>=0 "
          "ORDER BY match(dir,frecency,?1) DESC",
          &stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
    goto cleanup_mem;
  }

  const size_t runes_length = utf8_to_utf32(runes, search, search_length);
  query_t query = {.length = runes_length, .runes = runes};
  if (sqlite3_bind_pointer(stmt, 1, &query, "", NULL) != SQLITE_OK) {
    result = ZSQL_ERROR;
    goto cleanup_sql;
  }

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    printf("no result\n");
    result = ZSQL_ERROR;
    goto cleanup_sql;
  }

  // we need as many bytes in the char* as there are bytes in runes because
  // each character in the uint32_t may expand into 4 utf-8 bytes
  const size_t result_bytes = (size_t)sqlite3_column_bytes(stmt, 0);
  // should be aligned since sqlite requires memory allocations to have
  // at least 4 byte alignment
  const uint32_t *result_runes = sqlite3_column_blob(stmt, 0);
  char *str = (char *)runes;

  const int str_allocated = result_bytes > runes_bytes;
  if (str_allocated) {
    str = malloc(result_bytes);
    if (str == NULL) {
      result = ZSQL_ERROR;
      goto cleanup_sql;
    }
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
cleanup_mem:
  if (runes_allocated) {
    free(runes);
  }
exit:
  return result;
}

int main(int argc, char **argv) {
  int result = 0;

  ARGC = argc;
  ARGV = (const char *const *)argv;

  if (sqlite3_initialize() != SQLITE_OK) {
    result = 1;
    goto exit;
  }

  sqlite3 *db;
  if (zsql_open(&db) != ZSQL_OK) {
    result = 1;
    goto exit;
  }

  if (sqlite3_busy_timeout(db, 128) != SQLITE_OK) {
    result = 1;
    goto cleanup;
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
