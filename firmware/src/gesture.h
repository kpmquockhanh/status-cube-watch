#pragma once
#include <stdint.h>

// Turns polled touch samples into gestures. Pure logic -- no Arduino, no I2C --
// so the host tests in sim/tests can drive it with a fake clock.
//
// Swipes and taps are derived from the travel between touch-down and touch-up
// rather than from the CST816's gesture register, because that register's
// codes differ between the S/T/D chip variants.
enum class Gesture : uint8_t {
  None,
  Tap,
  SwipeNext,   // finger moved left: advance
  SwipePrev,
  SwipeUp,     // finger moved up the screen (closes the display panel)
  SwipeDown,   // finger moved down (opens the display panel)
  DragStart,   // drag mode: the finger locked to a vertical drag (lastY() is where it is)
  Drag,        // drag mode: the finger moved while dragging (lastY())
  DragEnd,     // the dragging finger lifted (also after drag mode was turned off mid-drag)
};

constexpr int SWIPE_MIN_PX = 40;
constexpr uint32_t SWIPE_MAX_MS = 700;
constexpr int TAP_MAX_PX = 16;
constexpr uint32_t TAP_MAX_MS = 400;
// In drag mode, how far the finger must move from where it landed before the
// move is called vertical (a drag) or horizontal (a swipe, decided on lift).
constexpr int DRAG_LOCK_PX = 10;

class GestureTracker {
 public:
  // Feed one sample per poll. At most one gesture comes back per call; a tap
  // comes back as the finger lifts.
  // `dragMode` (only the Volume card) turns a vertical move into
  // DragStart / Drag / DragEnd instead of a swipe.
  Gesture update(bool down, int16_t x, int16_t y, uint32_t now, bool dragMode = false);

  // Where the latest touch began. A tap carries no coordinates of its own, so
  // the editor hit-tests here: a few pixels of drift between down and up must
  // not move the press onto a neighbouring button.
  int16_t startX() const { return _sx; }
  int16_t startY() const { return _sy; }
  // The latest sample's height: where a drag is.
  int16_t lastY() const { return _ly; }

 private:
  enum : uint8_t { AXIS_NONE, AXIS_V, AXIS_H };
  bool _down = false;
  uint8_t _axis = AXIS_NONE; // drag mode: which way this touch locked
  int16_t _sx = 0, _sy = 0;  // where the touch began
  int16_t _lx = 0, _ly = 0;  // the latest sample
  uint32_t _t0 = 0;
};
