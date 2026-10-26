// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, firmware/devices/ws169-panel/src/critter.c.
// Claude's flat block only (no onigiri, no LVGL); poses, hats (hat_art.cpp),
// the celebrate pose and every opacity of the original are kept, an opacity as
// the shape's alpha. The bubble beside the critter is always drawn (the
// original's `bubble` flag is on for every tile here). Stroked eyes and the
// font "z" become short runs of small squares in Cut/Body ink; a hat row's run
// of one colour is one rectangle (the same pixels as the original's cells).
// The Mac menu's tiles draw a Swift port of this file and hat_art.cpp
// (mac-helper/Sources/CubeLinkCore/Critter.swift): edit them together.
#include "critter.h"

#include <string.h>

#include "hat_art.h"

namespace {

// The body group's transform, in cells: the bob, the squash and the mini
// critter move or scale everything drawn through it.
struct Pen {
  float sx, sy, tx, ty;
  uint8_t opa;  // the shapes' alpha, as the original's pen opacity
};

struct Out {
  CritterRect *r;
  size_t cap, n;
  uint32_t ms;
};

void put(Out &o, const Pen &p, float x, float y, float w, float h, CritterInk ink) {
  if (o.n >= o.cap || p.opa == 0) return;
  CritterRect &r = o.r[o.n++];
  r.x = p.tx + p.sx * x;
  r.y = p.ty + p.sy * y;
  r.w = p.sx * w;
  r.h = p.sy * h;
  r.ink = ink;
  r.alpha = p.opa;
}

Pen withOpa(Pen p, uint8_t opa) {
  p.opa = opa;
  return p;
}

Pen moved(Pen p, float dx, float dy) {
  p.tx += p.sx * dx;
  p.ty += p.sy * dy;
  return p;
}

// Where we are in a cycle of `period` ms, 0..1.
float phase(const Out &o, uint32_t period, uint32_t delay = 0) { return (float)((o.ms + delay) % period) / (float)period; }

bool laOn(const Out &o, uint32_t period) { return phase(o, period) < 0.5f; }

const float LEGS_A[4] = {2, 4, 9, 11};
const float LEGS_B[4] = {3, 5, 8, 10};

void legs(Out &o, const Pen &p, const float *xs) {
  for (int i = 0; i < 4; i++) put(o, p, xs[i], 6, 1, 1.6f, CritterInk::Body);
}

// Arms beside the body, at their top row l (left) and r (right).
void arms(Out &o, const Pen &p, float l, float r) {
  put(o, p, 0, l, 1, 2, CritterInk::Body);
  put(o, p, 13, r, 1, 2, CritterInk::Body);
}

void eyesOpenAt(Out &o, const Pen &p, float y, float h) {
  put(o, p, 4, y, 1, h, CritterInk::Cut);
  put(o, p, 9, y, 1, h, CritterInk::Cut);
}

// A small square centred on (x, y): one step of a stroked eye.
void dot(Out &o, const Pen &p, float x, float y) { put(o, p, x - 0.3f, y - 0.3f, 0.6f, 0.6f, CritterInk::Cut); }

// A straight stroke from (x1, y1) to (x2, y2) as `n` squares.
void line(Out &o, const Pen &p, float x1, float y1, float x2, float y2, int n) {
  for (int i = 0; i < n; i++) {
    float t = n > 1 ? (float)i / (float)(n - 1) : 0;
    dot(o, p, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t);
  }
}

// Bitmap rows ('#' cells) of cell size w x h from (x0, y0).
void pixels(Out &o, const Pen &p, const char *const *rows, int n, float x0, float w, float h, float y0, CritterInk k) {
  for (int r = 0; r < n; r++)
    for (int c = 0; rows[r][c]; c++)
      if (rows[r][c] == '#') put(o, p, x0 + c * w, y0 + r * h, w, h, k);
}

void dots(Out &o, const Pen &p0) {
  static const uint8_t steps[3] = {51, 153, 255};  // .2 -> 1, in three steps
  const Pen p = withOpa(p0, (uint8_t)(p0.opa * steps[(int)(phase(o, 1200) * 3)] / 255));
  put(o, p, 14.6f, -2.6f, 0.9f, 0.9f, CritterInk::Body);
  put(o, p, 15.9f, -2.6f, 0.9f, 0.9f, CritterInk::Body);
  put(o, p, 17.2f, -2.6f, 0.8f, 0.9f, CritterInk::Body);
}

// The little critter a delegating agent shows (MINI in the original).
void mini(Out &o, const Pen &p, float ox, float oy, float s) {
  Pen q = p;
  q.tx += q.sx * ox;
  q.ty += q.sy * oy;
  q.sx *= s;
  q.sy *= s;
  Pen b = laOn(o, 500) ? q : moved(q, 0, -0.5f);
  put(o, b, 1, 0, 12, 6, CritterInk::Body);
  put(o, b, 0, 2, 1, 2, CritterInk::Body);
  put(o, b, 13, 2, 1, 2, CritterInk::Body);
  put(o, b, 4, 1.5f, 1, 2, CritterInk::Cut);
  put(o, b, 9, 1.5f, 1, 2, CritterInk::Cut);
  legs(o, q, laOn(o, 500) ? LEGS_A : LEGS_B);
}

// A "z" of size s with its top-left at (x, y): bar, diagonal, bar.
void zed(Out &o, const Pen &p, float x, float y, float s) {
  float t = s / 4;
  put(o, p, x, y, s, t, CritterInk::Body);
  put(o, p, x + s - t, y + t, t, t, CritterInk::Body);
  put(o, p, x + s / 2 - t / 2, y + s / 2 - t / 2, t, t, CritterInk::Body);
  put(o, p, x, y + s - 2 * t, t, t, CritterInk::Body);
  put(o, p, x, y + s - t, s, t, CritterInk::Body);
}

enum class Face : uint8_t { Open2, Open1, Glance, Scan, Web, Hungry, Celebrate, Err, Compact, Exit, Shut };

// ---- hats -------------------------------------------------------------------

const struct {
  uint32_t main, shade;
} HAT_COLORS[8] = {{0x4FB3A6, 0x2F7F75}, {0x6F8FD8, 0x4A63A3}, {0xE8B94A, 0xB08429}, {0x6DBE6A, 0x468A44},
                   {0xA68BD8, 0x7660A6}, {0xE58BB0, 0xB05E80}, {0x5EC4D6, 0x3A8E9C}, {0xE8E4DC, 0xA8A39A}};

bool hatInk(char k, CritterInk *ink) {
  switch (k) {
    case 'a': *ink = CritterInk::HatMain; return true;
    case 'b': *ink = CritterInk::HatShade; return true;
    case 'c': *ink = CritterInk::HatHighlight; return true;
    case 'd': *ink = CritterInk::HatDark; return true;
    default: return false;
  }
}

void hatFrame(Out &o, const Pen &p, const HatFrame &f) {
  for (int r = 0; r < f.nrows; r++) {
    const char *row = f.rows[r];
    for (int c = 0; row[c];) {
      CritterInk ink;
      if (!hatInk(row[c], &ink)) {
        c++;
        continue;
      }
      int e = c + 1;
      while (row[e] == row[c]) e++;
      put(o, p, f.x + c, f.top + r, (float)(e - c), 1, ink);
      c = e;
    }
  }
  for (int i = 0; i < f.nextras; i++) {
    CritterInk ink;
    if (!hatInk(f.extras[i].key, &ink)) ink = CritterInk::HatDark;  // as the original: anything else is near-black
    put(o, p, f.extras[i].x, f.extras[i].y, 1, 1, ink);
  }
}

void hat(Out &o, const Pen &p, HatId id, bool faded, bool animate) {
  if (id.shape < 0 || id.shape >= HAT_ART_COUNT) return;
  const HatArt &art = HAT_ART[id.shape];
  const Pen q = faded ? withOpa(p, (uint8_t)(p.opa * 45 / 100)) : p;
  const HatFrame &f = animate ? art.frames[(o.ms / art.ms) % art.nframes] : art.still;
  hatFrame(o, q, f);
  if (strcmp(art.name, "headphones") == 0) {
    put(o, q, 0, 0, 1, 2, CritterInk::HatShade);
    put(o, q, 13, 0, 1, 2, CritterInk::HatShade);
  }
}

bool working(Pose pose) {
  switch (pose) {
    case Pose::Run:
    case Pose::Think:
    case Pose::Read:
    case Pose::Write:
    case Pose::Bash:
    case Pose::Web:
    case Pose::Plan:
    case Pose::Delegate:
    case Pose::Compact: return true;
    default: return false;
  }
}

}  // namespace

