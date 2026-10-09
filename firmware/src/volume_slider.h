#pragma once
// The Volume card: the pill's geometry and its touch model. Pure: no Arduino,
// the caller passes `now`. ui.cpp draws from the same constants the hit-tests
// use, so what is drawn and what is pressed cannot drift apart. Host-tested in
// sim/tests/volume_slider_test.cpp.
#include <stdint.h>
#include <string.h>

#include "volume_frame.h"

// The pill: full width less a 14 px gutter, between the top bar and the dots.
constexpr int VOL_PILL_X = 14;
constexpr int VOL_PILL_Y = 42;
constexpr int VOL_PILL_W = 212;
constexpr int VOL_PILL_H = 210;
constexpr int VOL_PILL_R = 40;
// The finger's travel, inset from the pill so both ends are easy to reach: at
// or above TOP is 100, at or below BOTTOM is 0.
constexpr int VOL_TRAVEL_TOP = 54;
constexpr int VOL_TRAVEL_BOTTOM = 240;
// The speaker glyph's centre; a tap around it toggles mute.
constexpr int VOL_SPK_CX = 120;
constexpr int VOL_SPK_CY = 218;

struct VolRect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
constexpr VolRect VOL_PILL_RECT{VOL_PILL_X, VOL_PILL_Y, VOL_PILL_W, VOL_PILL_H};
// Where a drag may grab the slider, and a tap jumps the level.
inline bool volumeInPill(int x, int y) { return VOL_PILL_RECT.contains(x, y); }
constexpr VolRect VOL_SPEAKER_ZONE{VOL_SPK_CX - 30, VOL_SPK_CY - 25, 60, 50};

// After a lift (or a tap), Mac states are parked this long: echoes of levels
// the finger already passed must not jerk the fill back.
constexpr uint32_t VOLUME_HOLD_MS = 600;
// Fewest ms between two requests while dragging.
constexpr uint32_t VOLUME_SEND_MS = 50;

// The level for a finger at height `y`, rounded to the nearest step.
constexpr uint8_t volumeLevelAt(int y) {
  return y <= VOL_TRAVEL_TOP      ? 100
         : y >= VOL_TRAVEL_BOTTOM ? 0
                                  : (uint8_t)(((VOL_TRAVEL_BOTTOM - y) * 100 + (VOL_TRAVEL_BOTTOM - VOL_TRAVEL_TOP) / 2) /
                                              (VOL_TRAVEL_BOTTOM - VOL_TRAVEL_TOP));
}

// The fill's top edge for a level (fractional while it eases): the inverse of
// volumeLevelAt, so the edge sits under the finger that set the level. The
// travel maps 1..99 linearly; the two end segments stretch from the travel to
// the pill's own top and bottom, so 100 fills the whole pill and 0 empties it.
inline int volumeFillTop(float level) {
  const float L = level < 0.0f ? 0.0f : level > 100.0f ? 100.0f : level;
  const float top99 = (float)VOL_TRAVEL_BOTTOM - (float)(VOL_TRAVEL_BOTTOM - VOL_TRAVEL_TOP) * 99.0f / 100.0f;
  const float bot1 = (float)VOL_TRAVEL_BOTTOM - (float)(VOL_TRAVEL_BOTTOM - VOL_TRAVEL_TOP) * 1.0f / 100.0f;
  const float pillBottom = (float)(VOL_PILL_Y + VOL_PILL_H);
  float y;
  if (L > 99.0f) {
    y = top99 + ((float)VOL_PILL_Y - top99) * (L - 99.0f);
  } else if (L < 1.0f) {
    y = pillBottom + (bot1 - pillBottom) * L;
  } else {
    y = (float)VOL_TRAVEL_BOTTOM - (float)(VOL_TRAVEL_BOTTOM - VOL_TRAVEL_TOP) * L / 100.0f;
  }
  return (int)(y + 0.5f);
}

enum class VolState : uint8_t {
  NoMac,     // link down, or no Volume write yet
  NoOutput,  // the Mac has no output device
  Live,
  Fixed,     // the device's volume cannot be set (HDMI and the like)
};

// What ui.cpp draws.
struct VolumeView {
  VolState state;
  uint8_t level;  // where the fill stands: the level when Live, 100 when Fixed, else 0
  bool muted;
  bool canMute;
  bool tracking;  // a finger holds the fill: draw it at `level`, no easing
  char name[VOLUME_NAME_MAX + 1];
};

