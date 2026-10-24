#pragma once
// One-off battery runtime test, built only with -DBATTERY_LOG (see
// battery_log.cpp). Without the flag main.cpp calls none of this.
#include <stdint.h>

// After batteryBegin(): mounts the flash file, prints what it holds over
// serial, then appends a boot marker and the first row.
void batteryLogBegin();
// Every loop pass: appends a row once a minute, and answers the serial
// commands `dump` (print the file again) and `clear` (erase it).
void batteryLogUpdate(uint32_t nowMs, bool screenOn, bool linkUp);
