#ifndef Z_FUZZY_MATCH_H
#define Z_FUZZY_MATCH_H

#include <stddef.h>
#include <stdint.h>

extern int fuzzy_match(const uint32_t *haystack, size_t haystack_length,
                       const uint32_t *needle, size_t needle_length);

#endif
