#include "check.h"
#include "hat_picker_layout.h"

static bool is(HatPick p, HatPick::Kind k, int index) { return p.kind == k && p.index == index; }
static bool kind(HatPick p, HatPick::Kind k) { return p.kind == k; }

int main() {
  CHECK(HAT_PICK_CELLS == 15 && HAT_PICK_SHAPES == 13 && HAT_PICK_COLORS == 8);
  CHECK(HAT_PICK_BACK.x == 0 && HAT_PICK_BACK.y == 30 && HAT_PICK_BACK.w == 96 && HAT_PICK_BACK.h == 30);

  // Cell geometry: 4 columns of 54 x 44 at x = 12 + col * 56, y = 60 + row * 46.
  CHECK(hatPickCell(0).x == 12 && hatPickCell(0).y == 60 && hatPickCell(0).w == 54 && hatPickCell(0).h == 44);
  CHECK(hatPickCell(3).x == 12 + 3 * 56 && hatPickCell(3).y == 60);
  CHECK(hatPickCell(4).x == 12 && hatPickCell(4).y == 106);
  CHECK(hatPickCell(14).x == 12 + 2 * 56 && hatPickCell(14).y == 60 + 3 * 46);
  // Swatches: 26 x 22 at x = 12 + i * 28, y = 250.
  CHECK(hatPickSwatch(0).x == 12 && hatPickSwatch(0).y == 250 && hatPickSwatch(0).w == 26 && hatPickSwatch(0).h == 22);
  CHECK(hatPickSwatch(7).x == 12 + 7 * 28);

  CHECK(is(hatPickerAt(13, 61), HatPick::Shape, 0));
  CHECK(is(hatPickerAt(12 + 3 * 56 + 1, 60 + 1), HatPick::Shape, 3));
  CHECK(is(hatPickerAt(12 + 0 * 56 + 10, 60 + 3 * 46 + 10), HatPick::Shape, 12));  // cell 12: row 3, col 0
  CHECK(kind(hatPickerAt(12 + 1 * 56 + 10, 60 + 3 * 46 + 10), HatPick::None));     // cell 13
  CHECK(kind(hatPickerAt(12 + 2 * 56 + 10, 60 + 3 * 46 + 10), HatPick::Auto));     // cell 14
  CHECK(kind(hatPickerAt(12 + 3 * 56 + 10, 60 + 3 * 46 + 10), HatPick::Nothing));  // no cell 15
  CHECK(is(hatPickerAt(12 + 7 * 28 + 1, 251), HatPick::Color, 7));
  CHECK(is(hatPickerAt(12, 250), HatPick::Color, 0));

  // Edges: left/top inside, right/bottom outside; the gaps are nothing.
  CHECK(is(hatPickerAt(12 + 53, 60 + 43), HatPick::Shape, 0));
  CHECK(kind(hatPickerAt(12 + 54, 70), HatPick::Nothing));  // the 2 px column gap
  CHECK(kind(hatPickerAt(12 + 55, 70), HatPick::Nothing));
  CHECK(is(hatPickerAt(12 + 56, 70), HatPick::Shape, 1));
  CHECK(kind(hatPickerAt(30, 60 + 44), HatPick::Nothing));  // the row gap
  CHECK(kind(hatPickerAt(30, 60 + 45), HatPick::Nothing));
  CHECK(is(hatPickerAt(30, 60 + 46), HatPick::Shape, 4));
  CHECK(kind(hatPickerAt(11, 70), HatPick::Nothing));
  CHECK(kind(hatPickerAt(12 + 26, 255), HatPick::Nothing));  // between swatches
  CHECK(kind(hatPickerAt(12 + 27, 255), HatPick::Nothing));
  CHECK(kind(hatPickerAt(20, 249), HatPick::Nothing));
  CHECK(kind(hatPickerAt(20, 272), HatPick::Nothing));
  CHECK(kind(hatPickerAt(12 + 7 * 28 + 26, 255), HatPick::Nothing));
  CHECK(kind(hatPickerAt(-1, -1), HatPick::Nothing));

  CHECK(kind(hatPickerAt(10, 40), HatPick::Back));
  CHECK(kind(hatPickerAt(95, 59), HatPick::Back));
  CHECK(kind(hatPickerAt(96, 40), HatPick::Nothing));
  CHECK(kind(hatPickerAt(10, 29), HatPick::Nothing));

  return checksDone("hat_picker_layout");
}
