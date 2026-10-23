#pragma once
// Pure: no Arduino or hardware calls.
#include <stddef.h>

// Largest prefix length <= maxBytes of the NUL-terminated UTF-8 string s that
// ends on a code-point boundary (a multi-byte character is never split).
// A stray continuation or invalid lead byte counts as one byte.
inline size_t utf8FitBytes(const char *s, size_t maxBytes) {
  size_t i = 0;
  while (s[i]) {
    unsigned char c = (unsigned char)s[i];
    size_t len = c < 0xC0 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    size_t k = 1;  // stop early on a truncated sequence
    while (k < len && ((unsigned char)s[i + k] & 0xC0) == 0x80) k++;
    if (i + k > maxBytes) break;
    i += k;
  }
  return i;
}
