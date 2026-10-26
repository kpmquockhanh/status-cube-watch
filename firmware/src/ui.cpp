#include "ui.h"

#include <math.h>

#include "age_label.h"
#include "battery_util.h"
#include "config.h"
#include "critter.h"
#include "detail_layout.h"
#include "hat_picker_layout.h"
#include "fonts_gen.h"
#include "dev_editor.h"
#include "editor_layout.h"
#include "settings.h"
#include "utf8_fit.h"

namespace {

// --- theme ---------------------------------------------------------------
// agents-deck's look (github.com/brushknight/agents-deck): a near-black ground,
// warm ink, one orange accent, hairline panels with square corners, Roboto Mono
// for names and values and Share Tech Mono for the small labels. The navigation
// screens keep their own look (the nav section below).
//
// The payload's accent names, as RGB888 as well as 565 because the threshold
// crossfade interpolates in 888 and converts once per frame; matches
// bridge/preview.html so the mock, the simulator and the panel all agree.
constexpr uint32_t RGB888[7] = {
    0xE8E4DC,  // ink
    0xFF4D00,  // accent
    0x6F8FD8,  // blue
    0x6DBE6A,  // green
    0xE8B94A,  // amber (agents-deck's yellow)
    0xA68BD8,  // violet (agents-deck's purple)
    0xFF4D00,  // red: agents-deck has one alarm colour, the accent
};

constexpr uint32_t BG_RGB = 0x000000;
constexpr uint32_t PANEL_RGB = 0x141412;  // a lifted surface: the display panel
constexpr uint32_t TILE_RGB = 0x0F0F0D;   // a critter's ground: Agents tiles, hat picker cells (darker than PANEL)
constexpr uint32_t DIM_RGB = 0x7A776F;    // secondary text
constexpr uint32_t LINE_RGB = 0x262622;   // hairlines, tracks, idle buttons
constexpr uint32_t MUTED_RGB = 0x4A4842;  // a disabled glyph on LINE
constexpr uint32_t EMPTY_RGB = 0x0B0B0B;  // an empty slot

uint16_t g_palette[7];
uint16_t BG, PANEL, TILE, INK, DIM, LINE, ACCENT, EMPTY;
// MUTED a disabled glyph; ACC_DEV the panel's accent.
uint16_t MUTED, ACC_DEV;

LGFX_Sprite g_canvas;
bool g_sprite = false;
bool g_animating = false;

// --- ring geometry -------------------------------------------------------
// A 270 degree arc with the gap at the bottom, so the two ends sit either
// side of the readout line and the ring never collides with it. Angles are
// LovyanGFX's: 0 at 3 o'clock, growing clockwise -- the same convention SVG
// uses, so these are the numbers from the design canvas unchanged.
constexpr int RING_CX = LCD_WIDTH / 2;
constexpr int RING_CY = 128;
constexpr int RING_R_OUT = 73;
constexpr int RING_R_IN = 60;
// Inside the ring: the value, its caption below, then (in the gap the arc leaves
// at the bottom) the percentage line, and under it all the card's name.
constexpr int VALUE_CY = RING_CY - 13;
constexpr int CAPTION_Y = RING_CY + 10;
constexpr int PCT_Y = RING_CY + 66;
constexpr int NAME_Y = RING_CY + 102;  // a line under the percentage, clear of the page dots
constexpr int VALUE_MAX_W = 104;  // between the ring's inner edges at the value's height
constexpr int VALUE_MAX_H = 36;
constexpr float ARC_START = 135.0f;
constexpr float ARC_SWEEP = 270.0f;
// Cut in the background colour at the amber and red thresholds, so the arc is
// legible against its own limits without a legend anywhere on screen.
constexpr float NOTCHES[] = {0.60f, 0.85f};
constexpr float NOTCH_HALF_DEG = 0.7f;

// --- motion --------------------------------------------------------------
// The board has no CSS, so the durations from the design's motion spec are
// reproduced here: one sweep the first time a card is seen after boot, and a
// short retarget whenever the number moves under it. Everything eases out --
// approximating cubic-bezier(0.23, 1, 0.32, 1) with a quartic, which is
// within a couple of percent of it across the whole curve.
constexpr uint32_t SWEEP_MS = 700;
constexpr uint32_t RETARGET_MS = 260;
constexpr uint32_t COLOR_MS = 400;

struct GaugeAnim {
  bool seen = false;
  float from = 0, to = 0, shown = 0;
  uint32_t start = 0, dur = 0;
  uint32_t colFrom = 0, colTo = 0;
  uint32_t colStart = 0;
};

// One per card. The sweep plays at boot and again whenever a card is brought
// up by a swipe (uiReplay), so the ring visibly fills each time you
// look at it; periodic redraws and data updates only retarget it.
GaugeAnim g_anim[MAX_CARDS];
GaugeAnim g_anim2[MAX_CARDS];  // the inner ring of a dual-ring card

float easeOut(float t) {
  if (t <= 0.0f) return 0.0f;
  if (t >= 1.0f) return 1.0f;
  const float u = 1.0f - t;
  return 1.0f - u * u * u * u;
}

float progress(uint32_t now, uint32_t start, uint32_t dur) {
  if (dur == 0) return 1.0f;
  const uint32_t elapsed = now - start;
  return elapsed >= dur ? 1.0f : (float)elapsed / (float)dur;
}

uint32_t lerpRgb(uint32_t a, uint32_t b, float t) {
  const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  return ((uint32_t)(ar + (br - ar) * t) << 16) |
         ((uint32_t)(ag + (bg - ag) * t) << 8) |
         (uint32_t)(ab + (bb - ab) * t);
}

uint16_t to565(uint32_t rgb) {
  return lgfx::color565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

LovyanGFX *target(Display &lcd) {
  return g_sprite ? static_cast<LovyanGFX *>(&g_canvas) : static_cast<LovyanGFX *>(&lcd);
}

// --- sending a frame -----------------------------------------------------------
// Every screen is composed whole in the sprite, but only the rows that differ
// from what the panel already shows go over SPI. A full frame is 134 KB, which
// keeps the CPU waiting on the bus for ~13.4 ms, and most frames change only a
// line or two of text (the age counter, a countdown). Each row is reduced
// to a hash and compared with the hash of the row last sent. A row that changes
// in a single word always changes its hash. As a backstop against a collision or
// a glitch on the glass, the whole frame is sent again once a minute.
constexpr uint32_t FULL_FRAME_MS = 60000;
uint32_t g_rowHash[LCD_HEIGHT];
bool g_rowsKnown = false;  // false: what the panel shows is unknown, so send it all
uint8_t g_rowsRot = 0;     // the rotation those rows were sent under
uint32_t g_lastFull = 0;

// FNV-1a over 32-bit words (two pixels per step), each step folded down with
// a shift: a multiply only carries a difference upward, so without the fold
// changes in the high bits of two words could cancel (the agents critters
// did: make bench caught rows left stale).
uint32_t rowHash(const uint32_t *w, int n) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < n; i++) { h = (h ^ w[i]) * 16777619u; h ^= h >> 15; }
  return h;
}

void present(Display &lcd) {
  if (!g_sprite) return;  // drawn straight to the panel already
  const uint16_t *buf = (const uint16_t *)g_canvas.getBuffer();
  const uint32_t now = millis();
  const bool all = !g_rowsKnown || g_rowsRot != lcd.getRotation() || now - g_lastFull >= FULL_FRAME_MS;
  if (all) {
    g_rowsKnown = true;
    g_rowsRot = lcd.getRotation();
    g_lastFull = now;
  }
  lcd.startWrite();
  int run = -1;  // first row of the run of changed rows being collected
  for (int y = 0; y <= LCD_HEIGHT; y++) {
    bool changed = false;
    if (y < LCD_HEIGHT) {
      const uint32_t h = rowHash((const uint32_t *)(buf + y * LCD_WIDTH), LCD_WIDTH / 2);
      changed = all || h != g_rowHash[y];
      g_rowHash[y] = h;
    }
    if (changed && run < 0) {
      run = y;
    } else if (!changed && run >= 0) {
      // One window per run, sent by the same pushSprite as a whole frame (so
      // the same DMA choice) clipped to the run. A row is 48 us on the bus and
      // a window ~5 us, so a run is never stretched over an unchanged row.
      lcd.setClipRect(0, run, LCD_WIDTH, y - run);
      g_canvas.pushSprite(&lcd, 0, 0);
      run = -1;
    }
  }
  lcd.clearClipRect();
  lcd.endWrite();
#ifdef SIM_BENCH
  benchFrameSent(buf);
#endif
}

// --- text ----------------------------------------------------------------
// Anti-aliased VLW fonts (rasterised by tools/gen_fonts.py). The built-in
// Adafruit-GFX and Font0 faces are 1-bit and show stair-steps on this panel.
// Each font is parsed once in uiBegin; the glyph bitmaps stay in flash.
struct Vlw {
  lgfx::PointerWrapper data;
  lgfx::VLWfont font;
};
// The theme: Share Tech Mono (RM) for labels, Roboto Mono Bold (RB) for names,
// titles and values.
Vlw V_RM11, V_RM13, V_RB14, V_RB18, V_RB24, V_RB30, V_RB40;
// Inter: the navigation screens.
Vlw V_B30, V_B24, V_S12;
// Navigation strings can carry Vietnamese letters: separate fonts so the shared
// ones stay pixel-identical (accented glyphs raise a font's line height).
Vlw V_B18V, V_S12V;

void loadVlw(Vlw &v, const uint8_t *blob, size_t len) {
  v.data.set(blob, len);
  v.font.loadFont(&v.data);
}

// Tried largest first; the middle of a ring only has ~130x42px to give.
Vlw *const VALUE_FONTS[] = {&V_RB40, &V_RB30, &V_RB24, &V_RB18};

typedef Vlw *ValueFont;

// Largest font that keeps `text` inside `maxW` and `maxH`, or the smallest in
// the ladder if nothing does.
ValueFont fitFont(LovyanGFX *g, const char *text, int maxW, int maxH) {
  for (ValueFont v : VALUE_FONTS) {
    g->setFont(&v->font);
    if (g->textWidth(text) <= maxW && g->fontHeight() <= maxH) return v;
  }
  return VALUE_FONTS[sizeof(VALUE_FONTS) / sizeof(VALUE_FONTS[0]) - 1];
}

// The design sets the middle of every ring at one size, so the countdown must
// not resize from card to card as digits come and go. Fit the longest value
// in the deck and let the shorter ones share it.
ValueFont fitDeck(LovyanGFX *g, const Payload &p, int maxW, int maxH) {
  int widest = 0;
  const char *pick = "";
  g->setFont(&VALUE_FONTS[0]->font);
  for (uint8_t i = 0; i < p.nCards; i++) {
    if (p.cards[i].gauge < GAUGE_BLANK || p.cards[i].gauge2 >= GAUGE_BLANK) continue;  // text and dual cards size themselves
    const int w = g->textWidth(p.cards[i].value);
    if (w > widest) {
      widest = w;
      pick = p.cards[i].value;
    }
  }
  return fitFont(g, pick, maxW, maxH);
}

// Centres `text` in the given font.
void drawIn(LovyanGFX *g, ValueFont f, const char *text, int cx, int cy,
            uint16_t color) {
  g->setFont(&f->font);
  g->setTextColor(color, BG);
  g->setTextDatum(middle_center);
  g->drawString(text, cx, cy);
}

void drawFitted(LovyanGFX *g, const char *text, int cx, int cy, int maxW, int maxH,
                uint16_t color) {
  drawIn(g, fitFont(g, text, maxW, maxH), text, cx, cy, color);
}

// The small labels, lower-case like agents-deck's: the bridge's captions
// ("1 NEEDS YOU", "5H") and the firmware's own copy alike are case-mapped
// here, so every label reads the same. `bg` is what the text sits on (BG
// unless on a panel).
void lowerCopy(const char *src, char *dst, size_t cap) {
  size_t i = 0;
  for (; src[i] && i < cap - 1; i++) dst[i] = (char)tolower((unsigned char)src[i]);
  dst[i] = '\0';
}

void drawLabel(LovyanGFX *g, const char *text, int x, int y, uint16_t color,
               textdatum_t datum, uint16_t bg) {
  char buf[48];
  lowerCopy(text, buf, sizeof(buf));
  g->setFont(&V_RM13.font);
  g->setTextDatum(datum);
  g->setTextColor(color, bg);
  g->drawString(buf, x, y);
}

void drawLabel(LovyanGFX *g, const char *text, int x, int y, uint16_t color, textdatum_t datum) {
  drawLabel(g, text, x, y, color, datum, BG);
}

int labelWidth(LovyanGFX *g, const char *text) {
  char buf[48];
  lowerCopy(text, buf, sizeof(buf));
  g->setFont(&V_RM13.font);
  return g->textWidth(buf);
}

// --- chrome --------------------------------------------------------------
// Page dots: small squares.
void drawDots(LovyanGFX *g, uint8_t n, uint8_t active) {
  constexpr int Y = LCD_HEIGHT - 12;
  const int spacing = 11;
  const int total = (n - 1) * spacing;
  for (uint8_t i = 0; i < n; i++) {
    const int x = LCD_WIDTH / 2 - total / 2 + i * spacing;
    g->fillRect(x - 2, Y - 2, 4, 4, i == active ? INK : LINE);
  }
}

// Set by uiRender for the frame being drawn; drawTopBar reads it.
UiLink g_link = UiLink::None;

// Battery glyph + "NN%", top-left. Fixed in position: the glyph sits at `leftX`
// and the number starts at a constant offset from it, so neither moves when the
// percentage changes width.
// `onAccent`: drawn on the orange prompt screen, everything in black.
void drawBattery(LovyanGFX *g, int leftX, int y, const BatteryView &bat, bool onAccent) {
  const uint16_t col = onAccent         ? BG
                       : bat.pct >= 40 ? g_palette[ACC_GREEN]
                       : bat.pct >= 15 ? g_palette[ACC_AMBER]
                                       : ACCENT;
  const uint16_t frame = onAccent ? BG : DIM;
  constexpr int BODY_W = 14, BODY_H = 8, NUB_W = 2, GAP = 4;
  g->drawRect(leftX, y - BODY_H / 2, BODY_W, BODY_H, frame);
  g->fillRect(leftX + BODY_W, y - 2, NUB_W, 5, frame);
  const int inner = BODY_W - 4;
  int fill = (inner * bat.pct + 50) / 100;
  if (bat.pct > 0 && fill < 1) fill = 1;
  if (fill > 0) g->fillRect(leftX + 2, y - BODY_H / 2 + 2, fill, BODY_H - 4, col);

  char txt[8];
  snprintf(txt, sizeof(txt), "%u%%", (unsigned)bat.pct);
  g->setFont(&V_RM13.font);
  g->setTextDatum(middle_left);
  g->setTextColor(col, onAccent ? ACCENT : BG);
  const int textX = leftX + BODY_W + NUB_W + GAP;
  g->drawString(txt, textX, y);
}

