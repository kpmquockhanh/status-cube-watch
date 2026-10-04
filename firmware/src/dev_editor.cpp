#include "dev_editor.h"

#include <stdio.h>

namespace {

struct Presets {
  const uint8_t *v;
  uint8_t n;
  uint8_t on;  // a switch row's value when turned on with nothing better to go back to; 0 = no switch
};

// 0 (never / off) is not a preset: the switch owns it.
constexpr uint8_t BRIGHT[] = {10, 40, 80, 120, 160, 200, 255};
constexpr uint8_t SLEEP_MIN[] = {1, 5, 10, 15, 30, 60, 120, 240};
constexpr uint8_t ROTATE_SEC[] = {5, 10, 15, 30, 60, 120};
constexpr uint8_t POLL_SEC[] = {2, 5, 10, 15, 30, 60};

#define PRESETS(a, on) Presets{a, (uint8_t)(sizeof(a) / sizeof(a[0])), on}
const Presets ROWS[DEV_EDIT_ROWS] = {PRESETS(BRIGHT, 0), PRESETS(SLEEP_MIN, 15), PRESETS(ROTATE_SEC, 10),
                                     PRESETS(POLL_SEC, 0)};
const char *const LABELS[DEV_EDIT_ROWS] = {"BRIGHTNESS", "SLEEP", "ADVANCE", "REFRESH"};

uint8_t *field(DeviceSettings &s, int row) {
  switch (row) {
    case 0: return &s.backlight;
    case 1: return &s.sleepMin;
    case 2: return &s.rotateSec;
    default: return &s.pollSec;
  }
}

uint8_t value(DeviceSettings s, int row) { return *field(s, row); }

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

bool devEditorHasSwitch(int row) { return row >= 0 && row < DEV_EDIT_ROWS && ROWS[row].on != 0; }

bool devEditorRowOn(int row, const DeviceSettings &s) {
  return !devEditorHasSwitch(row) || value(s, row) != 0;
}

void devEditorValue(int row, const DeviceSettings &s, char *buf, size_t cap) {
  if (devEditorHasSwitch(row) && !devEditorRowOn(row, s)) {
    snprintf(buf, cap, "OFF");
    return;
  }
  switch (row) {
    case 0: snprintf(buf, cap, "%u%%", (unsigned)((s.backlight * 100u + 127u) / 255u)); break;
    case 1: snprintf(buf, cap, "%u min", (unsigned)s.sleepMin); break;
    case 2: snprintf(buf, cap, "%u s", (unsigned)s.rotateSec); break;
    case 3: snprintf(buf, cap, "%u s", (unsigned)s.pollSec); break;
    default: if (cap) buf[0] = '\0';
  }
}

EditHit devEditorHit(int16_t x, int16_t y) {
  for (int r = 0; r < DEV_EDIT_ROWS; r++)
    if (devEditorHasSwitch(r) && editorLead(r).contains(x, y)) return EditHit{EditAction::Toggle, (uint8_t)r};
  return pomoEditorHit(x, y);
}

void devEditorApply(DeviceSettings &s, EditHit hit, const DeviceSettings &defaults,
                    const DeviceSettings &saved) {
  switch (hit.action) {
    case EditAction::Dec:
    case EditAction::Inc:
      if (hit.row >= DEV_EDIT_ROWS || !devEditorRowOn(hit.row, s)) return;
      *field(s, hit.row) = stepped(*field(s, hit.row), hit.action == EditAction::Inc, ROWS[hit.row]);
      break;
    case EditAction::Toggle: {
      if (!devEditorHasSwitch(hit.row)) return;
      uint8_t &v = *field(s, hit.row);
      if (v) v = 0;
      else if (value(saved, hit.row)) v = value(saved, hit.row);
      else if (value(defaults, hit.row)) v = value(defaults, hit.row);
      else v = ROWS[hit.row].on;
      break;
    }
    case EditAction::Reset:
      s = defaults;
      break;
    default:
      break;
  }
}
