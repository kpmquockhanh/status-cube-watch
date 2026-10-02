# Battery Indicator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a battery glyph + percentage in the top bar of every card; with no cell connected (USB only) it shows a full green `100%`.

**Architecture:** Pure, host-tested logic (`battery_util.h`: voltage-to-percent table, smoothing filter, no-cell rule) is wrapped by a thin hardware reader (`battery.cpp`, ADC on GPIO1) with a desktop stand-in (`sim/battery_sim.cpp`). `uiRender` takes a `BatteryView` and `drawTopBar` draws it left of the age readout. Nothing in the bridge or payload contract changes.

**Tech Stack:** C++17, Arduino-ESP32 (`analogReadMilliVolts`), LovyanGFX, PlatformIO; host tests via `sim/Makefile`.

**Spec:** `docs/superpowers/specs/2026-10-02-battery-indicator-design.md`

## Global Constraints

- Not in the payload contract: do not touch `bridge/`, `payload.h/.cpp`, `preview.html`.
- Firmware code shared with the simulator must stay within the `sim/Arduino.h` shim (millis/delay/Serial/min/max/constrain). Hardware-only calls (`analogReadMilliVolts`) live only in `battery.cpp`.
- `VBAT = VADC x 3` on `PIN_BAT_ADC` (GPIO1).
- Percent table anchors: 4200 mV = 100, 3700 mV ~ 50, 3300 mV = 0; clamped.
- No-cell cutoff: smoothed mV < 2500 -> `pct = 100`, `onUsb = true`.
- Colours: green >= 40, amber 15..39, red < 15; outline `DIM`.
- Sampling every ~5 s, 16-sample average; never every frame.
- Indicator on every card incl. Pomodoro; not on editor, portal, OTA, message screens.
- Not a git repo: there are no commit steps. The checkpoint after each task is the stated test/build passing.

## Review Focus

- Smoothed mV exactly at 2500 and just under/over it: under = USB 100%, at/over = cell percent (test the boundary).
- Voltage above 4200 (charging overshoot, e.g. 4260) must read 100, never wrap or exceed.
- Voltage between 2500 and 3300 (deeply discharged cell) must read 0%, not 100% and not negative.
- First sample must seed the filter (no ramp up from 0 at boot).
- Age text `OFFLINE` / `99m` plus a long card title: nothing may overlap or clip at the rounded corners.

---

## File Structure

| File | Responsibility |
|---|---|
| `firmware/src/battery_util.h` (new) | Pure: `BatteryView`, `batteryPercent`, `BatteryFilter`, constants. |
| `firmware/src/battery.h` / `battery.cpp` (new) | Hardware: `batteryBegin/Update/View` using the ADC. |
| `firmware/sim/battery_env.h` (new) | `batteryViewFromEnv()` reads `CUBE_BATTERY`. |
| `firmware/sim/battery_sim.cpp` (new) | Desktop `batteryBegin/Update/View` using the env helper. |
| `firmware/sim/tests/battery_util_test.cpp` (new) | Host tests. |
| `firmware/src/ui.h`, `ui.cpp` | `uiRender(..., const BatteryView &)`, `drawBattery`, `drawTopBar`. |
| `firmware/src/main.cpp` | Call battery API, pass view to `uiRender`. |
| `firmware/sim/shot.cpp`, `sim/Makefile` | Pass env battery to `uiRender`; build/test wiring. |
| `README.md`, `CLAUDE.md` | One line each. |

---

### Task 1: Pure battery logic

**Files:**
- Create: `firmware/src/battery_util.h`
- Create: `firmware/sim/tests/battery_util_test.cpp`
- Modify: `firmware/sim/Makefile` (the `TESTS :=` list)

**Interfaces:**
- Produces:
  - `struct BatteryView { uint8_t pct = 100; bool onUsb = true; };`
  - `constexpr uint32_t BAT_NO_CELL_MV = 2500;`
  - `uint8_t batteryPercent(uint32_t mv);`
  - `class BatteryFilter { public: void update(uint32_t mv); uint32_t mv() const; BatteryView view() const; };`

