#include <cstdint>

#include "check.h"
#include "pomo_editor.h"

namespace {

const PomoSettings DEF{25, 5, 15, 4};

void centre(const EditRect &r, int &x, int &y) {
  x = r.x + r.w / 2;
  y = r.y + r.h / 2;
}

void testTargetsAreBigEnough() {
  for (int r = 0; r < EDIT_ROWS; r++) {
    CHECK(editorMinus(r).w >= 44 && editorMinus(r).h >= 44);
    CHECK(editorPlus(r).w >= 44 && editorPlus(r).h >= 44);
  }
  CHECK(editorResetBtn().h >= 44 && editorDoneBtn().h >= 44);
  CHECK(editorDoneBtn().y + editorDoneBtn().h <= 280);  // fits the panel
}

// The lead (label, value, a switch) and the two steppers share a row without
// overlapping, left to right, inside the panel.
void testRowLayout() {
  for (int r = 0; r < EDIT_ROWS; r++) {
    const EditRect l = editorLead(r), m = editorMinus(r), p = editorPlus(r);
    CHECK(l.x == 0 && l.y == editorRow(r).y && l.h == editorRow(r).h);
    CHECK(l.x + l.w <= m.x && m.x + m.w <= p.x && p.x + p.w <= 240);
    CHECK(l.w >= 120);  // room for "SHORT BREAK", or a label, its value and a switch
  }
}

void testHitEveryButton() {
  int x, y;
  for (int r = 0; r < EDIT_ROWS; r++) {
    centre(editorMinus(r), x, y);
    EditHit h = pomoEditorHit((int16_t)x, (int16_t)y);
    CHECK(h.action == EditAction::Dec && h.row == r);
    centre(editorPlus(r), x, y);
    h = pomoEditorHit((int16_t)x, (int16_t)y);
    CHECK(h.action == EditAction::Inc && h.row == r);
  }
  centre(editorResetBtn(), x, y);
  CHECK(pomoEditorHit((int16_t)x, (int16_t)y).action == EditAction::Reset);
  centre(editorDoneBtn(), x, y);
  CHECK(pomoEditorHit((int16_t)x, (int16_t)y).action == EditAction::Done);
}

// Review Focus 3: margins, the label and value, the title and off-panel taps do
// nothing (the Pomodoro rows have no switch).
void testMissesDoNothing() {
  const EditRect m = editorMinus(1);
  const EditRect p = editorPlus(1);
  int x, y;
  centre(editorLead(1), x, y);
  CHECK(pomoEditorHit((int16_t)x, (int16_t)y).action == EditAction::None);          // label and value
  CHECK(pomoEditorHit((int16_t)(m.x - 1), m.y + 20).action == EditAction::None);     // just left of minus
  CHECK(pomoEditorHit((int16_t)(p.x + p.w), p.y + 20).action == EditAction::None);   // right margin
  CHECK(pomoEditorHit(30, 10).action == EditAction::None);                           // title
  CHECK(pomoEditorHit(30, 279).action == EditAction::None);                          // below the buttons
  CHECK(pomoEditorHit(-5, -5).action == EditAction::None);
  // The gap between RESET and DONE belongs to neither.
  const EditRect rb = editorResetBtn();
  const EditRect db = editorDoneBtn();
  if (rb.x + rb.w < db.x)
    CHECK(pomoEditorHit((int16_t)(rb.x + rb.w), rb.y + 5).action == EditAction::None);
}

// Review Focus 3: rows are exclusive at their shared boundary.
void testRowBoundaries() {
  for (int r = 0; r + 1 < EDIT_ROWS; r++) {
    const EditRect a = editorMinus(r);
    const EditHit last = pomoEditorHit((int16_t)(a.x + 5), (int16_t)(a.y + a.h - 1));
    const EditHit next = pomoEditorHit((int16_t)(a.x + 5), (int16_t)(a.y + a.h));
    CHECK(last.action == EditAction::Dec && last.row == r);
    CHECK(next.action == EditAction::Dec && next.row == r + 1);
  }
}

PomoSettings press(PomoSettings s, EditAction a, uint8_t row, int times = 1) {
  for (int i = 0; i < times; i++) pomoEditorApply(s, EditHit{a, row}, DEF);
  return s;
}

void testStepOneRows() {
  CHECK(press(DEF, EditAction::Inc, 1).shortMin == 6);
  CHECK(press(DEF, EditAction::Dec, 1).shortMin == 4);
  CHECK(press(DEF, EditAction::Inc, 3).sessions == 5);
  CHECK(press(DEF, EditAction::Dec, 3).sessions == 3);
}

void testStepFiveSnaps() {
  CHECK(press(DEF, EditAction::Inc, 0).focusMin == 30);
  CHECK(press(DEF, EditAction::Dec, 0).focusMin == 20);
  CHECK(press(PomoSettings{27, 5, 15, 4}, EditAction::Inc, 0).focusMin == 30);
  CHECK(press(PomoSettings{27, 5, 15, 4}, EditAction::Dec, 0).focusMin == 25);
  CHECK(press(PomoSettings{1, 5, 15, 4}, EditAction::Inc, 0).focusMin == 5);
  CHECK(press(PomoSettings{5, 5, 15, 4}, EditAction::Dec, 0).focusMin == 1);
  CHECK(press(PomoSettings{98, 5, 15, 4}, EditAction::Inc, 0).focusMin == 99);
  CHECK(press(PomoSettings{99, 5, 15, 4}, EditAction::Dec, 0).focusMin == 95);
  CHECK(press(DEF, EditAction::Inc, 2).longMin == 20);  // LONG steps by 5 too
}

// Review Focus 4: holding a button at the limit stays at the limit.
void testClampsNeverWrap() {
  CHECK(press(DEF, EditAction::Dec, 0, 50).focusMin == 1);
  CHECK(press(DEF, EditAction::Inc, 0, 50).focusMin == 99);
  CHECK(press(DEF, EditAction::Dec, 1, 50).shortMin == 1);
  CHECK(press(DEF, EditAction::Inc, 1, 200).shortMin == 99);
  CHECK(press(DEF, EditAction::Dec, 3, 50).sessions == 1);
  CHECK(press(DEF, EditAction::Inc, 3, 50).sessions == 9);
}

void testResetAndNoOps() {
  PomoSettings s{40, 10, 30, 2};
  pomoEditorApply(s, EditHit{EditAction::Reset, 0}, DEF);
  CHECK(s.focusMin == 25 && s.shortMin == 5 && s.longMin == 15 && s.sessions == 4);
  PomoSettings t{40, 10, 30, 2};
  pomoEditorApply(t, EditHit{EditAction::None, 0}, DEF);
  pomoEditorApply(t, EditHit{EditAction::Done, 0}, DEF);  // Done is acted on by the caller
  pomoEditorApply(t, EditHit{EditAction::Toggle, 0}, DEF);  // no Pomodoro row has a switch
  CHECK(t.focusMin == 40 && t.shortMin == 10 && t.longMin == 30 && t.sessions == 2);
  pomoEditorApply(t, EditHit{EditAction::Inc, 9}, DEF);  // a bad row index is ignored
  CHECK(t.focusMin == 40 && t.sessions == 2);
}

// Review Focus 1: only an idle timer, on the Pomodoro card, may be edited.
void testMayOpen() {
  CHECK(editorMayOpen(true, POMO_IDLE));
  CHECK(!editorMayOpen(true, POMO_FOCUS));
  CHECK(!editorMayOpen(true, POMO_BREAK));
  CHECK(!editorMayOpen(true, POMO_PAUSED));
  CHECK(!editorMayOpen(true, POMO_DONE));
  CHECK(!editorMayOpen(false, POMO_IDLE));
}

void testLabels() {
  CHECK(editorLabel(0)[0] == 'F');
  CHECK(editorLabel(3)[0] == 'S');
  CHECK(editorLabel(7)[0] == '\0');
}

}  // namespace

int main() {
  testTargetsAreBigEnough();
  testRowLayout();
  testHitEveryButton();
  testMissesDoNothing();
  testRowBoundaries();
  testStepOneRows();
  testStepFiveSnaps();
  testClampsNeverWrap();
  testResetAndNoOps();
  testMayOpen();
  testLabels();
  return checksDone("pomo_editor_test");
}
