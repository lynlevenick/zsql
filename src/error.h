#ifndef ZSQL_ERROR_H
#define ZSQL_ERROR_H

#include <inttypes.h>
#include <sqlite3.h>

typedef struct zsql_error_impl {
  struct zsql_error_impl *next;
  uintptr_t opaque;
  char *msg;
} zsql_error;

extern zsql_error *zsql_error_from_errno(zsql_error *next);
extern zsql_error *zsql_error_from_sqlite(sqlite3 *db, zsql_error *next);
extern zsql_error *zsql_error_from_text(const char *msg, zsql_error *next);
extern void zsql_error_print(const zsql_error *err);
extern void zsql_error_free(zsql_error *err);

#endif