// Battery top-left; freshness, and the transport marker beside it, top-right.
// `onAccent`: the orange prompt screen, all in black, the battery at `batX`.
void drawTopBar(LovyanGFX *g, bool online, uint32_t ageMs, const BatteryView &bat, bool onAccent = false,
                int batX = 36) {
  // The panel has rounded corners (~35px radius), so the bar is inset from the
  // edges; at the old 12px margin the text and status dot were clipped.
  constexpr int Y = 24;

  // Freshness beats a clock here: the board has no NTP sync in this sketch,
  // and what matters is whether the number on screen is current (age_label.h).
  char right[8];
  const uint32_t s = ageMs / 1000;
  ageLabel(right, sizeof(right), online, s);

  // Fresh: ink; stale or offline: the accent.
  const uint16_t bg = onAccent ? ACCENT : BG;
  const uint16_t col = onAccent ? BG : online && s < 30 ? INK : ACCENT;
  g->setFont(&V_RM13.font);
  g->setTextDatum(middle_right);
  g->setTextColor(col, bg);
  g->drawString(right, LCD_WIDTH - 46, Y);
  g->fillRect(LCD_WIDTH - 39, Y - 3, 6, 6, col);

  drawBattery(g, batX, Y, bat, onAccent);

  // Transport marker just left of the age readout, at a fixed place: the slot
  // is sized for the widest readout age_label.h can produce.
  int ageW = g->textWidth("59s");
  if (g->textWidth("99m") > ageW) ageW = g->textWidth("99m");
  if (g->textWidth("OFF") > ageW) ageW = g->textWidth("OFF");
  const int linkRight = LCD_WIDTH - 46 - ageW - 8;
  if (online && g_link != UiLink::None) {
    g->setFont(&V_RM13.font);
    g->setTextDatum(middle_right);
    g->setTextColor(onAccent ? BG : DIM, bg);
    g->drawString(g_link == UiLink::Ble ? "BLE" : "WIFI", linkRight, Y);
  }
}

// --- the ring ------------------------------------------------------------
// fillArc has no anti-aliasing, so the ring is rasterised here instead: every
// pixel in the annulus gets a coverage from its distance to the inner/outer
// radius and to the arc's end caps (distance along the arc ~ radius x angle),
// and is blended in 888 before the single conversion to 565.
//
// The geometry never changes, so the expensive part (sqrt, atan2 per pixel) is
// done once into a table of ~6k entries; a frame is then only a cheap pass over
// the table that writes straight into the sprite buffer.
struct RingPx {
  uint16_t idx;    // y * LCD_WIDTH + x
  uint16_t base;   // finished pixel (byte-swapped 565) when the fill does not reach it
  uint8_t cov;     // coverage of the track here (radial x end caps x notches), 0..255
  uint8_t fs;      // coverage of the fill's start cap, 0..255
  int16_t d;       // degrees clockwise from the arc start, x16
  uint8_t covNo;   // the same coverage with the notches left out, for rings without thresholds
};
constexpr uint32_t RING_TRACK = LINE_RGB;

// One precomputed ring: its radii and the pixels they cover.
struct RingTable {
  int rOut, rIn;
  RingPx *px;
  int n;
  float pxPerDeg() const { return (rOut + rIn) * 0.5f * 0.01745329252f; }  // at the band's middle
};
RingTable g_ringOuter = {RING_R_OUT, RING_R_IN, nullptr, 0};

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// `rgb` at coverage `scale` over the background, as byte-swapped 565.
uint16_t pack565(uint32_t rgb, float scale) {
  const uint32_t c = lerpRgb(BG_RGB, rgb, scale);
  return __builtin_bswap16(lgfx::color565((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF));
}

// The bare track at each coverage, for rings drawn without notches: their
// `base` cannot come from the table, which has the notches cut out of it.
uint16_t g_trackNo[256];

// Everything that does not depend on the current percentage or colour --
// coverage from the radii, end caps and notches -- is worked out once here.
void buildRingTable(RingTable &t) {
  constexpr float DEG = 0.01745329252f;
  const int rMax = t.rOut + 1;
  const int rMin = t.rIn - 1;
  for (int pass = 0; pass < 2; pass++) {
    int n = 0;
    for (int dy = -rMax; dy <= rMax; dy++) {
      for (int dx = -rMax; dx <= rMax; dx++) {
        const float r = sqrtf((float)(dx * dx + dy * dy));
        if (r > rMax || r < rMin) continue;
        const float covR = fminf(clamp01(t.rOut + 0.5f - r), clamp01(r - t.rIn + 0.5f));
        if (covR <= 0.0f) continue;
        // The gap sits at 270..360 degrees from the start; wrap just before
        // the start to a small negative number.
        float d = atan2f((float)dy, (float)dx) / DEG - ARC_START;
        while (d < -45.0f) d += 360.0f;
        while (d > 315.0f) d -= 360.0f;
        const float px = r * DEG;  // pixels per degree at this radius

        const float covPlain = covR * clamp01(fminf(d, ARC_SWEEP - d) * px + 0.5f);
        float cov = covPlain;
        // Threshold notches: a ~2px gap in the background colour.
        for (float f : NOTCHES) {
          cov *= clamp01(fabsf(d - ARC_SWEEP * f) * px - NOTCH_HALF_DEG * px + 0.5f);
        }
        // A pixel that only a notch removes still goes in the table: a ring
        // drawn without notches needs it. With notches its coverage is 0 and
        // it paints background, exactly as leaving it out did.
        if (covPlain <= 0.0f) continue;

        if (pass == 1) {
          RingPx &p = t.px[n];
          p.idx = (uint16_t)((RING_CY + dy) * LCD_WIDTH + RING_CX + dx);
          p.cov = (uint8_t)(cov * 255.0f + 0.5f);
          p.fs = (uint8_t)(clamp01(d * px + 0.5f) * 255.0f + 0.5f);
          p.d = (int16_t)lroundf(d * 16.0f);
          p.base = pack565(RING_TRACK, cov);
          p.covNo = (uint8_t)(covPlain * 255.0f + 0.5f);
        }
        n++;
      }
    }
    if (pass == 0) {
      t.px = (RingPx *)malloc(sizeof(RingPx) * n);
      if (!t.px) return;
      t.n = n;
    }
  }
}

void drawRing(LovyanGFX *g, const RingTable &ring, float pct, uint32_t rgb, bool filled,
              bool notches) {
  if (!ring.px) return;
  const float pxPerDeg = ring.pxPerDeg();
  const float fillEnd = filled ? ARC_SWEEP * (pct / 100.0f) : -10.0f;
  // Past this the fill (and its AA edge) cannot reach: use the cached pixel.
  const int past = (int)((fillEnd + 1.5f) * 16.0f);
  uint16_t *buf = g_sprite ? (uint16_t *)g_canvas.getBuffer() : nullptr;

  for (int i = 0; i < ring.n; i++) {
    const RingPx &p = ring.px[i];
    const uint8_t cov = notches ? p.cov : p.covNo;
    uint16_t c = notches ? p.base : g_trackNo[p.covNo];
    if (p.d <= past) {
      const float d = p.d * (1.0f / 16.0f);
      const float t = clamp01((fillEnd - d) * pxPerDeg + 0.5f) * (p.fs * (1.0f / 255.0f));
      const uint32_t col = lerpRgb(RING_TRACK, rgb, t);
      c = pack565(col, cov * (1.0f / 255.0f));
    }
    if (buf) buf[p.idx] = c;  // sprite memory is big-endian 565
    else g->drawPixel(p.idx % LCD_WIDTH, p.idx / LCD_WIDTH, __builtin_bswap16(c));
  }
}

// Advances one ring's sweep / retarget / colour crossfade to `now` and returns
// the arc to draw; `shownRgb` gets the colour to draw it in. Marks the frame as
// animating while either is still moving.
float stepGauge(GaugeAnim &a, float want, uint32_t rgb, uint32_t now, uint32_t &shownRgb) {
  if (!a.seen) {
    a.seen = true;
    a.from = 0.0f;
    a.to = want;
    a.start = now;
    a.dur = SWEEP_MS;
    a.colFrom = a.colTo = rgb;
    a.colStart = now - COLOR_MS;
  } else if (fabsf(want - a.to) > 0.01f) {
    a.from = a.shown;
    a.to = want;
    a.start = now;
    a.dur = RETARGET_MS;
  }

  const float p = progress(now, a.start, a.dur);
  a.shown = a.from + (a.to - a.from) * easeOut(p);

  const float cp = progress(now, a.colStart, COLOR_MS);
  shownRgb = lerpRgb(a.colFrom, a.colTo, easeOut(cp));
  if (rgb != a.colTo) {
    // Retarget the colour from wherever the crossfade currently is, the same
    // way the arc retargets, so a second threshold crossed mid-fade does not
    // snap back to the old hue first.
    a.colFrom = shownRgb;
    a.colTo = rgb;
    a.colStart = now;
  }
  if (p < 1.0f || cp < 1.0f) g_animating = true;
  return a.shown;
}

// --- the two card layouts ------------------------------------------------
// A gauge card: the ring is the number, the middle of the ring is the one
// thing a ring cannot show (time left), and the exact percentage drops to a
// single line in the gap at the bottom.
void drawGaugeCard(LovyanGFX *g, const Card &card, uint32_t rgb,
                   ValueFont valueFont, GaugeAnim &a, uint32_t now,
                   bool online, uint32_t ageMs, const BatteryView &bat) {
  const bool hasReading = card.gauge >= 0;

  uint32_t shownRgb;
  stepGauge(a, hasReading ? card.gauge : 0.0f, rgb, now, shownRgb);

  drawTopBar(g, online, ageMs, bat);
  drawRing(g, g_ringOuter, a.shown, shownRgb, hasReading, true);

  // Inside the ring, in the size the whole deck agreed on.
  drawIn(g, valueFont, card.value, RING_CX, VALUE_CY, hasReading ? INK : MUTED);
  if (card.sub1[0]) drawLabel(g, card.sub1, RING_CX, CAPTION_Y, DIM, top_center);

  // In the gap at the bottom: "14%  OF 5H LIMIT", laid out as one centred run
  // so the two type sizes stay optically joined.
  if (hasReading) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)lroundf(a.shown));
    g->setFont(&V_RB18.font);
    const int wPct = g->textWidth(pct);
    const int wLab = card.sub2[0] ? labelWidth(g, card.sub2) : 0;
    const int gap = wLab ? 6 : 0;
    const int x0 = RING_CX - (wPct + gap + wLab) / 2;

    g->setFont(&V_RB18.font);
    g->setTextDatum(middle_left);
    g->setTextColor(to565(shownRgb), BG);
    g->drawString(pct, x0, PCT_Y);
    if (wLab) drawLabel(g, card.sub2, x0 + wPct + gap, PCT_Y, DIM, middle_left);
  } else if (card.sub2[0]) {
    drawLabel(g, card.sub2, RING_CX, PCT_Y - 5, DIM, top_center);
  }

  drawLabel(g, card.title, RING_CX, NAME_Y, DIM, middle_center);
}

// The combined usage card: the two rate-limit windows as two tiles side by
// side, drawn like the Agents tiles (same width, same columns): each window's
// percentage large in the middle, its key above, its reset and the card's
// caption ("to reset") below, and the reading as a gauge round the tile's edge,
// cut at the 60/85 thresholds. Under them the unread-mail count, an envelope
// and the bridge's number. Gauges animate
// independently, like a gauge card's.
constexpr int USAGE_CAPTION_Y = 44;   // the card's name, where the Agents card has its caption
constexpr int USAGE_TILE_Y = 54;
constexpr int USAGE_TILE_W = 108;     // the Agents tiles' width and columns (agents_layout.h)
constexpr int USAGE_TILE_H = 156;
constexpr int USAGE_COL0_X = 9;
constexpr int USAGE_COL1_X = 123;
constexpr int USAGE_GAUGE_W = 4;      // twice a tile's context gauge: read across a desk
constexpr int USAGE_KEY_DY = 22;      // centres, from the tile's top
constexpr int USAGE_PCT_DY = 68;
constexpr int USAGE_PCT_MAX_W = USAGE_TILE_W - 2 * USAGE_GAUGE_W - 12;
constexpr int USAGE_PCT_MAX_H = 40;
constexpr int USAGE_RESET_DY = 112;
constexpr int USAGE_SUB_DY = 132;
constexpr int MAIL_CY = USAGE_TILE_Y + USAGE_TILE_H + 24;
constexpr uint32_t USAGE_TRACK_RGB = 0x2A2925;  // the tiles' gauge track (app.css --ring-track)
constexpr int USAGE_NOTCH = 2;                  // px cut at each threshold

void drawEnvelope(LovyanGFX *g, int x, int y, uint16_t col) {
  g->drawRect(x, y, 16, 11, col);
  g->drawLine(x + 2, y + 2, x + 8, y + 6, col);  // flap
  g->drawLine(x + 13, y + 2, x + 8, y + 6, col);
}

// Fills the stretch [a, b) of a `w`-wide band round the rect's edge, measured
// clockwise from the top centre in px: the top's right half, the right side,
// the bottom, the left side, the top's left half.
void fillEdge(LovyanGFX *g, int x, int y, int rw, int rh, int w, int a, int b, uint16_t col) {
  const int x2 = x + rw, y2 = y + rh, half = rw / 2;
  int s = 0;
  auto run = [&](int len, int dir) {  // dir: 0 right, 1 down, 2 left, 3 up, along the current side
    const int lo = a > s ? a - s : 0, hi = b - s < len ? b - s : len;
    if (hi > lo) {
      switch (dir) {
        case 0: g->fillRect(x + half + lo, y, hi - lo, w, col); break;
        case 1: g->fillRect(x2 - w, y + lo, w, hi - lo, col); break;
        case 2: g->fillRect(x2 - hi, y2 - w, hi - lo, w, col); break;
        case 3: g->fillRect(x, y2 - hi, w, hi - lo, col); break;
        case 4: g->fillRect(x + lo, y, hi - lo, w, col); break;
      }
    }
    s += len;
  };
  run(rw - half, 0);
  run(rh, 1);
  run(rw, 2);
  run(rh, 3);
  run(half, 4);
}

void drawUsageTile(LovyanGFX *g, const LegendRow *row, const char *sub, const char *why, int x, float shown, uint32_t rgb,
                   bool has) {
  const int y = USAGE_TILE_Y, cx = x + USAGE_TILE_W / 2;
  const uint16_t tile = to565(TILE_RGB);
  g->fillRect(x, y, USAGE_TILE_W, USAGE_TILE_H, tile);

  const int len = 2 * (USAGE_TILE_W + USAGE_TILE_H);
  fillEdge(g, x, y, USAGE_TILE_W, USAGE_TILE_H, USAGE_GAUGE_W, 0, len, to565(USAGE_TRACK_RGB));
  if (has) {
    const float pct = shown < 0 ? 0 : shown > 100 ? 100 : shown;
    fillEdge(g, x, y, USAGE_TILE_W, USAGE_TILE_H, USAGE_GAUGE_W, 0, (int)(len * pct / 100.0f + 0.5f), to565(rgb));
  }
  for (float f : NOTCHES) {
    const int at = (int)(len * f + 0.5f);
    fillEdge(g, x, y, USAGE_TILE_W, USAGE_TILE_H, USAGE_GAUGE_W, at - USAGE_NOTCH / 2, at + USAGE_NOTCH / 2, BG);
  }

  if (!row) return;
  drawLabel(g, row->key, cx, y + USAGE_KEY_DY, DIM, middle_center, tile);
  const ValueFont vf = fitFont(g, row->pct, USAGE_PCT_MAX_W, USAGE_PCT_MAX_H);
  g->setFont(&vf->font);
  g->setTextDatum(middle_center);
  g->setTextColor(has ? to565(rgb) : MUTED, tile);
  g->drawString(row->pct, cx, y + USAGE_PCT_DY);
  if (row->reset[0]) {
    g->setFont(&V_RB18.font);
    g->setTextColor(has ? INK : MUTED, tile);
    g->drawString(row->reset, cx, y + USAGE_RESET_DY);
    if (sub[0]) drawLabel(g, sub, cx, y + USAGE_SUB_DY, DIM, middle_center, tile);
  } else {  // no reading: the card's reason ("no data", "401") in the reset's place
    if (sub[0]) drawLabel(g, sub, cx, y + USAGE_RESET_DY, DIM, middle_center, tile);
    if (why[0]) drawLabel(g, why, cx, y + USAGE_SUB_DY, DIM, middle_center, tile);
  }
}

