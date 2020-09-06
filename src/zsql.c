#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "args.h"
#include "error.h"
#include "fuzzy_search.h"
#include "migrate.h"
#include "sqlh.h"
#include "sqlite3.h"
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

// fixme: windows
static const char *const env_primary = "XDG_DATA_HOME";
static const char *const env_fallback = "HOME";

static const char *const fallback_suffix = "/.local/share";

static const char *const cache_dir = "/zsql";
static const char *const cache_file = "/zsql.db";

static const char *const ensure_dir_error = "not a directory: ";
static zsql_error *zsql_ensure_dir(const char *path) {
  struct stat dir_stat;
  if (stat(path, &dir_stat) != 0) {
    if (mkdir(path, 0700) != 0) {
      return zsql_error_from_errno(NULL);
    }
  } else if (!S_ISDIR(dir_stat.st_mode)) {
    const size_t ensure_dir_error_length = strlen(ensure_dir_error);
    const size_t path_length = strlen(path);
    const size_t msg_length = ensure_dir_error_length + path_length + 1;
    char *msg = malloc(msg_length);
    if (msg == NULL) {
      return zsql_error_from_errno(NULL);
    }
    size_t offset = 0;

    memcpy(msg + offset, ensure_dir_error, ensure_dir_error_length);
    offset += ensure_dir_error_length;

    memcpy(msg + offset, path, path_length);
    offset += path_length;

    msg[offset] = 0;

    zsql_error *err = zsql_error_from_text(msg, NULL);
    free(msg);
    return err;
  }

  return NULL;
}

static zsql_error *zsql_open(sqlite3 **db) {
  zsql_error *err = NULL;

  int using_fallback = 0;
  const char *base = getenv(env_primary);
  if (base == NULL) {
    using_fallback = 1;
    base = getenv(env_fallback);
    if (base == NULL) {
      err = zsql_error_from_errno(err);
      goto exit;
    }
  }

  const size_t base_length = strlen(base);
  const size_t fallback_suffix_length = strlen(fallback_suffix);
  const size_t cache_dir_length = strlen(cache_dir);
  const size_t cache_file_length = strlen(cache_file);
  const size_t path_length = base_length +
                             (using_fallback ? fallback_suffix_length : 0) +
                             cache_dir_length + cache_file_length + 1;
  char *path = malloc(path_length);
  if (path == NULL) {
    err = zsql_error_from_errno(err);
    goto exit;
  }
  size_t offset = 0;

  memcpy(path + offset, base, base_length);
  offset += base_length;

  if (using_fallback) {
    memcpy(path + offset, fallback_suffix, fallback_suffix_length);

    // forcing an unroll here if the compiler supports it. always generates
    // better code, since iterating and checking the condition is pure overhead
    // for the low number of times this will be true
#pragma unroll
#pragma GCC unroll(fallback_suffix_length - 1)
    for (size_t slash_idx = 1; slash_idx < fallback_suffix_length;
         ++slash_idx) {
      if (fallback_suffix[slash_idx] == '/') {
        path[offset + slash_idx] = 0;
        if ((err = zsql_ensure_dir(path)) != NULL) {
          goto cleanup;
        }
        path[offset + slash_idx] = '/';
      }
    }

    offset += fallback_suffix_length;
  }

  path[offset] = 0;
  if ((err = zsql_ensure_dir(path)) != NULL) {
    goto cleanup;
  }

  memcpy(path + offset, cache_dir, cache_dir_length);
  offset += cache_dir_length;

  path[offset] = 0;
  if ((err = zsql_ensure_dir(path)) != NULL) {
    goto cleanup;
  }

  memcpy(path + offset, cache_file, cache_file_length);
  offset += cache_file_length;

  path[offset] = 0;

  if (sqlite3_open(path, db) != SQLITE_OK) {
    err = zsql_error_from_sqlite(*db, err);
    goto cleanup;
  }

  if (sqlite3_create_function(*db, "match", 2,
                              SQLITE_UTF8 | SQLITE_DETERMINISTIC |
                                  SQLITE_DIRECTONLY,
                              NULL, match_impl, NULL, NULL) != SQLITE_OK) {
    err = zsql_error_from_sqlite(*db, err);
    goto cleanup;
  }

cleanup:
  free(path);
exit:
  return err;
}

static zsql_error *zsql_search(sqlite3 *db, const uint32_t *runes,
                               size_t length) {
  zsql_error *err = NULL;

  sqlite3_stmt *stmt;
  if ((err = sqlh_prepare_static(
           db,
           "SELECT dir FROM dirs WHERE match(dir,?1)IS NOT NULL "
           "ORDER BY match(dir,?1)+frecency DESC",
           &stmt)) != NULL) {
    goto exit;
  }

  zsql_query query = {.length = length, .runes = runes};
  if (sqlite3_bind_pointer(stmt, 1, &query, "", NULL) != SQLITE_OK) {
    err = zsql_error_from_sqlite(db, err);
    goto cleanup;
  }

  const int status = sqlite3_step(stmt);
  if (status == SQLITE_DONE) {
    printf("no result\n");
    goto cleanup;
  } else if (status != SQLITE_ROW) {
    err = zsql_error_from_sqlite(db, err);
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
    err = zsql_error_from_errno(NULL);
    goto cleanup;
  }

  const size_t written =
      utf32_to_utf8(str, result_runes, result_bytes / sizeof(*result_runes));
  fwrite(str, 1, written, stdout);
  fputc('\n', stdout);

  free(str);

cleanup:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
}

