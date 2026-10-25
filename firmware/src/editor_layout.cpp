#include "editor_layout.h"

namespace {

constexpr int ROW_Y0 = 46;
constexpr int ROW_H = 44;
constexpr int BTN_W = 46;  // touch zone of each round stepper, side by side at the right
constexpr int MARGIN = 10;
constexpr int MINUS_X = 140;
constexpr int PLUS_X = MINUS_X + BTN_W;
constexpr int PANEL_W = 240;
// The bar sits clear of the glass's rounded bottom corners (LCD_CORNER_R 40):
// its pills end at y 264, where the corner arc is 8 px in, and start 16 px in.
constexpr int BAR_Y = 224;
constexpr int BAR_H = 44;
constexpr int BAR_MARGIN = 16;
constexpr int BAR_GAP = 8;
constexpr int BAR_BTN_W = (PANEL_W - 2 * BAR_MARGIN - BAR_GAP) / 2;

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
EditRect editorResetBtn() { return EditRect{BAR_MARGIN, BAR_Y, BAR_BTN_W, BAR_H}; }
EditRect editorDoneBtn() { return EditRect{BAR_MARGIN + BAR_BTN_W + BAR_GAP, BAR_Y, BAR_BTN_W, BAR_H}; }

EditHit editorHit(int16_t x, int16_t y) {
  for (int r = 0; r < EDIT_ROWS; r++) {
    if (editorMinus(r).contains(x, y)) return EditHit{EditAction::Dec, (uint8_t)r};
    if (editorPlus(r).contains(x, y)) return EditHit{EditAction::Inc, (uint8_t)r};
  }
  if (editorResetBtn().contains(x, y)) return EditHit{EditAction::Reset, 0};
  if (editorDoneBtn().contains(x, y)) return EditHit{EditAction::Done, 0};
  return EditHit{EditAction::None, 0};
}
