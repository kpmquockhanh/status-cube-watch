#pragma once
#include "settings.h"

// settings.h for the host tests: in memory, counts the saves of each group and
// can make one fail, so a test can see what settingsApplyJson stored and in
// which order. settingsLoad() resets all of it.
struct SettingsFake {
  int netSaves = 0, devSaves = 0, pomoSaves = 0;
  bool failNet = false, failDev = false, failPomo = false;
};
extern SettingsFake g_fake;
