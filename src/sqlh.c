#include "sqlh.h"

#include <stdlib.h>

#include "sqlite3.h"
#include "status.h"

int sqlh_exec(sqlite3 *db, const char *sql, int bufsize) {
  int result = ZSQL_OK;

  sqlite3_stmt *stmt;
  if ((result = sqlite3_prepare_v2(db, sql, bufsize, &stmt, NULL)) !=
      SQLITE_OK) {
    goto exit;
  }

  const int status = sqlite3_step(stmt);
  if (status != SQLITE_DONE && status != SQLITE_ROW) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

cleanup:
  if (sqlh_finalize(stmt) != ZSQL_OK) {
    // fixme: what to do in the double-failure case?
    result = ZSQL_ERROR;
  }
exit:
  return result;
}
