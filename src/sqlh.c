#include "sqlh.h"

#include <sqlite3.h>

#include "error.h"

// helper to prepare and execute a statement without returning
// any rows, skipping the complications of sqlite3_exec
zsql_error *sqlh_exec(sqlite3 *db, const char *sql, int bufsize) {
  zsql_error *err = NULL;

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(db, sql, bufsize, &stmt, NULL) != SQLITE_OK) {
    err = zsql_error_from_sqlite(db, err);
    goto exit;
  }

  int status = sqlite3_step(stmt);
  if (status != SQLITE_DONE && status != SQLITE_ROW) {
    err = zsql_error_from_sqlite(db, err);
    goto cleanup_stmt;
  }

cleanup_stmt:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
}
