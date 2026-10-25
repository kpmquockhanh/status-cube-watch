#pragma once
// Control 09 (cube -> Mac, fw_rev 10): the cube answers a Claude prompt that is
// waiting on the Mac. Pure: no Arduino, host-tested in sim/tests/answer_frame_test.cpp.
//
//   [0x09][la][agent id: la bytes][lp][prompt id: lp bytes][key]
//
// Both ids are the bridge's (at most 15 printable ASCII bytes), `key` is the one
// ASCII digit of the tapped option.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ble_frame.h"

constexpr size_t BLE_ANSWER_ID_MAX = 15;
constexpr size_t BLE_ANSWER_MAX = 1 + 1 + BLE_ANSWER_ID_MAX + 1 + BLE_ANSWER_ID_MAX + 1;  // 34

namespace answer_detail {
// Length of a usable id, or 0 (NULL, empty, too long, not printable ASCII).
inline size_t idLen(const char *s) {
  if (!s) return 0;
  size_t n = 0;
  for (; s[n]; n++) {
    if (n >= BLE_ANSWER_ID_MAX) return 0;
    if ((uint8_t)s[n] < 0x20 || (uint8_t)s[n] > 0x7E) return 0;
  }
  return n;
}
}  // namespace answer_detail

// Writes the message to `out` and returns its length, or 0 when an id is bad,
// `key` is not '1'..'9' or `cap` is too small. `out` is untouched on 0.
inline size_t answerEncode(const char *agent, const char *prompt, char key, uint8_t *out, size_t cap) {
  const size_t la = answer_detail::idLen(agent), lp = answer_detail::idLen(prompt);
  if (!la || !lp || key < '1' || key > '9' || !out) return 0;
  const size_t n = 1 + 1 + la + 1 + lp + 1;
  if (cap < n) return 0;
  size_t i = 0;
  out[i++] = BLE_CTRL_ANSWER;
  out[i++] = (uint8_t)la;
  memcpy(out + i, agent, la);
  i += la;
  out[i++] = (uint8_t)lp;
  memcpy(out + i, prompt, lp);
  i += lp;
  out[i++] = (uint8_t)key;
  return i;
}
