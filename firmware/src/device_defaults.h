#pragma once
// The compile-time display/behaviour defaults from config.h: what a cube with
// nothing stored in NVS uses. Only the settings layer (and its simulator
// stand-in) includes this.
#include "config.h"
#include "device_settings.h"

#ifndef BACKLIGHT
#define BACKLIGHT 160
#endif
#ifndef POLL_INTERVAL_MS
#define POLL_INTERVAL_MS 5000
#endif
#ifndef CARD_ROTATE_MS
#define CARD_ROTATE_MS 0
#endif
#ifndef SCREEN_SLEEP_MS
#define SCREEN_SLEEP_MS 900000  // older config.h files: 15 min
#endif
#ifndef BUZZER_LEVEL
#define BUZZER_LEVEL 2  // older config.h files: MED
#endif

constexpr DeviceSettings DEVICE_DEFAULTS{
    devClampU8(BACKLIGHT, DEV_MIN_BACKLIGHT, DEV_MAX_BACKLIGHT),
    devClampU8(devMsToUnits(SCREEN_SLEEP_MS, 60000), 0, DEV_MAX_SLEEP_MIN),
    devClampU8(devMsToUnits(CARD_ROTATE_MS, 1000), 0, DEV_MAX_ROTATE_SEC),
    devClampU8(devMsToUnits(POLL_INTERVAL_MS, 1000), DEV_MIN_POLL_SEC, DEV_MAX_POLL_SEC),
    devClampU8(BUZZER_LEVEL, 0, DEV_MAX_SOUND)};
