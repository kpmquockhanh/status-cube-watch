#pragma once
#include <stddef.h>

#include "device_settings.h"
#include "pomo_editor.h"

// The on-device display settings panel that slides down from the top, minus
// the drawing. It shares the Pomodoro editor's geometry (four rows of
// LABEL / value ... ( - ) ( + ) and a RESET / DONE bar); only the rows' meaning
// differs, and SLEEP and ADVANCE, which can be off, carry a switch in their
// lead. Pure: no Arduino.
constexpr int DEV_EDIT_ROWS = 4;  // BRIGHTNESS, SLEEP, AUTO-ADVANCE, REFRESH

const char *devEditorLabel(int row);  // "" for a row that does not exist

// The row's value as shown ("60%", "15 min", "5 s", "OFF" for a switch row
// that is off).
void devEditorValue(int row, const DeviceSettings &s, char *buf, size_t cap);

// SLEEP (0 = never) and ADVANCE (0 = off) are switched on and off by a tap on
// their lead; their steppers move between the non-zero presets only, and do
// nothing while the switch is off.
bool devEditorHasSwitch(int row);
bool devEditorRowOn(int row, const DeviceSettings &s);  // false only for a switch row that is off

// pomoEditorHit, plus Toggle for a tap on a switch row's lead.
EditHit devEditorHit(int16_t x, int16_t y);

// Dec / Inc move to the neighbouring preset (a stored value between presets,
// e.g. set from the portal, steps to the nearest one in that direction);
// Toggle turns a switch row off, or back on at its value in `saved` (what is
// stored now), else in `defaults`, else a preset, whichever is first non-zero;
// Reset restores `defaults`. Done and None leave `s` alone.
void devEditorApply(DeviceSettings &s, EditHit hit, const DeviceSettings &defaults,
                    const DeviceSettings &saved);

// A swipe down opens it from any card, unless an editor is already up.
inline bool devEditorMayOpen(bool editing) { return !editing; }
