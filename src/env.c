#include "env.h"

#include <stdlib.h>

void zsql_env_init(int argc, char **argv) {
  ARGC = argc;
  ARGV = argv;
  DEBUGGING = getenv("ZSQL_DEBUG") != NULL;
}

int ARGC = 1;
char **ARGV = (char *[]){"<unknown>"};
int DEBUGGING = 0;