- [ ] **Step 1: Write the failing test** — `firmware/sim/tests/battery_util_test.cpp`

```cpp
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

}  // namespace

int main() {
  testPercentTable();
  testPercentClamps();
  testFilterSeedsOnFirstSample();
  testFilterConverges();
  testNoCellIsUsb();
  testCellView();
  testDefaultViewBeforeAnySample();
  return checksDone("battery_util");
}
```

In `sim/Makefile`, add `build/battery_util_test` to `TESTS :=`:

```make
TESTS := build/portal_util_test build/pomodoro_test build/gesture_test build/deck_test \
         build/pomo_settings_test build/pomo_editor_test build/battery_util_test
```

- [ ] **Step 2: Run to verify it fails**

Run (from `firmware/sim/`): `make build/battery_util_test`
Expected: FAIL, `battery_util.h: No such file or directory`.

- [ ] **Step 3: Write the implementation** — `firmware/src/battery_util.h`

```cpp
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
class BatteryFilter {
 public:
  void update(uint32_t mv) {
    if (!_seeded) {
      _mv = (float)mv;
      _seeded = true;
    } else {
      _mv += ALPHA * ((float)mv - _mv);
    }
  }
  uint32_t mv() const { return (uint32_t)(_mv + 0.5f); }
  BatteryView view() const {
    BatteryView v;
    if (!_seeded || mv() < BAT_NO_CELL_MV) return v;  // default: 100%, onUsb
    v.onUsb = false;
    v.pct = batteryPercent(mv());
    return v;
  }

 private:
  static constexpr float ALPHA = 0.2f;
  float _mv = 0.0f;
  bool _seeded = false;
};
```

- [ ] **Step 4: Run to verify it passes**

Run: `make build/battery_util_test && ./build/battery_util_test`
Expected: `battery_util: N checks, 0 failed`. Then `make test` still passes as a whole.

---

### Task 2: Hardware reader and simulator stand-in

**Files:**
- Create: `firmware/src/battery.h`, `firmware/src/battery.cpp`
- Create: `firmware/sim/battery_env.h`, `firmware/sim/battery_sim.cpp`
- Modify: `firmware/sim/Makefile` (`SRCS`)

**Interfaces:**
- Consumes: `BatteryView`, `BatteryFilter` from Task 1; `PIN_BAT_ADC` from `board_pins.h`.
- Produces:
  - `void batteryBegin();` seeds the filter with one reading.
  - `void batteryUpdate(uint32_t nowMs);` samples if >= 5000 ms since last; cheap otherwise.
  - `BatteryView batteryView();`
  - sim only: `BatteryView batteryViewFromEnv();` (in `battery_env.h`)

- [ ] **Step 1: Write `firmware/src/battery.h`**

```cpp
#pragma once
#include <stdint.h>

#include "battery_util.h"

// Reads the battery through the board's divider (VBAT = VADC x 3). Call
// batteryUpdate from the main loop; it samples at most every 5 s.
void batteryBegin();
void batteryUpdate(uint32_t nowMs);
BatteryView batteryView();
```

- [ ] **Step 2: Write `firmware/src/battery.cpp`**

```cpp
#include "battery.h"

#include <Arduino.h>

#include "board_pins.h"

namespace {
constexpr uint32_t SAMPLE_EVERY_MS = 5000;
constexpr int SAMPLES = 16;
constexpr int DIVIDER = 3;  // R3 200K over R7 100K: VBAT = VADC x 3

BatteryFilter g_filter;
uint32_t g_lastSample = 0;

void sample() {
  uint32_t sum = 0;
  for (int i = 0; i < SAMPLES; i++) sum += analogReadMilliVolts(PIN_BAT_ADC);
  const uint32_t mv = sum / SAMPLES * DIVIDER;
  g_filter.update(mv);
  // Handy when checking a new board: what the divider really reads.
  Serial.printf("[bat] %lu mV -> %u%%%s\n", (unsigned long)g_filter.mv(), g_filter.view().pct,
                g_filter.view().onUsb ? " (no cell, USB)" : "");
}
}  // namespace

void batteryBegin() {
  sample();
  g_lastSample = millis();
}

void batteryUpdate(uint32_t nowMs) {
  if (nowMs - g_lastSample < SAMPLE_EVERY_MS) return;
  g_lastSample = nowMs;
  sample();
}

BatteryView batteryView() { return g_filter.view(); }
```