void drawDualCard(LovyanGFX *g, const Card &card, GaugeAnim &outer, GaugeAnim &inner,
                  uint32_t now, bool online, uint32_t ageMs, const BatteryView &bat) {
  const bool has1 = card.gauge >= 0;
  const bool has2 = card.gauge2 >= 0;
  uint32_t rgb1, rgb2;
  stepGauge(outer, has1 ? card.gauge : 0.0f, RGB888[card.color % 7], now, rgb1);
  stepGauge(inner, has2 ? card.gauge2 : 0.0f, RGB888[card.color2 % 7], now, rgb2);

  drawTopBar(g, online, ageMs, bat);
  if (card.title[0]) drawLabel(g, card.title, LCD_WIDTH / 2, USAGE_CAPTION_Y, DIM, middle_center);
  drawUsageTile(g, card.nRows > 0 ? &card.rows[0] : nullptr, card.sub1, card.sub2, USAGE_COL0_X, outer.shown, rgb1, has1);
  drawUsageTile(g, card.nRows > 1 ? &card.rows[1] : nullptr, card.sub1, card.sub2, USAGE_COL1_X, inner.shown, rgb2, has2);

  if (card.mail[0]) {
    const uint16_t col = to565(RGB888[card.mailColor % 7]);
    g->setFont(&V_RB18.font);
    const int run = 16 + 6 + g->textWidth(card.mail);
    const int x0 = LCD_WIDTH / 2 - run / 2;
    drawEnvelope(g, x0, MAIL_CY - 5, col);
    g->setTextDatum(middle_left);
    g->setTextColor(col, BG);
    g->drawString(card.mail, x0 + 22, MAIL_CY);
  }
}

// A text card: the original layout, still used by the optional spend deck,
// which has no percentage of anything to draw a ring from.
void drawTextCard(LovyanGFX *g, const Payload &p, const Card &card, uint16_t color,
                  bool online, uint32_t ageMs, const BatteryView &bat) {
  (void)p;
  drawTopBar(g, online, ageMs, bat);

  drawLabel(g, card.title, LCD_WIDTH / 2, 60, DIM, top_center);
  drawFitted(g, card.value, LCD_WIDTH / 2, 145, LCD_WIDTH - 20, 96, color);

  g->setFont(&V_RB14.font);
  g->setTextDatum(top_center);
  g->setTextColor(INK, BG);
  g->drawString(card.sub1, LCD_WIDTH / 2, 202);

  drawLabel(g, card.sub2, LCD_WIDTH / 2, 230, DIM, top_center);
}

// --- the waiting card -------------------------------------------------------
// Stands in for the payload cards until the first payload arrives (bridge down,
// no Mac yet), so the deck is never empty.
void drawWaitingCard(LovyanGFX *g, bool online, uint32_t ageMs, const BatteryView &bat) {
  drawTopBar(g, online, ageMs, bat);
  g->setFont(&V_RB24.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, BG);
  g->drawString("no data yet", LCD_WIDTH / 2, LCD_HEIGHT / 2 - 10);
  drawLabel(g, "waiting for the bridge", LCD_WIDTH / 2, LCD_HEIGHT / 2 + 22, DIM, middle_center);
}

// --- the Volume card ---------------------------------------------------------
// Minimal: the output's name, the level as a large number and, when it is not
// simply live, one status word, in a left column; a slim bar on the right that
// fills from the bottom; under both a full-width row of the previous /
// play-pause / next media keys. A tap on the number mutes.
// volume_slider.h owns the geometry, shared with the hit-testing.
constexpr uint32_t VOL_FILL = 0xE8E4DC;  // live: ink, the one bright thing on the card
constexpr uint32_t VOL_OFF = MUTED_RGB;  // muted, or an output that sets its own level (HDMI)
constexpr int VOL_COL_X = 24;            // the left column
constexpr int VOL_TEXT_MAX_W = VOL_BAR_X - VOL_COL_X - 16;
constexpr int VOL_NAME_Y = 62;
constexpr int VOL_NUM_Y = 134;  // baseline
constexpr int VOL_WORD_Y = 162;

GaugeAnim g_volAnim;

// `text` cut with "..." until it fits VOL_TEXT_MAX_W (the font must be set).
void volumeFit(LovyanGFX *g, char *text, size_t cap) {
  if (g->textWidth(text) <= VOL_TEXT_MAX_W) return;
  size_t n = strlen(text);
  char cut[40] = "...";
  while (n > 0) {
    text[--n] = '\0';
    while (n > 0 && text[n - 1] == ' ') text[--n] = '\0';
    snprintf(cut, sizeof(cut), "%s...", text);
    if (g->textWidth(cut) <= VOL_TEXT_MAX_W) break;
  }
  snprintf(text, cap, "%s", cut);
}

// The name is printable ASCII: volumeParse (volume_frame.h) rejects anything else.
// The caption: the output's name in lower case ("mac volume" when there is
// none), on one line or wrapped at a space onto two, the second cut with "..."
// if it still does not fit beside the bar.
void volumeLabel(LovyanGFX *g, const char *name, char *l1, char *l2, size_t cap) {
  size_t n = 0;
  for (const char *c = name; *c && n + 1 < cap; c++) l1[n++] = (char)tolower((unsigned char)*c);
  l1[n] = '\0';
  l2[0] = '\0';
  if (!n) snprintf(l1, cap, "mac volume");
  g->setFont(&V_RM13.font);
  if (g->textWidth(l1) <= VOL_TEXT_MAX_W) return;
  // The last space whose prefix fits ends the first line.
  for (size_t i = strlen(l1); i > 0; i--) {
    if (l1[i - 1] != ' ') continue;
    char head[40];
    snprintf(head, sizeof(head), "%.*s", (int)(i - 1), l1);
    if (g->textWidth(head) > VOL_TEXT_MAX_W) continue;
    snprintf(l2, cap, "%s", l1 + i);
    snprintf(l1, cap, "%s", head);
    volumeFit(g, l2, cap);
    return;
  }
  volumeFit(g, l1, cap);  // one long word
}

// Antialiased glyph pieces: LovyanGFX's wedge lines are the only smooth
// primitive besides circles and round rects, so the icons are built from them.

// A filled triangle with softened corners: the fill, then a thin stroke round its edges.
void smoothTriangle(LovyanGFX *g, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col) {
  g->fillTriangle(x0, y0, x1, y1, x2, y2, col);
  g->drawWideLine(x0, y0, x1, y1, 0.9f, col);
  g->drawWideLine(x1, y1, x2, y2, 0.9f, col);
  g->drawWideLine(x2, y2, x0, y0, 0.9f, col);
}

// Previous / next: a triangle and a bar, mirrored. `dir` 1 points right.
void drawSkip(LovyanGFX *g, int cx, int cy, int dir, uint16_t col) {
  const int tip = cx + 7 * dir, base = cx - 7 * dir;
  smoothTriangle(g, base, cy - 10, base, cy + 10, tip, cy, col);
  const int bar = dir > 0 ? cx + 8 : cx - 12;
  g->fillSmoothRoundRect(bar, cy - 10, 4, 21, 2, col);
}

// Play when paused, pause when playing, both in one when the Mac has not said.
void drawPlayPause(LovyanGFX *g, int cx, int cy, int8_t playing, uint16_t col) {
  if (playing == 1) {
    g->fillSmoothRoundRect(cx - 8, cy - 10, 6, 21, 2, col);
    g->fillSmoothRoundRect(cx + 2, cy - 10, 6, 21, 2, col);
  } else if (playing == 0) {
    smoothTriangle(g, cx - 6, cy - 11, cx - 6, cy + 11, cx + 11, cy, col);  // nudged right: optical centre
  } else {
    smoothTriangle(g, cx - 12, cy - 9, cx - 12, cy + 9, cx + 1, cy, col);
    g->fillSmoothRoundRect(cx + 4, cy - 9, 4, 19, 2, col);
    g->fillSmoothRoundRect(cx + 10, cy - 9, 4, 19, 2, col);
  }
}

// The button row: previous, play/pause (on a square: the main action) and next,
// one per equal zone. A pressed button lights its square for VOLUME_PRESS_MS.
void drawVolumeRow(LovyanGFX *g, const VolumeView &v) {
  constexpr int DISC_R = 27;  // half the square's side
  const uint16_t ink = v.state != VolState::NoMac ? INK : MUTED;
  const int8_t p = v.pressed;
  if (p == (int8_t)MediaKey::Previous) g->fillRect(VOL_PREV_CX - DISC_R, VOL_ROW_Y - DISC_R, 2 * DISC_R, 2 * DISC_R, LINE);
  if (p == (int8_t)MediaKey::Next) g->fillRect(VOL_NEXT_CX - DISC_R, VOL_ROW_Y - DISC_R, 2 * DISC_R, 2 * DISC_R, LINE);
  g->fillRect(VOL_PLAY_CX - DISC_R, VOL_ROW_Y - DISC_R, 2 * DISC_R, 2 * DISC_R,
              p == (int8_t)MediaKey::PlayPause ? MUTED : LINE);
  drawSkip(g, VOL_PREV_CX, VOL_ROW_Y, -1, ink);
  drawPlayPause(g, VOL_PLAY_CX, VOL_ROW_Y, v.playing, ink);
  drawSkip(g, VOL_NEXT_CX, VOL_ROW_Y, 1, ink);
}

void drawVolumeCard(LovyanGFX *g, const VolumeView &v, bool online, uint32_t ageMs, const BatteryView &bat) {
  const uint32_t now = millis();
  const bool live = v.state == VolState::Live, fixed = v.state == VolState::Fixed;
  const uint32_t rgb = fixed || v.muted ? VOL_OFF : VOL_FILL;
  GaugeAnim &a = g_volAnim;
  if (v.tracking) {
    // Under the finger: no easing, the fill is where the finger is.
    if (!a.seen) {
      a.seen = true;
      a.colFrom = a.colTo = rgb;
      a.colStart = now - COLOR_MS;
    }
    a.from = a.to = a.shown = v.level;
    a.start = now;
    a.dur = 0;
    g_animating = true;
  }
  uint32_t shownRgb = rgb;
  const float shown = stepGauge(a, v.level, rgb, now, shownRgb);
  const int edge = volumeFillTop(shown);
  const int bottom = VOL_BAR_Y + VOL_BAR_H;

  // A square bar (volume_slider.h's VOL_BAR_R is no longer drawn): the track, then
  // the fill from the bottom up to the edge.
  g->fillRect(VOL_BAR_X, VOL_BAR_Y, VOL_BAR_W, VOL_BAR_H, LINE);
  if (edge < bottom) g->fillRect(VOL_BAR_X, edge, VOL_BAR_W, bottom - edge, to565(shownRgb));

  char l1[32], l2[32];
  volumeLabel(g, v.name, l1, l2, sizeof(l1));
  g->setFont(&V_RM13.font);
  g->setTextDatum(baseline_left);
  g->setTextColor(DIM, BG);
  g->drawString(l1, VOL_COL_X, VOL_NAME_Y);
  if (*l2) g->drawString(l2, VOL_COL_X, VOL_NAME_Y + 16);

  char num[8];
  if (live) snprintf(num, sizeof(num), "%u", (unsigned)v.level);
  else snprintf(num, sizeof(num), "--");
  g->setFont(&V_RB40.font);
  g->setTextColor(live && !v.muted ? INK : MUTED, BG);
  g->drawString(num, VOL_COL_X, VOL_NUM_Y);
  if (live) {
    const int w = g->textWidth(num);
    g->setFont(&V_RB18.font);
    g->setTextColor(v.muted ? MUTED : DIM, BG);
    g->drawString("%", VOL_COL_X + w + 2, VOL_NUM_Y);
  }

  const char *word = v.state == VolState::NoMac      ? "NO MAC"
                     : v.state == VolState::NoOutput ? "NO OUTPUT"
                     : fixed                         ? "FIXED"
                     : v.muted                       ? "MUTED"
                                                     : "";
  if (*word) drawLabel(g, word, VOL_COL_X, VOL_WORD_Y, DIM, baseline_left);

  drawVolumeRow(g, v);
  drawTopBar(g, online, ageMs, bat);
}

// --- the settings sheets -----------------------------------------------------
// The display panel drops from the top as a sheet: a lifted, square-cornered
// panel with a grab handle at its bottom edge, an icon + title,
// hairline-divided rows of  LABEL over value ... [ - ] [ + ],  and a RESET / DONE
// bar. Geometry comes from editor_layout.cpp so drawing and hit-testing agree.
constexpr int STEP_R = 17;      // stepper circle radius, centred in its touch zone
constexpr int ROW_PAD = 14;     // side inset of the labels and dividers
constexpr int SW_W = 36;        // a row's on/off switch, right-aligned in the row's lead
constexpr int SW_H = 20;
constexpr int SW_GAP = 10;      // between the switch and the minus button

// What one row shows. `meterPct` -1 = no meter; `sw` -1 = no switch, else off/on.
// A stepper that would not change anything (at a limit, or the row is off) is
// drawn disabled.
struct SheetRow {
  const char *label;
  const char *value;
  int meterPct;
  int sw;
  bool canDec, canInc;
};

void drawStepper(LovyanGFX *g, const EditRect &zone, int yOff, bool plus, bool enabled) {
  const int cx = zone.x + zone.w / 2;
  const int cy = zone.y + zone.h / 2 + yOff;
  g->fillRect(cx - STEP_R, cy - STEP_R, 2 * STEP_R + 1, 2 * STEP_R + 1, LINE);
  const uint16_t ink = enabled ? INK : MUTED;
  g->fillRect(cx - 6, cy - 1, 13, 3, ink);
  if (plus) g->fillRect(cx - 1, cy - 6, 3, 13, ink);
}

void drawSwitch(LovyanGFX *g, int x, int cy, bool on, uint16_t accent) {
  constexpr int KNOB = SW_H - 6;
  g->fillRect(x, cy - SW_H / 2, SW_W, SW_H, on ? accent : LINE);
  const int kx = on ? x + SW_W - 3 - KNOB : x + 3;
  g->fillRect(kx, cy - KNOB / 2, KNOB, KNOB, on ? INK : DIM);
}

