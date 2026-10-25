#include <stdio.h>

#include "check.h"
#include "critter.h"

static const HatId NO_HAT = {-1, -1};

static bool sameGeometry(const CritterRect *a, size_t na, const CritterRect *b, size_t nb) {
  if (na != nb) return false;
  for (size_t i = 0; i < na; i++)
    if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w || a[i].h != b[i].h || a[i].ink != b[i].ink) return false;
  return true;
}

static bool same(const CritterRect *a, size_t na, const CritterRect *b, size_t nb) {
  if (!sameGeometry(a, na, b, nb)) return false;
  for (size_t i = 0; i < na; i++)
    if (a[i].alpha != b[i].alpha) return false;
  return true;
}

static bool has(const CritterRect *r, size_t n, CritterInk k) {
  for (size_t i = 0; i < n; i++)
    if (r[i].ink == k) return true;
  return false;
}

// The lowest alpha of any shape in ink k (256: none).
static int minAlpha(const CritterRect *r, size_t n, CritterInk k) {
  int m = 256;
  for (size_t i = 0; i < n; i++)
    if (r[i].ink == k && r[i].alpha < m) m = r[i].alpha;
  return m;
}

static bool isHat(CritterInk k) {
  return k == CritterInk::HatMain || k == CritterInk::HatShade || k == CritterInk::HatHighlight || k == CritterInk::HatDark;
}

