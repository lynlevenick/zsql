#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "fuzzy_search.h"
#include "migrate.h"
#include "sqlh.h"
#include "sqlite3.h"
#include "status.h"
#include "utf8.h"

typedef struct {
  const size_t length;
  const uint32_t *runes;
} zsql_query;

static void match_impl(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context,
                         "wrong number of arguments to function match()", -1);
    return;
  }

  if (sqlite3_value_type(argv[0]) != SQLITE_BLOB ||
      !sqlite3_value_frombind(argv[1])) {
    sqlite3_result_error(context, "incorrect arguments to function match()",
                         -1);
    return;
  }

  const size_t dir_length =
      (size_t)sqlite3_value_bytes(argv[0]) / sizeof(uint32_t);
  // should be aligned since sqlite requires memory allocations to have
  // at least 4 byte alignment
  const uint32_t *dir = sqlite3_value_blob(argv[0]);

  const zsql_query *query = sqlite3_value_pointer(argv[1], "");

  int score = fuzzy_search(dir, dir_length, query->runes, query->length);
  if (score >= 0) {
    sqlite3_result_int(context, score);
  } else {
    sqlite3_result_null(context);
  }
}

static const char *const env_primary =
#ifdef _WIN32
    "LOCALAPPDATA";
#else
    "XDG_DATA_HOME";
#endif

static const char *const env_fallback =
#ifdef _WIN32
    "APPDATA";
#else
    "HOME";
#endif

static const char *const fallback_suffix =
#ifdef _WIN32
    "";
#else
    "/.local/share";
#endif

static const char *const cache_dir = "/zsql";
static const char *const cache_file = "/zsql.db";

static int zsql_open(sqlite3 **db) {
  int result = ZSQL_OK;

  int using_fallback = 0;
  const char *base = getenv(env_primary);
  if (base == NULL) {
    using_fallback = 1;
    base = getenv(env_fallback);
    if (base == NULL) {
      result = ZSQL_ERROR;
      goto exit;
    }
  }

  const size_t base_length = strlen(base);
  const size_t fallback_suffix_length = strlen(fallback_suffix);
  const size_t cache_dir_length = strlen(cache_dir);
  const size_t cache_file_length = strlen(cache_file);
  const size_t path_length = base_length +
                             (using_fallback ? fallback_suffix_length : 0) +
                             strlen(cache_dir) + strlen(cache_file) + 1;
  char *path = malloc(path_length);
  if (path == NULL) {
    result = ZSQL_ERROR;
    goto exit;
  }

  size_t offset = 0;

  memcpy(path + offset, base, base_length);
  offset += base_length;

  if (using_fallback) {
    memcpy(path + offset, fallback_suffix, fallback_suffix_length);
    offset += fallback_suffix_length;
  }

  memcpy(path + offset, cache_dir, cache_dir_length);
  offset += cache_dir_length;

  path[offset] = 0;
  struct stat dir_stat;
  if (stat(path, &dir_stat) != 0) {
    fprintf(stderr, "stat ne 0\n");
    if (mkdir(path, 0700) != 0) {
      fprintf(stderr, "couldn't create dir\n");
      result = ZSQL_ERROR;
      goto cleanup;
    }
  } else if (!S_ISDIR(dir_stat.st_mode)) {
    fprintf(stderr, "not dir\n");
    result = ZSQL_ERROR;
    goto cleanup;
  }

  memcpy(path + offset, cache_file, cache_file_length);
  offset += cache_file_length;

  path[offset] = 0;

  if (sqlite3_open(path, db) != SQLITE_OK) {
    fprintf(stderr, "couldn't open\n");
    result = ZSQL_ERROR;
    goto cleanup;
  }

  if (sqlite3_create_function(*db, "match", 2,
                              SQLITE_UTF8 | SQLITE_DETERMINISTIC |
                                  SQLITE_DIRECTONLY,
                              NULL, match_impl, NULL, NULL) != SQLITE_OK) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

cleanup:
  free(path);
exit:
  return result;
}

static int zsql_search(sqlite3 *db, const uint32_t *runes, size_t length) {
  int result = ZSQL_OK;

  sqlite3_stmt *stmt;
  if (sqlh_prepare_static(db,
                          "SELECT dir FROM dirs WHERE match(dir,?1)IS NOT NULL "
                          "ORDER BY match(dir,?1)+frecency DESC",
                          &stmt) != ZSQL_OK) {
    result = ZSQL_ERROR;
    goto exit;
  }

  zsql_query query = {.length = length, .runes = runes};
  if (sqlite3_bind_pointer(stmt, 1, &query, "", NULL) != SQLITE_OK) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

  const int status = sqlite3_step(stmt);
  if (status == SQLITE_DONE) {
    printf("no result\n");
    goto cleanup;
  } else if (status != SQLITE_ROW) {
    printf("%s\n", sqlite3_errmsg(db));
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

typedef enum {
  ZSQL_BEHAVIOR_SEARCH,
  ZSQL_BEHAVIOR_ADD,
  ZSQL_BEHAVIOR_FORGET
} zsql_behavior;
typedef enum {
  ZSQL_CASE_SMART,
  ZSQL_CASE_SENSITIVE,
  ZSQL_CASE_IGNORE
} zsql_case_sensitive;

// fixme: use utf-16 on windows via sqlite3_open16
int main(int argc, char **argv) {
  int result = 0;

  // option parsing

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  if (argc < 2) {
    argv[1] = malloc(1024);
    fgets(argv[1], 1024, stdin);
    argc = 2;
  }
#endif

  zsql_behavior behavior = ZSQL_BEHAVIOR_SEARCH;
  zsql_case_sensitive case_sensitive = ZSQL_CASE_SMART;

  int ch;
  while ((ch = getopt(argc, argv, "acif")) >= 0) {
    switch (ch) {
    case 'a':
      behavior = ZSQL_BEHAVIOR_ADD;
      break;
    case 'c':
      case_sensitive = ZSQL_CASE_SENSITIVE;
      break;
    case 'i':
      case_sensitive = ZSQL_CASE_IGNORE;
      break;
    case 'f':
      behavior = ZSQL_BEHAVIOR_FORGET;
      break;
    case '?':
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
  for (size_t i = 0; i < argl_length; ++i) {
    search_length += (argl[i] = strlen(argv[optind + i]));
  }

  // we need as many bytes in the uint32_t* as there are bytes in argv because
  // each rune in the uint32_t may expand from 1 utf-8 byte
  uint32_t *runes = malloc(search_length * sizeof(*runes));
  if (runes == NULL) {
    result = 1;
    goto cleanup_argl;
  }

  size_t runes_length = 0;
  for (size_t i = 0; i < argl_length; ++i) {
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
  case ZSQL_BEHAVIOR_SEARCH:
    if (zsql_search(db, runes, runes_length) != ZSQL_OK) {
      result = 1;
      goto cleanup_sql;
    }
    break;
  case ZSQL_BEHAVIOR_ADD:
    if (zsql_add(db, runes, runes_length) != ZSQL_OK) {
      result = 1;
      goto cleanup_sql;
    }
    break;
  case ZSQL_BEHAVIOR_FORGET:
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