// "15 min" as a bold number and a small unit on one baseline; a value with no
// space ("63%", "off") is all bold.
void drawSheetValue(LovyanGFX *g, const char *value, int x, int baseline, uint16_t color) {
  char num[16];
  size_t n = 0;
  for (; value[n] && value[n] != ' ' && n < sizeof(num) - 1; n++) num[n] = value[n];
  num[n] = '\0';
  g->setFont(&V_RB18.font);
  g->setTextDatum(baseline_left);
  g->setTextColor(color, PANEL);
  g->drawString(num, x, baseline);
  if (value[n] == ' ') {
    const int w = g->textWidth(num);
    g->setFont(&V_RM13.font);
    g->setTextColor(DIM, PANEL);
    g->drawString(value + n + 1, x + w + 3, baseline);
  }
}

// One row: the label over its value (and a meter or switch beside them) on the
// left, the round - and + buttons on the right, and a hairline above (not on
// the first row).
void drawSheetRow(LovyanGFX *g, int r, const SheetRow &row, uint16_t accent, int yOff) {
  const EditRect box = editorRow(r);
  const int top = box.y + yOff;
  const int cy = top + box.h / 2;
  if (r > 0) g->drawFastHLine(ROW_PAD, top, LCD_WIDTH - 2 * ROW_PAD, LINE);
  drawLabel(g, row.label, ROW_PAD, cy - 5, DIM, baseline_left, PANEL);
  const bool on = row.sw != 0;
  drawSheetValue(g, row.value, ROW_PAD, cy + 14, on ? INK : DIM);
  if (row.meterPct >= 0) {
    constexpr int MX = ROW_PAD + 54, MW = 56;  // clear of "100%"
    g->fillRect(MX, cy + 7, MW, 4, LINE);
    const int fill = MW * row.meterPct / 100;
    if (fill > 0) g->fillRect(MX, cy + 7, fill, 4, accent);
  }
  if (row.sw >= 0) {
    const EditRect lead = editorLead(r);
    drawSwitch(g, lead.x + lead.w - SW_GAP - SW_W, cy, on, accent);
  }
  drawStepper(g, editorMinus(r), yOff, false, row.canDec);
  drawStepper(g, editorPlus(r), yOff, true, row.canInc);
}

void drawSheetBar(LovyanGFX *g, uint16_t accent, int yOff) {
  constexpr int INSET = 4;  // the pills sit inside their 44 px touch zones
  const EditRect reset = editorResetBtn();
  const EditRect done = editorDoneBtn();
  const int h = reset.h - 2 * INSET;
  g->fillRect(reset.x, reset.y + INSET + yOff, reset.w, h, LINE);
  g->fillRect(done.x, done.y + INSET + yOff, done.w, h, accent);
  g->setFont(&V_RB18.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, LINE);
  g->drawString("reset", reset.x + reset.w / 2, reset.y + reset.h / 2 + yOff);
  g->setTextColor(BG, accent);
  g->drawString("done", done.x + done.w / 2, done.y + done.h / 2 + yOff);
}

// The sheet body, `yOff` px from its resting place so it can slide (negative:
// still partly above the screen). Square: at rest the sheet is the whole screen
// and the glass supplies the corners; the bottom hairline is only drawn while
// sliding.
void drawSheetFrame(LovyanGFX *g, int yOff) {
  g->fillRect(0, yOff, LCD_WIDTH, LCD_HEIGHT, PANEL);
  if (yOff) g->drawFastHLine(0, yOff + LCD_HEIGHT - 1, LCD_WIDTH, LINE);
  g->fillRect(LCD_WIDTH / 2 - 16, yOff + LCD_HEIGHT - 8, 32, 4, LINE);
}

// The title with a sun icon in front.
void drawSheetTitle(LovyanGFX *g, const char *title, uint16_t accent, int yOff) {
  const int cy = 22 + yOff;
  constexpr int IX = ROW_PAD + 7;
  g->fillCircle(IX, cy, 4, accent);
  for (int i = 0; i < 8; i++) {
    const float a = i * 0.7853982f;
    g->drawLine(IX + (int)lroundf(cosf(a) * 6.5f), cy + (int)lroundf(sinf(a) * 6.5f),
                IX + (int)lroundf(cosf(a) * 8.5f), cy + (int)lroundf(sinf(a) * 8.5f), accent);
  }
  g->setFont(&V_RB18.font);
  g->setTextDatum(middle_left);
  g->setTextColor(accent, PANEL);
  char buf[16];
  lowerCopy(title, buf, sizeof(buf));
  g->drawString(buf, IX + 16, cy);
}

void drawDevicePanel(LovyanGFX *g, const DeviceSettings &s, int yOff) {
  drawSheetFrame(g, yOff);
  drawSheetTitle(g, "display", ACC_DEV, yOff + 4);
  for (int r = 0; r < DEV_EDIT_ROWS; r++) {
    char buf[12];
    devEditorValue(r, s, buf, sizeof(buf));
    lowerCopy(buf, buf, sizeof(buf));  // "OFF", "MED": lower-case like every label
    DeviceSettings dec = s, inc = s;
    devEditorApply(dec, EditHit{EditAction::Dec, (uint8_t)r}, s, s);
    devEditorApply(inc, EditHit{EditAction::Inc, (uint8_t)r}, s, s);
    const int sw = devEditorHasSwitch(r) ? (int)devEditorRowOn(r, s) : -1;
    drawSheetRow(g, r,
                 SheetRow{devEditorLabel(r), buf, r == 0 ? (int)s.backlight * 100 / 255 : -1, sw, dec != s, inc != s},
                 ACC_DEV, yOff);
  }
  drawSheetBar(g, ACC_DEV, yOff);
}

// Scanline-dims the part of the card still showing beside a sliding sheet, more
// the further the sheet has come. Every other row, or every fourth early on.
void dimCard(LovyanGFX *g, int y0, int y1, float pos) {
  if (pos < 0.25f) return;
  const int stride = pos < 0.7f ? 4 : 2;
  for (int y = y0 - (y0 % stride); y < y1; y += stride)
    if (y >= y0) g->drawFastHLine(0, y, LCD_WIDTH, BG);
}

// Slide of the display panel over the card: 0 = hidden above the screen,
// 1 = fully open. Ease-out coming in, ease-in going back.
constexpr uint32_t EDITOR_SLIDE_MS = 260;
bool g_edSliding = false;
bool g_edOpening = false;
DeviceSettings g_edDevice{};
uint32_t g_edStart = 0;

// Position in 0..1 of the panel now, ending the slide when it is over.
float editorSlidePos(uint32_t now) {
  float t = (float)(now - g_edStart) / (float)EDITOR_SLIDE_MS;
  if (t >= 1.0f) {
    g_edSliding = false;
    return g_edOpening ? 1.0f : 0.0f;
  }
  const float u = 1.0f - t;
  const float out = 1.0f - u * u * u;  // ease-out cubic
  const float in = t * t * t;          // ease-in cubic
  return g_edOpening ? out : 1.0f - in;
}

// --- the Agents card -----------------------------------------------------------
// agents-deck's board (docs/images/panel-board-v2.png, the web app's panel
// view: backend/internal/web/static/app.css): a two-column grid of square
// panels that scrolls under the card's caption, one per agent: the Claude
// chip, a mini critter with the subagent count when subagents work, a pixel
// critter whose pose follows what it is doing (critter.h) in its own hat,
// the folder name, the bridge's meta line, and a context gauge round the
// tile's edge, filled clockwise from the top centre. A waiting tile is solid
// orange, so it reads across the desk; a hungry one (a finished turn nobody
// looked at) slides orange stripes. agents_layout.h owns the geometry, which
// main.cpp hit-tests with too.
constexpr uint32_t ACCENT_RGB = 0xFF4D00;
constexpr uint32_t INK_RGB = 0xE8E4DC;
constexpr uint32_t CHIP_RGB = 0xC98B6B;     // the Claude chip (agents-deck's chip_color)
constexpr uint32_t RING_TRACK_RGB = 0x2A2925;  // the gauge's track (app.css --ring-track)
constexpr uint8_t WAIT_TRACK_ALPHA = 64;    // on orange: black at 25%
constexpr int RING_W = 2;                   // the gauge's stroke
constexpr int TILE_CELL = 3;                // px per critter cell on a tile: the body is 42 px, ~40% of the tile
constexpr int TILE_CRIT_X = 7;              // the body (cells 0..14) is centred: cell 0 at mid - 7 cells
constexpr int TILE_CRIT_Y = 24;             // cell 0 this far below the tile's top (hats reach -6.5)
constexpr int TILE_NAME_Y = 68;             // centres of the name, the meta line
constexpr int TILE_META_Y = 84;
constexpr int TILE_TEXT_PAD = 4;           // 100 px: the bridge's 16-character line at 6 px a letter
constexpr int CHIP = 6, CHIP_INSET = 6;
constexpr int SUBS_X = 6, SUBS_Y = 7;       // the mini critter's cell (0, 0), 1 px a cell
constexpr int SUBS_W = 14, SUBS_TOP = -1, SUBS_BOTTOM = 9;  // its body only: cells 0..14, rows -1..9
constexpr int STRIPE_PITCH = 16, STRIPE_W = 7;  // 5 px across the diagonal, as agents-deck draws them
constexpr int SCROLL_BAR_W = 3;

// `fg` at `alpha` (0..255) over `bg`, both 0xRRGGBB, as 565.
uint16_t blend565(uint32_t fg, uint32_t bg, uint8_t alpha) {
  return to565(lerpRgb(bg, fg, alpha * (1.0f / 255.0f)));
}

// The critter's colours, as agents-deck's critter_of picks them.
struct CritterInks {
  uint32_t body, cut;  // cut: the background behind it
};

// The critter for `pose` in `hat` at `ms` with its cell (0, 0) at (ox, oy),
// `cell` px a cell. Every shape is blended at its alpha over the background
// (not over the shapes under it: the sprite has no overlapping fades). Edges
// are rounded to whole pixels, and every shape keeps at least one.
void drawCritter(LovyanGFX *g, Pose pose, HatId hat, uint32_t ms, float ox, float oy, int cell,
                 const CritterInks &k) {
  static CritterRect r[CRITTER_MAX_RECTS];  // 2.5 KB: off the loop's stack
  const size_t n = critterShapes(pose, hat, ms, r, CRITTER_MAX_RECTS);
  for (size_t i = 0; i < n; i++) {
    uint32_t rgb;
    switch (r[i].ink) {
      case CritterInk::Body: rgb = k.body; break;
      case CritterInk::Cut: rgb = k.cut; break;
      case CritterInk::Flag: rgb = INK_RGB; break;
      case CritterInk::Accent: rgb = ACCENT_RGB; break;
      case CritterInk::HatMain: rgb = hatMainRgb(hat.color); break;
      case CritterInk::HatShade: rgb = hatShadeRgb(hat.color); break;
      case CritterInk::HatHighlight: rgb = hatHighlightRgb(); break;
      default: rgb = hatDarkRgb(); break;
    }
    // Size rounded once, apart from the position: a shape moved by half a
    // cell (the bob) then shifts whole, instead of growing or shrinking by a
    // pixel from frame to frame, which reads as blur on the panel.
    const int x0 = (int)lroundf(ox + r[i].x * cell), y0 = (int)lroundf(oy + r[i].y * cell);
    int w = (int)lroundf(r[i].w * cell), h = (int)lroundf(r[i].h * cell);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    g->fillRect(x0, y0, w, h, r[i].alpha == 255 ? to565(rgb) : blend565(rgb, k.cut, r[i].alpha));
  }
}

// --- wrapping ------------------------------------------------------------------
constexpr size_t LINE_CAP = 64;  // more than any line 200 px can hold

// True where byte `i` of `s` continues a UTF-8 character, so a cut there would
// split it (the tile line's separator and the subagent sign are two bytes).
bool midChar(const char *s, size_t i) { return ((uint8_t)s[i] & 0xC0) == 0x80; }

// `text` cut until it and a trailing "..." fit `maxW` in the font already set.
void ellipsize(LovyanGFX *g, const char *text, int maxW, char *out) {
  size_t k = strlen(text);
  if (k > LINE_CAP - 4) k = LINE_CAP - 4;
  for (;; k--) {
    if (k > 0 && midChar(text, k)) continue;
    size_t e = k;
    while (e > 0 && text[e - 1] == ' ') e--;
    memcpy(out, text, e);
    memcpy(out + e, "...", 4);
    if (k == 0 || g->textWidth(out) <= maxW) return;
  }
}

// Up to `maxLines` lines of `text`, each no wider than `maxW` in the font
// already set, broken at spaces (a word wider than a line is cut where it must
// be). When text remains after the last line, that line ends in "..." instead.
// Returns the line count. uiPromptMetrics counts with this same function, so
// what is drawn and what main.cpp hit-tests cannot disagree.
uint8_t wrapText(LovyanGFX *g, const char *text, int maxW, uint8_t maxLines, char (*out)[LINE_CAP]) {
  uint8_t n = 0;
  const char *p = text;
  char buf[LINE_CAP];
  while (n < maxLines) {
    while (*p == ' ') p++;
    if (!*p) break;
    // The longest run from p that fits.
    const size_t len = strlen(p);
    size_t fit = 0;
    for (size_t k = 1; k <= len && k < LINE_CAP; k++) {
      if (k < len && midChar(p, k)) continue;
      memcpy(buf, p, k);
      buf[k] = '\0';
      if (g->textWidth(buf) > maxW) break;
      fit = k;
    }
    if (fit == len) {
      memcpy(out[n++], p, len + 1);
      break;
    }
    if (n + 1 == maxLines) {
      ellipsize(g, p, maxW, out[n++]);
      break;
    }
    if (fit == 0) {  // not even one letter fits: never stall
      fit = 1;
      while (fit < len && midChar(p, fit)) fit++;
    }
    // End the line at the last space that fits, else cut the word.
    size_t cut = fit;
    if (p[fit] != ' ') {
      size_t s = fit;
      while (s > 0 && p[s] != ' ') s--;
      if (s > 0) cut = s;
    }
    size_t e = cut;
    while (e > 0 && p[e - 1] == ' ') e--;
    memcpy(out[n], p, e);
    out[n++][e] = '\0';
    p += cut;
  }
  return n;
}

// One line of `text` in `font`, cut with "..." to `maxW`, drawn at (x, y).
void drawLine(LovyanGFX *g, Vlw &font, const char *text, int x, int y, int maxW, uint16_t fg, uint16_t bg,
              textdatum_t datum) {
  char line[1][LINE_CAP];
  g->setFont(&font.font);
  if (!wrapText(g, text, maxW, 1, line)) return;
  g->setTextDatum(datum);
  g->setTextColor(fg, bg);
  g->drawString(line[0], x, y);
}

// A thin thumb at the right edge when the content is taller than its view.
void drawScrollBar(LovyanGFX *g, int x, int viewY, int viewH, int contentH, int scroll, uint16_t color) {
  if (contentH <= viewH) return;
  const int maxOff = contentH - viewH;
  int h = viewH * viewH / contentH;
  if (h < 16) h = 16;
  const int off = scroll < 0 ? 0 : scroll > maxOff ? maxOff : scroll;
  g->fillRect(x, viewY + (viewH - h) * off / maxOff, SCROLL_BAR_W, h, color);
}

