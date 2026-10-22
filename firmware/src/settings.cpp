#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include "config.h"
#include "device_defaults.h"

#ifndef OTA_PASSWORD
#define OTA_PASSWORD ""
#endif

namespace {

constexpr char NS[] = "cube";
Settings g_settings;
DeviceSettings g_dev = DEVICE_DEFAULTS;

uint8_t loadU8(Preferences &p, const char *key, uint8_t fallback) {
  return p.isKey(key) ? p.getUChar(key, fallback) : fallback;
}

void loadKey(Preferences &p, const char *key, char *dst, size_t cap, const char *fallback) {
  if (p.isKey(key)) {
    p.getString(key, dst, cap);
  } else {
    strlcpy(dst, fallback, cap);
  }
}

void useDefaults() {
  strlcpy(g_settings.ssid, WIFI_SSID, sizeof(g_settings.ssid));
  strlcpy(g_settings.pass, WIFI_PASSWORD, sizeof(g_settings.pass));
  strlcpy(g_settings.bridge, BRIDGE_URL, sizeof(g_settings.bridge));
  strlcpy(g_settings.otaPass, OTA_PASSWORD, sizeof(g_settings.otaPass));
  g_dev = DEVICE_DEFAULTS;
}

}  // namespace

void settingsLoad() {
  Preferences p;
  // Read-write even though we only read: a read-only open of a namespace that
  // does not exist yet fails and logs an error on every first boot.
  if (!p.begin(NS, false)) {
    Serial.println("[settings] NVS unavailable -- using config.h");
    useDefaults();
    return;
  }
  loadKey(p, "ssid", g_settings.ssid, sizeof(g_settings.ssid), WIFI_SSID);
  loadKey(p, "pass", g_settings.pass, sizeof(g_settings.pass), WIFI_PASSWORD);
  loadKey(p, "bridge", g_settings.bridge, sizeof(g_settings.bridge), BRIDGE_URL);
  loadKey(p, "otapass", g_settings.otaPass, sizeof(g_settings.otaPass), OTA_PASSWORD);
  g_dev = deviceClamp(DeviceSettings{loadU8(p, "bl", DEVICE_DEFAULTS.backlight),
                                     loadU8(p, "sl", DEVICE_DEFAULTS.sleepMin),
                                     loadU8(p, "rt", DEVICE_DEFAULTS.rotateSec),
                                     loadU8(p, "pi", DEVICE_DEFAULTS.pollSec),
                                     loadU8(p, "sd", DEVICE_DEFAULTS.sound)});
  p.end();
}

const Settings &settings() { return g_settings; }

bool settingsSave(const Settings &s) {
  Preferences p;
  if (!p.begin(NS, false)) return false;
  // putString returns the byte count, which is 0 for an empty string even on
  // success, so success here just means the namespace opened. An empty value
  // is stored on purpose: a blank password means "open network", not "use the
  // config.h one".
  p.putString("ssid", s.ssid);
  p.putString("pass", s.pass);
  p.putString("bridge", s.bridge);
  p.putString("otapass", s.otaPass);
  p.end();
  g_settings = s;
  return true;
}

bool settingsHaveWifi() { return g_settings.ssid[0] != '\0'; }

DeviceSettings deviceDefaults() { return DEVICE_DEFAULTS; }

const DeviceSettings &deviceSettings() { return g_dev; }

bool deviceSettingsSave(const DeviceSettings &s) {
  g_dev = deviceClamp(s);
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putUChar("bl", g_dev.backlight);
  p.putUChar("sl", g_dev.sleepMin);
  p.putUChar("rt", g_dev.rotateSec);
  p.putUChar("pi", g_dev.pollSec);
  p.putUChar("sd", g_dev.sound);
  p.end();
  return true;
}
