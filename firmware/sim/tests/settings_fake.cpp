#include "settings_fake.h"

SettingsFake g_fake;

namespace {
constexpr DeviceSettings DEV0 = {200, 15, 0, 5, 2};
constexpr PomoSettings POMO0 = {25, 5, 15, 4};
Settings g_net;
DeviceSettings g_dev = DEV0;
PomoSettings g_pomo = POMO0;
}  // namespace

void settingsLoad() {
  g_fake = SettingsFake{};
  g_net = Settings{};
  g_dev = DEV0;
  g_pomo = POMO0;
}

const Settings &settings() { return g_net; }

bool settingsSave(const Settings &s) {
  g_fake.netSaves++;
  if (g_fake.failNet) return false;
  g_net = s;
  return true;
}

bool settingsHaveWifi() { return g_net.ssid[0] != '\0'; }

PomoSettings pomoDefaults() { return POMO0; }
const PomoSettings &pomoSettings() { return g_pomo; }

bool pomoSettingsSave(const PomoSettings &s) {
  g_fake.pomoSaves++;
  g_pomo = pomoClamp(s);  // as settings.cpp: applied even when flash fails
  return !g_fake.failPomo;
}

DeviceSettings deviceDefaults() { return DEV0; }
const DeviceSettings &deviceSettings() { return g_dev; }

bool deviceSettingsSave(const DeviceSettings &s) {
  g_fake.devSaves++;
  g_dev = deviceClamp(s);
  return !g_fake.failDev;
}