class VolumeSlider {
 public:
  VolState state() const {
    if (!_mac.known) return VolState::NoMac;
    if (_mac.level == VOLUME_NO_DEVICE) return VolState::NoOutput;
    if (!_mac.canSet) return VolState::Fixed;
    return VolState::Live;
  }

  // The Mac's state arrived. known=false (the link dropped) forgets everything
  // at once, a drag in progress included. Otherwise, while a finger holds the
  // fill and for VOLUME_HOLD_MS after, the state is parked for tick().
  void fromMac(const MacVolume &v, uint32_t now) {
    if (!v.known) {
      *this = VolumeSlider();
      return;
    }
    if (_held || (_quiet && now - _releasedAt < VOLUME_HOLD_MS)) {
      _parked = v;
      _hasParked = true;
      return;
    }
    apply(v);
  }

  // Ends the hold window and applies a parked state. True when the view changed.
  bool tick(uint32_t now) {
    if (_quiet && now - _releasedAt >= VOLUME_HOLD_MS) _quiet = false;
    if (_held || _quiet || !_hasParked) return false;
    apply(_parked);
    return true;
  }

  // A drag began on the card. Only a live, settable output can be dragged.
  bool grab() {
    if (state() != VolState::Live) return false;
    _held = true;
    _quiet = false;
    return true;
  }

  // The finger is at height `y`. True when the level or mute changed.
  bool drag(int y) {
    if (!_held) return false;
    const uint8_t l = volumeLevelAt(y);
    if (l == _level && !_muted) return false;
    _level = l;
    _muted = false;  // any level change unmutes
    _pending = true;
    return true;
  }

  // The finger lifted. The last level goes out at once, and the hold window starts.
  bool release(uint32_t now) {
    if (!_held) return false;
    _held = false;
    _quiet = true;
    _releasedAt = now;
    if (_pending) _sendNow = true;
    return true;
  }

  // A tap: the speaker zone toggles mute; elsewhere on the pill the level jumps
  // to that height. True when something changed.
  bool tap(int x, int y, uint32_t now) {
    const VolState s = state();
    if (s == VolState::NoMac || s == VolState::NoOutput) return false;
    if (VOL_SPEAKER_ZONE.contains(x, y)) {
      if (!_mac.canMute) return false;
      _muted = !_muted;
      queue(now);
      return true;
    }
    if (s != VolState::Live || !volumeInPill(x, y)) return false;
    const uint8_t l = volumeLevelAt(y);
    if (l == _level && !_muted) return false;
    _level = l;
    _muted = false;
    queue(now);
    return true;
  }

  // A request for the Mac, when one is due: throttled to VOLUME_SEND_MS while
  // dragging, at once after a lift or a tap.
  bool takeSend(uint32_t now, uint8_t &level, bool &muted) {
    if (!_pending) return false;
    if (!_sendNow && _sentOnce && now - _lastSend < VOLUME_SEND_MS) return false;
    level = _level;
    muted = _muted;
    _pending = _sendNow = false;
    _lastSend = now;
    _sentOnce = true;
    return true;
  }

  VolumeView view() const {
    VolumeView v{};
    v.state = state();
    v.level = v.state == VolState::Live ? _level : v.state == VolState::Fixed ? 100 : 0;
    v.muted = (v.state == VolState::Live || v.state == VolState::Fixed) && _muted;
    v.canMute = _mac.canMute;
    v.tracking = _held;
    memcpy(v.name, _mac.name, sizeof(v.name));
    return v;
  }

  bool held() const { return _held; }

 private:
  void apply(const MacVolume &v) {
    _mac = v;
    _level = v.level == VOLUME_NO_DEVICE ? 0 : v.level;
    _muted = v.muted;
    _hasParked = false;
  }

  void queue(uint32_t now) {
    _pending = _sendNow = true;
    _quiet = true;
    _releasedAt = now;
  }

  MacVolume _mac{}, _parked{};
  bool _hasParked = false;
  uint8_t _level = 0;
  bool _muted = false, _held = false, _quiet = false;
  uint32_t _releasedAt = 0;
  bool _pending = false, _sendNow = false;
  uint32_t _lastSend = 0;
  bool _sentOnce = false;
};
