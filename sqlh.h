#ifndef Z_SQLH_H
#define Z_SQLH_H

#include <string.h>

#include "meta.h"
#include "sqlite3.h"

#define sqlh_exec(db, sql, ...)                                                \
  (META_IF_EMPTY(__VA_ARGS__)(                                                 \
      sqlh_exec_impl((db), (sql ""), strlen(sql) + 1))(                        \
      sqlh_exec_impl((db), (sql), __VA_ARGS__)))

#define sqlh_prepare(db, sql, bufsize_or_stmt, ...)                            \
  ((META_IF_EMPTY(__VA_ARGS__)(sqlite3_prepare_v2(                             \
       (db), (sql ""), strlen(sql) + 1, (bufsize_or_stmt), NULL))(             \
       sqlite3_prepare_v2((db), (sql), (bufsize_or_stmt), __VA_ARGS__,         \
                          NULL))) == SQLITE_OK                                 \
       ? Z_OK                                                                  \
       : Z_ERROR)

#define sqlh_finalize(stmt)                                                    \
  (sqlite3_finalize((stmt)) == SQLITE_OK ? Z_OK : Z_ERROR)

extern int sqlh_exec_impl(sqlite3 *db, const char *sql, int bufsize);

#endif
