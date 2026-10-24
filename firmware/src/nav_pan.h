#pragma once
// Dragging the navigation map. Pure: no Arduino, host-tested in
// sim/tests/nav_pan_test.cpp. The caller passes `now`.
//
// The offset is in screen pixels: how far the map is moved from where the phone
// would put it (the rider at 120,200). The phone builds every frame around the
// offset the cube last sent it (Control 08) and echoes it in the frame, so the
// cube draws the frame shifted by `x() - frame pan` and the map follows the
// finger before the phone has caught up. NAV_PAN_RETURN_MS after the last touch,
// or at a tap, the offset glides back to 0 over NAV_PAN_GLIDE_MS.
#include <stddef.h>
#include <stdint.h>

#include "nav_frame.h"  // NAV_PAN_MAX, BLE_CTRL_NAV_PAN (ble_frame.h)

constexpr uint32_t NAV_PAN_RETURN_MS = 8000;
constexpr uint32_t NAV_PAN_GLIDE_MS = 300;
constexpr uint32_t NAV_PAN_SEND_MS = 100;  // at most this often while dragging; the final value always

// Control [0x08, x lo, x hi, y lo, y hi]: the offset the phone should build frames around.
constexpr size_t BLE_NAV_PAN_LEN = 5;
inline void navPanEncode(int16_t x, int16_t y, uint8_t out[BLE_NAV_PAN_LEN]) {
  out[0] = BLE_CTRL_NAV_PAN;
  out[1] = (uint8_t)((uint16_t)x & 0xFF);
  out[2] = (uint8_t)((uint16_t)x >> 8);
  out[3] = (uint8_t)((uint16_t)y & 0xFF);
  out[4] = (uint8_t)((uint16_t)y >> 8);
}

class NavPan {
 public:
  // A ride started or ended: centred, and the phone is taken to be centred too.
  void reset() { *this = NavPan(); }

  // The finger locked to a drag; (fx, fy) is where it landed.
  void grab(int16_t fx, int16_t fy, uint32_t now) {
    if (gliding_) settle(now);
    held_ = true;
    gliding_ = false;
    ax_ = fx;
    ay_ = fy;
    gx_ = x_;
    gy_ = y_;
    touchAt_ = now;
  }

  // The finger is at (fx, fy). True when the offset moved.
  bool drag(int16_t fx, int16_t fy, uint32_t now) {
    if (!held_) return false;
    touchAt_ = now;
    const int16_t nx = clampPan(gx_ + (fx - ax_)), ny = clampPan(gy_ + (fy - ay_));
    if (nx == x_ && ny == y_) return false;
    x_ = nx;
    y_ = ny;
    return true;
  }

  void release(uint32_t now) {
    held_ = false;
    touchAt_ = now;
  }

  // Back to the rider: a tap, or NAV_PAN_RETURN_MS without a touch. False when already there.
  bool recenter(uint32_t now) {
    if (x_ == 0 && y_ == 0) return false;
    held_ = false;
    gliding_ = true;
    fromX_ = x_;
    fromY_ = y_;
    glideAt_ = now;
    return true;
  }

  // Advances the glide and starts the return when it is due. True when the offset moved.
  bool tick(uint32_t now) {
    if (!held_ && !gliding_ && (x_ != 0 || y_ != 0) && now - touchAt_ >= NAV_PAN_RETURN_MS) recenter(now);
    if (!gliding_) return false;
    settle(now);
    return true;
  }

  bool held() const { return held_; }
  bool gliding() const { return gliding_; }
  int16_t x() const { return x_; }
  int16_t y() const { return y_; }

  // What the phone should build around, when it is time to tell it: while
  // dragging at most every NAV_PAN_SEND_MS, otherwise at once. A glide sends its
  // end (0, 0) as it starts.
  bool takeSend(uint32_t now, int16_t &sx, int16_t &sy) {
    const int16_t tx = gliding_ ? 0 : x_, ty = gliding_ ? 0 : y_;
    if (tx == sentX_ && ty == sentY_) return false;
    if (held_ && sentAny_ && now - sentAt_ < NAV_PAN_SEND_MS) return false;
    sentX_ = sx = tx;
    sentY_ = sy = ty;
    sentAt_ = now;
    sentAny_ = true;
    return true;
  }

 private:
  static int16_t clampPan(int v) { return (int16_t)(v < -NAV_PAN_MAX ? -NAV_PAN_MAX : v > NAV_PAN_MAX ? NAV_PAN_MAX : v); }

  // The glide's position at `now` (ease-out); it ends at exactly 0.
  void settle(uint32_t now) {
    const uint32_t t = now - glideAt_;
    if (t >= NAV_PAN_GLIDE_MS) {
      x_ = y_ = 0;
      gliding_ = false;
      return;
    }
    const float u = 1.0f - (float)t / NAV_PAN_GLIDE_MS;  // 1 -> 0
    const float k = u * u;
    x_ = (int16_t)(fromX_ * k);
    y_ = (int16_t)(fromY_ * k);
  }

  bool held_ = false, gliding_ = false;
  int16_t x_ = 0, y_ = 0;
  int16_t ax_ = 0, ay_ = 0, gx_ = 0, gy_ = 0;  // where the finger landed, the offset then
  int16_t fromX_ = 0, fromY_ = 0;
  uint32_t touchAt_ = 0, glideAt_ = 0;
  int16_t sentX_ = 0, sentY_ = 0;
  uint32_t sentAt_ = 0;
  bool sentAny_ = false;
};
