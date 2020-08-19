#include "fuzzy_match.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define mem_uint32_t(haystack, haystack_length, needle)                        \
  _Generic((*haystack), uint32_t                                               \
           : ((uint32_t *)mem_uint32_t__impl((haystack), (haystack_length),    \
                                             (needle))),                       \
             default                                                           \
           : mem_uint32_t__impl((haystack), (haystack_length), (needle)))

static inline const uint32_t *mem_uint32_t__impl(const uint32_t *haystack,
                                                 size_t haystack_length,
                                                 uint32_t needle) {
  while (haystack_length-- > 0) {
    if (*haystack == needle) {
      return haystack;
    }

    haystack += 1;
  }

  return NULL;
}

int fuzzy_match(const uint32_t *haystack, size_t haystack_length,
                const uint32_t *needle, size_t needle_length) {
  if (needle_length <= 0) {
    // zero length needle matches all haystacks equally well
    return 0;
  }

  const uint32_t *needle_in_haystack =
      mem_uint32_t(haystack, haystack_length, needle[0]);
  if (needle_in_haystack == NULL) {
    // couldn't find first character; no match
    return INT_MIN;
  }

  size_t needle_idx = 0;
  for (size_t haystack_idx = needle_in_haystack - haystack;
       haystack_idx < haystack_length; ++haystack_idx) {
    // todo: compute bonuses based on word boundary etc?

    if (needle_idx < needle_length) {
      if (haystack[haystack_idx] == needle[needle_idx]) {
        ++needle_idx;
      }
    }
  }
  if (needle_idx < needle_length) {
    // didn't match the entire needle; no match
    return INT_MIN;
  }

  return 0;
}
