#include "error.h"

#include <errno.h>
#include <sqlite3.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"

#define MAXOF(A, B) ((A) < (B) ? (B) : (A))
#define FSIZEOF(T, F, N)                                                       \
  MAXOF(sizeof(T), offsetof(T, F) + sizeof(((T){0}).F[0]) * (N))

zsql_error zsql_error_oom = {.next = NULL,
                             .opaque = (uintptr_t)&zsql_error_oom.msg,
                             .msg = "not enough memory to allocate error"};

zsql_error *zsql_error_from_errno(zsql_error *next) {
  // fixme: not safe under threading
  return zsql_error_from_text(strerror(errno), next);
}
zsql_error *zsql_error_from_sqlite(sqlite3 *conn, zsql_error *next) {
  // fixme: not safe under threading
  const char *msg = sqlite3_errmsg(conn);

  // fixme: this is an inelegant hack around sqlite finalize erroring
  // with the same message that any earlier steps also errored with
  if (next != NULL && next->opaque == (uintptr_t)msg) {
    return next;
  }

  return zsql_error_from_text(msg, next);
}
zsql_error *zsql_error_from_text(const char *msg, zsql_error *next) {
  zsql_error *err = malloc(sizeof(*err));
  if (err == NULL) {
    return &zsql_error_oom;
  }
  const size_t msg_length = strlen(msg);
  char *msg_copied = malloc(msg_length + 1);
  if (msg_copied == NULL) {
    free(err);
    return &zsql_error_oom;
  }

  memcpy(msg_copied, msg, msg_length);
  msg_copied[msg_length] = 0;

  err->next = next;
  err->opaque = (uintptr_t)msg;
  err->msg = msg_copied;

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

  if (err != &zsql_error_oom) {
    free(err->msg);
    free(err);
  }
}
