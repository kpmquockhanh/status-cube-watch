#pragma once
// The Mac's output volume on the wire (docs/ble-protocol.md, "Volume"). Pure:
// no Arduino, host-tested in sim/tests/volume_frame_test.cpp.
//
//   Mac -> cube, Volume characteristic: [ver=1][level][flags][name...]
//   cube -> Mac, Control:               [0x05][level][muted], [0x06][key], [0x07]
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ble_frame.h"

constexpr uint8_t VOLUME_FRAME_VER = 1;
constexpr uint8_t VOLUME_NO_DEVICE = 0xFF;  // level when the Mac has no output device
constexpr size_t VOLUME_NAME_MAX = 23;
constexpr size_t VOLUME_FRAME_MAX = 3 + VOLUME_NAME_MAX;  // 26
constexpr uint8_t VOL_FLAG_MUTED = 1, VOL_FLAG_CAN_SET = 2, VOL_FLAG_CAN_MUTE = 4;
// Whether the Mac's Now Playing app plays (fw_rev 6): KNOWN set, then PLAYING says which.
constexpr uint8_t VOL_FLAG_PLAY_KNOWN = 8, VOL_FLAG_PLAYING = 16;
// Not volume: the Mac's screen is locked and it will unlock for Control 07 (fw_rev 7).
constexpr uint8_t VOL_FLAG_MAC_LOCKED = 32;
constexpr size_t BLE_VOLUME_REQ_LEN = 3;

// What the Mac last reported. known=false: nothing yet, or the link dropped.
struct MacVolume {
  bool known;
  uint8_t level;  // 0..100, or VOLUME_NO_DEVICE
  bool muted;
  bool canSet;    // the device's volume can be set (false: HDMI and the like)
  bool canMute;
  bool playKnown;  // the Mac reported whether anything plays
  bool playing;
  bool macLocked;  // the Mac is locked and waits for a tap on the unlock prompt
  char name[VOLUME_NAME_MAX + 1];
};

// Parses a Volume write. On any fault returns false and leaves `out` as it was.
inline bool volumeParse(const uint8_t *d, size_t n, MacVolume &out) {
  if (!d || n < 3 || n > VOLUME_FRAME_MAX) return false;
  if (d[0] != VOLUME_FRAME_VER) return false;
  if (d[1] > 100 && d[1] != VOLUME_NO_DEVICE) return false;
  MacVolume v{};
  for (size_t i = 3; i < n; i++) {
    if (d[i] < 0x20 || d[i] > 0x7E) return false;  // the fonts are printable ASCII only
    v.name[i - 3] = (char)d[i];
  }
  v.known = true;
  v.level = d[1];
  v.muted = (d[2] & VOL_FLAG_MUTED) != 0;
  v.canSet = (d[2] & VOL_FLAG_CAN_SET) != 0;
  v.canMute = (d[2] & VOL_FLAG_CAN_MUTE) != 0;
  v.playKnown = (d[2] & VOL_FLAG_PLAY_KNOWN) != 0;
  v.playing = v.playKnown && (d[2] & VOL_FLAG_PLAYING) != 0;
  v.macLocked = (d[2] & VOL_FLAG_MAC_LOCKED) != 0;
  out = v;
  return true;
}

// The Control message asking the Mac to set its output. An absolute target, so
// a lost or repeated message cannot drift the volume.
inline void volumeRequestEncode(uint8_t level, bool muted, uint8_t out[BLE_VOLUME_REQ_LEN]) {
  out[0] = BLE_CTRL_VOLUME;
  out[1] = level > 100 ? 100 : level;
  out[2] = muted ? 1 : 0;
}

// Media keys the Volume card can press on the Mac: Control [0x06][key].
enum class MediaKey : uint8_t { PlayPause = 0, Next = 1, Previous = 2 };
constexpr size_t BLE_MEDIA_REQ_LEN = 2;
inline void mediaRequestEncode(MediaKey key, uint8_t out[BLE_MEDIA_REQ_LEN]) {
  out[0] = BLE_CTRL_MEDIA;
  out[1] = (uint8_t)key;
}

// Asks the Mac to unlock its screen: Control [0x07].
constexpr size_t BLE_UNLOCK_REQ_LEN = 1;
inline void unlockRequestEncode(uint8_t out[BLE_UNLOCK_REQ_LEN]) { out[0] = BLE_CTRL_UNLOCK; }

inline bool volumeSame(const MacVolume &a, const MacVolume &b) {
  return a.known == b.known && a.level == b.level && a.muted == b.muted && a.canSet == b.canSet &&
         a.canMute == b.canMute && a.playKnown == b.playKnown && a.playing == b.playing &&
         a.macLocked == b.macLocked && strcmp(a.name, b.name) == 0;
}
