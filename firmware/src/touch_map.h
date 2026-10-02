#pragma once
// Raw CST816 coordinates are in the panel's native orientation. When the display
// is rotated 180° (lcd.setRotation(2)) a finger on what is drawn at the top-left
// reports bottom-right, so the point is mirrored before gesture/hit-testing code
// sees it. Pure, so it is host-tested.

#include <stdint.h>
#include "board_pins.h"

inline void touchToScreen(uint8_t rotation, int16_t &x, int16_t &y) {
  if (rotation != 2) return;
  int16_t nx = (int16_t)(LCD_WIDTH - 1 - x);
  int16_t ny = (int16_t)(LCD_HEIGHT - 1 - y);
  if (nx < 0) nx = 0;
  if (nx > LCD_WIDTH - 1) nx = LCD_WIDTH - 1;
  if (ny < 0) ny = 0;
  if (ny > LCD_HEIGHT - 1) ny = LCD_HEIGHT - 1;
  x = nx;
  y = ny;
}
