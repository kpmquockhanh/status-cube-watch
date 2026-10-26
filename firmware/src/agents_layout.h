#pragma once
// The Agents grid and the prompt screen: where things sit, and a vertical
// scroll for both. Pure: no Arduino. ui.cpp draws from these numbers and
// main.cpp hit-tests with them, so what is drawn and what is pressed cannot
// drift apart. Host-tested in sim/tests/agents_layout_test.cpp.
//
// All coordinates are screen pixels. `scroll` is how far the content has moved
// up (0 = the top). A point outside the view band never hits anything, even
// where a clipped tile or button is still drawn.
#include <math.h>
#include <stdint.h>

struct LayoutRect {
  int16_t x, y, w, h;
};

// The grid sits under the card's caption (y 44): two columns of tiles in a
// view that scrolls.
constexpr int16_t AGENTS_VIEW_Y = 54;
constexpr int16_t AGENTS_VIEW_H = 200;  // holds two rows of tiles (2 x 96 + 6)
constexpr int16_t TILE_W = 108;
constexpr int16_t TILE_H = 96;  // the 72 x 52 critter (4 px a cell), its hat, name, meta line, bar
constexpr int16_t TILE_GAP = 6;
constexpr int16_t TILE_COL0_X = 9;
constexpr int16_t TILE_COL1_X = 123;

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

// A running tile's ground (agents-deck's dot-matrix look): faint accent dots
// every RUN_DOT_PITCH px that a soft diagonal wave brightens, bottom-left to
// top-right over RUN_WAVE_SWEEP_MS, then a pause to RUN_WAVE_MS; and
// RUN_PARTICLES thought specks rising from the bottom at their own pace,
// fading in, holding, fading out before the top. A working agent never sits
// on a blank ground like an idle one. The web board draws the same
// (board.css .fx); ui.cpp draws these numbers.
constexpr int16_t RUN_DOT_PITCH = 6;   // a 1 px dot every 6 px
constexpr int16_t RUN_DOT_INSET = 4;   // the first dot this far in: clear of the 2 px gauge
constexpr uint32_t RUN_WAVE_MS = 2400;
constexpr uint32_t RUN_WAVE_SWEEP_MS = 1800;
constexpr uint8_t RUN_DOT_BASE = 38;   // ~15 % accent
constexpr uint8_t RUN_DOT_PEAK = 160;  // ~63 % on the wave's crest
constexpr float RUN_WAVE_BAND = 0.25f; // the crest's half-width, in diagonal units (0..1 across the tile)
constexpr uint8_t RUN_PARTICLES = 4;
constexpr int16_t RUN_PARTICLE_SIZE = 2;
constexpr uint8_t RUN_PARTICLE_ALPHA = 200;  // ~78 % at full

// The accent's alpha over the tile for the dot at (dx, dy) inside a w x h
// tile at `ms`: RUN_DOT_BASE, raised towards RUN_DOT_PEAK near the wave's crest.
inline uint8_t runDotAlpha(int16_t dx, int16_t dy, int16_t w, int16_t h, uint32_t ms) {
  const uint32_t t = ms % RUN_WAVE_MS;
  if (t >= RUN_WAVE_SWEEP_MS || w + h <= 2) return RUN_DOT_BASE;
  // The crest runs from just before the bottom-left corner to just past the top-right one.
  const float f = -RUN_WAVE_BAND + (1 + 2 * RUN_WAVE_BAND) * (float)t / (float)RUN_WAVE_SWEEP_MS;
  const float d = (float)(dx + (h - 1 - dy)) / (float)(w + h - 2);  // 0 bottom-left .. 1 top-right
  float k = 1 - fabsf(d - f) / RUN_WAVE_BAND;
  if (k <= 0) return RUN_DOT_BASE;
  k = k * k * (3 - 2 * k);  // smoothstep: a soft crest
  return (uint8_t)(RUN_DOT_BASE + k * (RUN_DOT_PEAK - RUN_DOT_BASE) + 0.5f);
}

// One rising speck: its top-left inside the tile, and its alpha (0: not drawn).
struct RunParticle {
  int16_t x, y;
  uint8_t alpha;
};

// Speck `i` (0..RUN_PARTICLES-1) of a w x h tile at `ms`. Each has its own
// column, period and start, so they never move in step.
inline RunParticle runParticle(uint8_t i, int16_t w, int16_t h, uint32_t ms) {
  static const uint8_t X_PCT[RUN_PARTICLES] = {22, 71, 46, 88};
  static const uint16_t PERIOD_MS[RUN_PARTICLES] = {3000, 3600, 2700, 3300};
  static const uint16_t OFFSET_MS[RUN_PARTICLES] = {0, 1400, 700, 2200};
  RunParticle p = {0, 0, 0};
  if (i >= RUN_PARTICLES) return p;
  const float t = (float)((ms + OFFSET_MS[i]) % PERIOD_MS[i]) / (float)PERIOD_MS[i];
  const int16_t bottom = (int16_t)(h - 6 - RUN_PARTICLE_SIZE), top = 6;  // inside the gauge, with air
  p.x = (int16_t)(w * X_PCT[i] / 100 - RUN_PARTICLE_SIZE / 2);
  p.y = (int16_t)(bottom - t * (bottom - top) + 0.5f);
  // In over the first 15 %, held to 60 %, out by the end: linear.
  const float a = t < 0.15f ? t / 0.15f : t > 0.6f ? (1 - t) / 0.4f : 1;
  p.alpha = (uint8_t)(a * RUN_PARTICLE_ALPHA + 0.5f);
  return p;
}

// The prompt screen: a column that scrolls under a fixed header (the tile's
// name, y 44).
constexpr int16_t PROMPT_VIEW_Y = 58;
constexpr int16_t PROMPT_VIEW_H = 202;
// The column across: the text and every button (answers and BACK) span it.
constexpr int16_t PROMPT_COL_X = 20;
constexpr int16_t PROMPT_COL_W = 200;
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

  // The button under screen point (x, y): its index, PROMPT_HIT_BACK, or -1.
  int buttonAt(int16_t x, int16_t y, int16_t scroll) const {
    if (x < PROMPT_COL_X || x >= PROMPT_COL_X + PROMPT_COL_W) return -1;
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
