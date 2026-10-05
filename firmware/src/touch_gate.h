#pragma once
// When the touch driver may skip its I2C read (touch.cpp). Pure, host-tested.
//
// The loop asks for touch on every pass, ~65 times a second, and each read is
// an I2C transaction, for a screen touched a few times an hour. The CST816
// pulls INT low when a finger lands, and touch.cpp latches that edge, so the
// read can wait for it. A read still happens on every pass while a finger is
// down (to follow a swipe and see the lift), and every FALLBACK_MS anyway.
//
// INT is trusted only once it has shown it works. Until a landing finger has
// come with an edge, every pass reads, as before, so a board whose INT never
// fires (or fires only for gestures) loses nothing. A new finger found with no
// edge means INT missed it, and every pass reads again until the next finger
// that does come with one.

#include <stdint.h>

class TouchGate {
 public:
  static constexpr uint32_t FALLBACK_MS = 250;

  // irq: an INT edge was latched since the last read.
  bool shouldRead(bool irq, uint32_t now) const {
    return !_trusted || _down || irq || now - _lastRead >= FALLBACK_MS;
  }

  // After a read. ok: the chip answered. finger: it reported one. irq: as
  // passed to shouldRead() for this read.
  void readDone(bool ok, bool finger, bool irq, uint32_t now) {
    _lastRead = now;
    if (!ok) return;  // a dropped read is not a lift: keep _down, so the next pass reads again
    if (finger && !_down) _trusted = irq;  // did INT announce this finger?
    _down = finger;
  }

  bool trusted() const { return _trusted; }

 private:
  bool _trusted = false;
  bool _down = false;
  uint32_t _lastRead = 0;
};