// --- the grid --------------------------------------------------------------------
// Sliding diagonal stripes (agents-deck's draw_stripes_event): 5 px accent
// lines at 20% from bottom-left to top-right every 16 px, moving one pixel
// every 60 ms. Drawn as runs along each row, inside the tile's outline.
void drawStripes(LovyanGFX *g, const LayoutRect &r, uint32_t now) {
  const uint16_t c = blend565(ACCENT_RGB, TILE_RGB, 51);
  const int shift = (int)((now / 60) % STRIPE_PITCH);
  const int y2 = r.y + r.h - 1;
  for (int y = r.y + 1; y < y2; y++) {
    // A pixel is on a line where (x - x1) + (y - y2) - shift, mod the pitch,
    // falls in the line's width: the runs start every pitch along the row.
    int start = shift - (y - y2);  // x - x1 where the phase is 0
    start = ((start % STRIPE_PITCH) + STRIPE_PITCH) % STRIPE_PITCH - STRIPE_PITCH;
    for (int x = start; x < r.w - 1; x += STRIPE_PITCH) {
      int a = x, b = x + STRIPE_W;
      if (a < 1) a = 1;
      if (b > r.w - 1) b = r.w - 1;
      if (b > a) g->drawFastHLine(r.x + a, y, b - a, c);
    }
  }
}

// A running tile's ground (agents_layout.h): the dot matrix with its wave,
// then the rising specks, all accent over the tile colour.
void drawRunGround(LovyanGFX *g, const LayoutRect &r, uint32_t now) {
  for (int16_t y = RUN_DOT_INSET; y < r.h - RUN_DOT_INSET; y += RUN_DOT_PITCH)
    for (int16_t x = RUN_DOT_INSET; x < r.w - RUN_DOT_INSET; x += RUN_DOT_PITCH)
      g->drawPixel(r.x + x, r.y + y, blend565(ACCENT_RGB, TILE_RGB, runDotAlpha(x, y, r.w, r.h, now)));
  for (uint8_t i = 0; i < RUN_PARTICLES; i++) {
    const RunParticle p = runParticle(i, r.w, r.h, now);
    if (p.alpha)
      g->fillRect(r.x + p.x, r.y + p.y, RUN_PARTICLE_SIZE, RUN_PARTICLE_SIZE, blend565(ACCENT_RGB, TILE_RGB, p.alpha));
  }
}

bool tileHungry(const AgentTile &t) {
  return t.kind == AgentKind::Idle && (t.unseen || poseFromName(t.pose) == Pose::Hungry);
}

// The context gauge (app.js makeRing): a RING_W band round the tile's edge,
// the track all the way round and `pct` of it filled clockwise from the top
// centre: the top's right half, the right side, the bottom, the left side, the
// top's left half.
void drawRing(LovyanGFX *g, const LayoutRect &r, uint8_t pct, uint16_t track, uint16_t fill) {
  const int x2 = r.x + r.w, y2 = r.y + r.h, half = r.w / 2;
  g->fillRect(r.x, r.y, r.w, RING_W, track);
  g->fillRect(r.x, y2 - RING_W, r.w, RING_W, track);
  g->fillRect(r.x, r.y, RING_W, r.h, track);
  g->fillRect(x2 - RING_W, r.y, RING_W, r.h, track);
  int left = 2 * (r.w + r.h) * (pct > 100 ? 100 : pct) / 100;
  int n = left < r.w - half ? left : r.w - half;  // top, centre to right
  if (n > 0) g->fillRect(r.x + half, r.y, n, RING_W, fill);
  left -= n;
  n = left < r.h ? left : r.h;  // right, down
  if (n > 0) g->fillRect(x2 - RING_W, r.y, RING_W, n, fill);
  left -= n;
  n = left < r.w ? left : r.w;  // bottom, right to left
  if (n > 0) g->fillRect(x2 - n, y2 - RING_W, n, RING_W, fill);
  left -= n;
  n = left < r.h ? left : r.h;  // left, up
  if (n > 0) g->fillRect(r.x, y2 - n, RING_W, n, fill);
  left -= n;
  n = left < half ? left : half;  // top, left to centre
  if (n > 0) g->fillRect(r.x, r.y, n, RING_W, fill);
}

// Subagents at work (app.js updateTile): a 1 px-a-cell critter in the top-left
// corner, its body only, and the bridge's "×N" beside it for more than one.
void drawSubs(LovyanGFX *g, const AgentTile &t, const LayoutRect &r, uint32_t now, uint32_t body,
              uint32_t bgRgb, uint16_t metaColor) {
  const int ox = r.x + SUBS_X, oy = r.y + SUBS_Y;
  int32_t cx, cy, cw, ch;  // within the grid's own clip
  g->getClipRect(&cx, &cy, &cw, &ch);
  const int x0 = ox > cx ? ox : (int)cx, x1 = ox + SUBS_W < cx + cw ? ox + SUBS_W : (int)(cx + cw);
  const int y0 = oy + SUBS_TOP > cy ? oy + SUBS_TOP : (int)cy;
  const int y1 = oy + SUBS_BOTTOM < cy + ch ? oy + SUBS_BOTTOM : (int)(cy + ch);
  if (x1 > x0 && y1 > y0) {
    g->setClipRect(x0, y0, x1 - x0, y1 - y0);
    drawCritter(g, Pose::Run, hatFromCode(-1), now, ox, oy, 1, CritterInks{body, bgRgb});
    g->setClipRect(cx, cy, cw, ch);
  }
  if (!t.subs[0]) return;
  g->setFont(&V_RM11.font);
  g->setTextDatum(bottom_left);
  g->setTextColor(metaColor, to565(bgRgb));
  g->drawString(t.subs, ox + SUBS_W + 2, oy + SUBS_BOTTOM + 1);
}

void drawTile(LovyanGFX *g, const AgentTile &t, const LayoutRect &r, uint32_t now) {
  const bool wait = t.kind == AgentKind::Wait, hungry = tileHungry(t);
  const bool quiet = (t.kind == AgentKind::Idle && !hungry) || t.kind == AgentKind::Exit || t.kind == AgentKind::Start;
  const uint32_t bgRgb = wait ? ACCENT_RGB : t.kind == AgentKind::Exit ? EMPTY_RGB : TILE_RGB;
  const uint16_t bg = to565(bgRgb);
  g->fillRect(r.x, r.y, r.w, r.h, bg);
  if (hungry) drawStripes(g, r, now);
  if (t.kind == AgentKind::Run) drawRunGround(g, r, now);

  g->fillRect(r.x + r.w - CHIP_INSET - CHIP, r.y + CHIP_INSET, CHIP, CHIP, wait ? BG : to565(CHIP_RGB));

  // The critter: black on orange, orange while working or hungry, ink on an
  // error, dim when idle, starting or gone.
  const uint32_t body = wait                                          ? BG_RGB
                        : t.kind == AgentKind::Run || hungry          ? ACCENT_RGB
                        : t.kind == AgentKind::Err                    ? INK_RGB
                                                                      : DIM_RGB;
  drawCritter(g, poseFromName(t.pose), hatFromCode(t.hat), now, r.x + r.w / 2 - TILE_CRIT_X * TILE_CELL,
              r.y + TILE_CRIT_Y, TILE_CELL, CritterInks{body, bgRgb});

  // The name and the meta line in Share Tech Mono, as the web board sets
  // them, each cut with "..." to the tile.
  const uint16_t ink = wait ? BG : quiet ? DIM : INK;
  const uint16_t meta = wait ? BG : hungry || t.kind == AgentKind::Err ? ACCENT : DIM;
  const int textW = r.w - 2 * TILE_TEXT_PAD, cx = r.x + r.w / 2;
  drawLine(g, V_RM13, t.name, cx, r.y + TILE_NAME_Y, textW, ink, bg, middle_center);
  drawLine(g, V_RM11, t.line, cx, r.y + TILE_META_Y, textW, meta, bg, middle_center);
  if (t.hasSubs) drawSubs(g, t, r, now, body, bgRgb, meta);

  drawRing(g, r, t.ctx, wait ? blend565(BG_RGB, ACCENT_RGB, WAIT_TRACK_ALPHA) : to565(RING_TRACK_RGB),
           wait ? BG : ACCENT);
}

// A slot with no agent, so the grid always shows whole rows and a screenful.
void drawEmptySlot(LovyanGFX *g, const LayoutRect &r) {
  g->fillRect(r.x, r.y, r.w, r.h, EMPTY);
  g->drawRect(r.x, r.y, r.w, r.h, PANEL);
}

constexpr int AGENTS_CAPTION_Y = 44;

void drawAgentsCard(LovyanGFX *g, const Card &card, const AgentsData &a, int16_t scroll, bool online,
                    uint32_t ageMs, const BatteryView &bat) {
  drawTopBar(g, online, ageMs, bat);
  if (card.sub1[0]) drawLabel(g, card.sub1, LCD_WIDTH / 2, AGENTS_CAPTION_Y, g_palette[card.color % 7], middle_center);

  // Animation time is the clock: main.cpp decides when frames happen, and each
  // one shows the critters and stripes as they are now.
  const uint32_t now = millis();
  // Whole rows, at least a screenful (two rows): empty slots fill the rest.
  const uint8_t slots = a.nTiles < 4 ? 4 : (uint8_t)((a.nTiles + 1) & ~1);
  g->setClipRect(0, AGENTS_VIEW_Y, LCD_WIDTH, AGENTS_VIEW_H);
  for (uint8_t i = 0; i < slots; i++) {
    const LayoutRect r = agentsTileRect(i, scroll);
    if (r.y + r.h <= AGENTS_VIEW_Y || r.y >= AGENTS_VIEW_Y + AGENTS_VIEW_H) continue;
    if (i < a.nTiles) {
      drawTile(g, a.tiles[i], r, now);
    } else {
      drawEmptySlot(g, r);
    }
  }
  g->clearClipRect();
  drawScrollBar(g, TILE_COL1_X + TILE_W + 2, AGENTS_VIEW_Y, AGENTS_VIEW_H, agentsContentH(a.nTiles), scroll, DIM);
}

// --- the prompt screen -------------------------------------------------------------
// agents-deck's permission screen (docs/images/panel-permission-v2.png): the
// whole screen orange, everything on it black. The agent's critter and name at
// the top, the question, the detail in a black box, the context, then the
// answers: the first solid black, the rest outlined.
constexpr int PROMPT_X = PROMPT_COL_X, PROMPT_W = PROMPT_COL_W;  // agents_layout.h: hit-tested there too
constexpr int PROMPT_HEADER_Y = 43;
constexpr int PROMPT_CRIT_X = 28, PROMPT_CRIT_Y = 16, PROMPT_CELL = 2;  // the small critter, top-left
constexpr int PROMPT_BATTERY_X = 76;  // the battery moves right of it
constexpr int DETAIL_PAD_X = 8;
constexpr int BTN_R = 3, BTN_LINE = 2;
constexpr int KEY_X = PROMPT_X + 12, LABEL_X = PROMPT_X + 28;
constexpr int NOTICE_CELL = 5;  // the critter on a prompt screen with no prompt to show
constexpr uint8_t TITLE_MAX_LINES = 2, DETAIL_MAX_LINES = 3;

// Black at 40% on the orange: what is off.
uint16_t promptOff() { return blend565(BG_RGB, ACCENT_RGB, 102); }

// The prompt's strings as they will be drawn, already wrapped.
struct PromptText {
  char title[TITLE_MAX_LINES][LINE_CAP];
  char detail[DETAIL_MAX_LINES][LINE_CAP];
  char context[1][LINE_CAP];
  uint8_t nTitle, nDetail;
  bool hasContext;
};

void promptText(LovyanGFX *g, const AgentPrompt &q, PromptText &t) {
  g->setFont(&V_RB18.font);
  t.nTitle = wrapText(g, q.title, PROMPT_W, TITLE_MAX_LINES, t.title);
  g->setFont(&V_RM13.font);
  if (q.cut) {
    snprintf(t.detail[0], LINE_CAP, "(detail cut)");
    t.nDetail = 1;
  } else {
    t.nDetail = wrapText(g, q.detail, PROMPT_W - 2 * DETAIL_PAD_X, DETAIL_MAX_LINES, t.detail);
  }
  g->setFont(&V_RM11.font);
  t.hasContext = wrapText(g, q.context, PROMPT_W, 1, t.context) > 0;
}

// The states whose buttons say why they are off, on a line of their own.
const char *offReason(ButtonsOff off) {
  switch (off) {
    case ButtonsOff::Input: return "answer at the terminal";
    case ButtonsOff::NotTmux: return "not in tmux";
    case ButtonsOff::Locked: return "unlock mac first";
    case ButtonsOff::Sent: return "sending...";
    case ButtonsOff::NoAnswer: return "no answer from mac";
    default: return nullptr;
  }
}

// An input prompt wants typed text: the cube offers no buttons for it.
uint8_t promptButtons(const AgentPrompt &q) { return q.kind == PromptKind::Input ? 0 : q.nOpts; }

PromptMetrics metricsOf(const PromptText &t, bool reason) {
  PromptMetrics m;
  m.titleLines = t.nTitle;
  m.detailLines = t.nDetail;
  m.context = t.hasContext;
  m.reason = reason;
  return m;
}

// A button: solid (black, orange text) or a 2 px outline in `ink`.
void drawPromptButton(LovyanGFX *g, int y, bool solid, uint16_t ink) {
  if (solid) {
    g->fillRoundRect(PROMPT_X, y, PROMPT_W, PROMPT_BTN_H, BTN_R, ink);
  } else {
    for (int k = 0; k < BTN_LINE; k++)
      g->drawRoundRect(PROMPT_X + k, y + k, PROMPT_W - 2 * k, PROMPT_BTN_H - 2 * k, BTN_R, ink);
  }
}

// One answer: the key digit, then the label. The first is solid black, the
// rest outlined; `lit` (the answer that was sent) is solid instead of the
// first. Off: black at 40%.
void drawOption(LovyanGFX *g, int y, const PromptOpt &o, bool solid, bool enabled) {
  const uint16_t ink = enabled ? BG : promptOff();
  drawPromptButton(g, y, solid, ink);
  const uint16_t fg = solid ? ACCENT : ink, bg = solid ? ink : ACCENT;
  const int cy = y + PROMPT_BTN_H / 2;
  const char key[2] = {o.key, '\0'};
  g->setFont(&V_RM13.font);
  g->setTextDatum(middle_left);
  g->setTextColor(fg, bg);
  g->drawString(key, KEY_X, cy);
  drawLine(g, V_RM13, o.label, LABEL_X, cy, PROMPT_X + PROMPT_W - 8 - LABEL_X, fg, bg, middle_left);
}

void drawBackButton(LovyanGFX *g, int y) {
  drawPromptButton(g, y, false, BG);
  g->setFont(&V_RM13.font);
  g->setTextDatum(middle_center);
  g->setTextColor(BG, ACCENT);
  g->drawString("back", LCD_WIDTH / 2, y + PROMPT_BTN_H / 2);
}

