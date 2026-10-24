#pragma once
#include <stdint.h>

#include "battery_util.h"

// Reads the battery through the board's divider (VBAT = VADC x 3). Call
// batteryUpdate from the main loop; it samples at most every 5 s.
void batteryBegin();
// Second line of setup(): drives SYS_EN HIGH so a cube started on battery with
// the PWR button stays on once the button is released.
void batteryHoldPower();
void batteryUpdate(uint32_t nowMs);
BatteryView batteryView();
// Last sample's median and the smoothed estimate, in mV (hardware only: the
// runtime log in battery_log.cpp reads them; the simulator has neither).
uint32_t batteryRawMv();
uint32_t batteryMv();
