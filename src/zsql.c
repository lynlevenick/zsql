#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utf8proc.h>

#include "env.h"
#include "error.h"
#include "fuzzy_search.h"
#include "migrate.h"
#include "sqlh.h"
#include "sqlite3.h"

typedef struct {
  const size_t length;
  const int32_t *runes;
  const utf8proc_option_t utf8proc_options;
} zsql_query;

#ifdef HAVE_THREAD_LOCAL
#define MATCH_BUFFER_SIZE 1024
static thread_local int32_t match_buffer[MATCH_BUFFER_SIZE];
#endif

static void match_impl(sqlite3_context *context, int argc,
                       sqlite3_value **argv) {
  // invariants

  if (argc != 2) {
    sqlite3_result_error(context,
                         "wrong number of arguments to function match()", -1);
    goto exit;
  }
  if (sqlite3_value_type(argv[0]) != SQLITE_BLOB ||
      !sqlite3_value_frombind(argv[1])) {
    sqlite3_result_error(context, "incorrect arguments to function match()",
                         -1);
    goto exit;
  }

  // get parameters

  const size_t dir_length = (size_t)sqlite3_value_bytes(argv[0]);
  const char *dir = sqlite3_value_blob(argv[0]);

  const zsql_query *query = sqlite3_value_pointer(argv[1], "");

  // convert dir to utf32

  size_t dir_utf32_length = dir_length * 2;
  int32_t *dir_utf32;
#ifdef HAVE_THREAD_LOCAL
  if (dir_utf32_length <= MATCH_BUFFER_SIZE) {
    dir_utf32 = match_buffer;
  } else {
#endif
    dir_utf32 = malloc(dir_utf32_length * sizeof(*dir_utf32));
    if (dir_utf32 == NULL) {
      sqlite3_result_error_nomem(context);
      goto exit;
    }
#ifdef HAVE_THREAD_LOCAL
  }
#endif

retry_decompose:;
  ssize_t result =
      utf8proc_decompose((uint8_t *)dir, dir_length, dir_utf32,
                         dir_utf32_length, query->utf8proc_options);
  if (result < 0) {
    sqlite3_result_error(context, utf8proc_errmsg(result), -1);
    goto cleanup_dir_utf32;
  } else if ((size_t)result > dir_utf32_length) {
    dir_utf32_length = result;
    void *allocation;
#ifdef HAVE_THREAD_LOCAL
    if (dir_utf32 == match_buffer) {
      allocation = malloc(dir_utf32_length * sizeof(*dir_utf32));
      if (allocation == NULL) {
        sqlite3_result_error_nomem(context);
        goto exit;
      }
    } else {
#endif
      allocation = realloc(dir_utf32, dir_utf32_length * sizeof(*dir_utf32));
      if (allocation == NULL) {
        sqlite3_result_error_nomem(context);
        goto cleanup_dir_utf32;
      }
#ifdef HAVE_THREAD_LOCAL
    }
#endif
    dir_utf32 = allocation;
    goto retry_decompose;
  } else {
    dir_utf32_length = result;
  }

  // score

  float score;
  zsql_error *err;
  if ((err = fuzzy_search(&score, dir_utf32, dir_utf32_length, query->runes,
                          query->length)) != NULL) {
    // fixme: this error may have chained errors in ->next, always ignored here
    sqlite3_result_error(context, err->msg, -1);
    zsql_error_free(err);
    goto cleanup_dir_utf32;
  }

  // return to sqlite

  if (score > -INFINITY) {
    sqlite3_result_double(context, (double)score);
  } else {
    sqlite3_result_null(context);
  }

cleanup_dir_utf32:
#ifdef HAVE_THREAD_LOCAL
  if (dir_utf32 != match_buffer) {
#endif
    free(dir_utf32);
#ifdef HAVE_THREAD_LOCAL
  }
#endif
exit:;
}

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

// fixme: windows
static const char *const env_primary = "XDG_DATA_HOME";
static const char *const env_fallback = "HOME";

static const char *const fallback_suffix = "/.local/share";

static const char *const cache_dir = "/zsql";
static const char *const cache_file = "/zsql.db";

