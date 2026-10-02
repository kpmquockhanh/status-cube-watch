#pragma once
#include <stddef.h>

#include "device_settings.h"
#include "pomo_editor.h"

// The on-device display settings panel that slides down from the top, minus
// the drawing. It shares the Pomodoro editor's geometry (four rows of
// [ - ] LABEL / value [ + ] and a RESET / DONE bar), so hit-testing is
// pomoEditorHit(); only the rows' meaning differs. Pure: no Arduino.
constexpr int DEV_EDIT_ROWS = 4;  // BRIGHTNESS, SLEEP, AUTO-ADVANCE, REFRESH

const char *devEditorLabel(int row);  // "" for a row that does not exist

// The row's value as shown ("60%", "NEVER", "15 min", "OFF", "5 s").
void devEditorValue(int row, const DeviceSettings &s, char *buf, size_t cap);

// Dec / Inc move to the neighbouring preset (a stored value between presets,
// e.g. set from the portal, steps to the nearest one in that direction);
// Reset restores `defaults`. Done and None leave `s` alone.
void devEditorApply(DeviceSettings &s, EditHit hit, const DeviceSettings &defaults);

// A swipe down opens it from any card, unless an editor is already up.
inline bool devEditorMayOpen(bool editing) { return !editing; }
