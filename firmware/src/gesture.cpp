#include "gesture.h"

#include <stdlib.h>

Gesture GestureTracker::update(bool down, int16_t x, int16_t y, uint32_t now, bool holdEnabled) {
  if (down) {
    if (!_down) {
      _down = true;
      _moved = false;
      _longFired = false;
      _resetFired = false;
      _holdBlocked = !holdEnabled;
      _lastT = now;
      _sx = _lx = x;
      _sy = _ly = y;
      _t0 = now;
      return Gesture::None;
    }
    _lx = x;
    _ly = y;
    if (abs(_lx - _sx) >= TAP_MAX_PX || abs(_ly - _sy) >= TAP_MAX_PX) _moved = true;
    // Once holds were off, or the loop stalled, this touch can never become a
    // hold: its age says nothing about how long the user meant to press.
    if (!holdEnabled || now - _lastT > SAMPLE_GAP_MS) _holdBlocked = true;
    _lastT = now;
    if (_holdBlocked || _moved) return Gesture::None;

    const uint32_t held = now - _t0;  // unsigned: correct across a millis() wrap
    if (!_longFired && held >= LONG_PRESS_MS) {
      _longFired = true;
      _longAt = now;
      return Gesture::LongPress;
    }
    if (_longFired && !_resetFired && held >= RESET_PRESS_MS &&
        now - _longAt >= RESET_PRESS_MS - LONG_PRESS_MS) {
      _resetFired = true;
      return Gesture::ResetPress;
    }
    return Gesture::None;
  }

  if (!_down) return Gesture::None;
  _down = false;

  // A hold that already acted must not also count as a tap on release.
  if (_longFired) return Gesture::None;

  const int dx = _lx - _sx;
  const int dy = _ly - _sy;
  const uint32_t dt = now - _t0;

  if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy) && dt <= SWIPE_MAX_MS) {
    return dx < 0 ? Gesture::SwipeNext : Gesture::SwipePrev;
  }
  if (abs(dy) >= SWIPE_MIN_PX && abs(dy) > abs(dx) && dt <= SWIPE_MAX_MS) {
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
  }
  if (abs(dx) < TAP_MAX_PX && abs(dy) < TAP_MAX_PX && dt <= TAP_MAX_MS) {
    return Gesture::Tap;
  }
  return Gesture::None;
}
