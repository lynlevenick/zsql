#ifndef ZSQL_FUZZY_SEARCH_H
#define ZSQL_FUZZY_SEARCH_H

#include <inttypes.h>
#include <stddef.h>

#include "error.h"

extern zsql_error *fuzzy_search(float *score, const int32_t *haystack,
                                size_t haystack_length, const int32_t *needle,
                                size_t needle_length);

#endif
