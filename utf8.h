#ifndef ZSQL_UTF8_H
#define ZSQL_UTF8_H

#include <stddef.h>
#include <stdint.h>

size_t utf8_to_utf32(uint32_t *restrict runes, const char *str, size_t len);
size_t utf32_to_utf8(char *restrict str, const uint32_t *runes, size_t len);

#endif
