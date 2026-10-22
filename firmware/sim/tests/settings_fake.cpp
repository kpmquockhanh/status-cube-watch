#include "settings_fake.h"

SettingsFake g_fake;

namespace {
constexpr DeviceSettings DEV0 = {200, 15, 0, 5, 2};
Settings g_net;
DeviceSettings g_dev = DEV0;
}  // namespace

void settingsLoad() {
  g_fake = SettingsFake{};
  g_net = Settings{};
  g_dev = DEV0;
}

const Settings &settings() { return g_net; }

bool settingsSave(const Settings &s) {
  g_fake.netSaves++;
  if (g_fake.failNet) return false;
  g_net = s;
  return true;
}

bool settingsHaveWifi() { return g_net.ssid[0] != '\0'; }

DeviceSettings deviceDefaults() { return DEV0; }
const DeviceSettings &deviceSettings() { return g_dev; }

bool deviceSettingsSave(const DeviceSettings &s) {
  g_fake.devSaves++;
  g_dev = deviceClamp(s);  // as settings.cpp: applied even when flash fails
  return !g_fake.failDev;
}
