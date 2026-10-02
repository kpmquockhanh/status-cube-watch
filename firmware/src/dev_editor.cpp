#include "dev_editor.h"

#include <stdio.h>

namespace {

struct Presets {
  const uint8_t *v;
  uint8_t n;
};

constexpr uint8_t BRIGHT[] = {10, 40, 80, 120, 160, 200, 255};
constexpr uint8_t SLEEP_MIN[] = {0, 1, 5, 10, 15, 30, 60, 120, 240};
constexpr uint8_t ROTATE_SEC[] = {0, 5, 10, 15, 30, 60, 120};
constexpr uint8_t POLL_SEC[] = {2, 5, 10, 15, 30, 60};

#define PRESETS(a) Presets{a, (uint8_t)(sizeof(a) / sizeof(a[0]))}
const Presets ROWS[DEV_EDIT_ROWS] = {PRESETS(BRIGHT), PRESETS(SLEEP_MIN), PRESETS(ROTATE_SEC), PRESETS(POLL_SEC)};
const char *const LABELS[DEV_EDIT_ROWS] = {"BRIGHTNESS", "SLEEP", "ADVANCE", "REFRESH"};

uint8_t *field(DeviceSettings &s, int row) {
  switch (row) {
    case 0: return &s.backlight;
    case 1: return &s.sleepMin;
    case 2: return &s.rotateSec;
    default: return &s.pollSec;
  }
}

// The next preset strictly above (up) or below `v`; `v` itself at either end.
uint8_t stepped(uint8_t v, bool up, const Presets &p) {
  if (up) {
    for (int i = 0; i < p.n; i++)
      if (p.v[i] > v) return p.v[i];
  } else {
    for (int i = p.n - 1; i >= 0; i--)
      if (p.v[i] < v) return p.v[i];
  }
  return v;
}

}  // namespace

const char *devEditorLabel(int row) { return (row >= 0 && row < DEV_EDIT_ROWS) ? LABELS[row] : ""; }

void devEditorValue(int row, const DeviceSettings &s, char *buf, size_t cap) {
  switch (row) {
    case 0: snprintf(buf, cap, "%u%%", (unsigned)((s.backlight * 100u + 127u) / 255u)); break;
    case 1:
      if (s.sleepMin == 0) snprintf(buf, cap, "NEVER");
      else snprintf(buf, cap, "%u min", (unsigned)s.sleepMin);
      break;
    case 2:
      if (s.rotateSec == 0) snprintf(buf, cap, "OFF");
      else snprintf(buf, cap, "%u s", (unsigned)s.rotateSec);
      break;
    case 3: snprintf(buf, cap, "%u s", (unsigned)s.pollSec); break;
    default: if (cap) buf[0] = '\0';
  }
}

void devEditorApply(DeviceSettings &s, EditHit hit, const DeviceSettings &defaults) {
  switch (hit.action) {
    case EditAction::Dec:
    case EditAction::Inc:
      if (hit.row >= DEV_EDIT_ROWS) return;
      *field(s, hit.row) = stepped(*field(s, hit.row), hit.action == EditAction::Inc, ROWS[hit.row]);
      break;
    case EditAction::Reset:
      s = defaults;
      break;
    default:
      break;
  }
}
