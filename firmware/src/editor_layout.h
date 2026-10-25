#pragma once
#include <stdint.h>

// The geometry of the on-device settings panel (dev_editor.h), minus the
// drawing. Layout rectangles live here and ui.cpp draws from them, so what is
// drawn and what is hit-tested cannot drift apart. Pure: no Arduino, no
// LovyanGFX.
//
//   y= 0..46    handle + icon/title
//   y=46..222   four rows of 44 px:  LABEL            ( - ) ( + )
//                                    value
//   y=224..268  [ RESET ]  [ DONE ]   (inset from the rounded corners)
constexpr int EDIT_ROWS = 4;

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

// Toggle is a tap on a row's lead; editorHit never returns it, devEditorHit
// adds it for the rows that have a switch.
enum class EditAction : uint8_t { None, Dec, Inc, Reset, Done, Toggle };
struct EditHit {
  EditAction action;
  uint8_t row;  // meaningful for Dec / Inc / Toggle
};

// The steppers and the RESET / DONE bar.
EditHit editorHit(int16_t x, int16_t y);
