#ifndef ZSQL_MIGRATE_H
#define ZSQL_MIGRATE_H

#include "error.h"
#include "sqlite3.h"

extern zsql_error *zsql_migrate(sqlite3 *db);

#endif
