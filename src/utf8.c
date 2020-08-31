#include "utf8.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// fixme: make sure these do nothing when they can't work
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

static const uint32_t invalid_bit = 0x70000000;

// adapted from simdjson's validate_utf8, which is adapted from fuschia
// both under the apache license
// extracts invalid utf8 data as separate runes which are distinct
// from any valid utf8 sequence. this is needed to support OS paths,
// which can contain any byte sequence except null
// assumes runes is long enough to store the entirety of str's runes,
// a length of at most length runes. returns the number of extracted runes
size_t utf8_to_utf32(uint32_t *restrict runes, const char *str, size_t length) {
  const uint8_t *data = (const uint8_t *)str;
  size_t data_pos = 0;

  size_t rune_pos = 0;

  while (data_pos < length) {
    // check if the next 16 bytes are ascii
    size_t next_data_pos = data_pos + 16;
    if (likely(next_data_pos <= length)) {
      // if it is safe to read 16 more bytes, check that they are ascii
      uint64_t v1;
      memcpy(&v1, data + data_pos, sizeof(v1));
      uint64_t v2;
      memcpy(&v2, data + data_pos + sizeof(v1), sizeof(v2));
      uint64_t v = v1 | v2;
      if (likely((v & 0x8080808080808080) == 0)) {
        for (int ii = 0; ii < 16; ++ii) {
          runes[rune_pos++] = data[data_pos++];
        }

        continue;
      }
    }

    const uint8_t byte = data[data_pos];
    if (likely(byte < 0x80)) {
      runes[rune_pos++] = byte;
      ++data_pos;
      continue;
    }

    uint32_t rune;

    if ((byte & 0xe0) == 0xc0) {
      next_data_pos = data_pos + 2;
      if (unlikely(next_data_pos > length)) {
        // non-utf8 sequence: multi-byte sequence at end
        goto copy1;
      }
      if (unlikely((data[data_pos + 1] & 0xc0) != 0x80)) {
        // non-utf8 sequence: multi-byte beginning followed by non-continuation
        goto copy2;
      }
      // range check
      rune =
          ((uint32_t)byte & 0x1f) << 6 | ((uint32_t)data[data_pos + 1] & 0x3f);

      if (unlikely(rune < 0x80 || 0x7ff < rune)) {
        // non-utf8 sequence: invalid multi-byte sequence
        goto copy2;
      }
    } else if ((byte & 0xf0) == 0xe0) {
      next_data_pos = data_pos + 3;
      if (unlikely(next_data_pos > length)) {
        // non-utf8 sequence: multi-byte sequence at end
        goto copy_loop;
      }
      if (unlikely((data[data_pos + 1] & 0xc0) != 0x80 ||
                   (data[data_pos + 2] & 0xc0) != 0x80)) {
        // non-utf8 sequence: multi-byte beginning followed by non-continuation
        goto copy3;
      }
      // range check
      rune = ((uint32_t)byte & 0x0f) << 12 |
             ((uint32_t)data[data_pos + 1] & 0x3f) << 6 |
             ((uint32_t)data[data_pos + 2] & 0x3f);

      if (unlikely(rune < 0x800 || 0xffff < rune ||
                   (0xd7ff < rune && rune < 0xe000))) {
        // non-utf8 sequence: invalid multi-byte sequence
        goto copy3;
      }
    } else if ((byte & 0xf8) == 0xf0) {
      next_data_pos = data_pos + 4;
      if (unlikely(next_data_pos > length)) {
        // non-utf8 sequence: multi-byte sequence at end
        goto copy_loop;
      }
      if (unlikely((data[data_pos + 1] & 0xc0) != 0x80 ||
                   (data[data_pos + 2] & 0xc0) != 0x80 ||
                   (data[data_pos + 3] & 0xc0) != 0x80)) {
        // non-utf8 sequence: multi-byte beginning followed by non-continuation
        goto copy4;
      }
      // range check
      rune = ((uint32_t)byte & 0x07) << 18 |
             ((uint32_t)data[data_pos + 1] & 0x3f) << 12 |
             ((uint32_t)data[data_pos + 2] & 0x3f) << 6 |
             ((uint32_t)data[data_pos + 3] & 0x3f);

      if (unlikely(rune < 0xffff || 0x10ffff < rune)) {
        // non-utf8 sequence: invalid multi-byte sequence
        goto copy4;
      }
    } else {
      // non-utf8 sequence: continuation or other invalid beginning byte
      // flag as invalid then writeback
      next_data_pos = data_pos + 1;
      rune = byte | invalid_bit;
    }

    runes[rune_pos++] = rune;
    data_pos = next_data_pos;
    continue;

  copy_loop:
    runes[rune_pos++] = byte | invalid_bit;
    ++data_pos;
    if (data_pos >= length) {
      break;
    }
    runes[rune_pos++] = data[data_pos++];
    if (data_pos >= length) {
      break;
    }
    runes[rune_pos++] = data[data_pos++];
    break;

  copy4:
    runes[rune_pos++] = data[data_pos++] | invalid_bit;
  copy3:
    runes[rune_pos++] = data[data_pos++] | invalid_bit;
  copy2:
    runes[rune_pos++] = data[data_pos++] | invalid_bit;
  copy1:
    runes[rune_pos++] = data[data_pos++] | invalid_bit;
  }

  return rune_pos;
}

