// Desktop stand-in for settings.cpp: a laptop has no NVS, so the simulator
// always runs on the config.h defaults and saving is a no-op.

#include <Arduino.h>

#include "../src/config.h"
#include "../src/pomo_defaults.h"
#include "../src/settings.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

namespace {
Settings g_settings;
PomoSettings g_pomo = POMO_DEFAULTS;
}

void settingsLoad() {
  strlcpy(g_settings.ssid, WIFI_SSID, sizeof(g_settings.ssid));
  strlcpy(g_settings.pass, WIFI_PASSWORD, sizeof(g_settings.pass));
  strlcpy(g_settings.bridge, BRIDGE_URL, sizeof(g_settings.bridge));
  strlcpy(g_settings.otaPass, OTA_PASSWORD, sizeof(g_settings.otaPass));
  g_pomo = POMO_DEFAULTS;
}

const Settings &settings() { return g_settings; }

bool settingsSave(const Settings &s) {
  g_settings = s;
  Serial.println("[settings] (sim) not persisted");
  return true;
}

bool settingsHaveWifi() { return g_settings.ssid[0] != '\0'; }

PomoSettings pomoDefaults() { return POMO_DEFAULTS; }

const PomoSettings &pomoSettings() { return g_pomo; }

bool pomoSettingsSave(const PomoSettings &s) {
  g_pomo = pomoClamp(s);
  Serial.println("[settings] (sim) pomodoro not persisted");
  return true;
}
