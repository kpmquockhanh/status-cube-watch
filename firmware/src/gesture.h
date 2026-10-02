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
  SwipeUp,     // finger moved up the screen (opens the Pomodoro editor)
  SwipeDown,   // finger moved down (closes it)
  LongPress,   // held in place for LONG_PRESS_MS; fires while the finger is still down
  ResetPress,  // still held at RESET_PRESS_MS, after a LongPress; fires once
};

constexpr int SWIPE_MIN_PX = 40;
constexpr uint32_t SWIPE_MAX_MS = 700;
constexpr int TAP_MAX_PX = 16;
constexpr uint32_t TAP_MAX_MS = 400;
constexpr uint32_t LONG_PRESS_MS = 600;
constexpr uint32_t RESET_PRESS_MS = 2000;
// Samples further apart than this mean the loop was stalled (a blocking fetch):
// how long the finger really held is then unknown, so holds are off for the touch.
constexpr uint32_t SAMPLE_GAP_MS = 150;

class GestureTracker {
 public:
  // Feed one sample per poll. At most one gesture comes back per call.
  // `holdEnabled` says whether holds mean anything right now (only the
  // Pomodoro card uses them). When false, a long touch is ignored on release,
  // exactly as a slow drag always was.
  Gesture update(bool down, int16_t x, int16_t y, uint32_t now, bool holdEnabled);

  // Where the latest touch began. A tap carries no coordinates of its own, so
  // the editor hit-tests here: a few pixels of drift between down and up must
  // not move the press onto a neighbouring button.
  int16_t startX() const { return _sx; }
  int16_t startY() const { return _sy; }

 private:
  bool _down = false;
  bool _moved = false;       // travelled past TAP_MAX_PX: no longer a stationary hold
  bool _longFired = false;
  bool _resetFired = false;
  bool _holdBlocked = false;  // holds meant nothing at some point in this touch
  int16_t _sx = 0, _sy = 0;  // where the touch began
  int16_t _lx = 0, _ly = 0;  // the latest sample
  uint32_t _t0 = 0;
  uint32_t _lastT = 0;       // time of the previous sample
  uint32_t _longAt = 0;      // when LongPress fired
};