int main() {
  static CritterRect a[CRITTER_MAX_RECTS], b[CRITTER_MAX_RECTS];

  // Every pose, with no hat and with each hat, draws a critter inside the
  // sprite box at every moment of its cycles. The box: x -5..20 (the cap's
  // brim reaches 3 cells left of the body, 5 while compact squashes it wide),
  // y -8..10 (the celebrate hop lifts the tallest hat).
  size_t worst = 0;
  for (int p = 0; p <= (int)Pose::Celebrate; p++) {
    for (int s = -1; s < 13; s++) {
      for (uint32_t ms = 0; ms < 5000; ms += 20) {
        const HatId hat = {(int8_t)s, (int8_t)(s < 0 ? -1 : s % 8)};
        size_t n = critterShapes((Pose)p, hat, ms, a, CRITTER_MAX_RECTS);
        if (n > worst) worst = n;
        CHECK(n >= (p == (int)Pose::Exit ? 4u : 6u));  // a gone agent is a hollow shell, no arms or legs
        for (size_t i = 0; i < n; i++) {
          CHECK(a[i].x >= -5.01f && a[i].x + a[i].w <= 20);  // -5 in floats
          CHECK(a[i].y >= -8 && a[i].y + a[i].h <= 10);
          CHECK(a[i].w > 0 && a[i].h > 0);
          CHECK(a[i].alpha > 0);  // a shape faded to nothing is not emitted
          if (s < 0) CHECK(!isHat(a[i].ink));
        }
      }
    }
  }
  printf("critter_test: worst case %zu shapes (cap %zu)\n", worst, CRITTER_MAX_RECTS);
  CHECK(worst + 16 <= CRITTER_MAX_RECTS);  // the constant is not just barely enough

  CHECK(poseFromName("write") == Pose::Write);
  CHECK(poseFromName("hungry") == Pose::Hungry);
  CHECK(poseFromName("celebrate") == Pose::Celebrate);
  CHECK(poseFromName("delegate") == Pose::Delegate);
  CHECK(poseFromName("bogus") == Pose::Idle);
  CHECK(poseFromName("") == Pose::Idle);
  CHECK(poseFromName(nullptr) == Pose::Idle);

  // waitIsStill: a waiting tile does not move; only its "?" blinks.
  size_t na = critterShapes(Pose::Wait, NO_HAT, 0, a, CRITTER_MAX_RECTS);
  size_t nb = critterShapes(Pose::Wait, NO_HAT, 777, b, CRITTER_MAX_RECTS);
  CHECK(sameGeometry(a, na, b, nb));

  // A running one steps: half a 500 ms period later the legs and bob differ.
  na = critterShapes(Pose::Run, NO_HAT, 0, a, CRITTER_MAX_RECTS);
  nb = critterShapes(Pose::Run, NO_HAT, 250, b, CRITTER_MAX_RECTS);
  CHECK(!same(a, na, b, nb));

  // everyHatDraws: each shape, still (idle) and animated (run), in its colour.
  for (int s = 0; s < 13; s++) {
    for (int p = 0; p < 2; p++) {
      const Pose pose = p ? Pose::Run : Pose::Idle;
      for (uint32_t ms = 0; ms < 2000; ms += 40) {
        size_t n = critterShapes(pose, HatId{(int8_t)s, 3}, ms, a, CRITTER_MAX_RECTS);
        CHECK(has(a, n, CritterInk::HatMain));
        for (size_t i = 0; i < n; i++) {
          CHECK(a[i].x >= -3 && a[i].x + a[i].w <= 20);
          CHECK(a[i].y >= -8 && a[i].y + a[i].h <= 10);
        }
      }
    }
  }
  // A working agent's hat animates (the propeller spins); an idle one's is still.
  {
    const HatId prop = {9, 0};
    bool moved = false;
    na = critterShapes(Pose::Read, prop, 0, a, CRITTER_MAX_RECTS);
    for (uint32_t ms = 120; ms < 600; ms += 120) {
      nb = critterShapes(Pose::Read, prop, ms, b, CRITTER_MAX_RECTS);
      size_t ha = 0, hb = 0;
      for (size_t i = 0; i < na; i++) ha += isHat(a[i].ink);
      for (size_t i = 0; i < nb; i++) hb += isHat(b[i].ink);
      if (ha != hb) moved = true;
      for (size_t i = 0, j = 0; !moved && i < na && j < nb; i++, j++)
        if (isHat(a[i].ink) && (a[i].x != b[j].x || a[i].w != b[j].w || a[i].ink != b[j].ink)) moved = true;
    }
    CHECK(moved);
  }

  // hatFromCodeRoundTrip: h = shape * 8 + colour, anything else is no hat.
  for (int c = 0; c <= 103; c++) {
    HatId h = hatFromCode(c);
    CHECK(h.shape == c / 8 && h.color == c % 8);
  }
  CHECK(hatFromCode(-1).shape == -1);
  CHECK(hatFromCode(104).shape == -1);
  CHECK(hatFromCode(1000).shape == -1);

  // agents-deck's hat colours: teal first, ink last.
  CHECK(hatMainRgb(0) == 0x4FB3A6 && hatShadeRgb(0) == 0x2F7F75);
  CHECK(hatMainRgb(2) == 0xE8B94A && hatShadeRgb(2) == 0xB08429);
  CHECK(hatMainRgb(7) == 0xE8E4DC && hatShadeRgb(7) == 0xA8A39A);
  CHECK(hatMainRgb(-1) == 0xE8E4DC && hatMainRgb(8) == 0xE8E4DC);  // out of range: ink
  CHECK(hatHighlightRgb() == 0xF7F4EC && hatDarkRgb() == 0x0A0A0A);

  // alphaFades: the "?" blinks, the gone agent's hat is faded, the sleeper's
  // z's and the runner's dots fade.
  na = critterShapes(Pose::Wait, NO_HAT, 0, a, CRITTER_MAX_RECTS);
  nb = critterShapes(Pose::Wait, NO_HAT, 700, b, CRITTER_MAX_RECTS);
  CHECK(!same(a, na, b, nb));
  CHECK(minAlpha(a, na, CritterInk::Body) == 255 && minAlpha(b, nb, CritterInk::Body) == 38);
  na = critterShapes(Pose::Exit, HatId{0, 0}, 0, a, CRITTER_MAX_RECTS);
  CHECK(minAlpha(a, na, CritterInk::HatMain) < 255);
  CHECK(minAlpha(a, na, CritterInk::HatMain) == 255 * 45 / 100);
  na = critterShapes(Pose::Idle, NO_HAT, 0, a, CRITTER_MAX_RECTS);
  nb = critterShapes(Pose::Idle, NO_HAT, 1300, b, CRITTER_MAX_RECTS);
  CHECK(minAlpha(a, na, CritterInk::Body) == 230 && minAlpha(b, nb, CritterInk::Body) == 102);
  na = critterShapes(Pose::Run, NO_HAT, 0, a, CRITTER_MAX_RECTS);
  CHECK(minAlpha(a, na, CritterInk::Body) == 51);

  // celebrateMoves: it hops, waves a chequered flag and twinkles.
  na = critterShapes(Pose::Celebrate, NO_HAT, 0, a, CRITTER_MAX_RECTS);
  nb = critterShapes(Pose::Celebrate, NO_HAT, 250, b, CRITTER_MAX_RECTS);
  CHECK(!same(a, na, b, nb));
  CHECK(has(a, na, CritterInk::Flag) && has(a, na, CritterInk::Cut));
  CHECK(a[0].y == 0 && b[0].y == -1.5f);  // the body, hopping
  CHECK(minAlpha(b, nb, CritterInk::Body) == 115);

  // A short buffer is filled, not overrun.
  CritterRect c[4];
  CHECK(critterShapes(Pose::Idle, NO_HAT, 0, c, 3) == 3);
  CHECK(critterShapes(Pose::Idle, NO_HAT, 0, c, 0) == 0);
  return checksDone("critter_test");
}
