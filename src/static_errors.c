#include "static_errors.h"

#include <inttypes.h>
#include <stdlib.h>

#include "error.h"

#define ZSQL_OOM_MESSAGE "not enough memory to allocate error"
struct {
  void *next;
  uintptr_t opaque;
  char msg[sizeof(ZSQL_OOM_MESSAGE)];
} zsql_error_oom_value = {.next = NULL,
                          .opaque = (uintptr_t) & (zsql_error_oom_value.msg),
                          .msg = ZSQL_OOM_MESSAGE};
zsql_error *zsql_error_oom = (zsql_error *)&zsql_error_oom_value;
