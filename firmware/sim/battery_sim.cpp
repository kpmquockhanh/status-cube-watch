// Desktop stand-in for battery.cpp: no ADC on a laptop, so the level comes from
// the CUBE_BATTERY environment variable (see battery_env.h).
#include "../src/battery.h"
#include "battery_env.h"

void batteryBegin() {}
void batteryUpdate(uint32_t) {}
BatteryView batteryView() { return batteryViewFromEnv(); }