// The column under the header, in PromptLayout's blocks, `scroll` px up.
void drawPromptColumn(LovyanGFX *g, const AgentPrompt &q, const PromptUi &v) {
  PromptText t;
  promptText(g, q, t);
  const char *reason = offReason(v.off);
  PromptLayout L;
  L.build(metricsOf(t, reason != nullptr), promptButtons(q));
  const int y0 = PROMPT_VIEW_Y - v.scroll;

  g->setClipRect(0, PROMPT_VIEW_Y, LCD_WIDTH, PROMPT_VIEW_H);
  g->setFont(&V_RB18.font);
  g->setTextDatum(top_left);
  g->setTextColor(BG, ACCENT);
  for (uint8_t i = 0; i < t.nTitle; i++) g->drawString(t.title[i], PROMPT_X, y0 + i * PROMPT_TITLE_LINE_H);
  int y = y0 + t.nTitle * PROMPT_TITLE_LINE_H;

  if (t.nDetail) {
    y += PROMPT_BLOCK_GAP;
    const int h = t.nDetail * PROMPT_DETAIL_LINE_H + PROMPT_DETAIL_PAD;
    g->fillRoundRect(PROMPT_X, y, PROMPT_W, h, BTN_R, BG);
    g->setFont(&V_RM13.font);
    g->setTextDatum(top_left);
    g->setTextColor(q.cut ? DIM : INK, BG);
    for (uint8_t i = 0; i < t.nDetail; i++)
      g->drawString(t.detail[i], PROMPT_X + DETAIL_PAD_X, y + PROMPT_DETAIL_PAD / 2 + i * PROMPT_DETAIL_LINE_H);
    y += h;
  }
  if (t.hasContext) {
    y += PROMPT_BLOCK_GAP;
    g->setFont(&V_RM11.font);
    g->setTextDatum(middle_left);
    g->setTextColor(BG, ACCENT);
    g->drawString(t.context[0], PROMPT_X, y + PROMPT_CONTEXT_H / 2);
  }

  // NoAnswer lets a tap through again, so its buttons look live.
  const bool enabled = v.off == ButtonsOff::No || v.off == ButtonsOff::NoAnswer;
  for (uint8_t i = 0; i < promptButtons(q); i++) {
    const bool solid = v.lit >= 0 ? i == v.lit : i == 0;
    drawOption(g, y0 + L.buttonY(i), q.opts[i], solid, enabled || i == v.lit);
  }
  if (reason) drawLine(g, V_RM11, reason, LCD_WIDTH / 2, y0 + L.reasonY() + PROMPT_REASON_H / 2, PROMPT_W, BG,
                       ACCENT, middle_center);
  drawBackButton(g, y0 + L.backY());
  g->clearClipRect();
  drawScrollBar(g, PROMPT_X + PROMPT_W + 6, PROMPT_VIEW_Y, PROMPT_VIEW_H, L.contentH(), v.scroll, promptOff());
}

// No prompt to answer here: the agent's critter, large, over two lines of copy.
void drawPromptNotice(LovyanGFX *g, const AgentTile &tile, const char *big, const char *small, uint32_t now) {
  drawCritter(g, Pose::Wait, hatFromCode(tile.hat), now, LCD_WIDTH / 2 - TILE_CRIT_X * NOTICE_CELL, 112,
              NOTICE_CELL, CritterInks{BG_RGB, ACCENT_RGB});
  for (Vlw *f : {&V_RB24, &V_RB18, &V_RB14}) {  // the largest that fits
    g->setFont(&f->font);
    if (g->textWidth(big) <= PROMPT_W) break;
  }
  g->setTextDatum(middle_center);
  g->setTextColor(BG, ACCENT);
  g->drawString(big, LCD_WIDTH / 2, 180);
  // A long agent name in "answer <name> first" takes a second line.
  char lines[2][LINE_CAP];
  g->setFont(&V_RM13.font);
  const uint8_t n = wrapText(g, small, PROMPT_W, 2, lines);
  g->setTextDatum(middle_center);
  g->setTextColor(BG, ACCENT);
  for (uint8_t i = 0; i < n; i++) g->drawString(lines[i], LCD_WIDTH / 2, 210 + i * 18);
}

}  // namespace

void uiBegin(Display &) {
  for (int i = 0; i < 7; i++) g_palette[i] = to565(RGB888[i]);
  BG = to565(BG_RGB);
  PANEL = to565(PANEL_RGB);
  TILE = to565(TILE_RGB);
  INK = g_palette[ACC_INK];
  DIM = to565(DIM_RGB);
  LINE = to565(LINE_RGB);
  ACCENT = g_palette[ACC_ACCENT];
  EMPTY = to565(EMPTY_RGB);
  MUTED = to565(MUTED_RGB);
  ACC_DEV = ACCENT;

  // 240x280x16bpp is 134 KB. It fits in internal RAM on an S3 and is much
  // faster there than in PSRAM, so try internal first and fall back to
  // drawing straight to the panel if the allocation fails.
  loadVlw(V_RM11, font_rm11, sizeof(font_rm11));
  loadVlw(V_RM13, font_rm13, sizeof(font_rm13));
  loadVlw(V_RB14, font_rb14, sizeof(font_rb14));
  loadVlw(V_RB18, font_rb18, sizeof(font_rb18));
  loadVlw(V_RB24, font_rb24, sizeof(font_rb24));
  loadVlw(V_RB30, font_rb30, sizeof(font_rb30));
  loadVlw(V_RB40, font_rb40, sizeof(font_rb40));
  loadVlw(V_B30, font_b30, sizeof(font_b30));
  loadVlw(V_B24, font_b24, sizeof(font_b24));
  loadVlw(V_S12, font_s12, sizeof(font_s12));
  loadVlw(V_B18V, font_b18v, sizeof(font_b18v));
  loadVlw(V_S12V, font_s12v, sizeof(font_s12v));

  g_canvas.setColorDepth(16);
  g_sprite = g_canvas.createSprite(LCD_WIDTH, LCD_HEIGHT) != nullptr;
  buildRingTable(g_ringOuter);
  for (int i = 0; i < 256; i++) g_trackNo[i] = pack565(RING_TRACK, i * (1.0f / 255.0f));
  Serial.printf("[ui] framebuffer: %s\n", g_sprite ? "sprite (double buffered)" : "direct");
}

bool uiAnimating() { return g_animating; }

void uiInvalidate() { g_rowsKnown = false; }

void uiReplay(uint8_t index) {
  if (index < MAX_CARDS) {
    g_anim[index].seen = false;
    g_anim2[index].seen = false;
  }
}

void uiReplayVolume() { g_volAnim.seen = false; }

namespace {
bool hasCards(const Payload &p) { return p.valid && p.nCards > 0; }
}  // namespace

uint8_t uiDeckSize(const Payload &p, bool volume) { return uiVolumeIndex(p) + (volume ? 1 : 0); }

uint8_t uiVolumeIndex(const Payload &p) { return hasCards(p) ? p.nCards : 1; }

void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const BatteryView &bat, UiLink link, const VolumeView *vol, const AgentsUi *agents) {
  g_animating = false;
  g_link = link;

  const bool hasVol = vol != nullptr;
  const uint8_t deck = uiDeckSize(p, hasVol);
  const uint8_t i = index % deck;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  if (hasVol && i == uiVolumeIndex(p)) {
    drawVolumeCard(g, *vol, online, ageMs, bat);
  } else if (!hasCards(p)) {
    drawWaitingCard(g, online, ageMs, bat);
  } else {
    const Card &card = p.cards[i];
    if (card.agents && p.agents.present) {
      drawAgentsCard(g, card, p.agents, agents ? agents->gridScroll : 0, online, ageMs, bat);
    } else if (card.gauge >= GAUGE_BLANK && card.gauge2 >= GAUGE_BLANK) {
      drawDualCard(g, card, g_anim[i], g_anim2[i], millis(), online, ageMs, bat);
    } else if (card.gauge >= GAUGE_BLANK) {
      // VALUE_MAX_W is what fits between the ring's inner edges at the value's
      // height; the size is settled across the deck, not per card.
      const ValueFont valueFont = fitDeck(g, p, VALUE_MAX_W, VALUE_MAX_H);
      drawGaugeCard(g, card, RGB888[card.color % 7], valueFont, g_anim[i],
                    millis(), online, ageMs, bat);
    } else {
      drawTextCard(g, p, card, g_palette[card.color % 7], online, ageMs, bat);
    }
  }

  drawDots(g, deck, i);

  if (g_edSliding) {
    const float pos = editorSlidePos(millis());
    if (pos > 0.0f) {
      const int off = (int)((1.0f - pos) * LCD_HEIGHT);
      dimCard(g, LCD_HEIGHT - off, LCD_HEIGHT, pos);
      drawDevicePanel(g, g_edDevice, -off);
    }
    // The last frame of the slide stays animating so it is pushed before the
    // static editor (or the plain card) takes over.
    g_animating = true;
  }

  present(lcd);
}

void uiDeviceSlide(bool open, const DeviceSettings &s) {
  g_edDevice = s;
  g_edOpening = open;
  g_edSliding = true;
  g_edStart = millis();
}

void uiDeviceEditor(Display &lcd, const DeviceSettings &s) {
  g_animating = false;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  drawDevicePanel(g, s, 0);
  present(lcd);
}

bool uiEditorSliding() { return g_edSliding; }

void uiMessage(Display &lcd, const char *title, const char *body) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  g->setTextDatum(middle_center);
  g->setFont(&V_RB24.font);
  g->setTextColor(ACCENT, BG);
  char low[32];
  lowerCopy(title, low, sizeof(low));  // lower-case like every label; the body may be an SSID, kept as is
  g->drawString(low, LCD_WIDTH / 2, LCD_HEIGHT / 2 - 16);
  g->setFont(&V_RM13.font);
  g->setTextColor(DIM, BG);
  g->drawString(body, LCD_WIDTH / 2, LCD_HEIGHT / 2 + 12);
  present(lcd);
}

void uiPortal(Display &lcd, const char *apName, const char *apIp, const char *lanIp) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  drawLabel(g, "wifi setup", LCD_WIDTH / 2, 30, ACCENT, middle_center);

  // A WiFi QR: phones offer to join the (open) network when it is scanned.
  // Version 3 is 29 modules, so 116px is a whole 4px per module. The white
  // plate gives the code its quiet zone against the black background.
  char join[64];
  snprintf(join, sizeof(join), "WIFI:T:nopass;S:%s;;", apName);
  constexpr int QR = 116;
  const int qx = (LCD_WIDTH - QR) / 2;
  const int qy = 50;
  g->fillRect(qx - 6, qy - 6, QR + 12, QR + 12, (uint16_t)0xFFFF);
  g->qrcode(join, qx, qy, QR, 3);

  g->setFont(&V_RB18.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, BG);
  g->drawString(apName, LCD_WIDTH / 2, 190);

  drawLabel(g, "join, then open", LCD_WIDTH / 2, 214, DIM, middle_center);
  g->setFont(&V_RM13.font);
  g->setTextColor(DIM, BG);
  g->drawString(apIp, LCD_WIDTH / 2, 234);
  if (lanIp && lanIp[0]) {
    char lan[40];
    snprintf(lan, sizeof(lan), "or on wifi %s", lanIp);
    g->drawString(lan, LCD_WIDTH / 2, 252);
  }

  present(lcd);
}

void uiBlePair(Display &lcd, uint32_t passkey) {
  g_animating = false;  // a static screen: a sweep cut short must not keep main.cpp drawing
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  drawLabel(g, "pair with mac", LCD_WIDTH / 2, 40, ACCENT, middle_center);

  g->setTextDatum(middle_center);
  if (passkey) {
    char code[8];
    snprintf(code, sizeof(code), "%06u", (unsigned)passkey);
    g->setFont(&V_RB30.font);
    g->setTextColor(INK, BG);
    g->drawString(code, LCD_WIDTH / 2, 118);
    drawLabel(g, "type this on your mac", LCD_WIDTH / 2, 156, DIM, middle_center);
  } else {
    g->setFont(&V_RB18.font);
    g->setTextColor(INK, BG);
    g->drawString("waiting for a mac", LCD_WIDTH / 2, 112);
    drawLabel(g, "open claude cube link", LCD_WIDTH / 2, 150, DIM, middle_center);
  }

  g->setFont(&V_RM13.font);
  g->setTextColor(DIM, BG);
  g->drawString("stuck? forget claude cube", LCD_WIDTH / 2, 226);
  g->drawString("in mac bluetooth settings", LCD_WIDTH / 2, 244);

  present(lcd);
}

void uiUnlock(Display &lcd, bool waiting) {
  g_animating = false;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  const uint16_t col = waiting ? DIM : ACCENT;

  // A padlock: the shackle is a ring with its lower half cut away, plus legs
  // down into the body. While waiting it is lifted and the right leg is free.
  constexpr int CX = LCD_WIDTH / 2, BODY_Y = 98, BODY_W = 64, BODY_H = 50;
  const int sy = waiting ? BODY_Y - 26 : BODY_Y - 14;
  g->fillSmoothCircle(CX, sy, 24, col);
  g->fillSmoothCircle(CX, sy, 16, BG);
  g->fillRect(CX - 25, sy, 50, 26, BG);
  g->fillRect(CX - 24, sy, 8, BODY_Y - sy, col);
  if (!waiting) g->fillRect(CX + 16, sy, 8, BODY_Y - sy, col);
  g->fillRect(CX - BODY_W / 2, BODY_Y, BODY_W, BODY_H, col);
  g->fillSmoothCircle(CX, BODY_Y + 20, 6, BG);
  g->fillRect(CX - 2, BODY_Y + 22, 5, 14, BG);

  g->setTextDatum(middle_center);
  g->setFont(&V_RB24.font);
  g->setTextColor(INK, BG);
  g->drawString(waiting ? "unlocking" : "tap to unlock", CX, 186);
  drawLabel(g, waiting ? "check your mac" : "your mac is locked", CX, 214, DIM, middle_center);

  drawLabel(g, "swipe for the cards", CX, 256, MUTED, middle_center);
  present(lcd);
}

void uiAgentPrompt(Display &lcd, const AgentsData &a, const AgentTile &tile, const PromptUi &v,
                   bool online, uint32_t ageMs, const BatteryView &bat, UiLink link) {
  g_animating = false;
  g_link = link;
  LovyanGFX *g = target(lcd);
  g->fillScreen(ACCENT);
  const uint32_t now = millis();
  const bool prompt = v.screen == PromptScreen::Prompt && a.hasPrompt;
  // The prompt carries the agent's small critter top-left (the battery moves
  // over for it); the notices draw it large instead.
  if (prompt)
    drawCritter(g, poseFromName(tile.pose), hatFromCode(tile.hat), now, PROMPT_CRIT_X, PROMPT_CRIT_Y, PROMPT_CELL,
                CritterInks{BG_RGB, ACCENT_RGB});
  drawTopBar(g, online, ageMs, bat, true, prompt ? PROMPT_BATTERY_X : 36);
  drawLine(g, V_RB18, tile.name, PROMPT_X, PROMPT_HEADER_Y, PROMPT_W, BG, ACCENT, middle_left);

  // The fixed copy of each screen (agent_view.h decides which).
  char queued[48];
  if (prompt) {
    drawPromptColumn(g, a.prompt, v);
  } else if (v.screen == PromptScreen::OverWifi) {
    drawPromptNotice(g, tile, "needs you", "open on bluetooth", now);
  } else if (v.screen == PromptScreen::Queued && v.queuedName && v.queuedName[0]) {
    snprintf(queued, sizeof(queued), "answer %s first", v.queuedName);
    drawPromptNotice(g, tile, "needs you", queued, now);
  } else if (v.screen == PromptScreen::Dropped) {
    drawPromptNotice(g, tile, "too long for the cube", "answer at the terminal", now);
  } else {
    drawPromptNotice(g, tile, "needs you", "answer at the terminal", now);
  }
  present(lcd);
}