- [ ] **Step 3: Write `firmware/sim/battery_env.h`**

```cpp
#pragma once
// The simulator has no battery. CUBE_BATTERY=<0..100> picks a level, "none"
// (or unset => 78) is the no-cell/USB case: a full battery.
#include <cstdlib>
#include <cstring>

#include "../src/battery_util.h"

inline BatteryView batteryViewFromEnv() {
  BatteryView v;
  const char *e = std::getenv("CUBE_BATTERY");
  if (!e) {
    v.pct = 78;
    v.onUsb = false;
    return v;
  }
  if (std::strcmp(e, "none") == 0) return v;  // 100, onUsb
  int p = std::atoi(e);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  v.pct = (uint8_t)p;
  v.onUsb = false;
  return v;
}
```

- [ ] **Step 4: Write `firmware/sim/battery_sim.cpp`**

```cpp
// Desktop stand-in for battery.cpp: no ADC on a laptop, so the level comes from
// the CUBE_BATTERY environment variable (see battery_env.h).
#include "../src/battery.h"
#include "battery_env.h"

void batteryBegin() {}
void batteryUpdate(uint32_t) {}
BatteryView batteryView() { return batteryViewFromEnv(); }
```

- [ ] **Step 5: Wire into `sim/Makefile`**

Add `battery_sim.cpp` to `SRCS`:

```make
SRCS := ../src/main.cpp ../src/ui.cpp ../src/payload.cpp ../src/gesture.cpp ../src/pomodoro.cpp ../src/pomo_editor.cpp \
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp ota_sim.cpp battery_sim.cpp sdl_main.cpp
```

- [ ] **Step 6: Verify it compiles**

Run (from `firmware/sim/`): `make build/battery_sim.o`
Expected: compiles with no errors. (Linking the whole sim happens after Task 3.)

---

### Task 3: Draw the battery in the top bar and thread it through

**Files:**
- Modify: `firmware/src/ui.h`, `firmware/src/ui.cpp` (`drawTopBar` ~195, `drawGaugeCard` ~345 and its `drawTopBar` call ~384, `drawTextCard` ~415/419, `drawPomodoroCard` ~493, `uiRender` ~572)
- Modify: `firmware/src/main.cpp` (`setup`, `loop`)
- Modify: `firmware/sim/shot.cpp` (two `uiRender` calls, ~156 and ~221), `firmware/sim/Makefile` (`SHOT_OBJ`)

**Interfaces:**
- Consumes: `BatteryView` (`battery_util.h`), `batteryBegin/Update/View` (`battery.h`), `batteryViewFromEnv()` (sim).
- Produces: `void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs, const PomoView &pomo, const BatteryView &bat);`

- [ ] **Step 1: Change the `uiRender` signature in `ui.h`**

Add `#include "battery_util.h"` to the includes, and:

```cpp
void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const PomoView &pomo, const BatteryView &bat);
```

- [ ] **Step 2: Add `drawBattery` and rework `drawTopBar` in `ui.cpp`**

Add `#include "battery_util.h"` near the other includes. Replace `drawTopBar` (currently lines ~193-215) with:

