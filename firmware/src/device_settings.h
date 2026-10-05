#pragma once
#include <stdint.h>

// Display and behaviour knobs that used to be compile-time constants in
// config.h, now editable in the setup portal. Pure data plus the rules for
// keeping it sane, so the host tests can exercise it without NVS or Arduino.
struct DeviceSettings {
  uint8_t backlight;    // 10..255 (0 would look like a dead cube)
  uint8_t sleepMin;     // screen-off after this many idle minutes; 0 = never
  uint8_t rotateSec;    // auto-advance the deck every N seconds; 0 = off
  uint8_t pollSec;      // WiFi poll interval in seconds
  uint8_t sound;        // buzzer: 0 = off, 1..3 = LOW / MED / HIGH
};

inline bool operator==(const DeviceSettings &a, const DeviceSettings &b) {
  return a.backlight == b.backlight && a.sleepMin == b.sleepMin && a.rotateSec == b.rotateSec &&
         a.pollSec == b.pollSec && a.sound == b.sound;
}
inline bool operator!=(const DeviceSettings &a, const DeviceSettings &b) { return !(a == b); }

constexpr int DEV_MIN_BACKLIGHT = 10;
constexpr int DEV_MAX_BACKLIGHT = 255;
constexpr int DEV_MAX_SLEEP_MIN = 240;  // stored as uint8_t, so 255 is the hard cap anyway
constexpr int DEV_MAX_ROTATE_SEC = 255;
constexpr int DEV_MIN_POLL_SEC = 2;
constexpr int DEV_MAX_POLL_SEC = 60;
constexpr int DEV_MAX_SOUND = 3;

// The buzzer level as the display panel and the [buzz] log show it.
inline const char *devSoundName(uint8_t level) {
  return level == 0 ? "OFF" : level == 1 ? "LOW" : level == 2 ? "MED" : "HIGH";
}

constexpr uint8_t devClampU8(int v, int lo, int hi) { return (uint8_t)(v < lo ? lo : (v > hi ? hi : v)); }

// config.h milliseconds to whole units, rounding up: a short non-zero value must
// not become 0, which means never / off.
constexpr long devMsToUnits(long ms, long unitMs) { return (ms + unitMs - 1) / unitMs; }

inline DeviceSettings deviceClamp(DeviceSettings s) {
  s.backlight = devClampU8(s.backlight, DEV_MIN_BACKLIGHT, DEV_MAX_BACKLIGHT);
  s.sleepMin = devClampU8(s.sleepMin, 0, DEV_MAX_SLEEP_MIN);
  s.rotateSec = devClampU8(s.rotateSec, 0, DEV_MAX_ROTATE_SEC);
  s.pollSec = devClampU8(s.pollSec, DEV_MIN_POLL_SEC, DEV_MAX_POLL_SEC);
  s.sound = devClampU8(s.sound, 0, DEV_MAX_SOUND);
  return s;
}