// converts the extended utf32 created by utf8_to_utf32 back to utf8,
// preserving the original invalid bytes
// assumes str is long enough to store runes encoded at utf8, a length
// of at most 4 * length bytes. returns the number of bytes written into str
size_t utf32_to_utf8(char *restrict str, const uint32_t *runes, size_t length) {
  size_t rune_pos = 0;

  uint8_t *data = (uint8_t *)str;
  size_t data_pos = 0;

  while (rune_pos < length) {
    // check if the next 32 bytes are ascii
    if (likely(rune_pos + 8 < length)) {
      uint64_t v1;
      memcpy(&v1, runes + rune_pos, sizeof(v1));
      uint64_t v2;
      memcpy(&v2, runes + rune_pos + sizeof(v1), sizeof(v2));
      uint64_t v3;
      memcpy(&v3, runes + rune_pos + sizeof(v1) + sizeof(v2), sizeof(v3));
      uint64_t v4;
      memcpy(&v4, runes + rune_pos + sizeof(v1) + sizeof(v2) + sizeof(v3),
             sizeof(v4));
      uint64_t v = v1 | v2 | v3 | v4;
      if (likely((v & 0xfff8fff8fff8fff8) == 0)) {
        for (int ii = 0; ii < 8; ++ii) {
          data[data_pos++] = (uint8_t)(runes[rune_pos++] & 0xff);
        }

        continue;
      }
    }

    const uint32_t rune = runes[rune_pos++];
    // NOLINTNEXTLINE(bugprone-branch-clone) intentionally same as non-utf8 path
    if (likely(rune < 0x80)) {
      data[data_pos++] = (uint8_t)(rune & 0xff);
    } else if (rune < 0x800) {
      data[data_pos++] = 0xc0 | (uint8_t)((rune >> 6) & 0xff);
      data[data_pos++] = 0x80 | (uint8_t)(rune & 0x3f);
    } else if (rune < 0x10000) {
      data[data_pos++] = 0xe0 | (uint8_t)((rune >> 12) & 0xff);
      data[data_pos++] = 0x80 | (uint8_t)((rune >> 6) & 0x3f);
      data[data_pos++] = 0x80 | (uint8_t)(rune & 0x3f);
    } else if (rune < 0x110000) {
      data[data_pos++] = 0xf0 | (uint8_t)((rune >> 18) & 0xff);
      data[data_pos++] = 0x80 | (uint8_t)((rune >> 12) & 0x3f);
      data[data_pos++] = 0x80 | (uint8_t)((rune >> 6) & 0x3f);
      data[data_pos++] = 0x80 | (uint8_t)(rune & 0x3f);
    } else {
      // source is a non-utf8 sequence, write back the truncation literally
      data[data_pos++] = (uint8_t)(rune & 0xff);
    }
  }

  return data_pos;
}
