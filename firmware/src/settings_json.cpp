#include "settings_json.h"

#include <ArduinoJson.h>

#include <string>

#include "portal_util.h"
#include "settings.h"

namespace {

// Integer in [lo, hi], or the key is absent. A float, string or out-of-range
// value is a rejection, not a clamp: the Mac should hear about it.
bool readInt(JsonObject o, const char *key, int lo, int hi, uint8_t &out) {
  JsonVariant v = o[key];
  if (v.isNull()) return true;
  if (!v.is<int>()) return false;
  const int n = v.as<int>();
  if (n < lo || n > hi) return false;
  out = (uint8_t)n;
  return true;
}

bool readStr(JsonObject o, const char *key, std::string &out, bool &present) {
  JsonVariant v = o[key];
  present = !v.isNull();
  if (!present) return true;
  if (!v.is<const char *>()) return false;
  out = v.as<const char *>();
  return true;
}

}  // namespace

size_t settingsToJson(char *buf, size_t cap) {
  const Settings &n = settings();
  const DeviceSettings &d = deviceSettings();
  const PomoSettings &p = pomoSettings();
  JsonDocument doc;
  doc["v"] = 1;
  doc["bl"] = d.backlight;
  doc["sl"] = d.sleepMin;
  doc["rt"] = d.rotateSec;
  doc["pi"] = d.pollSec;
  doc["sd"] = d.sound;
  doc["pf"] = p.focusMin;
  doc["ps"] = p.shortMin;
  doc["pl"] = p.longMin;
  doc["pn"] = p.sessions;
  doc["ssid"] = n.ssid;
  doc["bridge"] = n.bridge;
  doc["wifiPass"] = n.pass[0] != '\0';  // whether one is set, never the value
  doc["otaPass"] = n.otaPass[0] != '\0';
  const size_t len = measureJson(doc);
  if (len + 1 > cap) return 0;
  return serializeJson(doc, buf, cap);
}

SettingsResult settingsApplyJson(const char *json) {
  JsonDocument doc;
  if (deserializeJson(doc, json) || !doc.is<JsonObject>()) return SettingsResult::Invalid;
  JsonObject o = doc.as<JsonObject>();

  DeviceSettings d = deviceSettings();
  PomoSettings p = pomoSettings();
  if (!readInt(o, "bl", DEV_MIN_BACKLIGHT, DEV_MAX_BACKLIGHT, d.backlight) ||
      !readInt(o, "sl", 0, DEV_MAX_SLEEP_MIN, d.sleepMin) || !readInt(o, "rt", 0, DEV_MAX_ROTATE_SEC, d.rotateSec) ||
      !readInt(o, "pi", DEV_MIN_POLL_SEC, DEV_MAX_POLL_SEC, d.pollSec) ||
      !readInt(o, "sd", 0, DEV_MAX_SOUND, d.sound) ||
      !readInt(o, "pf", POMO_MIN_MINUTES, POMO_MAX_MINUTES, p.focusMin) ||
      !readInt(o, "ps", POMO_MIN_MINUTES, POMO_MAX_MINUTES, p.shortMin) ||
      !readInt(o, "pl", POMO_MIN_MINUTES, POMO_MAX_MINUTES, p.longMin) ||
      !readInt(o, "pn", POMO_MIN_SESSIONS, POMO_MAX_SESSIONS, p.sessions))
    return SettingsResult::Invalid;

  Settings n = settings();
  std::string ssid = n.ssid, pass = n.pass, bridge = n.bridge, ota = n.otaPass;
  bool hasSsid, hasPass, hasBridge, hasOta;
  if (!readStr(o, "ssid", ssid, hasSsid) || !readStr(o, "pass", pass, hasPass) ||
      !readStr(o, "bridge", bridge, hasBridge) || !readStr(o, "otapass", ota, hasOta))
    return SettingsResult::Invalid;
  if (hasBridge) bridge = portalTrim(bridge);
  // Same rules as the portal form. A new SSID without a new password means an
  // open network, exactly as there.
  if (hasSsid && !hasPass) pass = portalResolvePassword(n.ssid, ssid, n.pass, "");
  // Only the keys the patch names are checked (a new SSID also re-checks the
  // password it resolved to). The stored ones may be empty on a BLE-only cube,
  // or config.h values the portal would refuse; neither may block a display change.
  if ((hasSsid && !portalValidSsid(ssid)) || ((hasSsid || hasPass) && !portalValidWifiPassword(pass)) ||
      (hasBridge && !portalValidBridgeUrl(bridge)) || ota.size() > 63)
    return SettingsResult::Invalid;

  // Only the groups that changed are written, network last: it is the one that
  // reboots, and a write reported as rejected must not have reached flash. Once
  // a group is stored a later failure still reports Ok (no reboot), and the
  // Mac's read-back after Ok shows what made it.
  bool saved = false;
  if (d != deviceSettings()) {
    if (!deviceSettingsSave(d)) return SettingsResult::Invalid;
    saved = true;
  }
  if (p != pomoSettings()) {
    if (!pomoSettingsSave(p)) return saved ? SettingsResult::Ok : SettingsResult::Invalid;
    saved = true;
  }
  const bool netChanged = ssid != n.ssid || pass != n.pass || bridge != n.bridge || ota != n.otaPass;
  if (!netChanged) return SettingsResult::Ok;
  strlcpy(n.ssid, ssid.c_str(), sizeof(n.ssid));
  strlcpy(n.pass, pass.c_str(), sizeof(n.pass));
  strlcpy(n.bridge, bridge.c_str(), sizeof(n.bridge));
  strlcpy(n.otaPass, ota.c_str(), sizeof(n.otaPass));
  if (!settingsSave(n)) return saved ? SettingsResult::Ok : SettingsResult::Invalid;
  return SettingsResult::OkReboot;
}
