#include "fuzzy_search.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "utf8proc.h"

// like memchr but for uint32_t
static inline const int32_t *
mem_int32_t(const int32_t *haystack, size_t haystack_length, int32_t needle) {
  while (haystack_length-- > 0) {
    if (*haystack == needle) {
      return haystack;
    }

    ++haystack;
  }

  return NULL;
}

int fuzzy_search(const int32_t *haystack, size_t haystack_length,
                 const int32_t *needle, size_t needle_length) {
  if (needle_length == 0) {
    // zero length needle matches all haystacks equally well
    return 0;
  }

  const int32_t *needle_in_haystack =
      mem_int32_t(haystack, haystack_length, needle[0]);
  if (needle_in_haystack == NULL) {
    // couldn't find first character; no match
    return INT_MIN;
  }

  /* utf8proc_category_t prev_category = UTF8PROC_CATEGORY_CN; */

  size_t needle_idx = 0;
  for (size_t haystack_idx =
           ((uintptr_t)needle_in_haystack - (uintptr_t)haystack) /
           sizeof(*haystack);
       haystack_idx < haystack_length; ++haystack_idx) {
    /* utf8proc_category_t category = utf8proc_category(haystack[haystack_idx]);
     */
    /* if (category == UTF8PROC_CATEGORY_MN || category == UTF8PROC_CATEGORY_MC
     * || */
    /*     category == UTF8PROC_CATEGORY_ME) { */
    /*   // combining, use previous category */
    /*   category = prev_category; */
    /* } */

    if (needle_idx < needle_length) {
      if (haystack[haystack_idx] == needle[needle_idx]) {
        ++needle_idx;
      }
    }

    /* prev_category = category; */
  }
  if (needle_idx < needle_length) {
    // didn't match the entire needle; no match
    return INT_MIN;
  }

  return 0;
}