static zsql_error *zsql_open(sqlite3 **conn) {
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
    // for the low number of times this will be true, and whether it's true
    // can be statically determined
#pragma unroll
    for (size_t slash_idx = 1; slash_idx < fallback_suffix_length;
         ++slash_idx) {
      if (fallback_suffix[slash_idx] == '/') {
        path[offset + slash_idx] = 0;
        if ((err = zsql_ensure_dir(path)) != NULL) {
          goto cleanup_path;
        }
        path[offset + slash_idx] = '/';
      }
    }

    offset += fallback_suffix_length;
  }

  path[offset] = 0;
  if ((err = zsql_ensure_dir(path)) != NULL) {
    goto cleanup_path;
  }

  memcpy(path + offset, cache_dir, cache_dir_length);
  offset += cache_dir_length;

  path[offset] = 0;
  if ((err = zsql_ensure_dir(path)) != NULL) {
    goto cleanup_path;
  }

  memcpy(path + offset, cache_file, cache_file_length);
  offset += cache_file_length;

  path[offset] = 0;

  int retries = 0;
retry_open:;
  int status = sqlite3_open(path, conn);
  if (status == SQLITE_BUSY && retries < 8) {
    retries += 1;
    sqlite3_sleep(16);
    goto retry_open;
  } else if (status != SQLITE_OK) {
    err = zsql_error_from_sqlite(*conn, err);
    goto cleanup_path;
  }

  if (sqlite3_busy_timeout(*conn, 128) != SQLITE_OK) {
    err = zsql_error_from_sqlite(*conn, err);
    goto cleanup_sql;
  }

  if (sqlite3_create_function(*conn, "match", 2,
                              SQLITE_UTF8 | SQLITE_DETERMINISTIC
#if defined(SQLITE_VERSION_NUMBER) && SQLITE_VERSION_NUMBER >= 3031000
                                  | SQLITE_DIRECTONLY
#endif
                              ,
                              NULL, match_impl, NULL, NULL) != SQLITE_OK) {
    err = zsql_error_from_sqlite(*conn, err);
    goto cleanup_sql;
  }

  if (0) { // error path only
  cleanup_sql:
    sqlite3_close(*conn);
  }
cleanup_path:
  free(path);
exit:
  return err;
}

static zsql_error *zsql_add(sqlite3 *conn, const char *dir, size_t length) {
  zsql_error *err = NULL;

  sqlite3_stmt *stmt;
  if ((err = sqlh_prepare_static(
           conn,
           "INSERT INTO dirs(dir,visited_at)VALUES(?1,CURRENT_TIMESTAMP)"
           "ON CONFLICT(dir)DO UPDATE SET"
           " visits=visits+excluded.visits"
           ",visited_at=excluded.visited_at",
           &stmt)) != NULL) {
    goto exit;
  }

  if (sqlite3_bind_blob(stmt, 1, dir, length * sizeof(*dir), SQLITE_STATIC) !=
      SQLITE_OK) {
    err = zsql_error_from_sqlite(conn, err);
    goto cleanup_stmt;
  }

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    err = zsql_error_from_sqlite(conn, err);
    goto cleanup_stmt;
  }

cleanup_stmt:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
}

