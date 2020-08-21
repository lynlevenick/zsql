#ifndef ZSQL_SQLH_H
#define ZSQL_SQLH_H

#include <string.h>

#include "errno.h"
#include "sqlite3.h"

#define sqlh_exec_static(db, sql) (sqlh_exec((db), (sql ""), strlen(sql) + 1))

#define sqlh_prepare_static(db, sql, stmt)                                     \
  (sqlite3_prepare_v2((db), (sql ""), strlen((sql)) + 1, (stmt), NULL) ==      \
           SQLITE_OK                                                           \
       ? ZSQL_OK                                                               \
       : ZSQL_ERROR)
#define sqlh_prepare(db, sql, len, stmt)                                       \
  (sqlite3_prepare_v2((db), (sql), (len), (stmt), NULL) == SQLITE_OK           \
       ? ZSQL_OK                                                               \
       : ZSQL_ERROR)

#define sqlh_finalize(stmt)                                                    \
  (sqlite3_finalize((stmt)) == SQLITE_OK ? ZSQL_OK : ZSQL_ERROR)

extern int sqlh_exec(sqlite3 *db, const char *sql, int bufsize);

#endif
