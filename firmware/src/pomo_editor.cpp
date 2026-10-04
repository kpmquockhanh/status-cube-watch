#include "pomo_editor.h"

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

struct RowSpec {
  uint8_t step;
  uint8_t lo, hi;
  const char *label;
};
const RowSpec ROWS[EDIT_ROWS] = {
    {5, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "FOCUS"},
    {1, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "SHORT BREAK"},
    {5, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "LONG BREAK"},
    {1, POMO_MIN_SESSIONS, POMO_MAX_SESSIONS, "SESSIONS"},
};

uint8_t *field(PomoSettings &s, int row) {
  switch (row) {
    case 0: return &s.focusMin;
    case 1: return &s.shortMin;
    case 2: return &s.longMin;
    default: return &s.sessions;
  }
}

// Step-1 rows move by one. Step-5 rows snap to multiples of 5 so the usual
// 25 / 50 / 15 are reachable in a few taps: `+` goes to the next multiple
// above, `-` to the next one below, both clamped (1 is the floor, so 5 -> 1).
uint8_t stepped(uint8_t v, bool up, const RowSpec &spec) {
  int next;
  if (spec.step == 1) {
    next = up ? v + 1 : v - 1;
  } else if (up) {
    next = (v / 5 + 1) * 5;
  } else {
    next = (v % 5 == 0) ? v - 5 : (v / 5) * 5;
  }
  return pomoClampU8(next, spec.lo, spec.hi);
}

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

const char *editorLabel(int row) { return (row >= 0 && row < EDIT_ROWS) ? ROWS[row].label : ""; }

EditHit pomoEditorHit(int16_t x, int16_t y) {
  for (int r = 0; r < EDIT_ROWS; r++) {
    if (editorMinus(r).contains(x, y)) return EditHit{EditAction::Dec, (uint8_t)r};
    if (editorPlus(r).contains(x, y)) return EditHit{EditAction::Inc, (uint8_t)r};
  }
  if (editorResetBtn().contains(x, y)) return EditHit{EditAction::Reset, 0};
  if (editorDoneBtn().contains(x, y)) return EditHit{EditAction::Done, 0};
  return EditHit{EditAction::None, 0};
}

void pomoEditorApply(PomoSettings &s, EditHit hit, const PomoSettings &defaults) {
  switch (hit.action) {
    case EditAction::Dec:
    case EditAction::Inc:
      if (hit.row >= EDIT_ROWS) return;
      *field(s, hit.row) = stepped(*field(s, hit.row), hit.action == EditAction::Inc, ROWS[hit.row]);
      break;
    case EditAction::Reset:
      s = defaults;
      break;
    default:
      break;
  }
}