static zsql_error *zsql_match(sqlite3 *conn, sqlite3_stmt **stmt,
                              zsql_query *query) {
  zsql_error *err = NULL;

  if ((err = sqlh_prepare_static(
           conn,
           "SELECT id,dir,"
           "m-250000./(visits+300)+250000./301+500./DENSE_RANK()OVER("
           "ORDER BY visited_at DESC"
           ")r,visits FROM("
           "SELECT *,match(dir,?1)m FROM dirs LIMIT -1"
           ")WHERE m IS NOT NULL ORDER BY r DESC",
           stmt)) != NULL) {
    goto exit;
  }

  if (sqlite3_bind_pointer(*stmt, 1, query, "", SQLITE_STATIC) != SQLITE_OK) {
    err = zsql_error_from_sqlite(conn, err);
    goto cleanup_stmt;
  }

  int status = sqlite3_step(*stmt);
  if (status == SQLITE_DONE) {
    err = zsql_error_from_text("no matches", err);
    goto cleanup_stmt;
  } else if (status != SQLITE_ROW) {
    err = zsql_error_from_sqlite(conn, err);
    goto cleanup_stmt;
  }

  if (DEBUGGING) {
    for (; status == SQLITE_ROW; status = sqlite3_step(*stmt)) {
      const size_t result_length = (size_t)sqlite3_column_bytes(*stmt, 1);
      const char *result = sqlite3_column_blob(*stmt, 1);
      const double rank = sqlite3_column_double(*stmt, 2);
      const int64_t visits = sqlite3_column_int64(*stmt, 3);

      fprintf(stderr, "%.4lf\t%" PRId64 "\t%.*s\n", rank, visits,
              (int)(result_length > INT_MAX ? INT_MAX : result_length), result);
    }

    if (status == SQLITE_DONE) {
      status = sqlite3_reset(*stmt);
      if (status != SQLITE_OK) {
        err = zsql_error_from_sqlite(conn, err);
        goto cleanup_stmt;
      }

      status = sqlite3_step(*stmt);
      if (status == SQLITE_DONE) {
        err = zsql_error_from_text("inconsistent state after debug", err);
        goto cleanup_stmt;
      } else if (status != SQLITE_ROW) {
        err = zsql_error_from_sqlite(conn, err);
        goto cleanup_stmt;
      }
    } else {
      err = zsql_error_from_sqlite(conn, err);
      goto cleanup_stmt;
    }
  }

  if (0) { // error path only
  cleanup_stmt:
    err = sqlh_finalize(*stmt, err);
  }
exit:
  return err;
}

static zsql_error *zsql_forget(sqlite3 *conn, const int32_t *runes,
                               size_t length,
                               utf8proc_option_t utf8proc_options) {
  zsql_error *err = NULL;

  zsql_query query = {
      .length = length, .runes = runes, .utf8proc_options = utf8proc_options};
  sqlite3_stmt *stmt;
  if ((err = zsql_match(conn, &stmt, &query)) != NULL) {
    goto exit;
  }

  const int64_t id = sqlite3_column_int64(stmt, 0);
  const size_t result_length = (size_t)sqlite3_column_bytes(stmt, 1);
  const char *result = sqlite3_column_blob(stmt, 1);

  if (printf("Remove `%.*s'? [Yn] ",
             (int)(result_length > INT_MAX ? INT_MAX : result_length),
             result) < 0) {
    err = zsql_error_from_errno(err);
    goto cleanup_stmt;
  }
  const int response = fgetc(stdin);
  if (response == EOF && !feof(stdin)) {
    err = zsql_error_from_errno(err);
    goto cleanup_stmt;
  }
  const int should_remove =
      response != EOF && response != 'n' && response != 'N';

  if (should_remove) {
    if ((err = sqlh_finalize(stmt, err)) != NULL) {
      goto exit;
    }
    if ((err = sqlh_prepare_static(conn, "DELETE FROM dirs WHERE id=?1",
                                   &stmt)) != NULL) {
      goto exit;
    }

    if (sqlite3_bind_int64(stmt, 1, id) != SQLITE_OK) {
      err = zsql_error_from_sqlite(conn, err);
      goto cleanup_stmt;
    }

    int status = sqlite3_step(stmt);
    if (status != SQLITE_DONE) {
      err = zsql_error_from_sqlite(conn, err);
      goto cleanup_stmt;
    }
  }

cleanup_stmt:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
}

