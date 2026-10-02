#pragma once
#include <stddef.h>
#include <stdint.h>

#include "device_settings.h"
#include "pomo_settings.h"

// Everything the setup portal can change. Stored in NVS; any key that was
// never stored falls back to config.h, so a freshly flashed cube behaves
// exactly as it did before the portal existed.
struct Settings {
  char ssid[33];     // WiFi SSID, up to 32 bytes
  char pass[64];     // WPA passphrase, up to 63 bytes; empty = open network
  char bridge[128];  // bridge URL, up to 127 bytes
  char otaPass[64];  // ArduinoOTA password; empty = none
};

void settingsLoad();  // call once at boot, before anything reads settings()
const Settings &settings();
// Persists `s` and makes it the current settings. The caller reboots.
bool settingsSave(const Settings &s);
// True when there is any SSID to join, from NVS or from config.h.
bool settingsHaveWifi();

// Pomodoro durations, edited on the cube itself. Stored apart from the WiFi
// settings so saving one never rewrites the other; a never-stored key falls
// back to the config.h default (pomoDefaults()).
PomoSettings pomoDefaults();         // the compile-time values (RESET target)
const PomoSettings &pomoSettings();  // current, always within range
// Clamps and makes `s` current at once; returns whether it reached flash. If
// NVS is unavailable the new values still apply until the next reboot.
bool pomoSettingsSave(const PomoSettings &s);

// Backlight, screen sleep, card rotation and poll interval. Same contract as
// the Pomodoro pair: never-stored keys fall back to config.h, always in range.
DeviceSettings deviceDefaults();
const DeviceSettings &deviceSettings();
bool deviceSettingsSave(const DeviceSettings &s);
