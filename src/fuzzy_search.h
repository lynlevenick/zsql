#ifndef ZSQL_FUZZY_SEARCH_H
#define ZSQL_FUZZY_SEARCH_H

#include <stddef.h>
#include <stdint.h>

extern int fuzzy_search(const uint32_t *haystack, size_t haystack_length,
                        const uint32_t *needle, size_t needle_length);

#endif
