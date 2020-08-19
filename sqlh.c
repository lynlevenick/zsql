#include "sqlh.h"

#include <stdlib.h>

#include "errno.h"
#include "sqlite3.h"

int sqlh_exec_impl(sqlite3 *db, const char *sql, int bufsize) {
  int result = Z_OK;

  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(db, sql, bufsize, &stmt, NULL) != SQLITE_OK) {
    result = Z_ERROR;
    goto exit;
  }

  int status = sqlite3_step(stmt);
  if (status != SQLITE_DONE && status != SQLITE_ROW) {
    result = Z_ERROR;
    goto cleanup;
  }

cleanup:
  if (sqlh_finalize(stmt) != Z_OK) {
    result = Z_ERROR;
  }
exit:
  return result;
}