PromptMetrics uiPromptMetrics(const AgentPrompt &q, bool reason) {
  // Measured on the sprite object whether or not its buffer exists: the
  // fonts, not the target, decide the widths.
  PromptText t;
  promptText(&g_canvas, q, t);
  return metricsOf(t, reason);
}

PromptLayout uiPromptLayout(const AgentPrompt &q, ButtonsOff off) {
  PromptLayout L;
  L.build(uiPromptMetrics(q, offReason(off) != nullptr), promptButtons(q));
  return L;
}

// --- the detail screen -------------------------------------------------------------
// agents-deck's detail panel (docs/images/panel-detail-v2.png) condensed to
// 240 x 280: black, the critter top-left beside the title, then what it is
// doing, its context, the last prompt and the meta line. Every word is the
// payload's `d` (or the tile's) or fixed copy.
namespace {
constexpr int DETAIL_ROW_Y = 44;  // `< back` and the status pill
constexpr int DETAIL_X = 16, DETAIL_W = LCD_WIDTH - 2 * DETAIL_X;
constexpr int DETAIL_PILL_RIGHT = 224, DETAIL_PILL_H = 16, DETAIL_PILL_PAD = 5, DETAIL_PILL_MAX_W = 120;
// 4 px cells, as agents-deck's 240x280 port draws its critters: the half-cell
// bob is then exactly 2 px. Cell 0 at (16, 80): the cap's brim reaches x 4,
// the hat y 54, the feet y 116, the bubble x 88, short of the title.
constexpr int DETAIL_CELL = 4, DETAIL_CRIT_X = 16, DETAIL_CRIT_Y = 80;
constexpr int DETAIL_TITLE_X = 92, DETAIL_TITLE_W = 232 - DETAIL_TITLE_X, DETAIL_TITLE_Y = 60, DETAIL_TITLE_LINE_H = 20;
constexpr int DETAIL_DOING_LABEL_Y = 128, DETAIL_DOING_Y = 142;
constexpr int DETAIL_CTX_Y = 162, DETAIL_BAR_Y = 176, DETAIL_BAR_H = 4;
constexpr int DETAIL_PROMPT_LABEL_Y = 188, DETAIL_PROMPT_Y = 202, DETAIL_PROMPT_LINE_H = 15;
constexpr uint8_t DETAIL_PROMPT_LINES = 3;
// The last line sits clear of the glass's rounded bottom corners (radius 40):
// centred, 192 px wide, at y 252, where the arc is under 16 px in.
constexpr int DETAIL_META_Y = 252, DETAIL_META_W = 192;

// A small label (rm11, lower-case), its left edge at x; returns its width.
int detailLabel(LovyanGFX *g, const char *text, int x, int y, uint16_t color) {
  char buf[48];
  lowerCopy(text, buf, sizeof(buf));
  g->setFont(&V_RM11.font);
  g->setTextDatum(middle_left);
  g->setTextColor(color, BG);
  g->drawString(buf, x, y);
  return g->textWidth(buf);
}

// The status pill, right-aligned on the back row: rm11 in black on a filled
// rect, cut with "..." to DETAIL_PILL_MAX_W.
void detailPill(LovyanGFX *g, const char *text, uint16_t fill) {
  if (!text || !text[0]) return;
  char line[1][LINE_CAP];
  g->setFont(&V_RM11.font);
  if (!wrapText(g, text, DETAIL_PILL_MAX_W - 2 * DETAIL_PILL_PAD, 1, line)) return;
  const int w = g->textWidth(line[0]) + 2 * DETAIL_PILL_PAD;
  const int x = DETAIL_PILL_RIGHT - w;
  g->fillRect(x, DETAIL_ROW_Y - DETAIL_PILL_H / 2, w, DETAIL_PILL_H, fill);
  g->setTextDatum(middle_left);
  g->setTextColor(BG, fill);
  g->drawString(line[0], x + DETAIL_PILL_PAD, DETAIL_ROW_Y);
}
}  // namespace

void uiAgentDetail(Display &lcd, const AgentsData &a, const AgentTile &tile, int16_t hat, bool viaBle,
                   bool online, uint32_t ageMs, const BatteryView &bat, UiLink link) {
  g_animating = false;
  g_link = link;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  const uint32_t now = millis();
  drawTopBar(g, online, ageMs, bat);

  // `d` counts only when it is this agent's and came over BLE (the LAN variant
  // never carries it).
  const AgentDetail &d = a.detail;
  const bool has = viaBle && d.present && !strcmp(d.id, tile.id);
  const char *waitCopy = viaBle ? "loading..." : "details need Bluetooth";

  // The back row: `< back` (DETAIL_BACK is its touch zone) and the status pill.
  g->setFont(&V_RM13.font);
  g->setTextDatum(middle_left);
  g->setTextColor(INK, BG);
  g->drawString("< back", DETAIL_X, DETAIL_ROW_Y);
  const bool wait = tile.kind == AgentKind::Wait, hungry = tileHungry(tile);
  detailPill(g, has && d.status[0] ? d.status : tile.line, wait || hungry ? ACCENT : INK);

  // The critter in its pose and (shown) hat, inside DETAIL_CRITTER, coloured
  // as on its tile but on black.
  const uint32_t body = wait || tile.kind == AgentKind::Run || hungry ? ACCENT_RGB
                        : tile.kind == AgentKind::Err                 ? INK_RGB
                                                                      : DIM_RGB;
  drawCritter(g, poseFromName(tile.pose), hatFromCode(hat), now, DETAIL_CRIT_X, DETAIL_CRIT_Y, DETAIL_CELL,
              CritterInks{body, BG_RGB});

  // The title (rb18, two lines) and the subagents under it.
  char lines[DETAIL_PROMPT_LINES][LINE_CAP];
  g->setFont(&V_RB18.font);
  const uint8_t nTitle = wrapText(g, has && d.title[0] ? d.title : tile.name, DETAIL_TITLE_W, 2, lines);
  g->setTextDatum(top_left);
  g->setTextColor(INK, BG);
  for (uint8_t i = 0; i < nTitle; i++) g->drawString(lines[i], DETAIL_TITLE_X, DETAIL_TITLE_Y + i * DETAIL_TITLE_LINE_H);
  if (has && d.subs[0])
    drawLine(g, V_RM11, d.subs, DETAIL_TITLE_X, DETAIL_TITLE_Y + nTitle * DETAIL_TITLE_LINE_H + 6, DETAIL_TITLE_W,
             ACCENT, BG, middle_left);

  // Doing now: the last error, in the accent, wins over the tool.
  detailLabel(g, "doing now", DETAIL_X, DETAIL_DOING_LABEL_Y, DIM);
  const bool err = has && d.err[0];
  drawLine(g, V_RM13, !has ? waitCopy : err ? d.err : d.doing, DETAIL_X, DETAIL_DOING_Y, DETAIL_W,
           !has ? DIM : err ? ACCENT : INK, BG, middle_left);

  // Context: the bridge's figures beside the label, a bar filled to the tile's x.
  const int lw = detailLabel(g, "context", DETAIL_X, DETAIL_CTX_Y, DIM);
  if (has && d.ctx[0])
    drawLine(g, V_RM11, d.ctx, DETAIL_X + lw + 8, DETAIL_CTX_Y, DETAIL_W - lw - 8, DIM, BG, middle_left);
  g->fillRect(DETAIL_X, DETAIL_BAR_Y, DETAIL_W, DETAIL_BAR_H, LINE);
  const int fill = DETAIL_W * (tile.ctx > 100 ? 100 : tile.ctx) / 100;
  if (fill > 0) g->fillRect(DETAIL_X, DETAIL_BAR_Y, fill, DETAIL_BAR_H, INK);

  // The last prompt, wrapped to three lines.
  detailLabel(g, "last prompt", DETAIL_X, DETAIL_PROMPT_LABEL_Y, DIM);
  g->setFont(&V_RM13.font);
  const uint8_t nPrompt = wrapText(g, has ? d.prompt : waitCopy, DETAIL_W, DETAIL_PROMPT_LINES, lines);
  g->setTextDatum(middle_left);
  g->setTextColor(has ? INK : DIM, BG);
  for (uint8_t i = 0; i < nPrompt; i++) g->drawString(lines[i], DETAIL_X, DETAIL_PROMPT_Y + i * DETAIL_PROMPT_LINE_H);

  // Model, folder, branch.
  if (has && d.meta[0]) drawLine(g, V_RM11, d.meta, LCD_WIDTH / 2, DETAIL_META_Y, DETAIL_META_W, DIM, BG, middle_center);
  present(lcd);
}

// --- the hat picker ------------------------------------------------------------------
// Opened by a tap on the critter on the detail screen: the 13 shapes, each on a
// small idle critter in the current colour, `none`, `auto`, and the 8 colours.
// hat_picker_layout.h owns the geometry, which main.cpp hit-tests with too.
namespace {
constexpr int PICK_CELL = 2;      // px per critter cell: the hats reach 50 px across
constexpr int PICK_CRIT_Y = 21;   // cell 0 this far below the cell's top (hats reach -6.5, the body 7.6)
constexpr int PICK_MARK = 2;      // the outline of what is chosen
constexpr int PICK_NAME_X = 104;  // `hat`, beside `< back`

// A 2 px ink outline inside r (what is chosen), else a 1 px line.
void pickOutline(LovyanGFX *g, const LayoutRect &r, bool chosen) {
  if (!chosen) {
    g->drawRect(r.x, r.y, r.w, r.h, LINE);
    return;
  }
  for (int i = 0; i < PICK_MARK; i++) g->drawRect(r.x + i, r.y + i, r.w - 2 * i, r.h - 2 * i, INK);
}
}  // namespace

void uiHatPicker(Display &lcd, const AgentTile &tile, int16_t hat, bool autoOn, uint32_t now, bool online,
                 uint32_t ageMs, const BatteryView &bat, UiLink link) {
  (void)now;  // the critters stand still (idle pose): nothing here moves
  g_animating = false;
  g_link = link;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  drawTopBar(g, online, ageMs, bat);

  // The back row: `< back` (HAT_PICK_BACK is its touch zone), `hat`, and
  // whose hat it is, right-aligned.
  g->setFont(&V_RM13.font);
  g->setTextDatum(middle_left);
  g->setTextColor(INK, BG);
  g->drawString("< back", DETAIL_X, DETAIL_ROW_Y);
  g->setTextColor(DIM, BG);
  g->drawString("hat", PICK_NAME_X, DETAIL_ROW_Y);
  const int nameX = PICK_NAME_X + g->textWidth("hat") + 12;
  drawLine(g, V_RM11, tile.name, DETAIL_PILL_RIGHT, DETAIL_ROW_Y, DETAIL_PILL_RIGHT - nameX, DIM, BG, middle_right);

  const HatId cur = hatFromCode(hat);
  const int8_t color = cur.shape >= 0 ? cur.color : 0;
  for (int i = 0; i < HAT_PICK_CELLS; i++) {
    const LayoutRect r = hatPickCell(i);
    g->fillRect(r.x, r.y, r.w, r.h, TILE);
    bool chosen;
    if (i == HAT_PICK_AUTO_CELL) {
      chosen = autoOn;
      g->setFont(&V_RM13.font);
      g->setTextDatum(middle_center);
      g->setTextColor(chosen ? INK : DIM, TILE);
      g->drawString("auto", r.x + r.w / 2, r.y + r.h / 2);
    } else {
      const bool none = i == HAT_PICK_NONE_CELL;
      chosen = !autoOn && (none ? cur.shape < 0 : cur.shape == i);
      const HatId h = none ? HatId{-1, 0} : HatId{(int8_t)i, color};
      drawCritter(g, Pose::Idle, h, 0, r.x + r.w / 2 - TILE_CRIT_X * PICK_CELL, r.y + PICK_CRIT_Y, PICK_CELL,
                  CritterInks{DIM_RGB, TILE_RGB});
      if (none) {
        g->setFont(&V_RM11.font);
        g->setTextDatum(top_center);
        g->setTextColor(chosen ? INK : DIM, TILE);
        g->drawString("none", r.x + r.w / 2, r.y + 3);
      }
    }
    pickOutline(g, r, chosen);
  }

  // The colours: inert (dimmed, none chosen) while the agent wears no hat.
  for (int i = 0; i < HAT_PICK_COLORS; i++) {
    const LayoutRect r = hatPickSwatch(i);
    const uint32_t rgb = hatMainRgb((int8_t)i);
    g->fillRect(r.x, r.y, r.w, r.h, cur.shape >= 0 ? to565(rgb) : blend565(rgb, BG_RGB, 77));
    if (cur.shape >= 0 && i == cur.color) {
      for (int k = 0; k < PICK_MARK; k++) g->drawRect(r.x + k, r.y + k, r.w - 2 * k, r.h - 2 * k, INK);
      g->drawRect(r.x + PICK_MARK, r.y + PICK_MARK, r.w - 2 * PICK_MARK, r.h - 2 * PICK_MARK, BG);
    }
  }
  present(lcd);
}

