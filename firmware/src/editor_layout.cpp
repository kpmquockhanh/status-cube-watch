#include "editor_layout.h"

namespace {

constexpr int ROW_Y0 = 46;
constexpr int ROW_H = 44;
constexpr int BTN_W = 46;  // touch zone of each round stepper, side by side at the right
constexpr int MARGIN = 10;
constexpr int MINUS_X = 140;
constexpr int PLUS_X = MINUS_X + BTN_W;
constexpr int PANEL_W = 240;
constexpr int BAR_Y = 232;
constexpr int BAR_H = 44;

}  // namespace

EditRect editorRow(int row) {
  return EditRect{0, (int16_t)(ROW_Y0 + row * ROW_H), PANEL_W, ROW_H};
}
EditRect editorLead(int row) {
  return EditRect{0, (int16_t)(ROW_Y0 + row * ROW_H), MINUS_X, ROW_H};
}
EditRect editorMinus(int row) {
  return EditRect{MINUS_X, (int16_t)(ROW_Y0 + row * ROW_H), BTN_W, ROW_H};
}
EditRect editorPlus(int row) {
  return EditRect{PLUS_X, (int16_t)(ROW_Y0 + row * ROW_H), BTN_W, ROW_H};
}
EditRect editorResetBtn() { return EditRect{MARGIN, BAR_Y, 108, BAR_H}; }
EditRect editorDoneBtn() { return EditRect{122, BAR_Y, 108, BAR_H}; }

EditHit editorHit(int16_t x, int16_t y) {
  for (int r = 0; r < EDIT_ROWS; r++) {
    if (editorMinus(r).contains(x, y)) return EditHit{EditAction::Dec, (uint8_t)r};
    if (editorPlus(r).contains(x, y)) return EditHit{EditAction::Inc, (uint8_t)r};
  }
  if (editorResetBtn().contains(x, y)) return EditHit{EditAction::Reset, 0};
  if (editorDoneBtn().contains(x, y)) return EditHit{EditAction::Done, 0};
  return EditHit{EditAction::None, 0};
}
