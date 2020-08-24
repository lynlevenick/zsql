#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "fuzzy_search.h"
#include "migrate.h"
#include "sqlh.h"
#include "sqlite3.h"
#include "status.h"
#include "utf8.h"

typedef struct {
  const size_t length;
  const uint32_t *runes;
} query_t;

static void match_impl(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
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

  const size_t dir_length =
      (size_t)sqlite3_value_bytes(argv[0]) / sizeof(uint32_t);
  // should be aligned since sqlite requires memory allocations to have
  // at least 4 byte alignment
  const uint32_t *dir = sqlite3_value_blob(argv[0]);

  const int frecency = sqlite3_value_int(argv[1]);

  const query_t *query = sqlite3_value_pointer(argv[2], "");

  int score = fuzzy_search(dir, dir_length, query->runes, query->length);
  if (score >= 0) {
    sqlite3_result_int(context, score + frecency);
  } else {
    sqlite3_result_null(context);
  }
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

static const unsigned int force_cache_dir =
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
  char *path = malloc(path_bytes);
  if (path == NULL) {
    return ZSQL_ERROR;
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

  free(path);

  if (status != SQLITE_OK) {
    return ZSQL_ERROR;
  }

  if (sqlite3_create_function(*db, "match", 3,
                              SQLITE_UTF8 | SQLITE_DETERMINISTIC |
                                  SQLITE_DIRECTONLY,
                              NULL, match_impl, NULL, NULL) != SQLITE_OK) {
    return ZSQL_ERROR;
  }

  return ZSQL_OK;
}

static int zsql_search(sqlite3 *db, const uint32_t *runes, size_t length) {
  int result = ZSQL_OK;

  sqlite3_stmt *stmt;
  if (sqlh_prepare_static(
          db,
          "SELECT dir FROM dirs WHERE match(dir,frecency,?1)IS NOT NULL "
          "ORDER BY match(dir,frecency,?1) DESC",
          &stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
    goto exit;
  }

  query_t query = {.length = length, .runes = runes};
  if (sqlite3_bind_pointer(stmt, 1, &query, "", NULL) != SQLITE_OK) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

  const int status = sqlite3_step(stmt);
  if (status == SQLITE_DONE) {
    printf("no result\n");
    goto cleanup;
  } else if (status != SQLITE_ROW) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

  const size_t result_bytes = (size_t)sqlite3_column_bytes(stmt, 0);
  // should be aligned since sqlite requires memory allocations to have
  // at least 4 byte alignment
  const uint32_t *result_runes = sqlite3_column_blob(stmt, 0);

  // we need as many bytes in the char* as there are bytes in runes because
  // each character in the uint32_t may expand into 4 utf-8 bytes
  char *str = malloc(result_bytes);
  if (str == NULL) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

  const size_t written =
      utf32_to_utf8(str, result_runes, result_bytes / sizeof(*result_runes));
  fwrite(str, 1, written, stdout);
  putc('\n', stdout);

  free(str);

cleanup:
  if (sqlh_finalize(stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
  }
exit:
  return result;
}

static int zsql_add(sqlite3 *db, const uint32_t *runes, size_t length) {
  int result = ZSQL_OK;

  sqlite3_stmt *stmt;
  if (sqlh_prepare_static(
          db,
          "INSERT INTO dirs(dir)VALUES(?1)"
          "ON CONFLICT(dir)DO UPDATE SET frecency=frecency+excluded.frecency",
          &stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
    goto exit;
  }

  if (sqlite3_bind_blob(stmt, 1, runes, length * sizeof(*runes),
                        SQLITE_STATIC) != SQLITE_OK) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

cleanup:
  if (sqlh_finalize(stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
  }
exit:
  return result;
}

#define ZSQL_SEARCH 0
#define ZSQL_ADD 1
#define ZSQL_FORGET 2

int main(int argc, char **argv) {
  int result = 0;

  // option parsing

  if (argc < 2) {
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    argv[1] = malloc(1024);
    fgets(argv[1], 1024, stdin);
    argc = 2;
#else
    result = 1;
    goto exit;
#endif
  }

  int behavior = ZSQL_SEARCH;

  int ch;
  while ((ch = getopt(argc, argv, "af")) >= 0) {
    switch (ch) {
    case 'a':
      if (behavior != ZSQL_SEARCH) {
        result = 1;
        goto exit;
      }
      behavior = ZSQL_ADD;
      break;
    case 'f':
      if (behavior != ZSQL_SEARCH) {
        result = 1;
        goto exit;
      }
      behavior = ZSQL_FORGET;
      break;
    case '?':
    default:
      result = 1;
      goto exit;
    }
  }
  if (optind >= argc) {
    result = 1;
    goto exit;
  }

  // convert to utf32

  size_t argl_length = argc - optind;
  size_t *argl = malloc(argl_length * sizeof(*argl));
  if (argl == NULL) {
    result = 1;
    goto exit;
  }

  size_t search_length = 0;
  for (int i = 0; i < argl_length; ++i) {
    search_length += (argl[i] = strlen(argv[optind + i]));
  }

  uint32_t *runes = malloc(search_length * sizeof(*runes));
  if (runes == NULL) {
    result = 1;
    goto cleanup_argl;
  }

  size_t runes_length = 0;
  for (int i = 0; i < argl_length; ++i) {
    runes_length +=
        utf8_to_utf32(runes + runes_length, argv[optind + i], argl[i]);
  }

  // db init

  if (sqlite3_initialize() != SQLITE_OK) {
    result = 1;
    goto cleanup_runes;
  }

  sqlite3 *db;
  if (zsql_open(&db) != ZSQL_OK) {
    result = 1;
    goto cleanup_runes;
  }

  if (sqlite3_busy_timeout(db, 128) != SQLITE_OK) {
    result = 1;
    goto cleanup_sql;
  }
  if (zsql_migrate(db) != ZSQL_OK) {
    result = 1;
    goto cleanup_sql;
  }

  // behavior

  switch (behavior) {
  case ZSQL_SEARCH:
    if (zsql_search(db, runes, runes_length) != ZSQL_OK) {
      result = 1;
      goto cleanup_sql;
    }
    break;
  case ZSQL_ADD:
    if (zsql_add(db, runes, runes_length) != ZSQL_OK) {
      result = 1;
      goto cleanup_sql;
    }
    break;
  case ZSQL_FORGET:
    break;
  }

  // cleanup

cleanup_sql:
  sqlite3_close(db);
cleanup_runes:
  free(runes);
cleanup_argl:
  free(argl);
exit:
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  if (result != 0) {
    abort();
  }
#endif
  return result;
}
