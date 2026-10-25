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

// Feeds `mv` every BAT_SAMPLE_MS for `n` samples, starting at `t`.
void feed(PowerGuess &g, uint32_t &t, int n, uint16_t mv) {
  for (int i = 0; i < n; i++) {
    g.sample(t, mv, true);
    t += BAT_SAMPLE_MS;
  }
}

void powerSourceNoCell() {
  PowerGuess g;
  CHECK(!g.external());  // nothing known yet
  g.sample(0, 0, false);
  CHECK(g.external());
}

void powerSourceFull() {
  PowerGuess g;
  g.sample(0, 4160, true);
  CHECK(g.external());
}

void powerSourceUnplugged() {
  PowerGuess g;
  uint32_t t = 0;
  feed(g, t, 6, 4160);
  CHECK(g.external());
  feed(g, t, 1, 4149);  // under 4150 and no rise: battery
  CHECK(!g.external());
  feed(g, t, 59, 4100);  // the rest of 5 min
  CHECK(!g.external());
}

void powerSourceChargingRises() {
  PowerGuess g;
  uint32_t t = 0;
  feed(g, t, 30, 3800);
  CHECK(!g.external());
  feed(g, t, 1, 3825);
  CHECK(g.external());
  // The rise is measured from the window minimum, so it fades once 3825 holds for 120 s.
  feed(g, t, 40, 3825);
  CHECK(!g.external());
}

void powerSourceSteady() {
  PowerGuess g;
  uint32_t t = 0;
  feed(g, t, 100, 3900);
  CHECK(!g.external());
  for (int i = 0; i < 100; i++) {  // +-5 mV of noise is not a rise
    g.sample(t, (uint16_t)(3900 + (i % 2 ? 5 : -5)), true);
    t += BAT_SAMPLE_MS;
  }
  CHECK(!g.external());
}

// A full cell just unplugged sinks about 1 mV per 10 s: once the 120 s window
// shows a 10 mV drop it reads as battery although it is still above 4150 mV.
void powerSourceUnpluggedFull() {
  PowerGuess g;
  uint32_t t = 0;
  uint16_t mv = 4180;
  for (int i = 0; i < 12; i++) {  // 60 s: no full window yet
    g.sample(t, mv, true);
    t += BAT_SAMPLE_MS;
    if (i % 2) mv--;
  }
  CHECK(g.external());
  for (int i = 0; i < 28; i++) {  // on to 200 s
    g.sample(t, mv, true);
    t += BAT_SAMPLE_MS;
    if (i % 2) mv--;
  }
  CHECK(mv > PowerGuess::FULL_MV);
  CHECK(!g.external());
  PowerGuess n;  // no cell still wins
  n.sample(0, 0, false);
  CHECK(n.external());
}

void powerSourceChargerFullSteady() {
  PowerGuess g;
  uint32_t t = 0;
  feed(g, t, 60, 4190);
  CHECK(g.external());
}

// Through the filter, as battery.cpp feeds it: raw +-15 mV noise is neither a
// fall nor a rise.
void powerSourceNoisyRaw() {
  BatteryFilter f;
  PowerGuess steady, full;
  uint32_t t = 0;
  for (int i = 0; i < 100; i++) {
    const int d = i % 2 ? 15 : -15;
    f.update((uint32_t)(3900 + d));
    steady.sample(t, (uint16_t)f.mv(), true);
    t += BAT_SAMPLE_MS;
  }
  CHECK(!steady.external());
  BatteryFilter h;
  t = 0;
  for (int i = 0; i < 100; i++) {
    const int d = i % 2 ? 15 : -15;
    h.update((uint32_t)(4190 + d));
    full.sample(t, (uint16_t)h.mv(), true);
    t += BAT_SAMPLE_MS;
  }
  CHECK(full.external());
}

}  // namespace

int main() {
  powerSourceNoCell();
  powerSourceFull();
  powerSourceUnplugged();
  powerSourceChargingRises();
  powerSourceSteady();
  powerSourceUnpluggedFull();
  powerSourceChargerFullSteady();
  powerSourceNoisyRaw();
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
