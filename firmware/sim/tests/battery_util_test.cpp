#include "battery_util.h"
#include "check.h"

namespace {

void testPercentTable() {
  CHECK(batteryPercent(4200) == 100);
  CHECK(batteryPercent(3700) == 50);
  CHECK(batteryPercent(3300) == 0);
  CHECK(batteryPercent(3600) == 25);
  // Interpolates between anchors: 3650 is halfway between 3600 (25) and 3700 (50).
  CHECK(batteryPercent(3650) == 37);
}

void testPercentClamps() {
  CHECK(batteryPercent(4260) == 100);  // charging overshoot
  CHECK(batteryPercent(5000) == 100);
  CHECK(batteryPercent(3000) == 0);    // deeply discharged
  CHECK(batteryPercent(2500) == 0);
  CHECK(batteryPercent(0) == 0);
}

void testFilterSeedsOnFirstSample() {
  BatteryFilter f;
  f.update(4000);
  CHECK(f.mv() == 4000);  // no ramp up from 0
}

void testFilterConverges() {
  BatteryFilter f;
  f.update(4000);
  for (int i = 0; i < 60; i++) f.update(3600);
  CHECK(f.mv() >= 3598 && f.mv() <= 3602);
  // One noisy sample moves it only a little.
  BatteryFilter g;
  g.update(3800);
  g.update(3500);
  CHECK(g.mv() > 3700 && g.mv() < 3800);
}

void testNoCellIsUsb() {
  BatteryFilter f;
  f.update(0);
  CHECK(f.view().onUsb);
  CHECK(f.view().pct == 100);

  BatteryFilter g;
  g.update(BAT_NO_CELL_MV - 1);
  CHECK(g.view().onUsb && g.view().pct == 100);

  BatteryFilter h;
  h.update(BAT_NO_CELL_MV);  // at the cutoff: a real (empty) cell
  CHECK(!h.view().onUsb && h.view().pct == 0);
}

void testCellView() {
  BatteryFilter f;
  f.update(3700);
  CHECK(!f.view().onUsb);
  CHECK(f.view().pct == 50);
  BatteryFilter full;
  full.update(4260);
  CHECK(!full.view().onUsb && full.view().pct == 100);
}

void testDefaultViewBeforeAnySample() {
  BatteryFilter f;
  CHECK(f.view().onUsb && f.view().pct == 100);
}

// The shown number only moves once the estimate is 2 points away, so a dip from
// a screen refresh or a WiFi burst does not make it flicker 98/97/98.
void testHysteresis() {
  BatteryFilter f;
  f.update(3650);  // a steep part of the curve: 0.25 points per mV
  const uint8_t shown = f.view().pct;
  CHECK(shown == batteryPercent(3650));
  for (int i = 0; i < 40; i++) {
    f.update(i % 2 ? 3620 : 3680);  // the estimate wobbles by about a point
    CHECK(f.view().pct == shown);
  }
  // A real drop still comes through.
  for (int i = 0; i < 80; i++) f.update(3900);
  const int want = batteryPercent(3900), got = f.view().pct;
  CHECK(got >= want - 1 && got <= want + 1);  // hysteresis can leave it one point off
}

void testEndsSnap() {
  BatteryFilter f;
  f.update(4100);
  for (int i = 0; i < 80; i++) f.update(4250);
  CHECK(f.view().pct == 100);  // full is shown as full, not 99
  for (int i = 0; i < 80; i++) f.update(3300);
  CHECK(f.view().pct == 0);
}

}  // namespace

int main() {
  testHysteresis();
  testEndsSnap();
  testPercentTable();
  testPercentClamps();
  testFilterSeedsOnFirstSample();
  testFilterConverges();
  testNoCellIsUsb();
  testCellView();
  testDefaultViewBeforeAnySample();
  return checksDone("battery_util");
}
