#ifndef ZSQL_MIGRATE_H
#define ZSQL_MIGRATE_H

#include <sqlite3.h>

#include "error.h"

extern zsql_error *zsql_migrate(sqlite3 *conn);

#endif
