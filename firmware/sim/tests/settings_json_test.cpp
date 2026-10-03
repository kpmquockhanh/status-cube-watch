#include <cstring>

#include "check.h"
#include "settings.h"
#include "settings_fake.h"
#include "settings_json.h"

namespace {

constexpr const char *NET = "{\"ssid\":\"home\",\"pass\":\"longenough\"}";

// Only the groups a patch changes are written: a Mac nudging the backlight
// must not rewrite the Pomodoro keys, nor a repeat of what is stored anything.
void testSavesOnlyWhatChanged() {
  settingsLoad();
  CHECK(settingsApplyJson("{\"bl\":100}") == SettingsResult::Ok);
  CHECK(g_fake.devSaves == 1 && g_fake.pomoSaves == 0 && g_fake.netSaves == 0);
  CHECK(settingsApplyJson("{\"bl\":100,\"pf\":25}") == SettingsResult::Ok);  // both as stored
  CHECK(g_fake.devSaves == 1 && g_fake.pomoSaves == 0 && g_fake.netSaves == 0);
  CHECK(settingsApplyJson("{\"pf\":30}") == SettingsResult::Ok);
  CHECK(g_fake.devSaves == 1 && g_fake.pomoSaves == 1 && g_fake.netSaves == 0);
}

// The network goes last: it is the one that reboots, and it must never be the
// only thing stored by a write reported as rejected.
void testNetworkSavedLast() {
  settingsLoad();
  g_fake.failDev = true;
  CHECK(settingsApplyJson("{\"bl\":100,\"ssid\":\"home\",\"pass\":\"longenough\"}") ==
        SettingsResult::Invalid);
  CHECK(g_fake.netSaves == 0);
  CHECK(settings().ssid[0] == '\0');

  settingsLoad();
  g_fake.failPomo = true;
  CHECK(settingsApplyJson("{\"pf\":30,\"ssid\":\"home\",\"pass\":\"longenough\"}") ==
        SettingsResult::Invalid);
  CHECK(g_fake.netSaves == 0);
}

// Once one group is stored the write is not "nothing changed": it reports Ok,
// and the Mac's read-back after Ok shows what made it.
void testPartialSaveIsOk() {
  settingsLoad();
  g_fake.failPomo = true;
  CHECK(settingsApplyJson("{\"bl\":100,\"pf\":30}") == SettingsResult::Ok);
  CHECK(deviceSettings().backlight == 100);

  settingsLoad();
  g_fake.failNet = true;
  CHECK(settingsApplyJson("{\"bl\":100,\"ssid\":\"home\",\"pass\":\"longenough\"}") ==
        SettingsResult::Ok);  // no reboot: the network did not change
  CHECK(deviceSettings().backlight == 100);

  settingsLoad();
  g_fake.failNet = true;
  CHECK(settingsApplyJson(NET) == SettingsResult::Invalid);  // nothing else to show for it
}

}  // namespace

int main() {
  testSavesOnlyWhatChanged();
  testNetworkSavedLast();
  testPartialSaveIsOk();
  settingsLoad();
  char buf[SETTINGS_JSON_MAX + 1];

  // Reads never leak the passwords.
  const size_t n = settingsToJson(buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(!strstr(buf, "\"pass\"") && !strstr(buf, "otapass"));
  CHECK(strstr(buf, "\"bl\":") != nullptr);

  // A partial patch changes only what it names, and applies immediately.
  const uint8_t pollBefore = deviceSettings().pollSec;
  CHECK(settingsApplyJson("{\"bl\":100,\"pf\":30}") == SettingsResult::Ok);
  CHECK(deviceSettings().backlight == 100);
  CHECK(pomoSettings().focusMin == 30);
  CHECK(deviceSettings().pollSec == pollBefore);

  // Anything invalid rejects the whole object: nothing changes.
  CHECK(settingsApplyJson("{\"bl\":120,\"pi\":1}") == SettingsResult::Invalid);
  CHECK(deviceSettings().backlight == 100);
  CHECK(settingsApplyJson("{\"bl\":\"bright\"}") == SettingsResult::Invalid);
  CHECK(settingsApplyJson("{\"bl\":12.5}") == SettingsResult::Invalid);
  CHECK(settingsApplyJson("[1]") == SettingsResult::Invalid);
  CHECK(settingsApplyJson("not json") == SettingsResult::Invalid);
  CHECK(settingsApplyJson("{\"pn\":10}") == SettingsResult::Invalid);

  // Network changes ask for a reboot; bad ones are refused.
  CHECK(settingsApplyJson("{\"bridge\":\"https://x/\"}") == SettingsResult::Invalid);
  CHECK(settingsApplyJson("{\"ssid\":\"\"}") == SettingsResult::Invalid);
  CHECK(settingsApplyJson("{\"ssid\":\"home\",\"pass\":\"short\"}") == SettingsResult::Invalid);
  CHECK(settingsApplyJson("{\"ssid\":\"home\",\"pass\":\"longenough\",\"bridge\":\"http://h:1/a\"}") ==
        SettingsResult::OkReboot);
  CHECK(strcmp(settings().ssid, "home") == 0);
  CHECK(settingsApplyJson("{\"ssid\":\"home\"}") == SettingsResult::Ok);  // unchanged

  // A BLE-only cube has no SSID, and config.h may carry values the portal would
  // refuse. A patch that names no network key must still apply.
  Settings bleOnly{};
  strlcpy(bleOnly.pass, "short", sizeof(bleOnly.pass));
  strlcpy(bleOnly.bridge, "https://x/", sizeof(bleOnly.bridge));
  settingsSave(bleOnly);
  CHECK(settingsApplyJson("{\"bl\":90,\"pf\":20}") == SettingsResult::Ok);
  CHECK(deviceSettings().backlight == 90);
  CHECK(pomoSettings().focusMin == 20);
  // The network keys a patch does name are still checked.
  CHECK(settingsApplyJson("{\"ssid\":\"\"}") == SettingsResult::Invalid);
  CHECK(settingsApplyJson("{\"bridge\":\"https://y/\"}") == SettingsResult::Invalid);
  return checksDone("settings_json_test");
}
