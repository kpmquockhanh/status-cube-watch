#include <cstdint>

#include "check.h"
#include "pomo_settings.h"

namespace {

// Review Focus 2: a corrupt stored value never reaches the timer.
void testClamp() {
  const PomoSettings c = pomoClamp(PomoSettings{0, 200, 99, 0});
  CHECK(c.focusMin == 1);
  CHECK(c.shortMin == 99);
  CHECK(c.longMin == 99);
  CHECK(c.sessions == 1);
  const PomoSettings d = pomoClamp(PomoSettings{25, 5, 15, 255});
  CHECK(d.focusMin == 25 && d.shortMin == 5 && d.longMin == 15);
  CHECK(d.sessions == 9);
}

void testConfigFrom() {
  const PomoConfig c = pomoConfigFrom(PomoSettings{25, 5, 15, 4}, 1);
  CHECK(c.focusMs == 25u * 60000u);
  CHECK(c.shortMs == 5u * 60000u);
  CHECK(c.longMs == 15u * 60000u);
  CHECK(c.sessions == 4);
  const PomoConfig f = pomoConfigFrom(PomoSettings{25, 5, 15, 4}, 60);  // POMO_FAST=60
  CHECK(f.focusMs == 25000u);
}

}  // namespace

int main() {
  testClamp();
  testConfigFrom();
  return checksDone("pomo_settings_test");
}
