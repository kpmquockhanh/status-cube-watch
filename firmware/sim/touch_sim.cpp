// Desktop replacement for touch.cpp: the mouse stands in for a finger.
//
// Only the transport differs. Coordinates go back to main.cpp unchanged, so
// the swipe and tap thresholds under test here are the ones that will run on
// the board -- drag across the window to swipe, click to tap.

#include "../src/display.h"
#include "../src/touch.h"

Display *g_simDisplay = nullptr;

bool Touch::begin() {
  _present = g_simDisplay != nullptr;
  Serial.printf("[touch] %s\n", _present ? "mouse (simulated)" : "no display");
  return _present;
}

bool Touch::read(int16_t &x, int16_t &y) {
  if (!g_simDisplay) return false;
  lgfx::touch_point_t tp;
  if (!g_simDisplay->getTouch(&tp)) return false;
  x = (int16_t)tp.x;
  y = (int16_t)tp.y;
  return true;
}
