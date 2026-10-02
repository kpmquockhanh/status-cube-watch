#pragma once
// The simulator has no battery. CUBE_BATTERY=<0..100> picks a level, "none"
// is the no-cell/USB case: a full battery. Unset => 78.
#include <cstdlib>
#include <cstring>

#include "../src/battery_util.h"

inline BatteryView batteryViewFromEnv() {
  BatteryView v;
  const char *e = std::getenv("CUBE_BATTERY");
  if (!e) {
    v.pct = 78;
    v.onUsb = false;
    return v;
  }
  if (std::strcmp(e, "none") == 0) return v;  // 100, onUsb
  int p = std::atoi(e);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  v.pct = (uint8_t)p;
  v.onUsb = false;
  return v;
}

// CUBE_LINK=ble|wifi|none picks the transport marker in screenshots (default ble).
#include "../src/ui.h"
inline UiLink linkFromEnv() {
  const char *e = std::getenv("CUBE_LINK");
  if (e && std::strcmp(e, "none") == 0) return UiLink::None;
  if (e && std::strcmp(e, "wifi") == 0) return UiLink::Wifi;
  return UiLink::Ble;
}
