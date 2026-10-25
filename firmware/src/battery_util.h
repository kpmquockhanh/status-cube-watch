#pragma once
// Pure battery logic: no Arduino, no ADC, so the host tests in sim/tests can
// build it with a plain compiler. The hardware read is in battery.cpp.
#include <stdint.h>

// Below this the pin is floating or the cell is absent, so the cube can only be
// running from USB.
constexpr uint32_t BAT_NO_CELL_MV = 2500;

// How often battery.cpp samples (and feeds PowerGuess).
constexpr uint32_t BAT_SAMPLE_MS = 5000;

// A guess whether the cube runs on external power. The board has no VBUS or
// charge pin, so it is voltage-based. No cell: external. Otherwise a sustained
// fall (the latest filtered voltage 10 mV or more under the 120 s window's
// maximum) reads as battery even above 4150 mV, so a full cell just unplugged
// is caught while it sinks; failing that, a filtered voltage of at least
// 4150 mV or a rise of 20 mV over the window reads as external. Feed it the filtered
// millivolts the percent uses, once per BAT_SAMPLE_MS. Fixed ring, no heap.
class PowerGuess {
 public:
  static constexpr uint16_t FULL_MV = 4150;
  static constexpr uint16_t RISE_MV = 20;
  static constexpr uint16_t FALL_MV = 10;
  static constexpr uint32_t WINDOW_MS = 120000;

  void sample(uint32_t now, uint16_t mv, bool cell) {
    _cell = cell;
    _seen = true;
    if (!cell) {
      _n = 0;
      return;
    }
    _mv = mv;
    // Drop samples older than the window.
    while (_n > 0 && now - _t[_head] > WINDOW_MS) {
      _head = (_head + 1) % CAP;
      _n--;
    }
    if (_n == CAP) {  // cannot happen at the sample cadence; stay safe
      _head = (_head + 1) % CAP;
      _n--;
    }
    const int idx = (_head + _n) % CAP;
    _t[idx] = now;
    _v[idx] = mv;
    _n++;
    // Until a full window exists (oldest sample within one cadence of 120 s
    // old) the rise rule does not apply.
    _rise = false;
    _fall = false;
    if (now - _t[_head] + BAT_SAMPLE_MS >= WINDOW_MS) {
      // Rise = latest minus the window MINIMUM, so noise up and down is not
      // counted twice.
      uint16_t lo = _v[_head], hi = _v[_head];
      for (int i = 1; i < _n; i++) {
        const uint16_t x = _v[(_head + i) % CAP];
        if (x < lo) lo = x;
        if (x > hi) hi = x;
      }
      _rise = mv >= lo + RISE_MV;
      _fall = mv + FALL_MV <= hi;
    }
  }

  bool external() const {
    if (!_seen) return false;
    if (!_cell) return true;
    if (_fall) return false;  // beats the full-cell rule
    return _mv >= FULL_MV || _rise;
  }

 private:
  static constexpr int CAP = 32;  // 120 s / 5 s = 24, plus slack
  uint32_t _t[CAP] = {};
  uint16_t _v[CAP] = {};
  int _head = 0, _n = 0;
  uint16_t _mv = 0;
  bool _cell = true, _seen = false, _rise = false, _fall = false;
};

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
