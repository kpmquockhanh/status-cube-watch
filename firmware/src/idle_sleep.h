#pragma once
// When the screen goes to sleep. Pure (no Arduino) so it is host-tested.
// The screen sleeps after `timeoutMs` with no touch and no fresh data; data
// arriving while awake counts as activity (a live link keeps it on), but data
// never wakes a sleeping screen. A touch wakes it and is reported as consumed
// so it does not also change the card. `blocked` (Pomodoro running, alert,
// editor, pairing code...) forces the screen on and restarts the timer.
// timeoutMs == 0 disables sleeping.

#include <stdint.h>

class IdleSleep {
 public:
  void begin(uint32_t now) { last_ = now; asleep_ = false; }

  // A payload arrived.
  void data(uint32_t now) { if (!asleep_) last_ = now; }

  // A finger is down. True if this woke the screen: swallow the touch.
  bool touch(uint32_t now) {
    last_ = now;
    if (!asleep_) return false;
    asleep_ = false;
    return true;
  }

  // Returns true while the screen should be on.
  bool update(uint32_t now, bool blocked, uint32_t timeoutMs) {
    if (blocked) {
      last_ = now;
      asleep_ = false;
    } else if (timeoutMs != 0 && elapsed(now) >= timeoutMs) {
      asleep_ = true;
    }
    return !asleep_;
  }

 private:
  // `now` may be a little older than a stamp taken later in the same loop pass
  // (touch reads millis() itself): that is zero elapsed, not ~49 days.
  uint32_t elapsed(uint32_t now) const {
    const int32_t d = (int32_t)(now - last_);
    return d < 0 ? 0 : (uint32_t)d;
  }

  uint32_t last_ = 0;
  bool asleep_ = false;
};
