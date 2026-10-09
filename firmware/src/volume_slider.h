#pragma once
// The Volume card: the pill's geometry and its touch model. Pure: no Arduino,
// the caller passes `now`. ui.cpp draws from the same constants the hit-tests
// use, so what is drawn and what is pressed cannot drift apart. Host-tested in
// sim/tests/volume_slider_test.cpp.
#include <stdint.h>
#include <string.h>

#include "volume_frame.h"

// The card body, between the top bar and the dots: a vertical drag that starts
// anywhere in it moves the level by how far the finger travels (relative, like
// a scroll wheel), so touching alone never changes the volume.
constexpr int VOL_PILL_X = 14;
constexpr int VOL_PILL_Y = 42;
constexpr int VOL_PILL_W = 212;
constexpr int VOL_PILL_H = 210;
// The drawn bar: a slim capsule on the right that fills from the bottom,
// ending above the button row.
constexpr int VOL_BAR_X = 184;
constexpr int VOL_BAR_Y = 50;
constexpr int VOL_BAR_W = 30;
constexpr int VOL_BAR_H = 128;
constexpr int VOL_BAR_R = VOL_BAR_W / 2;
// How far a finger travels for the whole 0..100 range.
constexpr int VOL_DRAG_PX = 164;
// The button row, full width under the bar: previous, play/pause and next (a
// tap presses that media key on the Mac). Three equal zones, each
// VOL_ROW_ZONE_W wide and the row's full height, so they are easy to hit. A
// touch that lands in the row is never a drag (main.cpp), so a finger that
// wobbles on a button still taps it.
constexpr int VOL_ROW_TOP = 186;
constexpr int VOL_ROW_BOTTOM = 258;
constexpr int VOL_ROW_Y = 220;  // the glyphs' centre line
constexpr int VOL_ROW_X = 9;
constexpr int VOL_ROW_ZONE_W = 74;
constexpr int VOL_PREV_CX = VOL_ROW_X + VOL_ROW_ZONE_W / 2;
constexpr int VOL_PLAY_CX = VOL_PREV_CX + VOL_ROW_ZONE_W;  // 120: the screen's centre
constexpr int VOL_NEXT_CX = VOL_PLAY_CX + VOL_ROW_ZONE_W;
// The number: a tap anywhere on it (the left column under the name, down to
// the row) toggles mute.
constexpr int VOL_MUTE_X = 8;
constexpr int VOL_MUTE_Y = 78;
constexpr int VOL_MUTE_W = VOL_BAR_X - 8 - VOL_MUTE_X;
constexpr int VOL_MUTE_H = VOL_ROW_TOP - VOL_MUTE_Y;
constexpr int VOL_MUTE_CX = VOL_MUTE_X + VOL_MUTE_W / 2;
constexpr int VOL_MUTE_CY = VOL_MUTE_Y + VOL_MUTE_H / 2;

struct VolRect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
constexpr VolRect VOL_PILL_RECT{VOL_PILL_X, VOL_PILL_Y, VOL_PILL_W, VOL_PILL_H};
// Where a drag may grab the slider.
inline bool volumeInPill(int x, int y) { return VOL_PILL_RECT.contains(x, y); }
// Where a touch lands to be a button press rather than a drag.
inline bool volumeInRow(int y) { return y >= VOL_ROW_TOP; }
constexpr VolRect volRowZone(int i) {
  return VolRect{(int16_t)(VOL_ROW_X + i * VOL_ROW_ZONE_W), (int16_t)VOL_ROW_TOP, (int16_t)VOL_ROW_ZONE_W,
                 (int16_t)(VOL_ROW_BOTTOM - VOL_ROW_TOP)};
}
constexpr VolRect VOL_PREV_ZONE = volRowZone(0);
constexpr VolRect VOL_PLAY_ZONE = volRowZone(1);
constexpr VolRect VOL_NEXT_ZONE = volRowZone(2);
constexpr VolRect VOL_MUTE_ZONE{VOL_MUTE_X, VOL_MUTE_Y, VOL_MUTE_W, VOL_MUTE_H};
// How long a pressed media button stays highlighted.
constexpr uint32_t VOLUME_PRESS_MS = 180;

// The media key under a tap, if any.
inline bool volumeMediaAt(int x, int y, MediaKey &key) {
  if (VOL_PREV_ZONE.contains(x, y)) key = MediaKey::Previous;
  else if (VOL_PLAY_ZONE.contains(x, y)) key = MediaKey::PlayPause;
  else if (VOL_NEXT_ZONE.contains(x, y)) key = MediaKey::Next;
  else return false;
  return true;
}

// After a lift (or a tap), Mac states are parked this long: echoes of levels
// the finger already passed must not jerk the fill back.
constexpr uint32_t VOLUME_HOLD_MS = 600;
// Fewest ms between two requests while dragging.
constexpr uint32_t VOLUME_SEND_MS = 50;

// The level after a finger that grabbed at `fromLevel` moved `dy` px (down is
// positive), rounded to the nearest step and clamped to 0..100.
inline uint8_t volumeLevelAfter(uint8_t fromLevel, int dy) {
  const int delta = -dy * 100;
  const int steps = (delta >= 0 ? delta + VOL_DRAG_PX / 2 : delta - VOL_DRAG_PX / 2) / VOL_DRAG_PX;
  const int l = (int)fromLevel + steps;
  return (uint8_t)(l < 0 ? 0 : l > 100 ? 100 : l);
}

// The fill's top edge for a level (fractional while it eases): 0 empties the
// bar, 100 fills it.
inline int volumeFillTop(float level) {
  const float L = level < 0.0f ? 0.0f : level > 100.0f ? 100.0f : level;
  return (int)((float)(VOL_BAR_Y + VOL_BAR_H) - (float)VOL_BAR_H * L / 100.0f + 0.5f);
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
  int8_t pressed;  // the media button to highlight (a MediaKey), or -1
  int8_t playing;  // the Mac's Now Playing app: 1 playing, 0 paused, -1 unknown (draw play/pause)
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
  // Invariant: a Mac state is applied only when !_held && !_quiet, and main.cpp sends takeSend()
  // in the same pass as release / tap, so a pending request is never overwritten by an echo.
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

  // A drag began on the card, the finger having landed at height `y`. Only a
  // live, settable output can be dragged. Grabbing changes nothing by itself.
  bool grab(int y) {
    if (state() != VolState::Live) return false;
    _held = true;
    _quiet = false;
    _grabY = y;
    _grabLevel = _level;
    return true;
  }

  // The finger is at height `y`: the level moves by its travel since grab().
  // True when the level or mute changed.
  bool drag(int y) {
    if (!_held) return false;
    const uint8_t l = volumeLevelAfter(_grabLevel, y - _grabY);
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

  // A tap: on the number it toggles mute. Anywhere else a tap does nothing:
  // only a drag changes the level. True when something changed.
  bool tap(int x, int y, uint32_t now) {
    const VolState s = state();
    if (s == VolState::NoMac || s == VolState::NoOutput) return false;
    if (!VOL_MUTE_ZONE.contains(x, y) || !_mac.canMute) return false;
    _muted = !_muted;
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
    v.pressed = -1;  // main.cpp sets it: the press is not the slider's state
    v.playing = v.state == VolState::NoMac || !_mac.playKnown ? -1 : _mac.playing ? 1 : 0;
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
  int _grabY = 0;
  uint8_t _grabLevel = 0;
  uint32_t _releasedAt = 0;
  bool _pending = false, _sendNow = false;
  uint32_t _lastSend = 0;
  bool _sentOnce = false;
};
