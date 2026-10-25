// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, firmware/devices/ws169-panel/src/critter.c.
// Only Claude's flat block is kept (no onigiri, hats, celebrate pose or LVGL).
// Where the original draws with opacity (the "?" blink, the fading dots and
// z's) every shape is drawn at full ink: a CritterRect has no alpha. Stroked
// eyes and the font "z" become short runs of small squares in Cut/Body ink.
#include "critter.h"

#include <string.h>

namespace {

// The body group's transform, in cells: the bob, the squash and the mini
// critter move or scale everything drawn through it.
struct Pen {
  float sx, sy, tx, ty;
};

struct Out {
  CritterRect *r;
  size_t cap, n;
  uint32_t ms;
};

void put(Out &o, const Pen &p, float x, float y, float w, float h, CritterInk ink) {
  if (o.n >= o.cap) return;
  CritterRect &r = o.r[o.n++];
  r.x = p.tx + p.sx * x;
  r.y = p.ty + p.sy * y;
  r.w = p.sx * w;
  r.h = p.sy * h;
  r.ink = ink;
}

Pen moved(Pen p, float dx, float dy) {
  p.tx += p.sx * dx;
  p.ty += p.sy * dy;
  return p;
}

// Where we are in a cycle of `period` ms, 0..1.
float phase(const Out &o, uint32_t period) { return (float)(o.ms % period) / (float)period; }

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

void dots(Out &o, const Pen &p) {
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

enum class Face : uint8_t { Open2, Open1, Glance, Scan, Web, Hungry, Err, Compact, Exit, Shut };

}  // namespace

Pose poseFromName(const char *p) {
  static const struct {
    const char *name;
    Pose pose;
  } NAMES[] = {{"idle", Pose::Idle},   {"start", Pose::Start},       {"run", Pose::Run},         {"think", Pose::Think},
               {"read", Pose::Read},   {"write", Pose::Write},       {"bash", Pose::Bash},       {"web", Pose::Web},
               {"plan", Pose::Plan},   {"delegate", Pose::Delegate}, {"compact", Pose::Compact}, {"wait", Pose::Wait},
               {"err", Pose::Err},     {"exit", Pose::Exit},         {"hungry", Pose::Hungry}};
  if (!p) return Pose::Idle;
  for (size_t i = 0; i < sizeof NAMES / sizeof NAMES[0]; i++)
    if (strcmp(p, NAMES[i].name) == 0) return NAMES[i].pose;
  return Pose::Idle;
}

size_t critterShapes(Pose pose, uint32_t ms, CritterRect *out, size_t cap) {
  Out o = {out, cap, 0, ms};
  const Pen root = {1, 1, 0, 0};
  if (pose == Pose::Write) pose = Pose::Bash;  // writing looks like bash, as in the original

  Pen g = root;           // the body group: bob and squash move it
  uint32_t step = 0;      // legs stepping period (0: still)
  Face face = Face::Shut;
  float al = 2, ar = 2;   // arm rows
  bool armsStep = false;  // run, delegate: the arms swing with the legs
  bool noArms = false, noFeet = false;

  switch (pose) {
    case Pose::Think: face = Face::Glance; ar = 1; break;
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

  // Feet, outside the body group (the bob does not move them).
  if (!noFeet) legs(o, root, step == 0 || laOn(o, step) ? LEGS_A : LEGS_B);

  // The prop beside it says what it is doing.
  const Pen b = root;
  switch (pose) {
    case Pose::Think: {
      float pp = phase(o, 1200);
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
    case Pose::Run:
    case Pose::Start: dots(o, b); break;
    case Pose::Wait: {
      static const char *const r[] = {"###", "..#", ".#.", "...", ".#."};
      // The original blinks this "?" from full to 15% opacity; here it stays
      // on, so a waiting tile is still.
      pixels(o, b, r, 5, 14.5f, 1.1f, 0.9f, -4, CritterInk::Body);
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
      Pen z = moved(b, 0, dy[(int)(phase(o, 2400) * 4)]);
      zed(o, z, 14.8f, -1.8f, 1.6f);
      zed(o, z, 16.6f, -3.4f, 1.2f);
    }
  }
  return o.n;
}
