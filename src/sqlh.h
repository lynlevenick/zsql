#ifndef ZSQL_SQLH_H
#define ZSQL_SQLH_H

#include <sqlite3.h>
#include <string.h>

#include "error.h"

#define sqlh_exec_static(conn, sql) (sqlh_exec((conn), (sql ""), strlen(sql) + 1))

#define sqlh_prepare_static(conn, sql, stmt)                                     \
  (sqlite3_prepare_v2((conn), (sql ""), strlen((sql)) + 1, (stmt), NULL) ==      \
           SQLITE_OK                                                           \
       ? NULL                                                                  \
       : zsql_error_from_sqlite((conn), NULL))
#define sqlh_prepare(conn, sql, length, stmt)                                    \
  (sqlite3_prepare_v2((conn), (sql), (length), (stmt), NULL) == SQLITE_OK        \
       ? NULL                                                                  \
       : zsql_error_from_sqlite((conn), NULL))

#define sqlh_finalize(stmt, err)                                               \
  (sqlite3_finalize((stmt)) == SQLITE_OK ? err                                 \
                                         : zsql_error_from_sqlite((conn), err))

extern zsql_error *sqlh_exec(sqlite3 *conn, const char *sql, int bufsize);

#endif
