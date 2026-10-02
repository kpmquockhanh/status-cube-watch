#pragma once
#include <stdint.h>

#include "pomodoro.h"

// What the on-device editor changes. Pure data plus the rules for keeping it
// sane, so the host tests can exercise it without NVS or Arduino.
struct PomoSettings {
  uint8_t focusMin, shortMin, longMin;  // minutes, 1..99
  uint8_t sessions;                     // focus sessions before the long break, 1..9
};

constexpr int POMO_MIN_MINUTES = 1;
constexpr int POMO_MAX_MINUTES = 99;
constexpr int POMO_MIN_SESSIONS = 1;
constexpr int POMO_MAX_SESSIONS = 9;

inline uint8_t pomoClampU8(int v, int lo, int hi) {
  return (uint8_t)(v < lo ? lo : (v > hi ? hi : v));
}

// A stored or edited value that is out of range (a corrupt NVS key) never
// reaches the timer.
inline PomoSettings pomoClamp(PomoSettings s) {
  s.focusMin = pomoClampU8(s.focusMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  s.shortMin = pomoClampU8(s.shortMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  s.longMin = pomoClampU8(s.longMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  s.sessions = pomoClampU8(s.sessions, POMO_MIN_SESSIONS, POMO_MAX_SESSIONS);
  return s;
}

// `timeDiv` is POMO_TIME_DIV: 1 on the board, 60 for `make run POMO_FAST=60`.
inline PomoConfig pomoConfigFrom(const PomoSettings &s, uint32_t timeDiv) {
  // uint32_t throughout: `unsigned long` is 64-bit on the host and would narrow.
  return PomoConfig{(uint32_t)s.focusMin * 60000u / timeDiv, (uint32_t)s.shortMin * 60000u / timeDiv,
                    (uint32_t)s.longMin * 60000u / timeDiv, s.sessions};
}
