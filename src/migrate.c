#include "migrate.h"

#include <stdint.h>
#include <stdio.h>

#include "error.h"
#include "sqlh.h"
#include "sqlite3.h"

// migrations; each array is executed as a new transaction
// new arrays are automatically run if the database version is
// below what the program specifies
static const char *const *const migrations[] = {(const char *const[]){
    "CREATE TABLE dirs("
    "dir BLOB NOT NULL UNIQUE,"
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

static zsql_error *current_schema_version(sqlite3 *db, int *schema_version) {
  zsql_error *err = NULL;

  sqlite3_stmt *stmt;
  if ((err = sqlh_prepare_static(db, "PRAGMA user_version", &stmt)) != NULL) {
    goto exit;
  }

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    err = zsql_error_from_sqlite(db, NULL);
    goto cleanup;
  }

  *schema_version = sqlite3_column_int(stmt, 0);

cleanup:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
}

static zsql_error *set_schema_version(sqlite3 *db, int schema_version) {
  // 20 chars for pragma, 10 chars for positive schema version, 1 char null
  char buffer[31];
  const int buffer_length =
      sprintf(buffer, "PRAGMA user_version=%d", schema_version);
  return sqlh_exec(db, buffer, buffer_length + 1);
}

// mostly-constant way to detect little-endian systems
#define system_little_endian                                                   \
  (((union {                                                                   \
     uint16_t x;                                                               \
     uint8_t c;                                                                \
   }){.x = 1})                                                                 \
       .c)

static zsql_error *current_schema_little_endian(sqlite3 *db,
                                                int *schema_little_endian) {
  zsql_error *err = NULL;

  sqlite3_stmt *stmt;
  if ((err = sqlh_prepare_static(
           db, "SELECT value FROM meta WHERE key='little_endian'", &stmt)) !=
      NULL) {
    goto exit;
  }

  const int status = sqlite3_step(stmt);
  if (status == SQLITE_DONE) {
    *schema_little_endian = -1;
  } else if (status == SQLITE_ROW) {
    *schema_little_endian = sqlite3_column_int(stmt, 0);
  } else {
    err = zsql_error_from_sqlite(db, err);
    goto cleanup;
  }

cleanup:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
}

static zsql_error *set_schema_little_endian(sqlite3 *db,
                                            int schema_little_endian) {
  zsql_error *err = NULL;

  sqlite3_stmt *stmt;
  if ((err = sqlh_prepare_static(
           db,
           "INSERT INTO meta(key,value)VALUES('little_endian',?1)"
           "ON CONFLICT(key)DO UPDATE SET value=excluded.value",
           &stmt)) != NULL) {
    goto exit;
  }

  if (sqlite3_bind_int(stmt, 1, schema_little_endian) != SQLITE_OK) {
    err = zsql_error_from_sqlite(db, err);
    goto cleanup;
  }

  if (sqlite3_step(stmt) != SQLITE_DONE) {
    err = zsql_error_from_sqlite(db, err);
    goto cleanup;
  }

cleanup:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
}

// swaps the endianness of value; should compile down to a single instruction
// on systems that support it
static inline uint32_t swap_endianness(uint32_t value) {
  return ((value & 0xff000000) >> 24) | ((value & 0x00ff0000) >> 8) |
         ((value & 0x0000ff00) << 8) | ((value & 0x000000ff) << 24);
}

// migrate db up through the latest migrations available, failing if the
// database is for a version higher than what this program supports
zsql_error *zsql_migrate(sqlite3 *db) {
  zsql_error *err = NULL;

  // maintain schema via migrations

  int schema_version;
  if ((err = current_schema_version(db, &schema_version)) != NULL) {
    goto exit;
  }

  if (schema_version < 0 || schema_version > SCHEMA_VERSION) {
    // cannot operate on weird schemas we aren't aware of
    err = zsql_error_from_text("database schema newer than application", err);
    goto exit;
  }

  if (schema_version < SCHEMA_VERSION) {
    if ((err = sqlh_exec_static(db, "BEGIN EXCLUSIVE")) != NULL) {
      goto exit;
    }

    // once sqlite is in an exclusive transaction, there are no other readers or
    // writers. pull the schema version again to make sure a migration is not
    // performed twice
    if ((err = current_schema_version(db, &schema_version)) != NULL) {
      goto rollback;
    }

    if (schema_version < SCHEMA_VERSION) {
      while (schema_version < SCHEMA_VERSION) {
        for (const char *const *sql = migrations[schema_version]; *sql != NULL;
             ++sql) {
          if ((err = sqlh_exec(db, *sql, -1)) != NULL) {
            goto rollback;
          }
        }

        ++schema_version;
      }

      if ((err = set_schema_version(db, schema_version)) != NULL) {
        goto rollback;
      }
    }

    if ((err = sqlh_exec_static(db, "COMMIT")) != NULL) {
      goto rollback;
    }
  }

  // maintain metadata
  // this has to be done in a separate pass, since it depends on data
  // that is created in the initial schema setup

  int schema_little_endian;
  if ((err = current_schema_little_endian(db, &schema_little_endian)) != NULL) {
    goto exit;
  }

  if (schema_little_endian != system_little_endian) {
    if ((err = sqlh_exec_static(db, "BEGIN EXCLUSIVE")) != NULL) {
      goto exit;
    }

    if ((err = current_schema_little_endian(db, &schema_little_endian)) !=
        NULL) {
      goto rollback;
    }

    if (schema_little_endian != system_little_endian) {
      // todo: convert endianness of dirs in db treated as uint32_t

      if ((err = set_schema_little_endian(db, system_little_endian)) != NULL) {
        goto rollback;
      }
    }

    if ((err = sqlh_exec_static(db, "COMMIT")) != NULL) {
      goto rollback;
    }
  }

exit:
  return err;

rollback:
  // the error might have caused a rollback, so check if sqlite has autocommit
  // disabled. if it does (return is zero), then the transaction is still
  // happening and a rollback should be performed
  if (!sqlite3_get_autocommit(db)) {
    if (sqlh_exec_static(db, "ROLLBACK") != NULL) {
      // fixme: error while trying to rollback? how could one recover from
      // this state?
    }
  }

  return err;
}