```cpp
// Battery glyph + "NN%", right-aligned so its right edge is at `rightX`.
// Returns the x of its left edge so the caller can keep other text clear of it.
int drawBattery(LovyanGFX *g, int rightX, int y, const BatteryView &bat) {
  const uint16_t col = bat.pct >= 40   ? g_palette[ACC_GREEN]
                       : bat.pct >= 15 ? g_palette[ACC_AMBER]
                                       : g_palette[ACC_RED];
  char txt[8];
  snprintf(txt, sizeof(txt), "%u%%", (unsigned)bat.pct);
  g->setFont(&V_S12.font);
  g->setTextDatum(middle_right);
  g->setTextColor(col, BG);
  g->drawString(txt, rightX, y);
  const int textW = g->textWidth(txt);

  constexpr int BODY_W = 18, BODY_H = 9, NUB_W = 2, GAP = 4;
  const int left = rightX - textW - GAP - (BODY_W + NUB_W);
  g->drawRect(left, y - BODY_H / 2, BODY_W, BODY_H, DIM);
  g->fillRect(left + BODY_W, y - 2, NUB_W, 5, DIM);
  const int inner = BODY_W - 4;
  int fill = (inner * bat.pct + 50) / 100;
  if (bat.pct > 0 && fill < 1) fill = 1;
  if (fill > 0) g->fillRect(left + 2, y - BODY_H / 2 + 2, fill, BODY_H - 4, col);
  return left;
}

// `left` is what the bar says on the left: the card's own name on a gauge
// card, which needs no other title, and the data source on a text card.
void drawTopBar(LovyanGFX *g, const char *left, bool online, uint32_t ageMs,
                const BatteryView &bat) {
  // The panel has rounded corners (~35px radius), so the bar is inset from the
  // edges; at the old 12px margin the text and status dot were clipped.
  constexpr int Y = 24;
  constexpr int LEFT_X = 36;

  // Freshness beats a clock here: the board has no NTP sync in this sketch,
  // and what matters is whether the number on screen is current.
  char right[16];
  const uint32_t s = ageMs / 1000;
  if (!online) snprintf(right, sizeof(right), "OFFLINE");
  else if (s < 60) snprintf(right, sizeof(right), "%lus", (unsigned long)s);
  else snprintf(right, sizeof(right), "%lum", (unsigned long)(s / 60));

  const uint16_t col = online && s < 30 ? g_palette[ACC_GREEN] : g_palette[ACC_RED];
  g->setFont(&V_S12.font);
  g->setTextDatum(middle_right);
  g->setTextColor(col, BG);
  g->drawString(right, LCD_WIDTH - 46, Y);
  g->fillCircle(LCD_WIDTH - 36, Y, 3, col);
  const int ageLeft = LCD_WIDTH - 46 - g->textWidth(right);

  const int batLeft = drawBattery(g, ageLeft - 10, Y, bat);

  // The card name takes whatever is left; trim it rather than run into the
  // battery.
  char name[48];
  strlcpy(name, left, sizeof(name));
  const int maxW = batLeft - 8 - LEFT_X;
  for (size_t n = strlen(name); n > 1 && capsWidth(g, name) > maxW; n--) name[n - 1] = '\0';
  drawCaps(g, name, LEFT_X, Y, DIM, middle_left);
}
```

Notes for the implementer: `capsWidth` is defined above `drawTopBar` (line ~177) and measures the un-uppercased string, which is slightly narrower than the drawn caps. Uppercase glyphs are wider, so measure an uppercased copy: if the trim loop leaves clipping in Step 7, uppercase `name` with `toupper` before the loop (same as `drawCaps` does).

- [ ] **Step 3: Thread `bat` through the card functions**

Add a trailing `const BatteryView &bat` parameter and pass it on:
- `drawGaugeCard(..., bool online, uint32_t ageMs, const BatteryView &bat)`, and its call `drawTopBar(g, card.title, online, ageMs, bat);`
- `drawTextCard(..., bool online, uint32_t ageMs, const BatteryView &bat)`, and its call `drawTopBar(g, bar, online, ageMs, bat);`
- `drawPomodoroCard(..., bool online, uint32_t ageMs, const BatteryView &bat)`, passing `bat` into its `drawGaugeCard(...)` call.
- `uiRender(..., const PomoView &pomo, const BatteryView &bat)`, passing `bat` to all three calls (`drawPomodoroCard`, `drawGaugeCard`, `drawTextCard`).

