#pragma once
// The agent detail screen: where its two touch targets sit. Pure: no Arduino.
// ui.cpp draws the back label and the critter inside these rectangles and
// main.cpp hit-tests with them, so what is drawn and what is pressed cannot
// drift apart. Host-tested in sim/tests/detail_layout_test.cpp.
#include <stdint.h>

#include "agents_layout.h"  // LayoutRect

// `< back` at the left of the y 44 row; the zone is taller than the text.
constexpr LayoutRect DETAIL_BACK = {0, 30, 96, 30};
// The agent's critter, cell 3, its hat included (a tap opens the hat picker).
constexpr LayoutRect DETAIL_CRITTER = {4, 60, 80, 54};

constexpr int DETAIL_HIT_BACK = 1;
constexpr int DETAIL_HIT_CRITTER = 2;

// Left and top edges inside, right and bottom edges outside.
constexpr bool detailIn(const LayoutRect &r, int16_t x, int16_t y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// What a tap at (x, y) on the detail screen hits: DETAIL_HIT_BACK,
// DETAIL_HIT_CRITTER, or 0 for nothing. Back covers rows 30..59 and the
// critter zone starts at row 60, so they do not overlap.
constexpr int detailHit(int16_t x, int16_t y) {
  return detailIn(DETAIL_BACK, x, y) ? DETAIL_HIT_BACK : detailIn(DETAIL_CRITTER, x, y) ? DETAIL_HIT_CRITTER : 0;
}
