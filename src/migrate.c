#include "migrate.h"

#include <sqlite3.h>
#include <stdio.h>

#include "error.h"
#include "sqlh.h"

#define index_by_visits_and_dir                                                \
  "CREATE INDEX index_by_visits_and_dir ON dirs(visits, dir)"
#define index_by_visited_at                                                    \
  "CREATE INDEX index_by_visited_at ON dirs(visited_at)"
#define trigger_on_insert_forget                                               \
  "CREATE TRIGGER trigger_on_insert_forget "                                   \
  "INSERT ON dirs "                                                            \
  "WHEN(SELECT SUM(visits)FROM dirs)+NEW.visits>=5000 "                        \
  "BEGIN "                                                                     \
  "UPDATE dirs SET visits=CAST(visits*0.9 AS INT);"                            \
  "DELETE FROM dirs WHERE visits=0;"                                           \
  "END"
#define trigger_on_update_forget                                               \
  "CREATE TRIGGER trigger_on_update_forget "                                   \
  "AFTER UPDATE ON dirs "                                                      \
  "WHEN(SELECT SUM(visits)FROM dirs)>=5000 "                                   \
  "BEGIN "                                                                     \
  "UPDATE dirs SET visits=CAST(visits*0.9 AS INT);"                            \
  "DELETE FROM dirs WHERE visits=0;"                                           \
  "END"

// each array is considered a database version
// new arrays are automatically run if the database version is
// below what the program specifies
static const char *const *const migrations[] = {
    (const char *const[]){"CREATE TABLE dirs("
                          "dir BLOB NOT NULL UNIQUE,"
                          "visits INT NOT NULL DEFAULT 1)",

                          index_by_visits_and_dir, trigger_on_insert_forget,
                          trigger_on_update_forget, NULL},
    (const char *const[]){
        "ALTER TABLE dirs RENAME TO old_dirs",

        "CREATE TABLE dirs("
        "dir BLOB NOT NULL UNIQUE,"
        "visits INT NOT NULL DEFAULT 1,"
        "visited_at DATETIME NOT NULL)",

        "INSERT INTO dirs SELECT *,CURRENT_TIMESTAMP visited_at FROM old_dirs",
        "DROP TABLE old_dirs",

        index_by_visits_and_dir, index_by_visited_at, trigger_on_insert_forget,
        trigger_on_update_forget, NULL},
    (const char *const[]){
        "ALTER TABLE dirs RENAME TO old_dirs",

        "CREATE TABLE dirs("
        "id INTEGER PRIMARY KEY,"
        "dir BLOB NOT NULL UNIQUE,"
        "visits INT NOT NULL DEFAULT 1,"
        "visited_at DATETIME NOT NULL)",

        "INSERT INTO dirs SELECT oid id,* FROM old_dirs", "DROP TABLE old_dirs",

        index_by_visits_and_dir, index_by_visited_at, trigger_on_insert_forget,
        trigger_on_update_forget, NULL}};
static const int SCHEMA_VERSION = sizeof(migrations) / sizeof(*migrations);

static zsql_error *current_schema_version(sqlite3 *conn, int *schema_version) {
  zsql_error *err = NULL;

  sqlite3_stmt *stmt;
  if ((err = sqlh_prepare_static(conn, "PRAGMA user_version", &stmt)) != NULL) {
    goto exit;
  }

  if (sqlite3_step(stmt) != SQLITE_ROW) {
    err = zsql_error_from_sqlite(conn, NULL);
    goto cleanup_stmt;
  }

  *schema_version = sqlite3_column_int(stmt, 0);

cleanup_stmt:
  err = sqlh_finalize(stmt, err);
exit:
  return err;
}

static zsql_error *set_schema_version(sqlite3 *conn, int schema_version) {
  // 20 chars for pragma, 10 chars for positive schema version, 1 char null
  char buffer[31];
  const int buffer_length =
      sprintf(buffer, "PRAGMA user_version=%d", schema_version);
  return sqlh_exec(conn, buffer, buffer_length + 1);
}

// migrate conn up through the latest migrations available, failing if the
// database is for a version higher than what this program supports
zsql_error *zsql_migrate(sqlite3 *conn) {
  zsql_error *err = NULL;

  // maintain schema via migrations

  int schema_version;
  if ((err = current_schema_version(conn, &schema_version)) != NULL) {
    goto exit;
  }

  if (schema_version < 0 || schema_version > SCHEMA_VERSION) {
    // cannot operate on weird schemas we aren't aware of
    err = zsql_error_from_text("database schema newer than application", err);
    goto exit;
  }

  if (schema_version < SCHEMA_VERSION) {
    if ((err = sqlh_exec_static(conn, "BEGIN EXCLUSIVE")) != NULL) {
      goto exit;
    }

    // once sqlite is in an exclusive transaction, there are no other readers or
    // writers. pull the schema version again to make sure a migration is not
    // performed twice
    if ((err = current_schema_version(conn, &schema_version)) != NULL) {
      goto rollback;
    }

    if (schema_version < SCHEMA_VERSION) {
      while (schema_version < SCHEMA_VERSION) {
        for (const char *const *sql = migrations[schema_version]; *sql != NULL;
             ++sql) {
          if ((err = sqlh_exec(conn, *sql, -1)) != NULL) {
            goto rollback;
          }
        }

        ++schema_version;
      }

      if ((err = set_schema_version(conn, schema_version)) != NULL) {
        goto rollback;
      }
    }

    if ((err = sqlh_exec_static(conn, "COMMIT")) != NULL) {
      goto rollback;
    }
  }

  if (0) { // error path only
  rollback:
    // the error might have caused a rollback, so check if sqlite has autocommit
    // disabled. if it does (return is zero), then the transaction is still
    // happening and a rollback should be performed
    if (!sqlite3_get_autocommit(conn)) {
      if (sqlh_exec_static(conn, "ROLLBACK") != NULL) {
        // fixme: error while trying to rollback? how could one recover from
        // this state?
      }
    }
  }
exit:
  return err;
}
