// Desktop stand-in for portal.cpp: a laptop cannot host an access point, so
// show the setup screen and sit on it. The simulator's netBegin() always
// succeeds, so this is only reached by holding the mouse at startup.

#include <Arduino.h>

#include "../src/portal.h"
#include "../src/ui.h"

[[noreturn]] void portalRun(Display &lcd, bool) {
  uiPortal(lcd, "claude-cube-SIM0", "192.168.71.1", nullptr);
  for (;;) delay(100);
}
