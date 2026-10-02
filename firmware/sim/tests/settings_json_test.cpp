#include <cstring>

#include "check.h"
#include "settings.h"
#include "settings_json.h"

int main() {
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
  return checksDone("settings_json_test");
}
