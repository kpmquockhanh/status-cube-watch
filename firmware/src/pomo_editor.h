#pragma once
#include <stdint.h>

#include "pomo_settings.h"
#include "pomodoro.h"

// The Pomodoro settings editor, minus the drawing. Layout rectangles live
// here and ui.cpp draws from them, so what is drawn and what is hit-tested
// cannot drift apart. Pure: no Arduino, no LovyanGFX.
//
//   y= 0..46    handle + icon/title
//   y=46..222   four rows of 44 px:  LABEL            ( - ) ( + )
//                                    value
//   y=232..276  [ RESET ]  [ DONE ]
constexpr int EDIT_ROWS = 4;  // FOCUS, SHORT, LONG, SESSIONS

struct EditRect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

EditRect editorRow(int row);   // the whole row
EditRect editorLead(int row);  // the row left of the steppers: label, value, and a switch if it has one
EditRect editorMinus(int row);  // touch zones; ui.cpp centres the round buttons in them
EditRect editorPlus(int row);
EditRect editorResetBtn();
EditRect editorDoneBtn();
const char *editorLabel(int row);  // "" for a row that does not exist

// Toggle is a tap on a row's lead. Only the display panel's SLEEP and ADVANCE
// rows have a switch there (dev_editor.h); pomoEditorHit never returns it.
enum class EditAction : uint8_t { None, Dec, Inc, Reset, Done, Toggle };
struct EditHit {
  EditAction action;
  uint8_t row;  // meaningful for Dec / Inc / Toggle
};

EditHit pomoEditorHit(int16_t x, int16_t y);

// Applies Dec / Inc (step, snap, clamp) or Reset (to `defaults`). None,
// Toggle and Done leave `s` alone: the caller acts on Done.
void pomoEditorApply(PomoSettings &s, EditHit hit, const PomoSettings &defaults);

// True when a swipe up should open the editor: only the Pomodoro card, and
// only while the timer is idle, so a running session is never resized.
inline bool editorMayOpen(bool onPomodoro, PomoState state) {
  return onPomodoro && state == POMO_IDLE;
}