Pose poseFromName(const char *p) {
  static const struct {
    const char *name;
    Pose pose;
  } NAMES[] = {{"idle", Pose::Idle},   {"start", Pose::Start},       {"run", Pose::Run},         {"think", Pose::Think},
               {"read", Pose::Read},   {"write", Pose::Write},       {"bash", Pose::Bash},       {"web", Pose::Web},
               {"plan", Pose::Plan},   {"delegate", Pose::Delegate}, {"compact", Pose::Compact}, {"wait", Pose::Wait},
               {"err", Pose::Err},     {"exit", Pose::Exit},         {"hungry", Pose::Hungry},
               {"celebrate", Pose::Celebrate}};
  if (!p) return Pose::Idle;
  for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
    if (strcmp(p, NAMES[i].name) == 0) return NAMES[i].pose;
  return Pose::Idle;
}

HatId hatFromCode(int code) {
  if (code < 0 || code >= HAT_ART_COUNT * 8) return HatId{-1, -1};
  return HatId{(int8_t)(code / 8), (int8_t)(code % 8)};
}

uint32_t hatMainRgb(int8_t color) { return HAT_COLORS[color >= 0 && color < 8 ? color : 7].main; }
uint32_t hatShadeRgb(int8_t color) { return HAT_COLORS[color >= 0 && color < 8 ? color : 7].shade; }
uint32_t hatHighlightRgb() { return 0xF7F4EC; }
uint32_t hatDarkRgb() { return 0x0A0A0A; }

