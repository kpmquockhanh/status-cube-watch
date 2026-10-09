#include "gesture.h"

#include <stdlib.h>

namespace {
// What a finished run of `n` taps means.
Gesture tapsToGesture(uint8_t n) { return n >= 3 ? Gesture::TripleTap : n == 2 ? Gesture::DoubleTap : Gesture::Tap; }
}  // namespace

Gesture GestureTracker::update(bool down, int16_t x, int16_t y, uint32_t now, bool multiTap, bool dragMode) {
  // Taps waiting on a third that never came (or that stopped meaning anything
  // because the card changed): settle them. The gap ends at the next touch, so
  // never while a finger is down. Unsigned: correct across a millis() wrap.
  Gesture settled = Gesture::None;
  if (_taps && (!multiTap || (!_down && now - _tapUp > MULTI_TAP_GAP_MS))) {
    if (multiTap) settled = tapsToGesture(_taps);
    _taps = 0;
  }

  if (down) {
    if (!_down) {
      _down = true;
      _sx = _lx = x;
      _sy = _ly = y;
      _t0 = now;
      _axis = AXIS_NONE;
      return settled;
    }
    const int16_t prevY = _ly;
    _lx = x;
    _ly = y;
    if (_axis == AXIS_NONE && dragMode) {
      const int adx = abs(_lx - _sx), ady = abs(_ly - _sy);
      if (adx >= DRAG_LOCK_PX || ady >= DRAG_LOCK_PX) {
        _axis = ady > adx ? AXIS_V : AXIS_H;
        if (_axis == AXIS_V) {
          _taps = 0;
          return Gesture::DragStart;  // settled is None here: multiTap is off in drag mode
        }
      }
    } else if (_axis == AXIS_V && _ly != prevY) {
      return Gesture::Drag;
    }
    return settled;
  }

  if (!_down) return settled;
  _down = false;

  if (_axis == AXIS_V) {
    _axis = AXIS_NONE;
    _taps = 0;
    return Gesture::DragEnd;
  }

  const int dx = _lx - _sx;
  const int dy = _ly - _sy;
  const uint32_t dt = now - _t0;

  if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy) && dt <= SWIPE_MAX_MS) {
    _taps = 0;
    return dx < 0 ? Gesture::SwipeNext : Gesture::SwipePrev;
  }
  if (_axis != AXIS_H && abs(dy) >= SWIPE_MIN_PX && abs(dy) > abs(dx) && dt <= SWIPE_MAX_MS) {
    _taps = 0;
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
  }
  if (abs(dx) < TAP_MAX_PX && abs(dy) < TAP_MAX_PX && dt <= TAP_MAX_MS) {
    if (!multiTap) return Gesture::Tap;
    _tapUp = now;
    if (++_taps >= 3) {
      _taps = 0;
      return Gesture::TripleTap;
    }
    return settled;  // wait: this may yet be the first or second of three
  }
  _taps = 0;  // a drag or a long press breaks the sequence
  return Gesture::None;
}
