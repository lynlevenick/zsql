#include "fuzzy_search.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <utf8proc.h>

#include "error.h"

#define SWAP(T, A, B)                                                          \
  do {                                                                         \
    T SWAP = A;                                                                \
    A = B;                                                                     \
    B = SWAP;                                                                  \
  } while (0)

// returns non-zero if ranking is needed
static int fuzzy_match(float *score, const int32_t *haystack,
                       size_t haystack_length, const int32_t *needle,
                       size_t needle_length) {
  if (needle_length == 0) {
    // zero length needle matches everything equally poorly
    *score = 0.0;
    return 0;
  }
  if (needle_length > haystack_length) {
    // needle larger than haystack
    *score = -INFINITY;
    return 0;
  }

  size_t needle_idx = 0;
  for (size_t haystack_idx = 0; haystack_idx < haystack_length;
       ++haystack_idx) {
    if (haystack[haystack_idx] == needle[needle_idx]) {
      ++needle_idx;
      if (needle_idx >= needle_length) { // complete match
        break;
      }
    }
  }

  if (needle_idx < needle_length) {
    // didn't match the entire needle; no match
    *score = -INFINITY;
    return 0;
  }
  if (needle_length == haystack_length) {
    // matched and same lengths, perfect match
    *score = 1e6f;
    return 0;
  }

  return 1;
}

static inline int codepoint_is_word(int32_t codepoint, int previous) {
  utf8proc_category_t utfcat = utf8proc_category(codepoint);
  if (utfcat == UTF8PROC_CATEGORY_MC) {
    return previous;
  }
  return utfcat == UTF8PROC_CATEGORY_LL || utfcat == UTF8PROC_CATEGORY_LU ||
         utfcat == UTF8PROC_CATEGORY_LT || utfcat == UTF8PROC_CATEGORY_LM ||
         utfcat == UTF8PROC_CATEGORY_LO || utfcat == UTF8PROC_CATEGORY_ND;
}

static const float BONUS_BOUNDARY = 6250.f;

static inline zsql_error *
compute_match_bonus(float **match_bonus, const int32_t *string, size_t length) {
  zsql_error *err = NULL;

  float *temp = malloc(length * sizeof(*temp));
  if (temp == NULL) {
    err = zsql_error_from_errno(NULL);
    goto exit;
  }

  int prev_was_word = 0;

  for (size_t idx = 0; idx < length; ++idx) {
    int is_word = codepoint_is_word(string[idx], prev_was_word);
    if (prev_was_word != is_word) {
      temp[idx] = BONUS_BOUNDARY;
    } else {
      temp[idx] = 0.f;
    }

    prev_was_word = is_word;
  }

  *match_bonus = temp;
exit:
  return err;
}

static inline float f32_max(float a, float b) {
  if (a < b) {
    return b;
  } else {
    return a;
  }
}

static const float SCORE_GAP_INNER = -250.f;
static const float SCORE_GAP_LEADING = -75.f;
static const float SCORE_GAP_TRAILING = -150.f;
static const float SCORE_MATCH_CONSECUTIVE = 4500.f;

static inline void
fuzzy_rank_row(const int32_t *haystack, const float *match_bonus,
               size_t haystack_length, const int32_t *needle,
               size_t needle_length, const float *prev_best_with_match,
               const float *prev_best, float *restrict cur_best_with_match,
               float *restrict cur_best, size_t needle_idx) {
  const float gap_score =
      (needle_idx == needle_length - 1) ? SCORE_GAP_TRAILING : SCORE_GAP_INNER;

  float prev_score = -INFINITY;
  for (size_t haystack_idx = 0; haystack_idx < haystack_length;
       ++haystack_idx) {
    if (needle[needle_idx] == haystack[haystack_idx]) {
      float score = -INFINITY;
      if (needle_idx == 0) {
        score = (haystack_idx * SCORE_GAP_LEADING) + match_bonus[haystack_idx];
      } else if (haystack_idx > 0) {
        score = f32_max(prev_best[haystack_idx - 1] + match_bonus[haystack_idx],
                        prev_best_with_match[haystack_idx - 1] +
                            SCORE_MATCH_CONSECUTIVE);
      }

      cur_best_with_match[haystack_idx] = score;
      cur_best[haystack_idx] = prev_score =
          f32_max(score, prev_score + gap_score);
    } else {
      cur_best_with_match[haystack_idx] = -INFINITY;
      cur_best[haystack_idx] = prev_score = prev_score + gap_score;
    }
  }
}

static zsql_error *fuzzy_rank(float *score, const int32_t *haystack,
                              size_t haystack_length, const int32_t *needle,
                              size_t needle_length) {
  zsql_error *err = NULL;

  float *match_bonus;
  if ((err = compute_match_bonus(&match_bonus, haystack, haystack_length)) !=
      NULL) {
    goto exit;
  }

  float *prev_best_with_match =
      malloc(haystack_length * sizeof(*prev_best_with_match));
  if (prev_best_with_match == NULL) {
    err = zsql_error_from_errno(err);
    goto cleanup_match_bonus;
  }
  float *prev_best = malloc(haystack_length * sizeof(*prev_best));
  if (prev_best == NULL) {
    err = zsql_error_from_errno(err);
    goto cleanup_prev_best_with_match;
  }
  float *cur_best_with_match =
      malloc(haystack_length * sizeof(*cur_best_with_match));
  if (cur_best_with_match == NULL) {
    err = zsql_error_from_errno(err);
    goto cleanup_prev_best;
  }
  float *cur_best = malloc(haystack_length * sizeof(*cur_best));
  if (cur_best == NULL) {
    err = zsql_error_from_errno(err);
    goto cleanup_cur_best_with_match;
  }

  for (size_t needle_idx = 0; needle_idx < needle_length; ++needle_idx) {
    fuzzy_rank_row(haystack, match_bonus, haystack_length, needle,
                   needle_length, prev_best_with_match, prev_best,
                   cur_best_with_match, cur_best, needle_idx);

    SWAP(float *, cur_best_with_match, prev_best_with_match);
    SWAP(float *, cur_best, prev_best);
  }

  *score = prev_best[haystack_length - 1];

cleanup_cur_best:
  free(cur_best);
cleanup_cur_best_with_match:
  free(cur_best_with_match);
cleanup_prev_best:
  free(prev_best);
cleanup_prev_best_with_match:
  free(prev_best_with_match);
cleanup_match_bonus:
  free(match_bonus);
exit:
  return err;
}

zsql_error *fuzzy_search(float *score, const int32_t *haystack,
                         size_t haystack_length, const int32_t *needle,
                         size_t needle_length) {
  if (fuzzy_match(score, haystack, haystack_length, needle, needle_length) !=
      0) {
    return fuzzy_rank(score, haystack, haystack_length, needle, needle_length);
  }

  return NULL;
}