size_t critterShapes(Pose pose, HatId hatId, uint32_t ms, CritterRect *out, size_t cap) {
  Out o = {out, cap, 0, ms};
  const Pen root = {1, 1, 0, 0, 255};
  const bool animateHat = working(pose);
  if (pose == Pose::Write) pose = Pose::Bash;  // writing looks like bash, as in the original

  Pen g = root;           // the body group: bob and squash move it
  uint32_t step = 0;      // legs stepping period (0: still)
  Face face = Face::Shut;
  float al = 2, ar = 2;   // arm rows
  bool armsStep = false;  // run, delegate: the arms swing with the legs
  bool noArms = false, noFeet = false;

  switch (pose) {
    case Pose::Think:
      // Pondering, not asleep: a slow bob on the glance's 1.2 s, and the raised
      // hand taps the chin (rows 1 and 0, 400 ms each).
      face = Face::Glance;
      ar = laOn(o, 800) ? 1 : 0;
      if (!laOn(o, 1200)) g = moved(g, 0, -0.5f);
      break;
    case Pose::Read: face = Face::Scan; break;
    case Pose::Bash:
      face = Face::Open2;
      step = 200;
      if (!laOn(o, 200)) g = moved(g, 0, -0.5f);
      break;
    case Pose::Web: face = Face::Web; break;
    case Pose::Plan: face = Face::Open2; break;
    case Pose::Delegate:
    case Pose::Run:
      face = Face::Open2;
      step = 500;
      armsStep = true;
      if (!laOn(o, 500)) g = moved(g, 0, -0.5f);
      break;
    case Pose::Hungry: face = Face::Hungry; break;
    case Pose::Celebrate:
      face = Face::Celebrate;
      al = 0;
      ar = 0;
      if (!laOn(o, 400)) g = moved(g, 0, -1.5f);
      break;
    case Pose::Wait: face = Face::Open1; break;
    case Pose::Err: face = Face::Err; al = 0; ar = 0; break;
    case Pose::Start: face = Face::Open2; break;
    case Pose::Compact: {
      face = Face::Compact;
      int q = (int)(phase(o, 1200) * 4);
      float sx = q == 0 ? 1 : q == 2 ? 1.2f : 1.1f, sy = q == 0 ? 1 : q == 2 ? 0.62f : 0.8f;
      // scale around cell (7, 7)
      g.tx += g.sx * 7 * (1 - sx);
      g.ty += g.sy * 7 * (1 - sy);
      g.sx *= sx;
      g.sy *= sy;
      break;
    }
    case Pose::Exit: face = Face::Exit; noArms = true; noFeet = true; break;
    default: face = Face::Shut; break;
  }

  // The body group.
  put(o, g, 1, 0, 12, 6, CritterInk::Body);
  if (pose == Pose::Exit) put(o, g, 2, 1, 10, 4, CritterInk::Cut);
  if (!noArms) {
    if (armsStep) {
      if (laOn(o, 500)) arms(o, g, 2, 1);
      else arms(o, g, 1, 2);
    } else {
      arms(o, g, al, ar);
    }
  }
  Pen f = moved(g, 0, -0.5f);  // the block's face sits half a row higher
  switch (face) {
    case Face::Open2: eyesOpenAt(o, f, 2, 2); break;
    case Face::Open1: eyesOpenAt(o, f, 1, 2); break;
    case Face::Glance: eyesOpenAt(o, moved(f, laOn(o, 1200) ? -1 : 1, 0), 1, 1.5f); break;
    case Face::Scan: {
      static const float dx[4] = {0, -1, 0, 1};
      eyesOpenAt(o, moved(f, dx[(int)(phase(o, 1200) * 4)], 0), 2, 2);
      break;
    }
    case Face::Web:
      put(o, f, 4.4f, 1, 1, 2, CritterInk::Cut);
      put(o, f, 9.4f, 1, 1, 2, CritterInk::Cut);
      break;
    case Face::Hungry: {
      put(o, f, 4.6f, 1, 1, 2, CritterInk::Cut);
      put(o, f, 9.6f, 1, 1, 2, CritterInk::Cut);
      bool open = laOn(o, 800);
      put(o, f, 6.2f, open ? 4.2f : 4.8f, 2.2f, open ? 1.6f : 0.5f, CritterInk::Cut);
      break;
    }
    case Face::Celebrate:  // "^ ^"
      for (int i = 0; i < 2; i++) {
        float ex = i ? 9.5f : 4.5f;
        line(o, f, ex - 1, 3.3f, ex, 2.2f, 3);
        line(o, f, ex, 2.2f, ex + 1, 3.3f, 3);
      }
      break;
    case Face::Err:  // two crossed strokes per eye, an "x"
      for (int i = 0; i < 2; i++) {
        float ex = i ? 8.8f : 3.8f;
        line(o, f, ex - 0.4f, 1.8f, ex + 1.4f, 4.0f, 4);
        line(o, f, ex + 1.4f, 1.8f, ex - 0.4f, 4.0f, 4);
      }
      break;
    case Face::Compact:  // "> <"
      line(o, f, 3.6f, 1.9f, 4.8f, 2.7f, 3);
      line(o, f, 4.8f, 2.7f, 3.6f, 3.5f, 3);
      line(o, f, 10.4f, 1.9f, 9.2f, 2.7f, 3);
      line(o, f, 9.2f, 2.7f, 10.4f, 3.5f, 3);
      break;
    case Face::Exit:
      put(o, f, 4, 3, 1, 1, CritterInk::Body);
      put(o, f, 9, 3, 1, 1, CritterInk::Body);
      break;
    case Face::Shut:
      put(o, f, 3.5f, 3.2f, 2, 0.55f, CritterInk::Cut);
      put(o, f, 8.5f, 3.2f, 2, 0.55f, CritterInk::Cut);
      break;
  }

  hat(o, g, hatId, pose == Pose::Exit, animateHat);
  if (pose == Pose::Celebrate) {  // the finish flag in the raised hand, waving
    put(o, g, 14, -4, 1, 6, CritterInk::Flag);
    int ph = laOn(o, 500) ? 0 : 1;
    for (int cx = 0; cx < 3; cx++)
      for (int cy = 0; cy < 2; cy++) put(o, g, 15 + cx, -4 + cy, 1, 1, (cx + cy + ph) % 2 ? CritterInk::Cut : CritterInk::Flag);
  }

  // Feet, outside the body group (the bob does not move them; the hop does).
  if (!noFeet) legs(o, pose == Pose::Celebrate ? g : root, step == 0 || laOn(o, step) ? LEGS_A : LEGS_B);

  // The prop beside it says what it is doing.
  const Pen b = root;
  switch (pose) {
    case Pose::Think: {
      // The thought rises dot by dot, holds, then pops and starts again.
      float pp = phase(o, 1200);
      if (pp >= 0.85f) break;
      put(o, b, 14.3f, -0.9f, 0.6f, 0.6f, CritterInk::Body);
      if (pp >= 0.25f) put(o, b, 15.3f, -2.1f, 0.9f, 0.9f, CritterInk::Body);
      if (pp >= 0.5f) put(o, b, 16.5f, -3.8f, 1.3f, 1.3f, CritterInk::Body);
      break;
    }
    case Pose::Read: {
      static const char *const r[] = {".##..", "#..#.", "#..#.", ".##..", "....#"};
      pixels(o, b, r, 5, 14.6f, 0.75f, 0.75f, -4, CritterInk::Body);
      break;
    }
    case Pose::Bash: {
      static const char *const r[] = {"#..", ".#.", "#.."};
      pixels(o, b, r, 3, 14.5f, 0.7f, 0.8f, -3.7f, CritterInk::Body);
      if (laOn(o, 1000)) put(o, b, 16.5f, -1.7f, 1.2f, 0.5f, CritterInk::Body);
      break;
    }
    case Pose::Web: {
      static const char *const a[] = {".###.", "#.#.#", "#####", "#.#.#", ".###."};
      static const char *const z[] = {".###.", "##.##", "#####", "##.##", ".###."};
      pixels(o, b, laOn(o, 800) ? a : z, 5, 14.4f, 0.72f, 0.72f, -4.2f, CritterInk::Body);
      break;
    }
    case Pose::Plan: {
      float pp = phase(o, 2000);
      for (int i = 0; i < 3; i++) {
        float y = -3.9f + i * 1.25f;
        put(o, b, 14.4f, y, 0.9f, 0.9f, CritterInk::Body);
        if (pp < (i + 1) * 0.25f) put(o, b, 14.6f, y + 0.2f, 0.5f, 0.5f, CritterInk::Cut);
        put(o, b, 15.6f, y + 0.25f, 2.2f, 0.45f, CritterInk::Body);
      }
      break;
    }
    case Pose::Delegate: mini(o, b, 14.4f, 3.2f, 0.26f); break;
    case Pose::Hungry: {
      static const char *const r[] = {".###.", "##.##", "#####", "#.###", ".###."};
      pixels(o, laOn(o, 600) ? b : moved(b, 0, 0.6f), r, 5, 14.3f, 0.75f, 0.75f, -4.2f, CritterInk::Body);
      break;
    }
    case Pose::Celebrate:  // two twinkles
      for (int i = 0; i < 2; i++) {
        const Pen s = phase(o, 600, i ? 400 : 0) >= 0.34f ? withOpa(b, 115) : b;
        float sx = i ? 18.2f : 16.4f, sy = i ? -4.4f : -6.2f;
        put(o, s, sx - 0.5f, sy, 1.5f, 0.5f, CritterInk::Body);
        put(o, s, sx, sy - 0.5f, 0.5f, 1.5f, CritterInk::Body);
      }
      break;
    case Pose::Run:
    case Pose::Start: dots(o, b); break;
    case Pose::Wait: {  // a blinking "?"
      static const char *const r[] = {"###", "..#", ".#.", "...", ".#."};
      pixels(o, phase(o, 1000) >= 0.6f ? withOpa(b, 38) : b, r, 5, 14.5f, 1.1f, 0.9f, -4, CritterInk::Body);
      break;
    }
    case Pose::Err: {
      static const char *const r[] = {"#", "#", "#", ".", "#"};
      pixels(o, b, r, 5, 15.6f, 1.2f, 0.9f, -4, CritterInk::Accent);
      break;
    }
    case Pose::Compact: {
      int q = (int)(phase(o, 1200) * 4);
      float p1 = q == 0 ? 0 : q == 2 ? 1.1f : 0.4f, p2 = q == 0 ? 0 : q == 2 ? 2.2f : 0.8f;
      put(o, b, 14.6f, -3.8f + p2, 3.2f, 0.6f, CritterInk::Body);
      put(o, b, 14.6f, -2.7f + p1, 3.2f, 0.6f, CritterInk::Body);
      put(o, b, 14.6f, -1.6f, 3.2f, 0.6f, CritterInk::Body);
      break;
    }
    case Pose::Exit: break;
    default: {  // asleep: two z's drifting up
      static const float dy[4] = {0, -0.3f, -0.6f, -0.3f};
      static const uint8_t op[4] = {230, 166, 102, 166};
      const int k = (int)(phase(o, 2400) * 4);
      const Pen z = withOpa(moved(b, 0, dy[k]), op[k]);
      zed(o, z, 14.8f, -1.8f, 1.6f);
      zed(o, z, 16.6f, -3.4f, 1.2f);
    }
  }
  return o.n;
}
