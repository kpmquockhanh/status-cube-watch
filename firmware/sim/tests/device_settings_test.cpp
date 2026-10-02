#include "check.h"
#include "device_settings.h"

int main() {
  DeviceSettings s = deviceClamp({0, 255, 255, 0});
  CHECK(s.backlight == DEV_MIN_BACKLIGHT);
  CHECK(s.sleepMin == DEV_MAX_SLEEP_MIN);
  CHECK(s.rotateSec == 255);
  CHECK(s.pollSec == DEV_MIN_POLL_SEC);
  s = deviceClamp({160, 0, 0, 200});
  CHECK(s.backlight == 160 && s.sleepMin == 0 && s.rotateSec == 0 && s.pollSec == DEV_MAX_POLL_SEC);
  return checksDone("device_settings_test");
}
