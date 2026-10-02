#pragma once
#include <stdint.h>

#include "battery_util.h"

// Reads the battery through the board's divider (VBAT = VADC x 3). Call
// batteryUpdate from the main loop; it samples at most every 5 s.
void batteryBegin();
void batteryUpdate(uint32_t nowMs);
BatteryView batteryView();
