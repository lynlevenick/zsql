#include "migrate.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "sqlh.h"
#include "sqlite3.h"
#include "status.h"

static const char *const *const migrations[] = {(const char *const[]){
    "CREATE TABLE dirs("
    "dir BLOB NOT NULL UNIQUE CHECK (length(dir)%4==0),"
    "frecency INT NOT NULL DEFAULT 1)",

    "CREATE INDEX index_by_frecency_and_dir ON dirs(frecency, dir)",

    "CREATE TABLE meta(key TEXT NOT NULL UNIQUE,value NUMERIC NOT NULL)",

    "CREATE TRIGGER trigger_on_update_forget "
    "AFTER UPDATE ON dirs "
    "WHEN(SELECT SUM(frecency)FROM dirs)>=5000 "
    "BEGIN "
    "UPDATE dirs SET frecency=CAST(frecency*0.9 AS INT);"
    "DELETE FROM dirs WHERE frecency=0;"
    "END",
    NULL}};
static const int SCHEMA_VERSION = sizeof(migrations) / sizeof(*migrations);

static int current_schema_version(sqlite3 *db, int *schema_version) {
  int result = ZSQL_OK;

  sqlite3_stmt *stmt;
  if ((result = sqlh_prepare_static(db, "PRAGMA user_version", &stmt)) !=
      ZSQL_OK) {
    goto exit;
  }

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

  *schema_version = sqlite3_column_int(stmt, 0);

cleanup:
  if (sqlh_finalize(stmt) != ZSQL_OK) {
    // fixme: what to do in the double-failure case?
    result = ZSQL_ERROR;
  }
exit:
  return result;
}

static int set_schema_version(sqlite3 *db, int schema_version) {
  // 20 chars for pragma, 10 chars for positive schema version, 1 char null
  char buffer[31];
  const int buffer_length =
      sprintf(buffer, "PRAGMA user_version=%d", schema_version);
  return sqlh_exec(db, buffer, buffer_length + 1);
}

#define system_little_endian                                                   \
  (((union {                                                                   \
     uint16_t x;                                                               \
     uint8_t c;                                                                \
   }){.x = 1})                                                                 \
       .c)

static int current_schema_little_endian(sqlite3 *db,
                                        int *schema_little_endian) {
  int result = ZSQL_OK;

  sqlite3_stmt *stmt;
  if ((result = sqlh_prepare_static(
           db, "SELECT value FROM meta WHERE key='little_endian'", &stmt)) !=
      ZSQL_OK) {
    goto exit;
  }

  const int status = sqlite3_step(stmt);
  if (status == SQLITE_DONE) {
    *schema_little_endian = -1;
  } else if (status == SQLITE_ROW) {
    *schema_little_endian = sqlite3_column_int(stmt, 0);
  } else {
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

static int set_schema_little_endian(sqlite3 *db, int schema_little_endian) {
  int result = ZSQL_OK;

  sqlite3_stmt *stmt;
  if ((result = sqlh_prepare_static(
           db,
           "INSERT INTO meta(key,value)VALUES('little_endian',?1)"
           "ON CONFLICT(key)DO UPDATE SET value=excluded.value",
           &stmt)) != ZSQL_OK) {
    goto exit;
  }

  if (sqlite3_bind_int(stmt, 1, schema_little_endian) != SQLITE_OK) {
    result = ZSQL_ERROR;
    goto cleanup;
  }

  if (sqlite3_step(stmt) != SQLITE_DONE) {
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

static inline uint32_t swap_endianness(uint32_t value) {
  return ((value & 0xff000000) >> 24) | ((value & 0x00ff0000) >> 8) |
         ((value & 0x0000ff00) << 8) | ((value & 0x000000ff) << 24);
}

int zsql_migrate(sqlite3 *db) {
  int result = ZSQL_OK;

  // maintain schema via migrations

  int schema_version;
  if ((result = current_schema_version(db, &schema_version)) != ZSQL_OK) {
    goto exit;
  }

  if (schema_version < 0 || schema_version > SCHEMA_VERSION) {
    // cannot operate on weird schemas we aren't aware of
    result = ZSQL_ERROR;
    goto exit;
  }

  if (schema_version < SCHEMA_VERSION) {
    if ((result = sqlh_exec_static(db, "BEGIN EXCLUSIVE")) != ZSQL_OK) {
      goto exit;
    }

    // once sqlite is in an exclusive transaction, there are no other readers or
    // writers. pull the schema version again to make sure a migration is not
    // performed twice
    if ((result = current_schema_version(db, &schema_version)) != ZSQL_OK) {
      goto rollback;
    }

    if (schema_version < SCHEMA_VERSION) {
      while (schema_version < SCHEMA_VERSION) {
        for (const char *const *sql = migrations[schema_version]; *sql != NULL;
             ++sql) {
          if ((result = sqlh_exec(db, *sql, -1)) != ZSQL_OK) {
            goto rollback;
          }
        }

        ++schema_version;
      }

      if ((result = set_schema_version(db, schema_version)) != ZSQL_OK) {
        goto rollback;
      }
    }

    if ((result = sqlh_exec_static(db, "COMMIT")) != ZSQL_OK) {
      goto rollback;
    }
  }

  // maintain metadata
  // this has to be done in a separate pass, since it depends on data
  // that is created in the initial schema setup

  int schema_little_endian;
  if ((result = current_schema_little_endian(db, &schema_little_endian)) !=
      ZSQL_OK) {
    goto exit;
  }

  if (schema_little_endian != system_little_endian) {
    if ((result = sqlh_exec_static(db, "BEGIN EXCLUSIVE")) != ZSQL_OK) {
      goto exit;
    }

    if ((result = current_schema_little_endian(db, &schema_little_endian)) !=
        ZSQL_OK) {
      goto rollback;
    }

    if (schema_little_endian != system_little_endian) {
      // todo: convert endianness of dirs in db treated as uint32_t

      if ((result = set_schema_little_endian(db, system_little_endian)) !=
          ZSQL_OK) {
        goto rollback;
      }
    }

    if ((result = sqlh_exec_static(db, "COMMIT")) != ZSQL_OK) {
      goto rollback;
    }
  }

exit:
  return result;

rollback:
  // the error might have caused a rollback, so check if sqlite has autocommit
  // disabled. if it does (return is zero), then the transaction is still
  // happening and a rollback should be performed
  if (!sqlite3_get_autocommit(db)) {
    if (sqlh_exec_static(db, "ROLLBACK") != ZSQL_OK) {
      // fixme: error while trying to rollback? how could one recover from
      // this state?
    }
  }

  return result;
}