static zsql_error *zsql_search(sqlite3 *conn, const int32_t *runes,
                               size_t length,
                               utf8proc_option_t utf8proc_options) {
  zsql_error *err = NULL;

  zsql_query query = {
      .length = length, .runes = runes, .utf8proc_options = utf8proc_options};
  sqlite3_stmt *stmt;
  if ((err = zsql_match(conn, &stmt, &query)) != NULL) {
    goto exit;
  }

  const size_t result_length = (size_t)sqlite3_column_bytes(stmt, 1);
  const char *result = sqlite3_column_blob(stmt, 1);

#if HAVE_FLOCKFILE && HAVE_FUNLOCKFILE && HAVE_PUTC_UNLOCKED
  flockfile(stdout);
#if HAVE_FWRITE_UNLOCKED
  if (fwrite_unlocked(result, 1, result_length, stdout) != result_length) {
    err = zsql_error_from_errno(err);
    goto cleanup_stmt;
  }
#else
  for (size_t i = 0; i < result_length; ++i) {
    if (putc_unlocked(result[i], stdout) == EOF) {
      err = zsql_error_from_errno(err);
      goto cleanup_stmt;
    }
  }
#endif
  if (putc_unlocked('$', stdout) == EOF) {
    err = zsql_error_from_errno(err);
    goto cleanup_stmt;
  }
  funlockfile(stdout);
#else
  if (fwrite(result, 1, result_length, stdout) != result_length) {
    err = zsql_error_from_errno(err);
    goto cleanup_stmt;
  }
  if (putc('$', stdout) == EOF) {
    err = zsql_error_from_errno(err);
    goto cleanup_stmt;
  }
#endif

cleanup_stmt:
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

// clang-format off
static const char *script =
    "if test \"$ZSH_VERSION\";then "
        // for zsh, use precmd_functions
        "eval '"
            "typeset -ag precmd_functions;"
            "if test \"$precmd_functions[(Ie)__z_add]\" -eq 0;then "
                "precmd_functions+=(__z_add);"
            "fi"
        "';"
    "else "
        // for all other shells, assume PROMPT_COMMAND works
        "case \";${PROMPT_COMMAND:=__z_add};\" in "
            "*\\;__z_add\\;*);;"
            "*)PROMPT_COMMAND=\"${PROMPT_COMMAND:+$PROMPT_COMMAND;}__z_add\";"
        "esac;"
    "fi\n"

    "__z_add()"
        // run async because we're behind sqlite, fully lockstep
        "(command z -a \"$(pwd)\" &)"
    "\n"

    "__z_cd(){ "
        // when we get a match, we print an extra '$' character after
        // the match because otherwise the shell would strip trailing
        // whitespace. strip it back off here
        "if ! CDPATH= cd -- \"${1%?}\" 2>/dev/null;then "
            "printf 'z: could not cd to `%s'\\''\\n' \"${1%?}\";"
            "return 1;"
        "fi;"
    "}\n"

    "__z_check(){ "
        // emulate the argument checking behavior, returning non-zero
        // if any non-search action would be taken
        "while :;do "
            "case \"$1\" in "
                "-*[afS]*)"
                    "return 1;;"
                "--)"
                    "return 0;;"
                "-*)"
                    ";;"
                "*)"
                    "return 0;"
            "esac;"
            "shift;"
        "done;"
        "return 0;"
    "}\n"

    "z(){ "
        "if __z_check \"$@\";then "
            "__z_selection=\"$(command z \"$@\")\";"
            "__z_status=$?;"
            "if test $__z_status -eq 0;then "
                "__z_cd \"$__z_selection\";"
            "else "
                "return $__z_status;"
            "fi;"
        "else "
            "command z \"$@\";"
        "fi;"
    "}";
// clang-format on

int main(int argc, char **argv) {
  zsql_env_init(argc, argv);
  zsql_error *err = NULL;

  // option parsing

  zsql_behavior behavior = ZSQL_BEHAVIOR_SEARCH;
  zsql_case_sensitivity case_sensitivity = ZSQL_CASE_SMART;

  int ch;
  while ((ch = getopt(argc, argv, "acfiS")) >= 0) {
    switch (ch) {
    case 'a':
      behavior = ZSQL_BEHAVIOR_ADD;
      break;
    case 'c':
      case_sensitivity = ZSQL_CASE_SENSITIVE;
      break;
    case 'f':
      behavior = ZSQL_BEHAVIOR_FORGET;
      break;
    case 'i':
      case_sensitivity = ZSQL_CASE_IGNORE;
      break;
    case 'S':
      if (printf("%s", script) < 0) {
        err = zsql_error_from_errno(err);
      }
      goto exit;
    case '?':
      return EXIT_FAILURE;
    }
  }
  if (optind >= argc) {
    err = zsql_error_from_text("no search specified", err);
    goto exit;
  }
  if (behavior == ZSQL_BEHAVIOR_ADD) {
    if (argc - optind > 1) {
      err = zsql_error_from_text("invalid add with multiple args", err);
      goto exit;
    }
  }

  // db init

  if (sqlite3_initialize() != SQLITE_OK) {
    err = zsql_error_from_text("failed to initialize sqlite", err);
    goto exit;
  }

  sqlite3 *conn;
  if ((err = zsql_open(&conn)) != NULL) {
    goto exit;
  }

  if ((err = zsql_migrate(conn)) != NULL) {
    goto cleanup_sql;
  }

  // behavior

  switch (behavior) {
  case ZSQL_BEHAVIOR_ADD: {
    if ((err = zsql_add(conn, argv[optind], strlen(argv[optind]))) != NULL) {
      goto cleanup_sql;
    }
    break;
  }
  case ZSQL_BEHAVIOR_FORGET:
  case ZSQL_BEHAVIOR_SEARCH: {
    size_t argl_length = argc - optind;
    size_t *argl = malloc(argl_length * sizeof(*argl));
    if (argl == NULL) {
      err = zsql_error_from_errno(err);
      goto cleanup_sql;
    }

    size_t search_length = 0;
    for (size_t arg_idx = 0; arg_idx < argl_length; ++arg_idx) {
      search_length += (argl[arg_idx] = strlen(argv[optind + arg_idx]));
    }

    // smart case

    utf8proc_option_t utf8proc_options = UTF8PROC_COMPAT | UTF8PROC_COMPOSE |
                                         UTF8PROC_IGNORE | UTF8PROC_LUMP |
                                         UTF8PROC_STRIPNA;
    if (case_sensitivity == ZSQL_CASE_IGNORE) {
      utf8proc_options |= UTF8PROC_CASEFOLD;
    } else if (case_sensitivity == ZSQL_CASE_SMART) {
      int32_t codepoint;
      for (size_t arg_idx = 0; arg_idx < argl_length; ++arg_idx) {
        size_t offset = 0;
        while (offset < argl[arg_idx]) {
          ssize_t status =
              utf8proc_iterate((uint8_t *)argv[optind + arg_idx] + offset,
                               argl[arg_idx] - offset, &codepoint);
          if (status == UTF8PROC_ERROR_INVALIDUTF8) {
            break;
          } else if (status < 0) {
            err = zsql_error_from_text(utf8proc_errmsg(status), err);
            goto cleanup_sql;
          } else {
            offset += status;
            if (utf8proc_isupper(codepoint)) {
              case_sensitivity = ZSQL_CASE_SENSITIVE;
              goto end_detectcase;
            }
          }
        }
      }
      case_sensitivity = ZSQL_CASE_IGNORE;
      utf8proc_options |= UTF8PROC_CASEFOLD;
    end_detectcase:;
    }

    // pessimistically allocate more space than is needed to avoid
    // reallocating later except in pathological cases
    search_length *= 2;
    int32_t *runes = malloc(search_length * sizeof(*runes));
    if (runes == NULL) {
      err = zsql_error_from_errno(err);
      goto cleanup_argl;
    }

    size_t runes_length = 0;
    for (size_t arg_idx = 0; arg_idx < argl_length; ++arg_idx) {
    retry_decompose:;
      size_t remaining_length = search_length - runes_length;
      ssize_t status = utf8proc_decompose((uint8_t *)argv[optind + arg_idx],
                                          argl[arg_idx], runes + runes_length,
                                          remaining_length, utf8proc_options);
      if (status < 0) {
        err = zsql_error_from_text(utf8proc_errmsg(status), err);
        goto cleanup_runes;
      } else if ((size_t)status > remaining_length) {
        search_length *= 2;
        void *allocation = realloc(runes, search_length * sizeof(*runes));
        if (allocation == NULL) {
          err = zsql_error_from_errno(err);
          goto cleanup_runes;
        }
        runes = allocation;
        goto retry_decompose;
      } else {
        runes_length += status;
      }
    }

    if (behavior == ZSQL_BEHAVIOR_FORGET) {
      if ((err = zsql_forget(conn, runes, runes_length, utf8proc_options)) !=
          NULL) {
        goto cleanup_runes;
      }
    } else if (behavior == ZSQL_BEHAVIOR_SEARCH) {
      if ((err = zsql_search(conn, runes, runes_length, utf8proc_options)) !=
          NULL) {
        goto cleanup_runes;
      }
    }

  cleanup_runes:
    free(runes);
  cleanup_argl:
    free(argl);
    break;
  }
  default:
    err = zsql_error_from_text("inconsistent behavior", err);
    goto exit;
  }

cleanup_sql:
  sqlite3_close(conn);
exit:
  if (err != NULL) {
    zsql_error_print(err);
    zsql_error_free(err);
  }
  return err == NULL ? EXIT_SUCCESS : EXIT_FAILURE;
}
