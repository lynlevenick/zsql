#ifndef Z_UTF8_H
#define Z_UTF8_H

#include <stddef.h>
#include <stdint.h>

size_t utf8_to_utf32(uint32_t *restrict, const char *, size_t);
size_t utf32_to_utf8(char *restrict, const uint32_t *, size_t);

#endif
