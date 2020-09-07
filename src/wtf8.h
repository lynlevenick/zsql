#ifndef ZSQL_WTF8_H
#define ZSQL_WTF8_H

#include <stddef.h>
#include <stdint.h>

size_t wtf8_to_wtf32(uint32_t *restrict runes, const char *str, size_t length);
size_t wtf32_to_wtf8(char *restrict str, const uint32_t *runes, size_t length);

#endif
