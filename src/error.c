#include "error.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "sqlite3.h"

#define MAXOF(A, B) ((A) < (B) ? (B) : (A))
#define FSIZEOF(T, F, N)                                                       \
  MAXOF(sizeof(T), offsetof(T, F) + sizeof(((T){0}).F[0]) * (N))

static zsql_error not_enough_memory = {
    .next = NULL,
    .opaque = (uintptr_t) & (not_enough_memory.msg),
    .msg = "Not enough memory to allocate error"};

zsql_error *zsql_error_from_errno(zsql_error *next) {
  // fixme: not safe under threading
  return zsql_error_from_text(strerror(errno), next);
}
zsql_error *zsql_error_from_sqlite(sqlite3 *db, zsql_error *next) {
  // fixme: not safe under threading
  return zsql_error_from_text(sqlite3_errmsg(db), next);
}
zsql_error *zsql_error_from_text(const char *msg, zsql_error *next) {
  // fixme: this is an unelegant hack around sqlite finalize erroring
  // with the same message that any earlier steps also errored with
  if (next != NULL) {
    if (next->opaque == (uintptr_t)msg) {
      return next;
    }
  }

  const size_t msg_length = strlen(msg);
  zsql_error *err = malloc(FSIZEOF(zsql_error, msg, msg_length + 1));
  if (err == NULL) {
    return &not_enough_memory;
  }

  err->next = next;
  err->opaque = (uintptr_t)msg;
  memcpy(err->msg, msg, msg_length);
  err->msg[msg_length] = 0;

  return err;
}

void zsql_error_print(const zsql_error *err) {
  fprintf(stderr, "%s: %s\n", ARGV[0], err->msg);
  while (err->next != NULL) {
    err = err->next;
    fprintf(stderr, "\t%s\n", err->msg);
  }
}

void zsql_error_free(zsql_error *err) {
  zsql_error *next = err->next;
  if (next != NULL) {
    err->next = NULL;
    zsql_error_free(next);
  }

  if (err != &not_enough_memory) {
    free(err);
  }
}
