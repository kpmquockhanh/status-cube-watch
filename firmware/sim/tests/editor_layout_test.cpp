#include <cstdint>

#include "check.h"
#include "editor_layout.h"

namespace {

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
    CHECK(l.w >= 120);  // room for a label, its value and a switch
  }
}

void testHitEveryButton() {
  int x, y;
  for (int r = 0; r < EDIT_ROWS; r++) {
    centre(editorMinus(r), x, y);
    EditHit h = editorHit((int16_t)x, (int16_t)y);
    CHECK(h.action == EditAction::Dec && h.row == r);
    centre(editorPlus(r), x, y);
    h = editorHit((int16_t)x, (int16_t)y);
    CHECK(h.action == EditAction::Inc && h.row == r);
  }
  centre(editorResetBtn(), x, y);
  CHECK(editorHit((int16_t)x, (int16_t)y).action == EditAction::Reset);
  centre(editorDoneBtn(), x, y);
  CHECK(editorHit((int16_t)x, (int16_t)y).action == EditAction::Done);
}

// Review Focus 3: margins, the label and value, the title and off-panel taps do
// nothing (editorHit has no switch zone).
void testMissesDoNothing() {
  const EditRect m = editorMinus(1);
  const EditRect p = editorPlus(1);
  int x, y;
  centre(editorLead(1), x, y);
  CHECK(editorHit((int16_t)x, (int16_t)y).action == EditAction::None);          // label and value
  CHECK(editorHit((int16_t)(m.x - 1), m.y + 20).action == EditAction::None);     // just left of minus
  CHECK(editorHit((int16_t)(p.x + p.w), p.y + 20).action == EditAction::None);   // right margin
  CHECK(editorHit(30, 10).action == EditAction::None);                           // title
  CHECK(editorHit(30, 279).action == EditAction::None);                          // below the buttons
  CHECK(editorHit(-5, -5).action == EditAction::None);
  // The gap between RESET and DONE belongs to neither.
  const EditRect rb = editorResetBtn();
  const EditRect db = editorDoneBtn();
  if (rb.x + rb.w < db.x)
    CHECK(editorHit((int16_t)(rb.x + rb.w), rb.y + 5).action == EditAction::None);
}

// Review Focus 3: rows are exclusive at their shared boundary.
void testRowBoundaries() {
  for (int r = 0; r + 1 < EDIT_ROWS; r++) {
    const EditRect a = editorMinus(r);
    const EditHit last = editorHit((int16_t)(a.x + 5), (int16_t)(a.y + a.h - 1));
    const EditHit next = editorHit((int16_t)(a.x + 5), (int16_t)(a.y + a.h));
    CHECK(last.action == EditAction::Dec && last.row == r);
    CHECK(next.action == EditAction::Dec && next.row == r + 1);
  }
}

}  // namespace

int main() {
  testTargetsAreBigEnough();
  testRowLayout();
  testHitEveryButton();
  testMissesDoNothing();
  testRowBoundaries();
  return checksDone("editor_layout_test");
}
