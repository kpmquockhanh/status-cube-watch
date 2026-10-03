#include "ui.h"

#include <math.h>

#include "battery_util.h"
#include "config.h"
#include "fonts_gen.h"
#include "dev_editor.h"
#include "pomo_editor.h"
#include "settings.h"

namespace {

// --- palette -------------------------------------------------------------
// Kept as RGB888 as well as 565 because the threshold crossfade interpolates
// in 888 and converts once per frame; matches bridge/preview.html and the
// design canvas so the mock, the simulator and the panel all agree.
constexpr uint32_t RGB888[7] = {
    0xE7ECF5,  // ink
    0xFF8A5B,  // accent
    0x5B9DFF,  // blue
    0x46D08A,  // green
    0xF5C451,  // amber
    0xB18CFF,  // violet
    0xFF6B6B,  // red
};

uint16_t g_palette[7];
uint16_t DIM, FAINT, INK, PANEL, ACC_POMO, ACC_DEV;
constexpr uint16_t BG = 0x0000;

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
// The combined usage card adds a second, thinner ring inside the first (the 7d
// window), 4px clear of it. Same centre, same 270 degree arc and notches.
constexpr int RING2_R_OUT = 55;
constexpr int RING2_R_IN = 47;
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

// --- text ----------------------------------------------------------------
// Anti-aliased VLW fonts (Inter, rasterised by tools/gen_fonts.py). The built-in
// Adafruit-GFX and Font0 faces are 1-bit and show stair-steps on this panel.
// Each font is parsed once in uiBegin; the glyph bitmaps stay in flash.
struct Vlw {
  lgfx::PointerWrapper data;
  lgfx::VLWfont font;
};
Vlw V_B44, V_B36, V_B30, V_B24, V_B18, V_S12, V_R17;

void loadVlw(Vlw &v, const uint8_t *blob, size_t len) {
  v.data.set(blob, len);
  v.font.loadFont(&v.data);
}

// Tried largest first; the middle of a ring only has ~130x42px to give.
Vlw *const VALUE_FONTS[] = {&V_B44, &V_B36, &V_B30, &V_B24, &V_B18};

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

// The design sets its small labels in uppercase. No tracking, but the
// uppercasing is most of the effect and costs a stack buffer.
void drawCaps(LovyanGFX *g, const char *text, int x, int y, uint16_t color,
              textdatum_t datum) {
  char buf[48];
  size_t i = 0;
  for (; text[i] && i < sizeof(buf) - 1; i++) buf[i] = toupper((unsigned char)text[i]);
  buf[i] = '\0';
  g->setFont(&V_S12.font);
  g->setTextDatum(datum);
  g->setTextColor(color, BG);
  g->drawString(buf, x, y);
}

int capsWidth(LovyanGFX *g, const char *text) {
  g->setFont(&V_S12.font);
  return g->textWidth(text);
}

// --- chrome --------------------------------------------------------------
void drawDots(LovyanGFX *g, uint8_t n, uint8_t active) {
  constexpr int Y = LCD_HEIGHT - 12;
  const int spacing = 11;
  const int total = (n - 1) * spacing;
  for (uint8_t i = 0; i < n; i++) {
    const int x = LCD_WIDTH / 2 - total / 2 + i * spacing;
    g->fillCircle(x, Y, 2, i == active ? INK : FAINT);
  }
}

// Set by uiRender for the frame being drawn; drawTopBar reads it.
UiLink g_link = UiLink::None;

// Battery glyph + "NN%", top-left. Fixed in position: the glyph sits at `leftX`
// and the number starts at a constant offset from it, so neither moves when the
// percentage changes width. Returns the x where the run ends (sized for "100%").
int drawBattery(LovyanGFX *g, int leftX, int y, const BatteryView &bat) {
  const uint16_t col = bat.pct >= 40   ? g_palette[ACC_GREEN]
                       : bat.pct >= 15 ? g_palette[ACC_AMBER]
                                       : g_palette[ACC_RED];
  constexpr int BODY_W = 14, BODY_H = 8, NUB_W = 2, GAP = 4;
  g->drawRect(leftX, y - BODY_H / 2, BODY_W, BODY_H, DIM);
  g->fillRect(leftX + BODY_W, y - 2, NUB_W, 5, DIM);
  const int inner = BODY_W - 4;
  int fill = (inner * bat.pct + 50) / 100;
  if (bat.pct > 0 && fill < 1) fill = 1;
  if (fill > 0) g->fillRect(leftX + 2, y - BODY_H / 2 + 2, fill, BODY_H - 4, col);

  char txt[8];
  snprintf(txt, sizeof(txt), "%u%%", (unsigned)bat.pct);
  g->setFont(&V_S12.font);
  g->setTextDatum(middle_left);
  g->setTextColor(col, BG);
  const int textX = leftX + BODY_W + NUB_W + GAP;
  g->drawString(txt, textX, y);
  return textX + g->textWidth("100%");
}

// The battery sits top-left. `left` is a label for the card, if it has none of
// its own elsewhere (the text cards: the data source); a ring card passes "".
void drawTopBar(LovyanGFX *g, const char *left, bool online, uint32_t ageMs,
                const BatteryView &bat) {
  // The panel has rounded corners (~35px radius), so the bar is inset from the
  // edges; at the old 12px margin the text and status dot were clipped.
  constexpr int Y = 24;
  constexpr int LEFT_X = 36;

  // Freshness beats a clock here: the board has no NTP sync in this sketch,
  // and what matters is whether the number on screen is current. "OFF" rather
  // than "OFFLINE" so the readout never outgrows the slot reserved for it.
  char right[16];
  const uint32_t s = ageMs / 1000;
  if (!online) snprintf(right, sizeof(right), "OFF");
  else if (s < 60) snprintf(right, sizeof(right), "%lus", (unsigned long)s);
  else snprintf(right, sizeof(right), "%lum", (unsigned long)(s / 60));

  const uint16_t col = online && s < 30 ? g_palette[ACC_GREEN] : g_palette[ACC_RED];
  g->setFont(&V_S12.font);
  g->setTextDatum(middle_right);
  g->setTextColor(col, BG);
  g->drawString(right, LCD_WIDTH - 46, Y);
  g->fillCircle(LCD_WIDTH - 36, Y, 3, col);

  const int batRight = drawBattery(g, LEFT_X, Y, bat);

  // The label gets what is left between the battery and the age readout, in
  // capitals as drawCaps draws it; trim it rather than run into either.
  char name[48];
  size_t n = 0;
  for (; left[n] && n < sizeof(name) - 1; n++) name[n] = toupper((unsigned char)left[n]);
  name[n] = '\0';
  int ageW = g->textWidth("59s");
  if (g->textWidth("99m") > ageW) ageW = g->textWidth("99m");
  if (g->textWidth("OFF") > ageW) ageW = g->textWidth("OFF");

  // Transport marker just left of the age readout. The slot is always sized for
  // "WIFI" so the label's room does not change when the link switches.
  const int linkW = g->textWidth("WIFI");
  const int linkRight = LCD_WIDTH - 46 - ageW - 8;
  if (online && g_link != UiLink::None) {
    g->setFont(&V_S12.font);
    g->setTextDatum(middle_right);
    g->setTextColor(DIM, BG);
    g->drawString(g_link == UiLink::Ble ? "BLE" : "WIFI", linkRight, Y);
  }

  const int x0 = batRight + 10;
  const int maxW = linkRight - linkW - 6 - x0;
  for (; n > 1 && capsWidth(g, name) > maxW; n--) name[n - 1] = '\0';
  if (n) drawCaps(g, name, x0, Y, DIM, middle_left);
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
constexpr uint32_t RING_TRACK = 0x2A3242;

// One precomputed ring: its radii and the pixels they cover.
struct RingTable {
  int rOut, rIn;
  RingPx *px;
  int n;
  float pxPerDeg() const { return (rOut + rIn) * 0.5f * 0.01745329252f; }  // at the band's middle
};
RingTable g_ringOuter = {RING_R_OUT, RING_R_IN, nullptr, 0};
RingTable g_ringInner = {RING2_R_OUT, RING2_R_IN, nullptr, 0};

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

uint16_t pack565(uint32_t rgb, float scale) {
  return __builtin_bswap16(lgfx::color565((uint8_t)(((rgb >> 16) & 0xFF) * scale),
                                          (uint8_t)(((rgb >> 8) & 0xFF) * scale),
                                          (uint8_t)((rgb & 0xFF) * scale)));
}

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
    // `base` was cached with the notches cut out; without them it is rebuilt.
    uint16_t c = notches ? p.base : pack565(RING_TRACK, p.covNo * (1.0f / 255.0f));
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

// How one ring card is dressed. Payload cards take their colour from the
// payload and show everything; the Pomodoro card picks its own colour, has no
// percentage (the ring is time left, not a limit) and no thresholds.
struct RingStyle {
  uint32_t rgb;   // arc colour
  bool pct;       // "NN%" and its label in the gap at the bottom; off = the label alone
  bool notches;   // cuts at the amber/red thresholds
  float flash;    // 0..1 blend of the arc towards white (the Pomodoro phase-end pulse)
};

RingStyle ringStyle(uint32_t rgb) {
  RingStyle s;
  s.rgb = rgb;
  s.pct = true;
  s.notches = true;
  s.flash = 0.0f;
  return s;
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
void drawGaugeCard(LovyanGFX *g, const Card &card, const RingStyle &style,
                   ValueFont valueFont, GaugeAnim &a, uint32_t now,
                   bool online, uint32_t ageMs, const BatteryView &bat) {
  const bool hasReading = card.gauge >= 0;
  const uint32_t rgb = style.rgb;

  uint32_t shownRgb;
  stepGauge(a, hasReading ? card.gauge : 0.0f, rgb, now, shownRgb);

  drawTopBar(g, "", online, ageMs, bat);
  const uint32_t ringRgb = style.flash > 0.0f ? lerpRgb(shownRgb, 0xFFFFFF, style.flash) : shownRgb;
  drawRing(g, g_ringOuter, a.shown, ringRgb, hasReading, style.notches);

  // Inside the ring, in the size the whole deck agreed on.
  drawIn(g, valueFont, card.value, RING_CX, VALUE_CY, hasReading ? INK : FAINT);
  if (card.sub1[0]) drawCaps(g, card.sub1, RING_CX, CAPTION_Y, DIM, top_center);

  // In the gap at the bottom: "14%  OF 5H LIMIT", laid out as one centred run
  // so the two type sizes stay optically joined.
  if (hasReading && style.pct) {
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)lroundf(a.shown));
    g->setFont(&V_B18.font);
    const int wPct = g->textWidth(pct);
    const int wLab = card.sub2[0] ? capsWidth(g, card.sub2) : 0;
    const int gap = wLab ? 6 : 0;
    const int x0 = RING_CX - (wPct + gap + wLab) / 2;

    g->setFont(&V_B18.font);
    g->setTextDatum(middle_left);
    g->setTextColor(to565(shownRgb), BG);
    g->drawString(pct, x0, PCT_Y);
    if (wLab) drawCaps(g, card.sub2, x0 + wPct + gap, PCT_Y, DIM, middle_left);
  } else if (card.sub2[0]) {
    drawCaps(g, card.sub2, RING_CX, PCT_Y - 5, DIM, top_center);
  }

  drawCaps(g, card.title, RING_CX, NAME_Y, DIM, middle_center);
}

// The combined usage card: both rate-limit windows as concentric rings (outer =
// the 5h session, inner = the 7d week), the session countdown in the middle,
// the unread-mail badge in the gap the arcs leave at the bottom, and one legend
// line per window under them. Rings animate independently, like a gauge card's.
constexpr int DUAL_VALUE_CY = RING_CY - 10;
constexpr int DUAL_CAPTION_Y = RING_CY + 9;
constexpr int DUAL_VALUE_MAX_W = 80;  // between the inner ring's edges at the value's height
constexpr int DUAL_VALUE_MAX_H = 30;
constexpr int MAIL_CY = RING_CY + 48;
constexpr int ROW1_Y = RING_CY + 86;
constexpr int ROW2_Y = RING_CY + 108;
constexpr int ROW_LEFT = 34;
constexpr int ROW_RIGHT = LCD_WIDTH - 34;

void drawEnvelope(LovyanGFX *g, int x, int y, uint16_t col) {
  g->drawRoundRect(x, y, 16, 11, 2, col);
  g->drawLine(x + 2, y + 2, x + 8, y + 6, col);  // flap
  g->drawLine(x + 13, y + 2, x + 8, y + 6, col);
}

void drawLegendRow(LovyanGFX *g, const LegendRow &row, int y, uint32_t swatch) {
  g->fillRoundRect(ROW_LEFT, y - 3, 7, 7, 2, to565(swatch));
  drawCaps(g, row.key, ROW_LEFT + 13, y, DIM, middle_left);
  g->setFont(&V_B18.font);
  g->setTextDatum(middle_left);
  g->setTextColor(INK, BG);
  g->drawString(row.pct, ROW_LEFT + 38, y);
  if (row.reset[0]) drawCaps(g, row.reset, ROW_RIGHT, y, DIM, middle_right);
}

void drawDualCard(LovyanGFX *g, const Card &card, GaugeAnim &outer, GaugeAnim &inner,
                  uint32_t now, bool online, uint32_t ageMs, const BatteryView &bat) {
  const bool has1 = card.gauge >= 0;
  const bool has2 = card.gauge2 >= 0;
  uint32_t rgb1, rgb2;
  stepGauge(outer, has1 ? card.gauge : 0.0f, RGB888[card.color % 7], now, rgb1);
  stepGauge(inner, has2 ? card.gauge2 : 0.0f, RGB888[card.color2 % 7], now, rgb2);

  drawTopBar(g, "", online, ageMs, bat);
  drawRing(g, g_ringOuter, outer.shown, rgb1, has1, true);
  drawRing(g, g_ringInner, inner.shown, rgb2, has2, true);

  const ValueFont vf = fitFont(g, card.value, DUAL_VALUE_MAX_W, DUAL_VALUE_MAX_H);
  drawIn(g, vf, card.value, RING_CX, DUAL_VALUE_CY, has1 ? INK : FAINT);
  if (card.sub1[0]) drawCaps(g, card.sub1, RING_CX, DUAL_CAPTION_Y, DIM, top_center);

  if (card.mail[0]) {
    const uint16_t col = to565(RGB888[card.mailColor % 7]);
    g->setFont(&V_B18.font);
    const int run = 16 + 6 + g->textWidth(card.mail);
    const int x0 = RING_CX - run / 2;
    drawEnvelope(g, x0, MAIL_CY - 5, col);
    g->setTextDatum(middle_left);
    g->setTextColor(col, BG);
    g->drawString(card.mail, x0 + 22, MAIL_CY);
  }

  if (card.nRows > 0) drawLegendRow(g, card.rows[0], ROW1_Y, rgb1);
  if (card.nRows > 1) drawLegendRow(g, card.rows[1], ROW2_Y, rgb2);
}

// A text card: the original layout, still used by the optional spend deck,
// which has no percentage of anything to draw a ring from.
void drawTextCard(LovyanGFX *g, const Payload &p, const Card &card, uint16_t color,
                  bool online, uint32_t ageMs, const BatteryView &bat) {
  (void)p;
  drawTopBar(g, "", online, ageMs, bat);

  drawCaps(g, card.title, LCD_WIDTH / 2, 60, DIM, top_center);
  drawFitted(g, card.value, LCD_WIDTH / 2, 145, LCD_WIDTH - 20, 96, color);

  g->setFont(&V_R17.font);
  g->setTextDatum(top_center);
  g->setTextColor(INK, BG);
  g->drawString(card.sub1, LCD_WIDTH / 2, 202);

  drawCaps(g, card.sub2, LCD_WIDTH / 2, 230, DIM, top_center);
}

// --- the Pomodoro card ---------------------------------------------------
// The timer lives on the device, so this is the one card whose text the
// firmware formats itself (MM:SS). It reuses the gauge layout: the ring is the
// time left in the phase, the middle is the clock, and the line in the gap at
// the bottom is "sessions done  +  what a double tap does".
constexpr uint32_t POMO_MUTED = 0x7C8598;  // idle / paused: the ring waits, so it is grey

GaugeAnim g_pomoAnim;

const char *phaseName(PomoPhase p) {
  return p == PHASE_FOCUS ? "Focus" : (p == PHASE_SHORT ? "Break" : "Long break");
}

uint32_t phaseRgb(PomoPhase p) { return p == PHASE_FOCUS ? RGB888[ACC_AMBER] : RGB888[ACC_GREEN]; }

void pomodoroCard(const PomoView &v, Card &card, RingStyle &style) {
  char time[8];
  pomoFormatTime(v.displaySec, time, sizeof(time));
  char count[8];
  snprintf(count, sizeof(count), "%u/%u", (unsigned)v.completed, (unsigned)v.sessions);

  strlcpy(card.title, "Pomodoro", sizeof(card.title));
  strlcpy(card.value, time, sizeof(card.value));
  card.gauge = (int8_t)v.fraction;

  const char *hint = "";
  switch (v.state) {
    case POMO_IDLE:
      strlcpy(card.sub1, "Ready", sizeof(card.sub1));
      hint = "2 taps: start";
      style.rgb = POMO_MUTED;
      break;
    case POMO_FOCUS:
      strlcpy(card.sub1, "Focus", sizeof(card.sub1));
      hint = "2 taps: pause";
      style.rgb = phaseRgb(PHASE_FOCUS);
      break;
    case POMO_BREAK:
      strlcpy(card.sub1, phaseName(v.phase), sizeof(card.sub1));
      hint = "2 taps: pause";
      style.rgb = phaseRgb(v.phase);
      break;
    case POMO_PAUSED:
      strlcpy(card.sub1, "Paused", sizeof(card.sub1));
      hint = "2 taps: resume";
      style.rgb = POMO_MUTED;
      break;
    case POMO_DONE:
      strlcpy(card.value, "Done", sizeof(card.value));
      card.gauge = 100;  // a finished ring: it is what the alert flash pulses
      snprintf(card.sub1, sizeof(card.sub1), "%s done", phaseName(v.phase));
      hint = v.next == PHASE_FOCUS ? "2 taps: focus"
                                   : (v.next == PHASE_SHORT ? "2 taps: break" : "2 taps: long break");
      style.rgb = phaseRgb(v.phase);
      break;
  }
  snprintf(card.sub2, sizeof(card.sub2), "%s  %s", count, hint);
  style.pct = false;      // the ring is time left, not a share of a limit
  style.notches = false;  // and has no thresholds
}

void drawPomodoroCard(LovyanGFX *g, const PomoView &v, float flash, bool online, uint32_t ageMs,
                       const BatteryView &bat) {
  Card card{};
  RingStyle style = ringStyle(POMO_MUTED);
  pomodoroCard(v, card, style);
  style.flash = flash;
  // Sized on its own: it never nudges the font the payload cards agreed on.
  drawGaugeCard(g, card, style, fitFont(g, "88:88", VALUE_MAX_W, VALUE_MAX_H), g_pomoAnim, millis(), online, ageMs, bat);
}

// --- the settings sheets -----------------------------------------------------
// Both the Pomodoro editor (rises from the bottom) and the display panel (drops
// from the top) are sheets: a lifted background with rounded corners on the edge
// facing the card, a grab handle on that edge, an icon + title, hairline-divided
// rows of  LABEL ... ( - ) value ( + ),  and a RESET / DONE bar. Each has its own
// accent. Geometry comes from pomo_editor.cpp so drawing and hit-testing agree.
constexpr int STEP_R = 12;      // stepper circle radius
constexpr int STEP_MINUS_CX = 124;
constexpr int STEP_PLUS_CX = 216;
constexpr int VALUE_CX = 170;
constexpr int ROW_PAD = 14;     // side inset of the labels and dividers
constexpr int SHEET_R = LCD_CORNER_R;  // matches the glass, so the sheet sits flush at rest

void drawStepper(LovyanGFX *g, int cx, int cy, bool plus, uint16_t accent) {
  (void)accent;
  g->fillCircle(cx, cy, STEP_R, FAINT);
  g->fillRect(cx - 5, cy - 1, 11, 2, INK);
  if (plus) g->fillRect(cx - 1, cy - 5, 2, 11, INK);
}

// One row: [LABEL ...... ( - ) value ( + )], with an optional meter (0..100)
// under the label and a hairline above (not on the first row).
void drawSheetRow(LovyanGFX *g, int r, const char *label, const char *value, int meterPct,
                  uint16_t accent, int yOff) {
  const EditRect row = editorRow(r);
  const int top = row.y + yOff;
  const int cy = top + row.h / 2;
  if (r > 0) g->drawFastHLine(ROW_PAD, top, LCD_WIDTH - 2 * ROW_PAD, FAINT);
  const int labelY = meterPct >= 0 ? cy - 5 : cy;
  drawCaps(g, label, ROW_PAD, labelY, DIM, middle_left);
  if (meterPct >= 0) {
    constexpr int MW = 70;
    g->fillRoundRect(ROW_PAD, cy + 7, MW, 3, 1, FAINT);
    const int fill = MW * meterPct / 100;
    if (fill > 0) g->fillRoundRect(ROW_PAD, cy + 7, fill < 3 ? 3 : fill, 3, 1, accent);
  }
  drawStepper(g, STEP_MINUS_CX, cy, false, accent);
  drawStepper(g, STEP_PLUS_CX, cy, true, accent);
  g->setFont(&V_B18.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, PANEL);
  g->drawString(value, VALUE_CX, cy);
}

void drawSheetBar(LovyanGFX *g, uint16_t accent, int yOff) {
  const EditRect reset = editorResetBtn();
  const EditRect done = editorDoneBtn();
  g->setFont(&V_B18.font);
  g->setTextDatum(middle_center);
  g->setTextColor(DIM, PANEL);
  g->drawString("RESET", reset.x + reset.w / 2, reset.y + reset.h / 2 + yOff);
  g->fillRoundRect(done.x, done.y + 5 + yOff, done.w, done.h - 10, (done.h - 10) / 2, accent);
  g->setTextColor(BG, accent);
  g->drawString("DONE", done.x + done.w / 2, done.y + done.h / 2 + yOff);
}

// The sheet body, `yOff` px down from its resting place so it can slide. `atTop`
// marks the panel that drops in from the top: its rounded edge and handle are at
// the bottom. The outline is only drawn while sliding: at rest the sheet is the
// whole screen and the glass supplies the corners, so an arc of our own radius
// could only disagree with the bezel.
void drawSheetFrame(LovyanGFX *g, int yOff, bool atTop, uint16_t accent) {
  if (atTop) {
    g->fillRoundRect(0, yOff - SHEET_R - 2, LCD_WIDTH, LCD_HEIGHT + SHEET_R + 2, SHEET_R, PANEL);
    if (yOff) g->drawRoundRect(0, yOff - SHEET_R - 2, LCD_WIDTH, LCD_HEIGHT + SHEET_R + 2, SHEET_R, FAINT);
    g->fillRoundRect(LCD_WIDTH / 2 - 16, yOff + LCD_HEIGHT - 8, 32, 4, 2, FAINT);
  } else {
    g->fillRoundRect(0, yOff, LCD_WIDTH, LCD_HEIGHT + SHEET_R + 2, SHEET_R, PANEL);
    if (yOff) g->drawRoundRect(0, yOff, LCD_WIDTH, LCD_HEIGHT + SHEET_R + 2, SHEET_R, FAINT);
    g->fillRoundRect(LCD_WIDTH / 2 - 16, yOff + 7, 32, 4, 2, FAINT);
  }
  (void)accent;
}

void drawSheetTitle(LovyanGFX *g, const char *title, bool sun, uint16_t accent, int yOff) {
  const int cy = (sun ? 22 : 26) + yOff;
  constexpr int IX = ROW_PAD + 7;
  if (sun) {
    g->fillCircle(IX, cy, 4, accent);
    for (int i = 0; i < 8; i++) {
      const float a = i * 0.7853982f;
      g->drawLine(IX + (int)lroundf(cosf(a) * 6.5f), cy + (int)lroundf(sinf(a) * 6.5f),
                  IX + (int)lroundf(cosf(a) * 8.5f), cy + (int)lroundf(sinf(a) * 8.5f), accent);
    }
  } else {
    g->fillCircle(IX, cy + 1, 7, accent);
    g->drawLine(IX, cy - 6, IX + 3, cy - 9, g_palette[ACC_GREEN]);
    g->drawLine(IX + 1, cy - 6, IX + 4, cy - 9, g_palette[ACC_GREEN]);
  }
  g->setFont(&V_B18.font);
  g->setTextDatum(middle_left);
  g->setTextColor(accent, PANEL);
  char buf[16];
  size_t i = 0;
  for (; title[i] && i < sizeof(buf) - 1; i++) buf[i] = toupper((unsigned char)title[i]);
  buf[i] = '\0';
  g->drawString(buf, IX + 16, cy);
}

void drawEditorPanel(LovyanGFX *g, const PomoSettings &s, int yOff) {
  drawSheetFrame(g, yOff, false, ACC_POMO);
  drawSheetTitle(g, "Pomodoro", false, ACC_POMO, yOff + 4);
  const uint8_t vals[EDIT_ROWS] = {s.focusMin, s.shortMin, s.longMin, s.sessions};
  for (int r = 0; r < EDIT_ROWS; r++) {
    char buf[12];
    if (r == EDIT_ROWS - 1) snprintf(buf, sizeof(buf), "%u", (unsigned)vals[r]);
    else snprintf(buf, sizeof(buf), "%u min", (unsigned)vals[r]);
    drawSheetRow(g, r, editorLabel(r), buf, -1, ACC_POMO, yOff);
  }
  drawSheetBar(g, ACC_POMO, yOff);
}

void drawDevicePanel(LovyanGFX *g, const DeviceSettings &s, int yOff) {
  drawSheetFrame(g, yOff, true, ACC_DEV);
  drawSheetTitle(g, "Display", true, ACC_DEV, yOff + 4);
  for (int r = 0; r < DEV_EDIT_ROWS; r++) {
    char buf[12];
    devEditorValue(r, s, buf, sizeof(buf));
    drawSheetRow(g, r, devEditorLabel(r), buf, r == 0 ? (int)s.backlight * 100 / 255 : -1,
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

// Slide of the editor over the Pomodoro card: 0 = hidden below the screen,
// 1 = fully open. Ease-out going up, ease-in coming back down.
constexpr uint32_t EDITOR_SLIDE_MS = 260;
bool g_edSliding = false;
bool g_edOpening = false;
bool g_edFromTop = false;  // the display panel drops in from the top; the Pomodoro one rises from the bottom
DeviceSettings g_edDevice{};
uint32_t g_edStart = 0;
PomoSettings g_edSettings{};

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

// --- phase-end alert -----------------------------------------------------
// The board has no speaker, so the end of a phase is a few pulses of the ring
// colour towards white and of the backlight, then back to normal.
constexpr uint32_t ALERT_MS = 2000;
constexpr int ALERT_PULSES = 3;
constexpr float ALERT_RING_MIX = 0.85f;  // how far the ring goes towards white at a peak
uint32_t g_alertStart = 0;
bool g_alertOn = false;

// 0 at the start and end, 1 at each of ALERT_PULSES peaks in between.
float alertPulse(uint32_t now) {
  const float t = (float)(now - g_alertStart) / (float)ALERT_MS;
  return 0.5f - 0.5f * cosf(6.2831853f * ALERT_PULSES * t);
}

}  // namespace

void uiBegin(Display &lcd) {
  for (int i = 0; i < 7; i++) g_palette[i] = to565(RGB888[i]);
  INK = g_palette[ACC_INK];
  DIM = to565(0x7C8598);
  FAINT = to565(0x2A3242);
  PANEL = BG;  // drawCaps paints text on BG, so the sheet shares it
  ACC_POMO = to565(0xFF6A4D);  // tomato
  ACC_DEV = to565(0x4DA3FF);   // sky

  // 240x280x16bpp is 134 KB. It fits in internal RAM on an S3 and is much
  // faster there than in PSRAM, so try internal first and fall back to
  // drawing straight to the panel if the allocation fails.
  loadVlw(V_B44, font_b44, sizeof(font_b44));
  loadVlw(V_B36, font_b36, sizeof(font_b36));
  loadVlw(V_B30, font_b30, sizeof(font_b30));
  loadVlw(V_B24, font_b24, sizeof(font_b24));
  loadVlw(V_B18, font_b18, sizeof(font_b18));
  loadVlw(V_S12, font_s12, sizeof(font_s12));
  loadVlw(V_R17, font_r17, sizeof(font_r17));

  g_canvas.setColorDepth(16);
  g_sprite = g_canvas.createSprite(LCD_WIDTH, LCD_HEIGHT) != nullptr;
  buildRingTable(g_ringOuter);
  buildRingTable(g_ringInner);
  Serial.printf("[ui] framebuffer: %s\n", g_sprite ? "sprite (double buffered)" : "direct");
}

bool uiAnimating() { return g_animating; }

void uiReplay(uint8_t index) {
  if (index < MAX_CARDS) {
    g_anim[index].seen = false;
    g_anim2[index].seen = false;
  }
}

void uiReplayPomodoro() { g_pomoAnim.seen = false; }

void uiAlertStart() {
  g_alertStart = millis();
  g_alertOn = true;
}

uint8_t uiDeckSize(const Payload &p) { return (p.valid ? p.nCards : 0) + 1; }

uint8_t uiPomodoroIndex(const Payload &p) { return p.valid ? p.nCards : 0; }

void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const PomoView &pomo, const BatteryView &bat, UiLink link) {
  g_animating = false;
  g_link = link;

  float flash = 0.0f;
  if (g_alertOn) {
    const uint32_t now = millis();
    if (now - g_alertStart >= ALERT_MS) {
      g_alertOn = false;
      lcd.setBrightness(deviceSettings().backlight);
    } else {
      flash = alertPulse(now);
      const uint8_t base = deviceSettings().backlight;
      lcd.setBrightness((uint8_t)(base + (255 - base) * flash));
      g_animating = true;
    }
  }

  const uint8_t deck = uiDeckSize(p);
  const uint8_t i = index % deck;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  if (i == uiPomodoroIndex(p)) {
    drawPomodoroCard(g, pomo, flash * ALERT_RING_MIX, online, ageMs, bat);
  } else {
    const Card &card = p.cards[i];
    if (card.gauge >= GAUGE_BLANK && card.gauge2 >= GAUGE_BLANK) {
      drawDualCard(g, card, g_anim[i], g_anim2[i], millis(), online, ageMs, bat);
    } else if (card.gauge >= GAUGE_BLANK) {
      // VALUE_MAX_W is what fits between the ring's inner edges at the value's
      // height; the size is settled across the deck, not per card.
      const ValueFont valueFont = fitDeck(g, p, VALUE_MAX_W, VALUE_MAX_H);
      drawGaugeCard(g, card, ringStyle(RGB888[card.color % 7]), valueFont, g_anim[i],
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
      if (g_edFromTop) {
        dimCard(g, LCD_HEIGHT - off, LCD_HEIGHT, pos);
        drawDevicePanel(g, g_edDevice, -off);
      } else {
        dimCard(g, 0, off, pos);
        drawEditorPanel(g, g_edSettings, off);
      }
    }
    // The last frame of the slide stays animating so it is pushed before the
    // static editor (or the plain card) takes over.
    g_animating = true;
  }

  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}

void uiDeviceSlide(bool open, const DeviceSettings &s) {
  g_edDevice = s;
  g_edFromTop = true;
  g_edOpening = open;
  g_edSliding = true;
  g_edStart = millis();
}

void uiDeviceEditor(Display &lcd, const DeviceSettings &s) {
  g_animating = false;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  drawDevicePanel(g, s, 0);
  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}

void uiEditorSlide(bool open, const PomoSettings &s) {
  g_edFromTop = false;
  g_edSettings = s;
  g_edOpening = open;
  g_edSliding = true;
  g_edStart = millis();
}

bool uiEditorSliding() { return g_edSliding; }

void uiMessage(Display &lcd, const char *title, const char *body) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  g->setTextDatum(middle_center);
  g->setFont(&V_B24.font);
  g->setTextColor(g_palette[ACC_ACCENT], BG);
  g->drawString(title, LCD_WIDTH / 2, LCD_HEIGHT / 2 - 16);
  g->setFont(&V_S12.font);
  g->setTextColor(DIM, BG);
  g->drawString(body, LCD_WIDTH / 2, LCD_HEIGHT / 2 + 12);
  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}

void uiPomodoroEditor(Display &lcd, const PomoSettings &s) {
  g_animating = false;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  drawEditorPanel(g, s, 0);
  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}

void uiPortal(Display &lcd, const char *apName, const char *apIp, const char *lanIp) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  drawCaps(g, "WiFi setup", LCD_WIDTH / 2, 30, g_palette[ACC_ACCENT], middle_center);

  // A WiFi QR: phones offer to join the (open) network when it is scanned.
  // Version 3 is 29 modules, so 116px is a whole 4px per module. The white
  // plate gives the code its quiet zone against the black background.
  char join[64];
  snprintf(join, sizeof(join), "WIFI:T:nopass;S:%s;;", apName);
  constexpr int QR = 116;
  const int qx = (LCD_WIDTH - QR) / 2;
  const int qy = 50;
  g->fillRoundRect(qx - 6, qy - 6, QR + 12, QR + 12, 6, (uint16_t)0xFFFF);
  g->qrcode(join, qx, qy, QR, 3);

  g->setFont(&V_B18.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, BG);
  g->drawString(apName, LCD_WIDTH / 2, 190);

  drawCaps(g, "Join, then open", LCD_WIDTH / 2, 214, DIM, middle_center);
  g->setFont(&V_S12.font);
  g->setTextColor(DIM, BG);
  g->drawString(apIp, LCD_WIDTH / 2, 234);
  if (lanIp && lanIp[0]) {
    char lan[40];
    snprintf(lan, sizeof(lan), "or on WiFi %s", lanIp);
    g->drawString(lan, LCD_WIDTH / 2, 252);
  }

  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}

void uiBlePair(Display &lcd, uint32_t passkey) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  drawCaps(g, "Pair with Mac", LCD_WIDTH / 2, 40, g_palette[ACC_ACCENT], middle_center);

