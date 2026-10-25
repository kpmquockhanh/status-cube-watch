#pragma once
// The Agents grid and the prompt screen: where things sit, and a vertical
// scroll for both. Pure: no Arduino. ui.cpp draws from these numbers and
// main.cpp hit-tests with them, so what is drawn and what is pressed cannot
// drift apart. Host-tested in sim/tests/agents_layout_test.cpp.
//
// All coordinates are screen pixels. `scroll` is how far the content has moved
// up (0 = the top). A point outside the view band never hits anything, even
// where a clipped tile or button is still drawn.
#include <stdint.h>

struct LayoutRect {
  int16_t x, y, w, h;
};

// The grid sits under the card's caption (y 44): two columns of tiles in a
// view that scrolls.
constexpr int16_t AGENTS_VIEW_Y = 58;
constexpr int16_t AGENTS_VIEW_H = 196;
constexpr int16_t TILE_W = 102;
constexpr int16_t TILE_H = 84;
constexpr int16_t TILE_GAP = 8;
constexpr int16_t TILE_COL0_X = 14;
constexpr int16_t TILE_COL1_X = 124;

// Tile `i` (row-major, two per row) on screen at this scroll.
inline LayoutRect agentsTileRect(uint8_t i, int16_t scroll) {
  LayoutRect r;
  r.x = (i & 1) ? TILE_COL1_X : TILE_COL0_X;
  r.y = (int16_t)(AGENTS_VIEW_Y + (i / 2) * (TILE_H + TILE_GAP) - scroll);
  r.w = TILE_W;
  r.h = TILE_H;
  return r;
}

// Height of all rows together, 0 for no tiles.
inline int16_t agentsContentH(uint8_t nTiles) {
  const int rows = (nTiles + 1) / 2;
  return (int16_t)(rows == 0 ? 0 : rows * TILE_H + (rows - 1) * TILE_GAP);
}

// The tile under a touch, or -1 for none (a gap, past the last tile, or
// outside the view).
inline int agentsTileAt(int16_t x, int16_t y, uint8_t nTiles, int16_t scroll) {
  if (y < AGENTS_VIEW_Y || y >= AGENTS_VIEW_Y + AGENTS_VIEW_H) return -1;
  for (uint8_t i = 0; i < nTiles; i++) {
    const LayoutRect r = agentsTileRect(i, scroll);
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
  }
  return -1;
}

// A vertical drag, the way volume_slider.h drags its fill: the offset follows
// the finger relative to where it landed, so a touch alone never moves it.
struct ScrollY {
  // The content and the view it scrolls in; the offset is re-clamped.
  void setRange(int16_t contentH, int16_t viewH) {
    _max = contentH > viewH ? (int16_t)(contentH - viewH) : (int16_t)0;
    _off = clamp(_off);
  }
  void grab(int16_t y) {
    _held = true;
    _grabY = y;
    _grabOff = _off;
  }
  // The finger is at `y`; true when the offset moved.
  bool drag(int16_t y) {
    if (!_held) return false;
    const int16_t o = clamp((int16_t)(_grabOff - (y - _grabY)));
    if (o == _off) return false;
    _off = o;
    return true;
  }
  void release() { _held = false; }
  int16_t offset() const { return _off; }
  void reset() {
    _off = 0;
    _held = false;
  }

 private:
  int16_t clamp(int16_t o) const { return o < 0 ? (int16_t)0 : o > _max ? _max : o; }
  int16_t _max = 0, _off = 0, _grabY = 0, _grabOff = 0;
  bool _held = false;
};

// The prompt screen: a column that scrolls under a fixed header (the tile's
// name, y 44).
constexpr int16_t PROMPT_VIEW_Y = 58;
constexpr int16_t PROMPT_VIEW_H = 202;
constexpr int16_t PROMPT_BTN_H = 34;
constexpr int16_t PROMPT_BTN_GAP = 6;
constexpr int16_t PROMPT_BLOCK_GAP = 8;
constexpr int16_t PROMPT_TITLE_LINE_H = 20;
constexpr int16_t PROMPT_DETAIL_LINE_H = 14;
constexpr int16_t PROMPT_DETAIL_PAD = 12;
constexpr int16_t PROMPT_CONTEXT_H = 16;
constexpr int16_t PROMPT_REASON_H = 18;
constexpr int PROMPT_HIT_BACK = -2;

// What the text measured to: the caller wraps the strings, this places them.
struct PromptMetrics {
  uint8_t titleLines, detailLines;  // detailLines 0..3, 0 = no detail box
  bool context, reason;
};

// Blocks stacked in content coordinates from 0: title, detail box, context
// line, reason line, option buttons, BACK. The optional ones leave no gap. The
// reason (why the buttons are off) sits above the buttons so it shows without
// scrolling on a typical prompt.
struct PromptLayout {
  void build(const PromptMetrics &m, uint8_t nButtons) {
    _n = nButtons;
    int16_t y = (int16_t)(m.titleLines * PROMPT_TITLE_LINE_H);
    if (m.detailLines > 0) y = (int16_t)(y + PROMPT_BLOCK_GAP + m.detailLines * PROMPT_DETAIL_LINE_H + PROMPT_DETAIL_PAD);
    if (m.context) y = (int16_t)(y + PROMPT_BLOCK_GAP + PROMPT_CONTEXT_H);
    _reason = -1;
    if (m.reason) {
      _reason = (int16_t)(y + PROMPT_BLOCK_GAP);
      y = (int16_t)(_reason + PROMPT_REASON_H);
    }
    _btn0 = (int16_t)(y + PROMPT_BLOCK_GAP);
    if (nButtons > 0) y = (int16_t)(_btn0 + nButtons * PROMPT_BTN_H + (nButtons - 1) * PROMPT_BTN_GAP);
    _back = (int16_t)(y + PROMPT_BLOCK_GAP);
    _h = (int16_t)(_back + PROMPT_BTN_H);
  }
  int16_t contentH() const { return _h; }
  int16_t buttonY(uint8_t i) const { return (int16_t)(_btn0 + i * (PROMPT_BTN_H + PROMPT_BTN_GAP)); }
  // Top of the reason line, or -1 when there is none.
  int16_t reasonY() const { return _reason; }
  int16_t backY() const { return _back; }

  // The button under screen height `y`: its index, PROMPT_HIT_BACK, or -1.
  int buttonAt(int16_t y, int16_t scroll) const {
    if (y < PROMPT_VIEW_Y || y >= PROMPT_VIEW_Y + PROMPT_VIEW_H) return -1;
    const int c = y - PROMPT_VIEW_Y + scroll;  // content y
    for (uint8_t i = 0; i < _n; i++) {
      const int by = buttonY(i);
      if (c >= by && c < by + PROMPT_BTN_H) return i;
    }
    if (c >= _back && c < _back + PROMPT_BTN_H) return PROMPT_HIT_BACK;
    return -1;
  }

 private:
  uint8_t _n = 0;
  int16_t _btn0 = 0, _back = 0, _h = 0, _reason = -1;
};
