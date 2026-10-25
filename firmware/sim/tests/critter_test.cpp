#include "check.h"
#include "critter.h"

static bool same(const CritterRect *a, size_t na, const CritterRect *b, size_t nb) {
  if (na != nb) return false;
  for (size_t i = 0; i < na; i++)
    if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].w != b[i].w || a[i].h != b[i].h || a[i].ink != b[i].ink) return false;
  return true;
}

int main() {
  // Every pose draws a critter inside the sprite box, at every moment of its cycles.
  for (int p = 0; p <= (int)Pose::Hungry; p++) {
    for (uint32_t ms = 0; ms < 5000; ms += 50) {
      CritterRect r[CRITTER_MAX_RECTS];
      size_t n = critterShapes((Pose)p, ms, r, CRITTER_MAX_RECTS);
      CHECK(n >= (p == (int)Pose::Exit ? 4u : 6u));  // a gone agent is a hollow shell, no arms or legs
      CHECK(n < CRITTER_MAX_RECTS);  // the constant is not just barely enough
      for (size_t i = 0; i < n; i++) {
        CHECK(r[i].x >= -1.5f && r[i].x + r[i].w <= 19);
        CHECK(r[i].y >= -5 && r[i].y + r[i].h <= 10);
        CHECK(r[i].w > 0 && r[i].h > 0);
      }
    }
  }

  CHECK(poseFromName("write") == Pose::Write);
  CHECK(poseFromName("hungry") == Pose::Hungry);
  CHECK(poseFromName("delegate") == Pose::Delegate);
  CHECK(poseFromName("bogus") == Pose::Idle);
  CHECK(poseFromName("") == Pose::Idle);
  CHECK(poseFromName(nullptr) == Pose::Idle);

  // A waiting tile does not jitter.
  CritterRect a[CRITTER_MAX_RECTS], b[CRITTER_MAX_RECTS];
  size_t na = critterShapes(Pose::Wait, 0, a, CRITTER_MAX_RECTS);
  size_t nb = critterShapes(Pose::Wait, 777, b, CRITTER_MAX_RECTS);
  CHECK(same(a, na, b, nb));

  // A running one steps: half a 500 ms period later the legs and bob differ.
  na = critterShapes(Pose::Run, 0, a, CRITTER_MAX_RECTS);
  nb = critterShapes(Pose::Run, 250, b, CRITTER_MAX_RECTS);
  CHECK(!same(a, na, b, nb));

  // A short buffer is filled, not overrun.
  CritterRect c[4];
  CHECK(critterShapes(Pose::Idle, 0, c, 3) == 3);
  CHECK(critterShapes(Pose::Idle, 0, c, 0) == 0);
  return checksDone("critter_test");
}
