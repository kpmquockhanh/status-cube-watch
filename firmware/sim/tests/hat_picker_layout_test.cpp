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
  // Swatches: 24 x 20 at x = 16 + i * 26, y = 246, clear of the glass's rounded
  // bottom corners (radius 40): the outer corners (16, 265) and (221, 265) are
  // inside the arc.
  CHECK(hatPickSwatch(0).x == 16 && hatPickSwatch(0).y == 246 && hatPickSwatch(0).w == 24 && hatPickSwatch(0).h == 20);
  CHECK(hatPickSwatch(7).x == 16 + 7 * 26);
  CHECK((16 - 40) * (16 - 40) + (265 - 240) * (265 - 240) < 40 * 40);
  CHECK((16 + 7 * 26 + 23 - 200) * (16 + 7 * 26 + 23 - 200) + (265 - 240) * (265 - 240) < 40 * 40);

  CHECK(is(hatPickerAt(13, 61), HatPick::Shape, 0));
  CHECK(is(hatPickerAt(12 + 3 * 56 + 1, 60 + 1), HatPick::Shape, 3));
  CHECK(is(hatPickerAt(12 + 0 * 56 + 10, 60 + 3 * 46 + 10), HatPick::Shape, 12));  // cell 12: row 3, col 0
  CHECK(kind(hatPickerAt(12 + 1 * 56 + 10, 60 + 3 * 46 + 10), HatPick::None));     // cell 13
  CHECK(kind(hatPickerAt(12 + 2 * 56 + 10, 60 + 3 * 46 + 10), HatPick::Auto));     // cell 14
  CHECK(kind(hatPickerAt(12 + 3 * 56 + 10, 60 + 3 * 46 + 10), HatPick::Nothing));  // no cell 15
  CHECK(is(hatPickerAt(16 + 7 * 26 + 1, 247), HatPick::Color, 7));
  CHECK(is(hatPickerAt(16, 246), HatPick::Color, 0));

  // Edges: left/top inside, right/bottom outside; the gaps are nothing.
  CHECK(is(hatPickerAt(12 + 53, 60 + 43), HatPick::Shape, 0));
  CHECK(kind(hatPickerAt(12 + 54, 70), HatPick::Nothing));  // the 2 px column gap
  CHECK(kind(hatPickerAt(12 + 55, 70), HatPick::Nothing));
  CHECK(is(hatPickerAt(12 + 56, 70), HatPick::Shape, 1));
  CHECK(kind(hatPickerAt(30, 60 + 44), HatPick::Nothing));  // the row gap
  CHECK(kind(hatPickerAt(30, 60 + 45), HatPick::Nothing));
  CHECK(is(hatPickerAt(30, 60 + 46), HatPick::Shape, 4));
  CHECK(kind(hatPickerAt(11, 70), HatPick::Nothing));
  CHECK(kind(hatPickerAt(16 + 24, 255), HatPick::Nothing));  // between swatches
  CHECK(kind(hatPickerAt(16 + 25, 255), HatPick::Nothing));
  CHECK(kind(hatPickerAt(15, 255), HatPick::Nothing));
  CHECK(kind(hatPickerAt(20, 245), HatPick::Nothing));
  CHECK(kind(hatPickerAt(20, 266), HatPick::Nothing));
  CHECK(kind(hatPickerAt(16 + 7 * 26 + 24, 255), HatPick::Nothing));
  CHECK(kind(hatPickerAt(-1, -1), HatPick::Nothing));

  CHECK(kind(hatPickerAt(10, 40), HatPick::Back));
  CHECK(kind(hatPickerAt(95, 59), HatPick::Back));
  CHECK(kind(hatPickerAt(96, 40), HatPick::Nothing));
  CHECK(kind(hatPickerAt(10, 29), HatPick::Nothing));

  // What a pick means, given the hat shown now (-1 none).
  const HatPick shape4{HatPick::Shape, 4}, color2{HatPick::Color, 2};
  CHECK(hatPickCode(shape4, 17) == 4 * 8 + 1);  // keeps colour 1
  CHECK(hatPickCode(shape4, -1) == 4 * 8 + 0);  // none: colour 0
  CHECK(hatPickCode(HatPick{HatPick::Shape, 12}, 103) == 12 * 8 + 7);
  CHECK(hatPickCode(color2, 17) == 2 * 8 + 2);  // keeps shape 2
  CHECK(hatPickCode(HatPick{HatPick::Color, 7}, 96) == 12 * 8 + 7);
  CHECK(hatPickCode(color2, -1) == HAT_PICK_IGNORE);  // no hat to colour
  CHECK(hatPickCode(HatPick{HatPick::None, 0}, 17) == HAT_CODE_NONE && HAT_CODE_NONE == -1);
  CHECK(hatPickCode(HatPick{HatPick::Auto, 0}, 17) == HAT_CODE_AUTO && HAT_CODE_AUTO == -2);
  CHECK(hatPickCode(HatPick{HatPick::Auto, 0}, -1) == HAT_CODE_AUTO);
  CHECK(hatPickCode(HatPick{HatPick::Back, 0}, 17) == HAT_PICK_IGNORE);
  CHECK(hatPickCode(HatPick{HatPick::Nothing, 0}, 17) == HAT_PICK_IGNORE);
  // The code on the wire (Control 0B).
  CHECK(hatPickWire(HAT_CODE_NONE) == 0xFE && hatPickWire(HAT_CODE_NONE) == HAT_NONE);
  CHECK(hatPickWire(HAT_CODE_AUTO) == 0xFF && hatPickWire(HAT_CODE_AUTO) == HAT_AUTO);
  CHECK(hatPickWire(0) == 0 && hatPickWire(33) == 33 && hatPickWire(103) == 103);

  return checksDone("hat_picker_layout");
}