static zsql_error *zsql_add(sqlite3 *db, const uint32_t *runes, size_t length) {
  zsql_error *err = NULL;

  sqlite3_stmt *stmt;
  if ((err = sqlh_prepare_static(
           db,
           "INSERT INTO dirs(dir)VALUES(?1)"
           "ON CONFLICT(dir)DO UPDATE SET frecency=frecency+excluded.frecency",
           &stmt)) != NULL) {
    goto exit;
  }

  if (sqlite3_bind_blob(stmt, 1, runes, length * sizeof(*runes),
                        SQLITE_STATIC) != SQLITE_OK) {
    err = zsql_error_from_sqlite(db, err);
    goto cleanup;
  }

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    err = zsql_error_from_sqlite(db, err);
    goto cleanup;
  }

cleanup:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
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
} zsql_case_sensitivity;

// fixme: windows
int main(int argc, char **argv) {
  zsql_error *err = NULL;

  // option parsing

#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
  if (argc < 2) {
    char **new_argv = malloc(2 * sizeof(*new_argv));
    if (new_argv == NULL) {
      err = zsql_error_from_errno(err);
      goto exit;
    }
    new_argv[0] = argv[0];
    new_argv[1] = malloc(1024 * sizeof(*new_argv[1]));
    if (new_argv[1] == NULL) {
      err = zsql_error_from_errno(err);
      goto exit;
    }
    argv = new_argv;
    fgets(argv[1], 1024, stdin);
    argc = 2;
  }
#endif

  ARGC = argc;
  ARGV = argv;

  zsql_behavior behavior = ZSQL_BEHAVIOR_SEARCH;
  zsql_case_sensitivity case_sensitivity = ZSQL_CASE_SMART;

  int ch;
  while ((ch = getopt(argc, argv, "acif")) >= 0) {
    switch (ch) {
    case 'a':
      behavior = ZSQL_BEHAVIOR_ADD;
      break;
    case 'c':
      case_sensitivity = ZSQL_CASE_SENSITIVE;
      break;
    case 'i':
      case_sensitivity = ZSQL_CASE_IGNORE;
      break;
    case 'f':
      behavior = ZSQL_BEHAVIOR_FORGET;
      break;
    case '?':
      goto exit;
    }
  }
  if (optind >= argc) {
    err = zsql_error_from_text("no search specified", err);
    goto exit;
  }

  // convert to utf32

  size_t argl_length = argc - optind;
  size_t *argl = malloc(argl_length * sizeof(*argl));
  if (argl == NULL) {
    err = zsql_error_from_errno(err);
    goto exit;
  }

  size_t search_length = 0;
  for (size_t arg_idx = 0; arg_idx < argl_length; ++arg_idx) {
    search_length += (argl[arg_idx] = strlen(argv[optind + arg_idx]));
  }

  // we need as many bytes in the uint32_t* as there are bytes in argv because
  // each rune in the uint32_t may expand from 1 utf-8 byte
  uint32_t *runes = malloc(search_length * sizeof(*runes));
  if (runes == NULL) {
    err = zsql_error_from_errno(err);
    goto cleanup_argl;
  }

  size_t runes_length = 0;
  for (size_t arg_idx = 0; arg_idx < argl_length; ++arg_idx) {
    runes_length += utf8_to_utf32(runes + runes_length, argv[optind + arg_idx],
                                  argl[arg_idx]);
  }

  // db init

  if (sqlite3_initialize() != SQLITE_OK) {
    err = zsql_error_from_text("failed to initialize sqlite", err);
    goto cleanup_runes;
  }

  sqlite3 *db;
  if ((err = zsql_open(&db)) != NULL) {
    goto cleanup_runes;
  }

  if (sqlite3_busy_timeout(db, 128) != SQLITE_OK) {
    err = zsql_error_from_sqlite(db, err);
    goto cleanup_sql;
  }
  if ((err = zsql_migrate(db)) != NULL) {
    goto cleanup_sql;
  }

  // behavior

  switch (behavior) {
  case ZSQL_BEHAVIOR_SEARCH:
    if ((err = zsql_search(db, runes, runes_length)) != NULL) {
      goto cleanup_sql;
    }
    break;
  case ZSQL_BEHAVIOR_ADD:
    if ((err = zsql_add(db, runes, runes_length)) != NULL) {
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
  if (err != NULL) {
    zsql_error_print(err);
#ifdef FUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION
    abort();
#endif
  }
  return err == NULL ? EXIT_SUCCESS : EXIT_FAILURE;
}
