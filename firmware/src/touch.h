#pragma once
#include <Arduino.h>
#include "touch_gate.h"

// Minimal CST816 driver -- enough for taps and swipes.
//
// The chip exposes a gesture register, but the gesture codes differ between
// CST816S / CST816T / CST816D variants, so this reports raw coordinates and
// main.cpp derives swipes from the travel between touch-down and touch-up.
// That behaves the same on every variant.
//
// The INT line only decides when a read is worth doing (touch_gate.h).
class Touch {
 public:
  bool begin();
  // True while a finger is on the panel; x/y are panel coordinates.
  bool read(int16_t &x, int16_t &y);
  bool present() const { return _present; }

 private:
  bool _present = false;
  TouchGate _gate;
};
