// Battery runtime test: how long a full cell lasts in normal use. Built only
// with -DBATTERY_LOG. The USB cable would power the cube, so nobody can watch
// the run over serial; instead a row goes to a LittleFS file on the flash's
// spare data partition once a minute, closed after each write so a dying cell
// loses at most the last minute. Plug USB back in and the file is printed at
// boot (or type `dump`) as CSV between BEGIN/END markers; `clear` erases it.
#ifdef BATTERY_LOG

#include "battery_log.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "battery.h"

namespace {
constexpr uint32_t ROW_EVERY_MS = 60000;
constexpr const char *PATH = "/batlog.csv";

bool g_mounted = false;
uint32_t g_lastRow = 0;
char g_cmd[16];
size_t g_cmdLen = 0;

void append(const char *line) {
  if (!g_mounted) return;
  File f = LittleFS.open(PATH, FILE_APPEND);
  if (!f) {
    Serial.println("[batlog] open failed");
    return;
  }
  f.println(line);
  f.close();
}

void dump() {
  Serial.println("[batlog] BEGIN min,raw_mv,mv,pct,screen,link");
  if (g_mounted) {
    File f = LittleFS.open(PATH, FILE_READ);
    if (f) {
      while (f.available()) Serial.write(f.read());
      f.close();
    }
  }
  Serial.println("[batlog] END");
}

void row(uint32_t nowMs, bool screenOn, bool linkUp) {
  const BatteryView v = batteryView();
  char line[64];
  snprintf(line, sizeof line, "%lu,%lu,%lu,%u,%d,%d", (unsigned long)(nowMs / 60000),
           (unsigned long)batteryRawMv(), (unsigned long)batteryMv(), v.pct, screenOn ? 1 : 0, linkUp ? 1 : 0);
  append(line);
}

void command(const char *c) {
  if (strcmp(c, "dump") == 0) {
    dump();
  } else if (strcmp(c, "clear") == 0) {
    if (g_mounted) LittleFS.remove(PATH);
    Serial.println("[batlog] cleared");
  }
}
}  // namespace

void batteryLogBegin() {
  g_mounted = LittleFS.begin(true);  // formats the partition on first use
  if (!g_mounted) Serial.println("[batlog] LittleFS mount failed: not logging");
  dump();
  append("# boot");
  row(millis(), true, false);
  g_lastRow = millis();
}

void batteryLogUpdate(uint32_t nowMs, bool screenOn, bool linkUp) {
  while (Serial.available()) {
    const int ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      g_cmd[g_cmdLen] = 0;
      if (g_cmdLen) command(g_cmd);
      g_cmdLen = 0;
    } else if (g_cmdLen < sizeof g_cmd - 1) {
      g_cmd[g_cmdLen++] = (char)ch;
    }
  }
  if (nowMs - g_lastRow < ROW_EVERY_MS) return;
  g_lastRow += ROW_EVERY_MS;  // keeps rows on the minute even if a pass runs late
  row(nowMs, screenOn, linkUp);
}

#endif  // BATTERY_LOG
