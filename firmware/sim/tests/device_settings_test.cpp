#include <cstring>

#include "check.h"
#include "device_settings.h"

int main() {
  DeviceSettings s = deviceClamp({0, 255, 255, 0, 9});
  CHECK(s.backlight == DEV_MIN_BACKLIGHT);
  CHECK(s.sleepMin == DEV_MAX_SLEEP_MIN);
  CHECK(s.rotateSec == 255);
  CHECK(s.pollSec == DEV_MIN_POLL_SEC);
  CHECK(s.sound == DEV_MAX_SOUND);
  s = deviceClamp({160, 0, 0, 200, 0});
  CHECK(s.backlight == 160 && s.sleepMin == 0 && s.rotateSec == 0 && s.pollSec == DEV_MAX_POLL_SEC &&
        s.sound == 0);
  // == sees the level too: a change made only to it must still be saved
  // (closeDeviceEditor and settings_json compare with it).
  const DeviceSettings a{160, 15, 0, 5, 2};
  DeviceSettings b = a;
  CHECK(a == b);
  b.sound = 3;
  CHECK(a != b);
  CHECK(!strcmp(devSoundName(0), "OFF") && !strcmp(devSoundName(1), "LOW"));
  CHECK(!strcmp(devSoundName(2), "MED") && !strcmp(devSoundName(3), "HIGH"));
  // config.h gives milliseconds; the settings store whole minutes / seconds. A
  // short non-zero value must not round down to 0, which means never / off.
  CHECK(devMsToUnits(0, 60000) == 0);
  CHECK(devMsToUnits(30000, 60000) == 1);
  CHECK(devMsToUnits(60000, 60000) == 1);
  CHECK(devMsToUnits(60001, 60000) == 2);
  CHECK(devMsToUnits(900000, 60000) == 15);
  CHECK(devMsToUnits(500, 1000) == 1);
  CHECK(devMsToUnits(5000, 1000) == 5);
  return checksDone("device_settings_test");
}
