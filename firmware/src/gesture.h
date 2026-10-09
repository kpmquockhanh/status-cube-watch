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
  SwipeUp,     // finger moved up the screen (opens the Pomodoro editor; closes the display panel)
  SwipeDown,   // finger moved down (closes the Pomodoro editor; opens the display panel)
  DoubleTap,   // two taps in quick succession (multi-tap on only); start / pause / resume
  TripleTap,   // three taps in quick succession (multi-tap on only); reset
  DragStart,   // drag mode: the finger locked to a vertical drag (lastY() is where it is)
  Drag,        // drag mode: the finger moved while dragging (lastY())
  DragEnd,     // the dragging finger lifted (also after drag mode was turned off mid-drag)
};

constexpr int SWIPE_MIN_PX = 40;
constexpr uint32_t SWIPE_MAX_MS = 700;
constexpr int TAP_MAX_PX = 16;
constexpr uint32_t TAP_MAX_MS = 400;
// Longest pause between one tap lifting and the next touching for them to count
// as one multi-tap. It is also how long a double tap waits to rule out a third.
constexpr uint32_t MULTI_TAP_GAP_MS = 350;
// In drag mode, how far the finger must move from where it landed before the
// move is called vertical (a drag) or horizontal (a swipe, decided on lift).
constexpr int DRAG_LOCK_PX = 10;

class GestureTracker {
 public:
  // Feed one sample per poll, including polls with no finger down: a double tap
  // is only known to be one once MULTI_TAP_GAP_MS passes with no third tap.
  // At most one gesture comes back per call.
  // `multiTap` says whether taps group into DoubleTap/TripleTap (only the
  // Pomodoro card uses them). When false every tap is a plain Tap, at once.
  // `dragMode` (only the Volume card) turns a vertical move into
  // DragStart / Drag / DragEnd instead of a swipe. The two are never both on.
  Gesture update(bool down, int16_t x, int16_t y, uint32_t now, bool multiTap, bool dragMode = false);

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
  uint8_t _taps = 0;         // taps so far in the sequence waiting on its gap
  uint8_t _axis = AXIS_NONE; // drag mode: which way this touch locked
  int16_t _sx = 0, _sy = 0;  // where the touch began
  int16_t _lx = 0, _ly = 0;  // the latest sample
  uint32_t _t0 = 0;
  uint32_t _tapUp = 0;       // when the last counted tap lifted
};
