#pragma once
// When the next critter frame is due. Pure (no Arduino) so it is host-tested.
//
// The critters and the hungry stripes on the Agents card and the prompt screen
// move with millis() whenever a frame is composed; this only says how often to
// compose one: every ANIM_EXTERNAL_MS on external power, every ANIM_BATTERY_MS on
// battery (the bench's agents-battery run holds its MCU current within 1.2x of
// the same card drawn still). `animating` is the caller's "the screen is on and
// the Agents card or the prompt screen is what it shows"; while it is false
// nothing is due, and when it turns true the first frame is a period away (the
// screen that started it was just drawn). A late pass does not owe the frames it
// missed. These frames are not activity: the caller must not feed them to the
// screen-sleep timer.

#include <stdint.h>

constexpr uint32_t ANIM_EXTERNAL_MS = 100;  // 10 frames/s on USB or a full charger
constexpr uint32_t ANIM_BATTERY_MS = 500;   // 2 frames/s on the cell

class AnimPacer {
 public:
  // True at most once per period while `animating`.
  bool due(uint32_t now, bool animating, bool external) {
    if (!animating) {
      running_ = false;
      return false;
    }
    if (!running_) {
      running_ = true;
      last_ = now;
      return false;
    }
    // A stamp newer than `now` is zero elapsed, not ~49 days (as in idle_sleep.h).
    const int32_t d = (int32_t)(now - last_);
    if (d < (int32_t)(external ? ANIM_EXTERNAL_MS : ANIM_BATTERY_MS)) return false;
    last_ = now;
    return true;
  }

 private:
  bool running_ = false;
  uint32_t last_ = 0;
};
