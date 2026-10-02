#include "ui.h"

#include <math.h>

#include "battery_util.h"
#include "config.h"
#include "fonts_gen.h"
#include "pomo_editor.h"

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
uint16_t DIM, FAINT, INK;
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
// up by a swipe or tap (uiReplay), so the ring visibly fills each time you
// look at it; periodic redraws and data updates only retarget it.
GaugeAnim g_anim[MAX_CARDS];

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
    if (p.cards[i].gauge < GAUGE_BLANK) continue;  // text cards size themselves
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
  const int x0 = batRight + 10;
  const int maxW = LCD_WIDTH - 46 - ageW - 6 - x0;
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
constexpr float RING_PX_PER_DEG = 1.16f;  // ~radius x pi/180 over the 60..73px band
RingPx *g_ringPx = nullptr;
int g_ringN = 0;

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

uint16_t pack565(uint32_t rgb, float scale) {
  return __builtin_bswap16(lgfx::color565((uint8_t)(((rgb >> 16) & 0xFF) * scale),
                                          (uint8_t)(((rgb >> 8) & 0xFF) * scale),
                                          (uint8_t)((rgb & 0xFF) * scale)));
}

// Everything that does not depend on the current percentage or colour --
// coverage from the radii, end caps and notches -- is worked out once here.
void buildRingTable() {
  constexpr float DEG = 0.01745329252f;
  const int rMax = RING_R_OUT + 1;
  const int rMin = RING_R_IN - 1;
  for (int pass = 0; pass < 2; pass++) {
    int n = 0;
    for (int dy = -rMax; dy <= rMax; dy++) {
      for (int dx = -rMax; dx <= rMax; dx++) {
        const float r = sqrtf((float)(dx * dx + dy * dy));
        if (r > rMax || r < rMin) continue;
        const float covR = fminf(clamp01(RING_R_OUT + 0.5f - r), clamp01(r - RING_R_IN + 0.5f));
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
          RingPx &p = g_ringPx[n];
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
      g_ringPx = (RingPx *)malloc(sizeof(RingPx) * n);
      if (!g_ringPx) return;
      g_ringN = n;
    }
  }
}

void drawRing(LovyanGFX *g, float pct, uint32_t rgb, bool filled, bool notches) {
  if (!g_ringPx) return;
  const float fillEnd = filled ? ARC_SWEEP * (pct / 100.0f) : -10.0f;
  // Past this the fill (and its AA edge) cannot reach: use the cached pixel.
  const int past = (int)((fillEnd + 1.5f) * 16.0f);
  uint16_t *buf = g_sprite ? (uint16_t *)g_canvas.getBuffer() : nullptr;

  for (int i = 0; i < g_ringN; i++) {
    const RingPx &p = g_ringPx[i];
    const uint8_t cov = notches ? p.cov : p.covNo;
    // `base` was cached with the notches cut out; without them it is rebuilt.
    uint16_t c = notches ? p.base : pack565(RING_TRACK, p.covNo * (1.0f / 255.0f));
    if (p.d <= past) {
      const float d = p.d * (1.0f / 16.0f);
      const float t = clamp01((fillEnd - d) * RING_PX_PER_DEG + 0.5f) * (p.fs * (1.0f / 255.0f));
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

// --- the two card layouts ------------------------------------------------
// A gauge card: the ring is the number, the middle of the ring is the one
// thing a ring cannot show (time left), and the exact percentage drops to a
// single line in the gap at the bottom.
void drawGaugeCard(LovyanGFX *g, const Card &card, const RingStyle &style,
                   ValueFont valueFont, GaugeAnim &a, uint32_t now,
                   bool online, uint32_t ageMs, const BatteryView &bat) {
  const bool hasReading = card.gauge >= 0;
  const uint32_t rgb = style.rgb;

  if (!a.seen) {
    a.seen = true;
    a.from = 0.0f;
    a.to = hasReading ? card.gauge : 0.0f;
    a.start = now;
    a.dur = SWEEP_MS;
    a.colFrom = a.colTo = rgb;
    a.colStart = now - COLOR_MS;
  } else {
    const float want = hasReading ? card.gauge : 0.0f;
    if (fabsf(want - a.to) > 0.01f) {
      a.from = a.shown;
      a.to = want;
      a.start = now;
      a.dur = RETARGET_MS;
    }
  }

  const float p = progress(now, a.start, a.dur);
  a.shown = a.from + (a.to - a.from) * easeOut(p);

  const float cp = progress(now, a.colStart, COLOR_MS);
  const uint32_t shownRgb = lerpRgb(a.colFrom, a.colTo, easeOut(cp));
  if (rgb != a.colTo) {
    // Retarget the colour from wherever the crossfade currently is, the same
    // way the arc retargets, so a second threshold crossed mid-fade does not
    // snap back to the old hue first.
    a.colFrom = shownRgb;
    a.colTo = rgb;
    a.colStart = now;
  }
  if (p < 1.0f || cp < 1.0f) g_animating = true;

  drawTopBar(g, "", online, ageMs, bat);
  const uint32_t ringRgb = style.flash > 0.0f ? lerpRgb(shownRgb, 0xFFFFFF, style.flash) : shownRgb;
  drawRing(g, a.shown, ringRgb, hasReading, style.notches);

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
// the bottom is "sessions done  +  what a hold does".
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
      hint = "Hold to start";
      style.rgb = POMO_MUTED;
      break;
    case POMO_FOCUS:
      strlcpy(card.sub1, "Focus", sizeof(card.sub1));
      hint = "Hold to pause";
      style.rgb = phaseRgb(PHASE_FOCUS);
      break;
    case POMO_BREAK:
      strlcpy(card.sub1, phaseName(v.phase), sizeof(card.sub1));
      hint = "Hold to pause";
      style.rgb = phaseRgb(v.phase);
      break;
    case POMO_PAUSED:
      strlcpy(card.sub1, "Paused", sizeof(card.sub1));
      hint = "Hold to resume";
      style.rgb = POMO_MUTED;
      break;
    case POMO_DONE:
      strlcpy(card.value, "Done", sizeof(card.value));
      card.gauge = 100;  // a finished ring: it is what the alert flash pulses
      snprintf(card.sub1, sizeof(card.sub1), "%s done", phaseName(v.phase));
      hint = v.next == PHASE_FOCUS ? "Hold for focus"
                                   : (v.next == PHASE_SHORT ? "Hold for break" : "Hold for long break");
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

// --- the Pomodoro editor ---------------------------------------------------
// A button is its hit rectangle inset by 3 px, so neighbours never touch while
// the full rectangle stays the touch target.
void drawEditorButton(LovyanGFX *g, const EditRect &r0, const char *text, uint16_t fill,
                      uint16_t ink, int yOff = 0) {
  constexpr int GAP = 3;
  EditRect r = r0;
  r.y += yOff;
  g->fillRoundRect(r.x + GAP, r.y + GAP, r.w - 2 * GAP, r.h - 2 * GAP, 8, fill);
  g->setFont(text[1] == '\0' ? &V_B24.font : &V_B18.font);  // a lone - or + is drawn big
  g->setTextDatum(middle_center);
  g->setTextColor(ink, fill);
  g->drawString(text, r.x + r.w / 2, r.y + r.h / 2);
}

// The editor panel, drawn `yOff` px down from its resting place so it can slide.
void drawEditorPanel(LovyanGFX *g, const PomoSettings &s, int yOff) {
  g->fillRect(0, yOff, LCD_WIDTH, LCD_HEIGHT, BG);
  g->drawFastHLine(0, yOff, LCD_WIDTH, FAINT);  // edge that separates it from the card behind

  drawCaps(g, "Pomodoro", LCD_WIDTH / 2, 24 + yOff, g_palette[ACC_ACCENT], middle_center);

  const uint8_t vals[EDIT_ROWS] = {s.focusMin, s.shortMin, s.longMin, s.sessions};
  for (int r = 0; r < EDIT_ROWS; r++) {
    const EditRect row = editorRow(r);
    drawEditorButton(g, editorMinus(r), "-", FAINT, INK, yOff);
    drawEditorButton(g, editorPlus(r), "+", FAINT, INK, yOff);
    drawCaps(g, editorLabel(r), LCD_WIDTH / 2, row.y + 12 + yOff, DIM, middle_center);
    char buf[12];
    if (r == EDIT_ROWS - 1) snprintf(buf, sizeof(buf), "%u", (unsigned)vals[r]);
    else snprintf(buf, sizeof(buf), "%u min", (unsigned)vals[r]);
    g->setFont(&V_B24.font);
    g->setTextDatum(middle_center);
    g->setTextColor(INK, BG);
    g->drawString(buf, LCD_WIDTH / 2, row.y + 31 + yOff);
  }
  drawEditorButton(g, editorResetBtn(), "RESET", FAINT, INK, yOff);
  drawEditorButton(g, editorDoneBtn(), "DONE", g_palette[ACC_ACCENT], BG, yOff);
}

// Slide of the editor over the Pomodoro card: 0 = hidden below the screen,
// 1 = fully open. Ease-out going up, ease-in coming back down.
constexpr uint32_t EDITOR_SLIDE_MS = 260;
bool g_edSliding = false;
bool g_edOpening = false;
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
  buildRingTable();
  Serial.printf("[ui] framebuffer: %s\n", g_sprite ? "sprite (double buffered)" : "direct");
}

bool uiAnimating() { return g_animating; }

void uiReplay(uint8_t index) {
  if (index < MAX_CARDS) g_anim[index].seen = false;
}

void uiReplayPomodoro() { g_pomoAnim.seen = false; }

void uiAlertStart() {
  g_alertStart = millis();
  g_alertOn = true;
}

uint8_t uiDeckSize(const Payload &p) { return (p.valid ? p.nCards : 0) + 1; }

uint8_t uiPomodoroIndex(const Payload &p) { return p.valid ? p.nCards : 0; }

void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const PomoView &pomo, const BatteryView &bat) {
  g_animating = false;

  float flash = 0.0f;
  if (g_alertOn) {
    const uint32_t now = millis();
    if (now - g_alertStart >= ALERT_MS) {
      g_alertOn = false;
      lcd.setBrightness(BACKLIGHT);
    } else {
      flash = alertPulse(now);
      lcd.setBrightness((uint8_t)(BACKLIGHT + (255 - BACKLIGHT) * flash));
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
    if (card.gauge >= GAUGE_BLANK) {
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
    if (pos > 0.0f) drawEditorPanel(g, g_edSettings, (int)((1.0f - pos) * LCD_HEIGHT));
    // The last frame of the slide stays animating so it is pushed before the
    // static editor (or the plain card) takes over.
    g_animating = true;
  }

  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}

void uiEditorSlide(bool open, const PomoSettings &s) {
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
  drawEditorPanel(g, s, 0);
  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}

void uiPortal(Display &lcd, const char *apName) {
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
  g->drawString("192.168.4.1", LCD_WIDTH / 2, 234);

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
