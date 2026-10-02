#pragma once
// Pure battery logic: no Arduino, no ADC, so the host tests in sim/tests can
// build it with a plain compiler. The hardware read is in battery.cpp.
#include <stdint.h>

// Below this the pin is floating or the cell is absent, so the cube can only be
// running from USB.
constexpr uint32_t BAT_NO_CELL_MV = 2500;

struct BatteryView {
  uint8_t pct = 100;
  bool onUsb = true;  // no cell detected: shown as a full battery
};

// Single-cell Li-ion resting voltage -> percent, piecewise linear. An estimate:
// load and temperature move the real curve by a few percent.
inline uint8_t batteryPercent(uint32_t mv) {
  struct Pt { uint16_t mv; uint8_t pct; };
  static const Pt T[] = {{3300, 0},  {3500, 10}, {3600, 25}, {3700, 50}, {3800, 65},
                         {3900, 78}, {4000, 88}, {4100, 95}, {4200, 100}};
  constexpr int N = sizeof(T) / sizeof(T[0]);
  if (mv <= T[0].mv) return 0;
  if (mv >= T[N - 1].mv) return 100;
  for (int i = 1; i < N; i++) {
    if (mv <= T[i].mv) {
      const uint32_t span = T[i].mv - T[i - 1].mv;
      const uint32_t into = mv - T[i - 1].mv;
      return (uint8_t)(T[i - 1].pct + (T[i].pct - T[i - 1].pct) * into / span);
    }
  }
  return 100;
}

// Exponential moving average on millivolts so WiFi TX dips do not make the
// number flicker. The first sample seeds it, so boot does not ramp up from 0.
// On top of that the shown percent only moves once the estimate is HYSTERESIS
// points away (0 and 100 snap), so a refresh-induced dip cannot toggle 98/97/98.
class BatteryFilter {
 public:
  void update(uint32_t mv) {
    if (!_seeded) {
      _mv = (float)mv;
      _seeded = true;
      _shown = batteryPercent(mv);
      return;
    }
    _mv += ALPHA * ((float)mv - _mv);
    const uint8_t p = batteryPercent(this->mv());
    const int diff = (int)p - (int)_shown;
    if (diff >= HYSTERESIS || -diff >= HYSTERESIS || p == 0 || p == 100) _shown = p;
  }
  uint32_t mv() const { return (uint32_t)(_mv + 0.5f); }
  BatteryView view() const {
    BatteryView v;
    if (!_seeded || mv() < BAT_NO_CELL_MV) return v;  // default: 100%, onUsb
    v.onUsb = false;
    v.pct = _shown;
    return v;
  }

 private:
  static constexpr float ALPHA = 0.1f;
  static constexpr int HYSTERESIS = 2;
  float _mv = 0.0f;
  bool _seeded = false;
  uint8_t _shown = 100;
};
