#pragma once
#include <stddef.h>
#include <stdint.h>

// The Mac app edits settings over BLE as a small JSON object (see
// docs/ble-protocol.md). Keys: bl sl rt pi (display), pf ps pl pn (Pomodoro),
// ssid pass bridge otapass (network). Reads omit the two passwords.
constexpr size_t SETTINGS_JSON_MAX = 512;  // one ATT value

// Wire values of the Control `03 <status>` reply.
enum class SettingsResult : uint8_t {
  Ok = 0,        // applied and saved; nothing else to do
  Invalid = 1,   // malformed or out of range; nothing changed
  OkReboot = 2,  // applied and saved; the network settings changed, so the cube reboots
};

// Writes the current settings as JSON into `buf`; returns the length (0 if it
// did not fit). Never contains `pass` or `otapass`.
size_t settingsToJson(char *buf, size_t cap);

// Applies a partial object: only the keys present change, and the whole
// object is rejected (nothing saved) if any one of them is out of range.
SettingsResult settingsApplyJson(const char *json);
