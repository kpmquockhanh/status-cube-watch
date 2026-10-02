#pragma once
// Which way up the cube is. Pure (no Arduino) so it is host-tested.
//
// `up` is the accelerometer axis along the screen's vertical, already signed so
// that +1 g means "the top of the screen points at the ceiling"; `az` is the axis
// out of the screen face. The display flips between rotation 0 (upright) and 2
// (upside down) only when the cube is clearly on its edge the other way round:
// |up| over FLIP_G and larger than |az| (so lying flat, or tilted towards
// face-up/face-down, never flips), held for HOLD_MS. Needing up < -FLIP_G to go
// to 2 and up > +FLIP_G to come back is the hysteresis. `busy` (a finger is
// down) postpones a due flip until it lifts. NaN readings never match, so they
// leave the orientation alone.

#include <stdint.h>

class Orientation {
 public:
  static constexpr float FLIP_G = 0.6f;
  static constexpr uint32_t HOLD_MS = 1000;

  // The value to give lcd.setRotation(): 0 or 2.
  uint8_t rotation() const { return rot_; }

  // True when the rotation changed on this call.
  bool update(uint32_t now, float up, float az, bool busy) {
    const float a = up < 0 ? -up : up;
    const float z = az < 0 ? -az : az;
    uint8_t want = rot_;
    if (a > FLIP_G && a > z) want = up > 0 ? 0 : 2;
    if (want == rot_) {
      pending_ = false;
      return false;
    }
    if (!pending_) {
      pending_ = true;
      since_ = now;
    }
    // Signed difference: survives millis() wrap, and a `now` a touch older than
    // since_ counts as zero elapsed rather than ~49 days.
    if (busy || (int32_t)(now - since_) < (int32_t)HOLD_MS) return false;
    rot_ = want;
    pending_ = false;
    return true;
  }

 private:
  uint8_t rot_ = 0;
  bool pending_ = false;
  uint32_t since_ = 0;
};
