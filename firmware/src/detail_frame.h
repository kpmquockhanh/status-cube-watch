#pragma once
// Control 0A and 0B (cube -> Mac, fw_rev 11): the agent detail screen and its
// hat picker. Pure: no Arduino, host-tested in sim/tests/detail_frame_test.cpp.
//
//   [0x0A][la][agent id: la bytes]         the detail screen shows this agent; la 0 = closed
//   [0x0B][la][agent id: la bytes][hat]    set the agent's folder hat
//
// The agent id is the bridge's (1..15 printable ASCII bytes, as for Control 09).
// `hat` is shape * 8 + colour (0..103), HAT_NONE or HAT_AUTO.
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "answer_frame.h"
#include "ble_frame.h"

constexpr uint8_t HAT_MAX_CODE = 103, HAT_NONE = 0xFE, HAT_AUTO = 0xFF;
constexpr size_t BLE_DETAIL_MAX = 1 + 1 + BLE_ANSWER_ID_MAX;     // 17
constexpr size_t BLE_HAT_MAX = 1 + 1 + BLE_ANSWER_ID_MAX + 1;    // 18

// Writes Control 0A to `out` and returns its length, or 0 when the id is bad
// or `cap` is too small. `agent` nullptr or "" = the screen closed (`0a 00`).
// `out` is untouched on 0.
inline size_t detailEncode(const char *agent, uint8_t *out, size_t cap) {
  if (!out) return 0;
  const bool closed = !agent || !agent[0];
  const size_t la = closed ? 0 : answer_detail::idLen(agent);
  if (!closed && !la) return 0;
  const size_t n = 1 + 1 + la;
  if (cap < n) return 0;
  out[0] = BLE_CTRL_DETAIL;
  out[1] = (uint8_t)la;
  if (la) memcpy(out + 2, agent, la);
  return n;
}

// Writes Control 0B to `out` and returns its length, or 0 when the id is bad
// (a hat always names an agent), `hat` is not 0..103 / HAT_NONE / HAT_AUTO or
// `cap` is too small. `out` is untouched on 0.
inline size_t hatEncode(const char *agent, uint8_t hat, uint8_t *out, size_t cap) {
  const size_t la = answer_detail::idLen(agent);
  if (!la || !out) return 0;
  if (hat > HAT_MAX_CODE && hat != HAT_NONE && hat != HAT_AUTO) return 0;
  const size_t n = 1 + 1 + la + 1;
  if (cap < n) return 0;
  out[0] = BLE_CTRL_HAT;
  out[1] = (uint8_t)la;
  memcpy(out + 2, agent, la);
  out[2 + la] = hat;
  return n;
}
