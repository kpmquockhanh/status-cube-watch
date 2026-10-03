#pragma once
#include <stddef.h>
#include <stdio.h>

// The freshness readout at the top right of every card: how long ago the data
// on screen arrived. Pure, so the host tests in sim/tests can pin it; ui.cpp
// draws it and bridge/preview.html mirrors it by hand. "OFF" rather than
// "OFFLINE", and hours past the hour, so it never outgrows the slot ui.cpp
// reserves for "59s" / "99m" / "OFF". Minutes and hours are floored.
inline void ageLabel(char *out, size_t cap, bool online, unsigned long sec) {
  if (!online) snprintf(out, cap, "OFF");
  else if (sec < 60) snprintf(out, cap, "%lus", sec);
  else if (sec < 3600) snprintf(out, cap, "%lum", sec / 60);
  else if (sec < 100ul * 3600) snprintf(out, cap, "%luh", sec / 3600);
  else snprintf(out, cap, "OLD");
}