  g->setTextDatum(middle_center);
  if (passkey) {
    char code[8];
    snprintf(code, sizeof(code), "%06u", (unsigned)passkey);
    g->setFont(&V_B24.font);
    g->setTextColor(INK, BG);
    g->drawString(code, LCD_WIDTH / 2, 118);
    drawCaps(g, "Type this on your Mac", LCD_WIDTH / 2, 156, DIM, middle_center);
  } else {
    g->setFont(&V_B18.font);
    g->setTextColor(INK, BG);
    g->drawString("Waiting for a Mac", LCD_WIDTH / 2, 112);
    drawCaps(g, "Open Claude Cube Link", LCD_WIDTH / 2, 150, DIM, middle_center);
  }

  g->setFont(&V_S12.font);
  g->setTextColor(DIM, BG);
  g->drawString("Stuck? Forget Claude Cube", LCD_WIDTH / 2, 226);
  g->drawString("in Mac Bluetooth settings", LCD_WIDTH / 2, 244);

  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}

void uiOta(Display &lcd, uint8_t percent) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  constexpr int W = 160, H = 8, X = (LCD_WIDTH - W) / 2, Y = 150;
  drawCaps(g, "Updating", LCD_WIDTH / 2, 100, g_palette[ACC_ACCENT], middle_center);

  g->fillRoundRect(X, Y, W, H, H / 2, FAINT);
  const int pct = percent > 100 ? 100 : percent;
  const int fill = W * pct / 100;
  // Never narrower than the bar is tall, or the rounded ends degenerate.
  if (fill > 0) g->fillRoundRect(X, Y, fill < H ? H : fill, H, H / 2, g_palette[ACC_ACCENT]);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);
  g->setFont(&V_B24.font);
  g->setTextDatum(middle_center);
  g->setTextColor(INK, BG);
  g->drawString(buf, LCD_WIDTH / 2, 190);

  drawCaps(g, "Do not unplug", LCD_WIDTH / 2, 230, DIM, middle_center);
  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}