// --- navigation ----------------------------------------------------------------
// The ride keeps its own look: the colours below shadow the theme's inside
// `nav`, and the map uses the Inter fonts.
namespace {
namespace nav {

const uint16_t BG = 0x0000;
const uint16_t INK = to565(0xE7ECF5);
const uint16_t DIM = to565(0x7C8598);
const uint16_t MUTED = to565(0x4A5366);
const uint16_t AMBER = to565(0xF5C451);
const uint16_t RED = to565(0xFF6B6B);
const uint16_t ACCENT = to565(0xFF8A5B);

constexpr uint32_t NAV_STREET_RGB[4] = {0x59606F, 0x4A5263, 0x3A4152, 0x2A3040};
constexpr int NAV_STREET_W[4] = {7, 5, 3, 2};
constexpr int NAV_BANNER_H = 62;
constexpr int NAV_GLYPH_CX = 38, NAV_GLYPH_CY = 31;  // the 44 px box at x 16..60
constexpr int NAV_TEXT_X = 72;
constexpr int NAV_BOTTOM_Y = 262;
constexpr int NAV_BOTTOM_MAX_W = 212;

// A stroked polyline with round caps and joins (a filled circle at every vertex),
// moved by (ox, oy): the dragged map's shift.
void navStroke(LovyanGFX *g, const NavPoint *p, size_t n, int w, uint16_t col, int ox = 0, int oy = 0) {
  const float r = w * 0.5f;
  for (size_t i = 0; i + 1 < n; i++) g->drawWideLine(p[i].x + ox, p[i].y + oy, p[i + 1].x + ox, p[i + 1].y + oy, r, col);
  if (w <= 2) return;  // a 2 px line has no joint worth rounding
  for (size_t i = 0; i < n; i++) g->fillSmoothCircle(p[i].x + ox, p[i].y + oy, (int)(r + 0.5f), col);
}

void navPolyline(LovyanGFX *g, const NavPoint *p, size_t n, int w, uint16_t col, int ox, int oy) {
  if (w <= 2) {
    for (size_t i = 0; i + 1 < n; i++) g->drawWideLine(p[i].x + ox, p[i].y + oy, p[i + 1].x + ox, p[i + 1].y + oy, 1.0f, col);
  } else {
    navStroke(g, p, n, w, col, ox, oy);
  }
}

// A turn arrow from a polyline in glyph space (origin = box centre, y down, the
// last point is the tip), rotated clockwise by `rot` radians.
void navArrow(LovyanGFX *g, const int8_t (*pts)[2], size_t n, float rot, uint16_t col) {
  const float cs = cosf(rot), sn = sinf(rot);
  NavPoint q[10];
  if (n > 10) n = 10;
  for (size_t i = 0; i < n; i++) {
    const float x = pts[i][0], y = pts[i][1];
    q[i] = NavPoint{(int16_t)lroundf(NAV_GLYPH_CX + x * cs - y * sn), (int16_t)lroundf(NAV_GLYPH_CY + x * sn + y * cs)};
  }
  // Head: a triangle at the tip, along the last segment.
  const float dx = q[n - 1].x - q[n - 2].x, dy = q[n - 1].y - q[n - 2].y;
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 1.0f) return;
  const float ux = dx / len, uy = dy / len;
  constexpr float HEAD_L = 13.0f, HEAD_W = 8.5f;
  const float bx = q[n - 1].x - ux * HEAD_L, by = q[n - 1].y - uy * HEAD_L;
  q[n - 1] = NavPoint{(int16_t)lroundf(bx + ux * 2.0f), (int16_t)lroundf(by + uy * 2.0f)};
  navStroke(g, q, n, 5, col);
  g->fillTriangle((int)lroundf(bx - uy * HEAD_W), (int)lroundf(by + ux * HEAD_W), (int)lroundf(bx + uy * HEAD_W),
                  (int)lroundf(by - ux * HEAD_W), (int)lroundf(bx + ux * HEAD_L), (int)lroundf(by + uy * HEAD_L), col);
}

#define NAV_ARROW(g, rot, col, ...)                       \
  do {                                                    \
    static const int8_t P[][2] = {__VA_ARGS__};           \
    navArrow(g, P, sizeof(P) / sizeof(P[0]), rot, col);   \
  } while (0)

// The turn glyph in the banner's left box.
void drawNavTurn(LovyanGFX *g, NavTurn turn, uint8_t exitNo, uint8_t dir, uint16_t col) {
  switch (turn) {
    case NavTurn::Straight: NAV_ARROW(g, 0, col, {0, 20}, {0, -22}); break;
    case NavTurn::Right: NAV_ARROW(g, 0, col, {-9, 20}, {-9, -1}, {22, -1}); break;
    case NavTurn::Left: NAV_ARROW(g, 0, col, {9, 20}, {9, -1}, {-22, -1}); break;
    case NavTurn::SlightRight: NAV_ARROW(g, 0, col, {-7, 20}, {-7, 6}, {12, -20}); break;
    case NavTurn::SlightLeft: NAV_ARROW(g, 0, col, {7, 20}, {7, 6}, {-12, -20}); break;
    case NavTurn::SharpRight: NAV_ARROW(g, 0, col, {-14, 20}, {-14, -8}, {12, 12}); break;
    case NavTurn::SharpLeft: NAV_ARROW(g, 0, col, {14, 20}, {14, -8}, {-12, 12}); break;
    case NavTurn::UTurn:
      NAV_ARROW(g, 0, col, {10, 20}, {10, -6}, {7, -14}, {0, -18}, {-7, -14}, {-10, -6}, {-10, 14});
      break;
    case NavTurn::KeepRight:
      g->drawWideLine(NAV_GLYPH_CX - 4, NAV_GLYPH_CY + 2, NAV_GLYPH_CX - 4, NAV_GLYPH_CY - 21, 2.0f, MUTED);
      NAV_ARROW(g, 0, col, {-4, 20}, {-4, 2}, {12, -18});
      break;
    case NavTurn::KeepLeft:
      g->drawWideLine(NAV_GLYPH_CX + 4, NAV_GLYPH_CY + 2, NAV_GLYPH_CX + 4, NAV_GLYPH_CY - 21, 2.0f, MUTED);
      NAV_ARROW(g, 0, col, {4, 20}, {4, 2}, {-12, -18});
      break;
    case NavTurn::Roundabout: {
      const int cx = NAV_GLYPH_CX, cy = NAV_GLYPH_CY + 4;
      g->fillSmoothCircle(cx, cy, 15, col);
      g->fillSmoothCircle(cx, cy, 10, g->color565(0x0B, 0x0D, 0x12));
      g->fillRect(cx - 2, cy + 14, 5, 6, col);  // the entry
      static const int8_t P[][2] = {{0, -8}, {0, -22}};
      navArrow(g, P, 2, 0.0f, col);
      if (exitNo) {
        char d[4];
        snprintf(d, sizeof(d), "%u", (unsigned)exitNo);
        g->setFont(&V_S12.font);
        g->setTextDatum(middle_center);
        g->setTextColor(col, g->color565(0x0B, 0x0D, 0x12));
        g->drawString(d, cx, cy);
      }
      break;
    }
    case NavTurn::Arrive: {
      const int cx = NAV_GLYPH_CX, cy = NAV_GLYPH_CY;
      g->fillSmoothCircle(cx, cy, 20, col);
      g->fillSmoothCircle(cx, cy, 15, g->color565(0x0B, 0x0D, 0x12));
      const NavPoint chk[3] = {{(int16_t)(cx - 8), (int16_t)cy}, {(int16_t)(cx - 2), (int16_t)(cy + 7)}, {(int16_t)(cx + 9), (int16_t)(cy - 7)}};
      navStroke(g, chk, 3, 5, col);
      break;
    }
    case NavTurn::Rejoin: {
      const float rot = (float)dir * (6.2831853f / 256.0f);
      NAV_ARROW(g, rot, col, {0, 20}, {0, -22});
      break;
    }
  }
}

// The bottom line: street (INK) then the ETA (DIM), the street cut with "..."
// so the pair fits NAV_BOTTOM_MAX_W pixels. An ETA too long to leave the street
// its "..." is cut first, so the line never starts left of the margin.
void drawNavBottom(LovyanGFX *g, const NavScene &s) {
  g->setFont(&V_B18V.font);
  g->setTextDatum(middle_left);
  const int dotsW = g->textWidth("...");
  char eta[32];
  eta[0] = '\0';
  if (s.eta[0]) snprintf(eta, sizeof(eta), "  %s", s.eta);
  size_t en = strlen(eta);
  if (en && dotsW + g->textWidth(eta) > NAV_BOTTOM_MAX_W) {
    while (en > 2) {  // never into the two leading spaces
      en = utf8FitBytes(eta, en - 1);
      eta[en] = '\0';
      if (dotsW + g->textWidth(eta) + dotsW <= NAV_BOTTOM_MAX_W) break;
    }
    memcpy(eta + en, "...", 4);
  }
  const int etaW = eta[0] ? g->textWidth(eta) : 0;
  char street[72];
  snprintf(street, sizeof(street), "%s", s.street);
  size_t n = strlen(street);
  if (g->textWidth(street) + etaW > NAV_BOTTOM_MAX_W) {
    while (n > 0) {
      n = utf8FitBytes(street, n - 1);
      street[n] = '\0';
      if (g->textWidth(street) + dotsW + etaW <= NAV_BOTTOM_MAX_W) break;
    }
    memcpy(street + n, "...", 4);
  }
  const int streetW = g->textWidth(street);
  int x = (LCD_WIDTH - (streetW + etaW)) / 2;
  if (x < (LCD_WIDTH - NAV_BOTTOM_MAX_W) / 2) x = (LCD_WIDTH - NAV_BOTTOM_MAX_W) / 2;
  g->setTextColor(INK, BG);
  g->drawString(street, x, NAV_BOTTOM_Y);
  if (eta[0]) {
    g->setTextColor(DIM, BG);
    g->drawString(eta, x + streetW, NAV_BOTTOM_Y);
  }
}

void drawNavArrived(LovyanGFX *g, const NavScene &s) {
  const int cx = LCD_WIDTH / 2, cy = 92;
  const uint16_t acc = ACCENT;
  g->fillSmoothCircle(cx, cy, 50, acc);
  g->fillSmoothCircle(cx, cy, 42, BG);
  const NavPoint chk[3] = {{(int16_t)(cx - 20), (int16_t)(cy + 2)}, {(int16_t)(cx - 6), (int16_t)(cy + 16)}, {(int16_t)(cx + 22), (int16_t)(cy - 16)}};
  navStroke(g, chk, 3, 10, acc);
  g->setFont(&V_B30.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, BG);
  g->drawString("Arrived", cx, 172);
  if (s.street[0]) {
    g->setFont(&V_B18V.font);
    char street[72];
    snprintf(street, sizeof(street), "%s", s.street);
    size_t n = strlen(street);
    if (g->textWidth(street) > NAV_BOTTOM_MAX_W) {
      const int dotsW = g->textWidth("...");
      while (n > 0) {
        n = utf8FitBytes(street, n - 1);
        street[n] = '\0';
        if (g->textWidth(street) + dotsW <= NAV_BOTTOM_MAX_W) break;
      }
      memcpy(street + n, "...", 4);
    }
    g->setTextColor(DIM, BG);
    g->drawString(street, cx, 210);
  }
}

void draw(Display &lcd, const NavScene &s, bool phoneLost, const BatteryView &bat, int16_t panX, int16_t panY) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  const bool lowBat = !bat.onUsb && bat.pct < 15;
  const uint16_t amber = AMBER;

  if (s.state == NavState::Arrived) {
    drawNavArrived(g, s);
  } else {
    // The frame was built around the pan the phone last heard of: what the
    // finger has moved since is drawn as a shift (nav_pan.h).
    const int ox = panX - s.panX, oy = panY - s.panY;
    // Streets, widest last so junctions of big roads stay clean.
    for (int cls = 3; cls >= 0; cls--) {
      const uint16_t col = to565(NAV_STREET_RGB[cls]);
      for (uint8_t i = 0; i < s.nStreets; i++) {
        if (s.streetCls[i] == cls)
          navPolyline(g, &s.streetPts[s.streetStart[i]], s.streetLen[i], NAV_STREET_W[cls], col, ox, oy);
      }
    }
    const bool lost = s.state != NavState::Navigating;
    if (s.nRoute >= 2) navStroke(g, s.route, s.nRoute, 12, lost ? DIM : to565(0x4DA3FF), ox, oy);

    // The rider: a chevron with a black outline, grey when the GPS is weak. It
    // moves with the dragged map (off screen when dragged far enough).
    const uint16_t arrow = (s.flags & 1) ? DIM : INK;
    const int rx = panX, ry = panY;
    const int tip[2] = {120 + rx, 180 + ry}, bl[2] = {104 + rx, 220 + ry}, nt[2] = {120 + rx, 210 + ry},
              br[2] = {136 + rx, 220 + ry};
    g->fillTriangle(tip[0], tip[1], bl[0], bl[1], nt[0], nt[1], BG);
    g->fillTriangle(tip[0], tip[1], nt[0], nt[1], br[0], br[1], BG);
    g->drawWideLine(tip[0], tip[1], bl[0], bl[1], 3.0f, BG);
    g->drawWideLine(bl[0], bl[1], nt[0], nt[1], 3.0f, BG);
    g->drawWideLine(nt[0], nt[1], br[0], br[1], 3.0f, BG);
    g->drawWideLine(br[0], br[1], tip[0], tip[1], 3.0f, BG);
    g->fillSmoothCircle(tip[0], tip[1], 3, BG);
    g->fillSmoothCircle(bl[0], bl[1], 3, BG);
    g->fillSmoothCircle(br[0], br[1], 3, BG);
    g->fillTriangle(tip[0], tip[1], bl[0], bl[1], nt[0], nt[1], arrow);
    g->fillTriangle(tip[0], tip[1], nt[0], nt[1], br[0], br[1], arrow);

    // The banner.
    const uint16_t bannerBg = to565(0x0B0D12);
    g->fillRect(0, 0, LCD_WIDTH, NAV_BANNER_H, bannerBg);
    drawNavTurn(g, s.turn, s.exit, s.dir, INK);
    g->setTextDatum(middle_left);
    g->setTextColor(INK, bannerBg);
    if (s.state == NavState::Rerouting) {
      g->setFont(&V_B24.font);
      g->drawString("Rerouting", NAV_TEXT_X, 31);
    } else if (s.state == NavState::OffRouteNoSignal) {
      g->setFont(&V_B30.font);
      g->drawString(s.dist, NAV_TEXT_X, 23);
      g->setFont(&V_S12V.font);
      g->setTextColor(amber, bannerBg);
      g->drawString("OFF ROUTE \xC2\xB7 NO SIGNAL", NAV_TEXT_X, 49);
    } else {
      g->setFont(&V_B30.font);
      g->drawString(s.dist, NAV_TEXT_X, 31);
    }
    if (phoneLost) {
      g->setFont(&V_S12V.font);
      g->setTextDatum(middle_right);
      g->setTextColor(amber, bannerBg);
      g->drawString("NO PHONE", 232, 12);
    }

    g->fillRect(0, NAV_BOTTOM_Y - 16, LCD_WIDTH, LCD_HEIGHT - (NAV_BOTTOM_Y - 16), BG);
    drawNavBottom(g, s);
  }

  // Low battery: top right, in the banner's slot; just below NO PHONE when that is up.
  if (lowBat) {
    char txt[8];
    snprintf(txt, sizeof(txt), "%u%%", (unsigned)bat.pct);
    const bool arrived = s.state == NavState::Arrived;
    g->setFont(&V_S12V.font);
    g->setTextDatum(middle_right);
    g->setTextColor(RED, arrived ? BG : to565(0x0B0D12));
    g->drawString(txt, 232, phoneLost && !arrived ? 28 : 12);
  }
  present(lcd);
}

}  // namespace nav
}  // namespace

void uiNav(Display &lcd, const NavScene &s, bool phoneLost, const BatteryView &bat, int16_t panX, int16_t panY) {
  g_animating = false;
  nav::draw(lcd, s, phoneLost, bat, panX, panY);
}

void uiOta(Display &lcd, uint8_t percent) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  constexpr int W = 160, H = 8, X = (LCD_WIDTH - W) / 2, Y = 150;
  drawLabel(g, "updating", LCD_WIDTH / 2, 100, ACCENT, middle_center);

  g->fillRect(X, Y, W, H, LINE);
  const int pct = percent > 100 ? 100 : percent;
  const int fill = W * pct / 100;
  if (fill > 0) g->fillRect(X, Y, fill, H, ACCENT);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  g->setFont(&V_RB24.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, BG);
  g->drawString(buf, LCD_WIDTH / 2, 190);

  drawLabel(g, "do not unplug", LCD_WIDTH / 2, 230, DIM, middle_center);
  present(lcd);
}
