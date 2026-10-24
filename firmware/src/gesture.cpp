#include "gesture.h"

#include <stdlib.h>

Gesture GestureTracker::update(bool down, int16_t x, int16_t y, uint32_t now, DragMode drag) {
  if (down) {
    if (!_down) {
      _down = true;
      _sx = _lx = x;
      _sy = _ly = y;
      _t0 = now;
      _axis = AXIS_NONE;
      return Gesture::None;
    }
    const int16_t prevX = _lx, prevY = _ly;
    _lx = x;
    _ly = y;
    if (_axis == AXIS_NONE && drag != DragMode::Off) {
      const int adx = abs(_lx - _sx), ady = abs(_ly - _sy);
      if (adx >= DRAG_LOCK_PX || ady >= DRAG_LOCK_PX) {
        _axis = drag == DragMode::Free ? AXIS_FREE : (ady > adx ? AXIS_V : AXIS_H);
        if (_axis != AXIS_H) return Gesture::DragStart;
      }
    } else if (_axis == AXIS_V && _ly != prevY) {
      return Gesture::Drag;
    } else if (_axis == AXIS_FREE && (_lx != prevX || _ly != prevY)) {
      return Gesture::Drag;
    }
    return Gesture::None;
  }

  if (!_down) return Gesture::None;
  _down = false;

  if (_axis == AXIS_V || _axis == AXIS_FREE) {
    _axis = AXIS_NONE;
    return Gesture::DragEnd;
  }

  const int dx = _lx - _sx;
  const int dy = _ly - _sy;
  const uint32_t dt = now - _t0;  // unsigned: correct across a millis() wrap

  if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy) && dt <= SWIPE_MAX_MS) {
    return dx < 0 ? Gesture::SwipeNext : Gesture::SwipePrev;
  }
  if (_axis != AXIS_H && abs(dy) >= SWIPE_MIN_PX && abs(dy) > abs(dx) && dt <= SWIPE_MAX_MS) {
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
  }
  if (abs(dx) < TAP_MAX_PX && abs(dy) < TAP_MAX_PX && dt <= TAP_MAX_MS) return Gesture::Tap;
  return Gesture::None;  // a drag or a long press
}