(`drawGaugeCard`'s parameter list is at `ui.cpp:345`; add the parameter after `ageMs`.)

- [ ] **Step 4: Wire `main.cpp`**

Add `#include "battery.h"` with the other includes. In `setup()` after `touch.begin();` add `batteryBegin();`. In `loop()`, just after `pollTouch();` add `batteryUpdate(now);`. Change the render call:

```cpp
    else uiRender(lcd, payload, cardIndex, netOnline(), age, pv, batteryView());
```

Also mark a redraw when the percent changes: before the render block add

```cpp
  static uint8_t lastBatPct = 255;
  const BatteryView bv = batteryView();
  if (bv.pct != lastBatPct) {
    lastBatPct = bv.pct;
    dirty = true;
  }
```

(The 1 s housekeeping redraw would pick it up anyway; this just makes it immediate. If `static` locals are not used elsewhere in `main.cpp`, put `lastBatPct` in the anonymous namespace beside `lastPomoSec` to match style, and use `bv` in the render call instead of calling `batteryView()` again.)

- [ ] **Step 5: Update `shot.cpp` and the Makefile**

Add `#include "battery_env.h"` to `sim/shot.cpp`. Change both calls (lines ~156 and ~221):

```cpp
    uiRender(lcd, none, 0, true, 4000, v, batteryViewFromEnv());
...
      uiRender(lcd, payload, i, true, 4000, idle, batteryViewFromEnv());
```

(`shot.cpp` is compiled with `-I.` in `sim/`, so `battery_env.h` resolves; it includes `../src/battery_util.h` relative to itself.)

- [ ] **Step 6: Build everything**

Run (from `firmware/sim/`): `make clean && make test && make build/cube-sim build/cube-shot`
Expected: all host tests pass; both binaries link. Then (needs the PlatformIO build for the device, from `firmware/`): `pio run`
Expected: SUCCESS. If `pio run` reports `analogReadMilliVolts` undeclared, add `#include <esp32-hal-adc.h>` to `battery.cpp`.

- [ ] **Step 7: Look at it**

Run (from `firmware/sim/`, bridge running or use a saved payload e.g. `fixtures/*.json`):

```sh
CUBE_BATTERY=none ./build/cube-shot build/bat-usb fixtures/<any>.json
CUBE_BATTERY=78   ./build/cube-shot build/bat-78  fixtures/<any>.json
CUBE_BATTERY=30   ./build/cube-shot build/bat-30  fixtures/<any>.json
CUBE_BATTERY=8    ./build/cube-shot build/bat-8   fixtures/<any>.json
./build/cube-shot build/bat-pomo @pomo-focus
```

Open the PNGs. Expected: `none` is a full green glyph with `100%`; 78 green; 30 amber; 8 red with a 1-px-minimum fill; the Pomodoro shot carries it too; the card title never touches the glyph; the age text and dot are unchanged. Check also a long title and the `OFFLINE` state: render a payload with a long card title and pass a different `online` value if needed (temporarily change `true` to `false` in `shot.cpp`, then revert) and confirm nothing clips at the rounded corners.

---

### Task 4: Docs and on-device check

**Files:**
- Modify: `README.md`, `CLAUDE.md`

- [ ] **Step 1: Add one line to each doc**

`CLAUDE.md`, in the Architecture section, after the Pomodoro paragraph:

```
**Battery indicator.** The top bar of every card shows a battery glyph + percent, read on-device from `PIN_BAT_ADC` (`battery.cpp`; logic in `battery_util.h`, host-tested). It is local-only, not in the payload contract and not in `preview.html`. With no cell fitted (mV < 2500) the cube is on USB and shows a full 100%. In the simulator set `CUBE_BATTERY=<0..100>|none` (default 78).
```

`README.md`, in the firmware/simulator section, a matching sentence: the battery indicator, its no-battery behaviour, and `CUBE_BATTERY`.

- [ ] **Step 2: Flash and verify on the real board (user has no battery attached)**

Run (from `firmware/`): `pio run -t upload && pio device monitor`
Expected in the serial log: `[bat] <mV> mV -> 100% (no cell, USB)` about every 5 s, and `100%` with a full green glyph on every card.

If the log shows a `mV` value of 2500 or more with no cell fitted (a floating pin), the indicator will show a bogus percent. In that case raise `BAT_NO_CELL_MV` in `battery_util.h` to just above the logged value (and update the test constants that reference the boundary), and re-run `make test`.
