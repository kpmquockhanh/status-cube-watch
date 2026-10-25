#pragma once
// The hat picker (opened by a tap on the critter on the agent detail screen):
// where its cells, colour swatches and `< back` sit. Pure: no Arduino. ui.cpp
// draws inside these rectangles and main.cpp hit-tests with hatPickerAt, so
// what is drawn and what is pressed cannot drift apart; hatPickCode and
// hatPickWire say what a pick sends. Host-tested in
// sim/tests/hat_picker_layout_test.cpp.
#include <stdint.h>

#include "agents_layout.h"  // LayoutRect
#include "detail_frame.h"   // HAT_NONE, HAT_AUTO

// 15 cells: 0..12 the shapes in agents-deck's order, 13 `none`, 14 `auto`.
constexpr int HAT_PICK_SHAPES = 13, HAT_PICK_CELLS = 15, HAT_PICK_COLORS = 8;
constexpr int HAT_PICK_NONE_CELL = 13, HAT_PICK_AUTO_CELL = 14;
constexpr int HAT_PICK_COLS = 4;
constexpr int HAT_PICK_X = 12, HAT_PICK_Y = 60, HAT_PICK_W = 54, HAT_PICK_H = 44;
constexpr int HAT_PICK_PITCH_X = 56, HAT_PICK_PITCH_Y = 46;
// The colour swatches, one row under the cells, clear of the glass's rounded
// bottom corners (radius 40).
constexpr int HAT_SWATCH_X = 16, HAT_SWATCH_Y = 246, HAT_SWATCH_W = 24, HAT_SWATCH_H = 20, HAT_SWATCH_PITCH = 26;
// `< back` at the left of the y 44 row, as on the detail screen.
constexpr LayoutRect HAT_PICK_BACK = {0, 30, 96, 30};

struct HatPick {
  enum Kind : uint8_t { Nothing, Back, Shape, None, Auto, Color } kind;
  uint8_t index;  // the shape (0..12) or the colour (0..7); 0 otherwise
};

constexpr LayoutRect hatPickCell(int i) {
  return LayoutRect{(int16_t)(HAT_PICK_X + (i % HAT_PICK_COLS) * HAT_PICK_PITCH_X),
                    (int16_t)(HAT_PICK_Y + (i / HAT_PICK_COLS) * HAT_PICK_PITCH_Y), (int16_t)HAT_PICK_W,
                    (int16_t)HAT_PICK_H};
}

constexpr LayoutRect hatPickSwatch(int i) {
  return LayoutRect{(int16_t)(HAT_SWATCH_X + i * HAT_SWATCH_PITCH), (int16_t)HAT_SWATCH_Y, (int16_t)HAT_SWATCH_W,
                    (int16_t)HAT_SWATCH_H};
}

// Left and top edges inside, right and bottom edges outside.
constexpr bool hatPickIn(const LayoutRect &r, int x, int y) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// gnu++11: single-return constexpr helpers only.
constexpr int hatPickCol(int x) {
  return x < HAT_PICK_X || (x - HAT_PICK_X) % HAT_PICK_PITCH_X >= HAT_PICK_W ? -1 : (x - HAT_PICK_X) / HAT_PICK_PITCH_X;
}
constexpr int hatPickRow(int y) {
  return y < HAT_PICK_Y || (y - HAT_PICK_Y) % HAT_PICK_PITCH_Y >= HAT_PICK_H ? -1 : (y - HAT_PICK_Y) / HAT_PICK_PITCH_Y;
}
// The cell under (x, y), or -1.
constexpr int hatPickCellAt(int x, int y) {
  return hatPickCol(x) < 0 || hatPickCol(x) >= HAT_PICK_COLS || hatPickRow(y) < 0 ||
                 hatPickRow(y) * HAT_PICK_COLS + hatPickCol(x) >= HAT_PICK_CELLS
             ? -1
             : hatPickRow(y) * HAT_PICK_COLS + hatPickCol(x);
}
// The swatch under (x, y), or -1.
constexpr int hatPickSwatchAt(int x, int y) {
  return y < HAT_SWATCH_Y || y >= HAT_SWATCH_Y + HAT_SWATCH_H || x < HAT_SWATCH_X ||
                 (x - HAT_SWATCH_X) % HAT_SWATCH_PITCH >= HAT_SWATCH_W ||
                 (x - HAT_SWATCH_X) / HAT_SWATCH_PITCH >= HAT_PICK_COLORS
             ? -1
             : (x - HAT_SWATCH_X) / HAT_SWATCH_PITCH;
}

constexpr HatPick hatPickOfCell(int cell) {
  return cell < 0                      ? HatPick{HatPick::Nothing, 0}
         : cell == HAT_PICK_NONE_CELL ? HatPick{HatPick::None, 0}
         : cell == HAT_PICK_AUTO_CELL ? HatPick{HatPick::Auto, 0}
                                      : HatPick{HatPick::Shape, (uint8_t)cell};
}

// What a tap at (x, y) on the picker hits.
constexpr HatPick hatPickerAt(int16_t x, int16_t y) {
  return hatPickIn(HAT_PICK_BACK, x, y)    ? HatPick{HatPick::Back, 0}
         : hatPickSwatchAt(x, y) >= 0      ? HatPick{HatPick::Color, (uint8_t)hatPickSwatchAt(x, y)}
                                           : hatPickOfCell(hatPickCellAt(x, y));
}

// A pick as DetailView::pick takes it: 0..103 (shape * 8 + colour), none, or
// automatic; HAT_PICK_IGNORE for a tap that picks nothing.
constexpr int16_t HAT_CODE_NONE = -1, HAT_CODE_AUTO = -2, HAT_PICK_IGNORE = -3;

// The code a pick makes, given the hat shown now (-1 = none): a shape keeps the
// current colour (0 when none), a colour keeps the current shape (ignored
// while there is none).
constexpr int16_t hatPickCode(HatPick p, int16_t shown) {
  return p.kind == HatPick::Shape ? (int16_t)(p.index * 8 + (shown >= 0 ? shown % 8 : 0))
         : p.kind == HatPick::Color ? (shown >= 0 ? (int16_t)((shown / 8) * 8 + p.index) : HAT_PICK_IGNORE)
         : p.kind == HatPick::None  ? HAT_CODE_NONE
         : p.kind == HatPick::Auto  ? HAT_CODE_AUTO
                                    : HAT_PICK_IGNORE;
}

// A code as Control 0B carries it (detail_frame.h): 0..103, HAT_NONE, HAT_AUTO.
constexpr uint8_t hatPickWire(int16_t code) {
  return code == HAT_CODE_NONE ? HAT_NONE : code == HAT_CODE_AUTO ? HAT_AUTO : (uint8_t)code;
}
