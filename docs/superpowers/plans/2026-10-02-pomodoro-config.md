# On-device Pomodoro configuration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Edit the Pomodoro focus / short / long / sessions values from the cube's touchscreen (swipe up on the idle Pomodoro card), persisted in NVS.

**Architecture:** A pure, host-tested editor module (`pomo_editor`) owns layout rectangles, hit-testing and step/clamp/reset rules; `ui.cpp` only draws from those rectangles; `settings.cpp` persists four u8 NVS keys; `Pomodoro::setConfig` applies them while idle; `main.cpp` wires a modal `editing` state. Two new gestures (`SwipeUp`/`SwipeDown`) open and close the editor.

**Tech Stack:** C++17, Arduino/PlatformIO (ESP32-S3), LovyanGFX, host unit tests (`make test` in `firmware/sim/`, no framework: `tests/check.h`).

**Spec:** `docs/superpowers/specs/2026-10-02-pomodoro-config-design.md`

**Repo note:** this directory is not a git repository, so there are no commit steps. Each task ends with a verification checkpoint instead.

## Global Constraints

- Ranges: minutes 1-99, sessions 1-9. Defaults 25 / 5 / 15 / 4 (or the `POMO_*` values in `config.h`).
- Steps: FOCUS 5, SHORT 1, LONG 5, SESSIONS 1. Step-5 rows snap to multiples of 5, with 1 as the lower bound: `+` goes to the next multiple above, `-` to the next one below.
- NVS namespace `cube`, keys `pf`, `ps`, `pl`, `pn` (u8). A never-stored key falls back to the `config.h` default.
- Touch targets at least 44x44 px; screen is 240x280 (`LCD_WIDTH`, `LCD_HEIGHT`).
- Firmware code shared with the simulator must stay within `sim/Arduino.h` (millis/delay/Serial/min/max/constrain/strlcpy as already used).
- No bridge, payload, `preview.html` or portal changes. `POMO_TIME_DIV` keeps scaling every duration.
- The editor opens only on the Pomodoro card while the timer is `POMO_IDLE`.
- Tests are host-side only (`firmware/sim/tests/*_test.cpp`, no framework beyond `check.h`).

## Review Focus

1. Swipe up on a running, paused or done Pomodoro, or on any other card, must not open the editor (`editorMayOpen` test, Task 1).
2. A corrupt or out-of-range stored value (0, 200) must never reach the timer: clamped on load and on save (Task 1).
3. A tap in a margin, between the buttons, or on the title must change nothing, and neighbouring rows must not bleed into each other at their boundaries (Task 1).
4. Pressing `-` at the minimum or `+` at the maximum repeatedly must stay put, never wrap an unsigned value (Task 1).
5. A tap's coordinates must be where the finger began, so a small drift while tapping `+` still hits `+` (Task 3).

---

## File Structure

| File | Responsibility |
|------|----------------|
| `firmware/src/pomo_settings.h` (new) | `PomoSettings`, range constants, `pomoClamp`, `pomoConfigFrom`. Header-only, pure. |
| `firmware/src/pomo_editor.h/.cpp` (new) | Editor layout rects, `pomoEditorHit`, `pomoEditorApply`, `editorMayOpen`, labels. Pure. |
| `firmware/src/pomo_defaults.h` (new) | `POMO_DEFAULTS` from `config.h` (moved out of `main.cpp`), with the range `static_assert`s. |
| `firmware/src/pomodoro.h/.cpp` | Add `setConfig`. |
| `firmware/src/gesture.h/.cpp` | Add `SwipeUp`, `SwipeDown`, `startX()`, `startY()`. |
| `firmware/src/settings.h/.cpp`, `firmware/sim/settings_sim.cpp` | `pomoDefaults()`, `pomoSettings()`, `pomoSettingsSave()` (NVS and in-memory). |
| `firmware/src/ui.h/.cpp` | `uiPomodoroEditor()`. |
| `firmware/src/main.cpp` | `editing` state, gesture handling, build `Pomodoro` from settings. |
| `firmware/sim/shot.cpp`, `firmware/sim/Makefile` | `@pomo-edit` shot; build and test rules. |
| `firmware/sim/tests/` | `pomo_settings_test.cpp`, `pomo_editor_test.cpp` (new); `pomodoro_test.cpp`, `gesture_test.cpp` (extended). |
| `README.md`, `CLAUDE.md`, `firmware/src/config.h.example` | Docs. |

---

### Task 1: Settings type and pure editor logic

**Files:**
- Create: `firmware/src/pomo_settings.h`, `firmware/src/pomo_editor.h`, `firmware/src/pomo_editor.cpp`
- Test: `firmware/sim/tests/pomo_settings_test.cpp`, `firmware/sim/tests/pomo_editor_test.cpp`
- Modify: `firmware/sim/Makefile` (TESTS list and rule)

**Interfaces:**
- Produces:
  - `struct PomoSettings { uint8_t focusMin, shortMin, longMin, sessions; }`
  - `PomoSettings pomoClamp(PomoSettings)`; `PomoConfig pomoConfigFrom(const PomoSettings&, uint32_t timeDiv)`
  - `struct EditRect { int16_t x, y, w, h; bool contains(int px, int py) const; }`
  - `constexpr int EDIT_ROWS = 4`; `EditRect editorRow(int)`, `editorMinus(int)`, `editorPlus(int)`, `editorResetBtn()`, `editorDoneBtn()`; `const char *editorLabel(int row)`
  - `enum class EditAction : uint8_t { None, Dec, Inc, Reset, Done }`; `struct EditHit { EditAction action; uint8_t row; }`
  - `EditHit pomoEditorHit(int16_t x, int16_t y)`
  - `void pomoEditorApply(PomoSettings &s, EditHit hit, const PomoSettings &defaults)`
  - `bool editorMayOpen(bool onPomodoro, PomoState state)`

- [ ] **Step 1: Write the failing settings test**

Create `firmware/sim/tests/pomo_settings_test.cpp`:

```cpp
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
```

- [ ] **Step 2: Write the failing editor test**

Create `firmware/sim/tests/pomo_editor_test.cpp`:

```cpp
#include <cstdint>

#include "check.h"
#include "pomo_editor.h"

namespace {

const PomoSettings DEF{25, 5, 15, 4};

EditRect centre(const EditRect &r, int &x, int &y) {
  x = r.x + r.w / 2;
  y = r.y + r.h / 2;
  return r;
}

void testTargetsAreBigEnough() {
  for (int r = 0; r < EDIT_ROWS; r++) {
    CHECK(editorMinus(r).w >= 44 && editorMinus(r).h >= 44);
    CHECK(editorPlus(r).w >= 44 && editorPlus(r).h >= 44);
  }
  CHECK(editorResetBtn().h >= 44 && editorDoneBtn().h >= 44);
  CHECK(editorDoneBtn().y + editorDoneBtn().h <= 280);  // fits the panel
}

void testHitEveryButton() {
  int x, y;
  for (int r = 0; r < EDIT_ROWS; r++) {
    centre(editorMinus(r), x, y);
    EditHit h = pomoEditorHit((int16_t)x, (int16_t)y);
    CHECK(h.action == EditAction::Dec && h.row == r);
    centre(editorPlus(r), x, y);
    h = pomoEditorHit((int16_t)x, (int16_t)y);
    CHECK(h.action == EditAction::Inc && h.row == r);
  }
  centre(editorResetBtn(), x, y);
  CHECK(pomoEditorHit((int16_t)x, (int16_t)y).action == EditAction::Reset);
  centre(editorDoneBtn(), x, y);
  CHECK(pomoEditorHit((int16_t)x, (int16_t)y).action == EditAction::Done);
}

// Review Focus 3: margins, the value area, the title and off-panel taps do nothing.
void testMissesDoNothing() {
  const EditRect m = editorMinus(1);
  const EditRect p = editorPlus(1);
  CHECK(pomoEditorHit(120, m.y + 20).action == EditAction::None);       // value area
  CHECK(pomoEditorHit((int16_t)(m.x - 1), m.y + 20).action == EditAction::None);  // left margin
  CHECK(pomoEditorHit((int16_t)(p.x + p.w), p.y + 20).action == EditAction::None);  // right margin
  CHECK(pomoEditorHit(30, 10).action == EditAction::None);               // title
  CHECK(pomoEditorHit(30, 279).action == EditAction::None);              // below the buttons
  CHECK(pomoEditorHit(-5, -5).action == EditAction::None);
  // The gap between RESET and DONE belongs to neither.
  const EditRect rb = editorResetBtn();
  const EditRect db = editorDoneBtn();
  if (rb.x + rb.w < db.x) CHECK(pomoEditorHit((int16_t)(rb.x + rb.w), rb.y + 5).action == EditAction::None);
}

// Review Focus 3: rows are exclusive at their shared boundary.
void testRowBoundaries() {
  for (int r = 0; r + 1 < EDIT_ROWS; r++) {
    const EditRect a = editorMinus(r);
    const EditHit last = pomoEditorHit((int16_t)(a.x + 5), (int16_t)(a.y + a.h - 1));
    const EditHit next = pomoEditorHit((int16_t)(a.x + 5), (int16_t)(a.y + a.h));
    CHECK(last.action == EditAction::Dec && last.row == r);
    CHECK(next.action == EditAction::Dec && next.row == r + 1);
  }
}

PomoSettings press(PomoSettings s, EditAction a, uint8_t row, int times = 1) {
  for (int i = 0; i < times; i++) pomoEditorApply(s, EditHit{a, row}, DEF);
  return s;
}

void testStepOneRows() {
  CHECK(press(DEF, EditAction::Inc, 1).shortMin == 6);
  CHECK(press(DEF, EditAction::Dec, 1).shortMin == 4);
  CHECK(press(DEF, EditAction::Inc, 3).sessions == 5);
  CHECK(press(DEF, EditAction::Dec, 3).sessions == 3);
}

void testStepFiveSnaps() {
  CHECK(press(DEF, EditAction::Inc, 0).focusMin == 30);
  CHECK(press(DEF, EditAction::Dec, 0).focusMin == 20);
  CHECK(press(PomoSettings{27, 5, 15, 4}, EditAction::Inc, 0).focusMin == 30);
  CHECK(press(PomoSettings{27, 5, 15, 4}, EditAction::Dec, 0).focusMin == 25);
  CHECK(press(PomoSettings{1, 5, 15, 4}, EditAction::Inc, 0).focusMin == 5);
  CHECK(press(PomoSettings{5, 5, 15, 4}, EditAction::Dec, 0).focusMin == 1);
  CHECK(press(PomoSettings{98, 5, 15, 4}, EditAction::Inc, 0).focusMin == 99);
  CHECK(press(PomoSettings{99, 5, 15, 4}, EditAction::Dec, 0).focusMin == 95);
  CHECK(press(DEF, EditAction::Inc, 2).longMin == 20);  // LONG steps by 5 too
}

// Review Focus 4: holding a button at the limit stays at the limit.
void testClampsNeverWrap() {
  CHECK(press(DEF, EditAction::Dec, 0, 50).focusMin == 1);
  CHECK(press(DEF, EditAction::Inc, 0, 50).focusMin == 99);
  CHECK(press(DEF, EditAction::Dec, 1, 50).shortMin == 1);
  CHECK(press(DEF, EditAction::Inc, 1, 200).shortMin == 99);
  CHECK(press(DEF, EditAction::Dec, 3, 50).sessions == 1);
  CHECK(press(DEF, EditAction::Inc, 3, 50).sessions == 9);
}

void testResetAndNoOps() {
  PomoSettings s{40, 10, 30, 2};
  pomoEditorApply(s, EditHit{EditAction::Reset, 0}, DEF);
  CHECK(s.focusMin == 25 && s.shortMin == 5 && s.longMin == 15 && s.sessions == 4);
  PomoSettings t{40, 10, 30, 2};
  pomoEditorApply(t, EditHit{EditAction::None, 0}, DEF);
  pomoEditorApply(t, EditHit{EditAction::Done, 0}, DEF);  // Done is acted on by the caller
  CHECK(t.focusMin == 40 && t.shortMin == 10 && t.longMin == 30 && t.sessions == 2);
  pomoEditorApply(t, EditHit{EditAction::Inc, 9}, DEF);  // a bad row index is ignored
  CHECK(t.focusMin == 40 && t.sessions == 2);
}

// Review Focus 1: only an idle timer, on the Pomodoro card, may be edited.
void testMayOpen() {
  CHECK(editorMayOpen(true, POMO_IDLE));
  CHECK(!editorMayOpen(true, POMO_FOCUS));
  CHECK(!editorMayOpen(true, POMO_BREAK));
  CHECK(!editorMayOpen(true, POMO_PAUSED));
  CHECK(!editorMayOpen(true, POMO_DONE));
  CHECK(!editorMayOpen(false, POMO_IDLE));
}

void testLabels() {
  CHECK(editorLabel(0)[0] == 'F');
  CHECK(editorLabel(3)[0] == 'S');
  CHECK(editorLabel(7)[0] == '\0');
}

}  // namespace

int main() {
  testTargetsAreBigEnough();
  testHitEveryButton();
  testMissesDoNothing();
  testRowBoundaries();
  testStepOneRows();
  testStepFiveSnaps();
  testClampsNeverWrap();
  testResetAndNoOps();
  testMayOpen();
  testLabels();
  return checksDone("pomo_editor_test");
}
```

- [ ] **Step 3: Register the tests in the Makefile**

In `firmware/sim/Makefile`, change the `TESTS` line and add the source rule:

```make
TESTS := build/portal_util_test build/pomodoro_test build/gesture_test build/deck_test \
         build/pomo_settings_test build/pomo_editor_test

build/pomodoro_test: ../src/pomodoro.cpp
build/gesture_test: ../src/gesture.cpp
build/pomo_editor_test: ../src/pomo_editor.cpp
```

- [ ] **Step 4: Run to verify it fails**

Run: `cd firmware/sim && make test`
Expected: FAIL to compile, `pomo_settings.h: No such file or directory`.

- [ ] **Step 5: Write `pomo_settings.h`**

Create `firmware/src/pomo_settings.h`:

```cpp
#pragma once
#include <stdint.h>

#include "pomodoro.h"

// What the on-device editor changes. Pure data plus the rules for keeping it
// sane, so the host tests can exercise it without NVS or Arduino.
struct PomoSettings {
  uint8_t focusMin, shortMin, longMin;  // minutes, 1..99
  uint8_t sessions;                     // focus sessions before the long break, 1..9
};

constexpr int POMO_MIN_MINUTES = 1;
constexpr int POMO_MAX_MINUTES = 99;
constexpr int POMO_MIN_SESSIONS = 1;
constexpr int POMO_MAX_SESSIONS = 9;

inline uint8_t pomoClampU8(int v, int lo, int hi) {
  return (uint8_t)(v < lo ? lo : (v > hi ? hi : v));
}

// A stored or edited value that is out of range (a corrupt NVS key) never
// reaches the timer.
inline PomoSettings pomoClamp(PomoSettings s) {
  s.focusMin = pomoClampU8(s.focusMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  s.shortMin = pomoClampU8(s.shortMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  s.longMin = pomoClampU8(s.longMin, POMO_MIN_MINUTES, POMO_MAX_MINUTES);
  s.sessions = pomoClampU8(s.sessions, POMO_MIN_SESSIONS, POMO_MAX_SESSIONS);
  return s;
}

// `timeDiv` is POMO_TIME_DIV: 1 on the board, 60 for `make run POMO_FAST=60`.
inline PomoConfig pomoConfigFrom(const PomoSettings &s, uint32_t timeDiv) {
  return PomoConfig{s.focusMin * 60000UL / timeDiv, s.shortMin * 60000UL / timeDiv,
                    s.longMin * 60000UL / timeDiv, s.sessions};
}
```

- [ ] **Step 6: Write `pomo_editor.h`**

Create `firmware/src/pomo_editor.h`:

```cpp
#pragma once
#include <stdint.h>

#include "pomo_settings.h"
#include "pomodoro.h"

// The Pomodoro settings editor, minus the drawing. Layout rectangles live
// here and ui.cpp draws from them, so what is drawn and what is hit-tested
// cannot drift apart. Pure: no Arduino, no LovyanGFX.
//
//   y= 0..44    title
//   y=44..228   four rows of 46 px:  [ - ]   LABEL / value   [ + ]
//   y=234..278  [ RESET ] [ DONE ]
constexpr int EDIT_ROWS = 4;  // FOCUS, SHORT, LONG, SESSIONS

struct EditRect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

EditRect editorRow(int row);  // the whole row, for placing the label and value
EditRect editorMinus(int row);
EditRect editorPlus(int row);
EditRect editorResetBtn();
EditRect editorDoneBtn();
const char *editorLabel(int row);  // "" for a row that does not exist

enum class EditAction : uint8_t { None, Dec, Inc, Reset, Done };
struct EditHit {
  EditAction action;
  uint8_t row;  // meaningful for Dec / Inc
};

EditHit pomoEditorHit(int16_t x, int16_t y);

// Applies Dec / Inc (step, snap, clamp) or Reset (to `defaults`). None and
// Done leave `s` alone: the caller acts on Done.
void pomoEditorApply(PomoSettings &s, EditHit hit, const PomoSettings &defaults);

// True when a swipe up should open the editor: only the Pomodoro card, and
// only while the timer is idle, so a running session is never resized.
inline bool editorMayOpen(bool onPomodoro, PomoState state) {
  return onPomodoro && state == POMO_IDLE;
}
```

- [ ] **Step 7: Write `pomo_editor.cpp`**

Create `firmware/src/pomo_editor.cpp`:

```cpp
#include "pomo_editor.h"

namespace {

constexpr int ROW_Y0 = 44;
constexpr int ROW_H = 46;
constexpr int BTN_W = 48;
constexpr int MARGIN = 10;
constexpr int PANEL_W = 240;
constexpr int BAR_Y = 234;
constexpr int BAR_H = 44;

struct RowSpec {
  uint8_t step;
  uint8_t lo, hi;
  const char *label;
};
const RowSpec ROWS[EDIT_ROWS] = {
    {5, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "FOCUS"},
    {1, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "SHORT BREAK"},
    {5, POMO_MIN_MINUTES, POMO_MAX_MINUTES, "LONG BREAK"},
    {1, POMO_MIN_SESSIONS, POMO_MAX_SESSIONS, "SESSIONS"},
};

uint8_t *field(PomoSettings &s, int row) {
  switch (row) {
    case 0: return &s.focusMin;
    case 1: return &s.shortMin;
    case 2: return &s.longMin;
    default: return &s.sessions;
  }
}

// Step-1 rows move by one. Step-5 rows snap to multiples of 5 so the usual
// 25 / 50 / 15 are reachable in a few taps: `+` goes to the next multiple
// above, `-` to the next one below, both clamped (1 is the floor, so 5 -> 1).
uint8_t stepped(uint8_t v, bool up, const RowSpec &spec) {
  int next;
  if (spec.step == 1) {
    next = up ? v + 1 : v - 1;
  } else if (up) {
    next = (v / 5 + 1) * 5;
  } else {
    next = (v % 5 == 0) ? v - 5 : (v / 5) * 5;
  }
  return pomoClampU8(next, spec.lo, spec.hi);
}

}  // namespace

EditRect editorRow(int row) {
  return EditRect{0, (int16_t)(ROW_Y0 + row * ROW_H), PANEL_W, ROW_H};
}
EditRect editorMinus(int row) {
  return EditRect{MARGIN, (int16_t)(ROW_Y0 + row * ROW_H), BTN_W, ROW_H};
}
EditRect editorPlus(int row) {
  return EditRect{(int16_t)(PANEL_W - MARGIN - BTN_W), (int16_t)(ROW_Y0 + row * ROW_H), BTN_W, ROW_H};
}
EditRect editorResetBtn() { return EditRect{MARGIN, BAR_Y, 108, BAR_H}; }
EditRect editorDoneBtn() { return EditRect{122, BAR_Y, 108, BAR_H}; }

const char *editorLabel(int row) { return (row >= 0 && row < EDIT_ROWS) ? ROWS[row].label : ""; }

EditHit pomoEditorHit(int16_t x, int16_t y) {
  for (int r = 0; r < EDIT_ROWS; r++) {
    if (editorMinus(r).contains(x, y)) return EditHit{EditAction::Dec, (uint8_t)r};
    if (editorPlus(r).contains(x, y)) return EditHit{EditAction::Inc, (uint8_t)r};
  }
  if (editorResetBtn().contains(x, y)) return EditHit{EditAction::Reset, 0};
  if (editorDoneBtn().contains(x, y)) return EditHit{EditAction::Done, 0};
  return EditHit{EditAction::None, 0};
}

void pomoEditorApply(PomoSettings &s, EditHit hit, const PomoSettings &defaults) {
  switch (hit.action) {
    case EditAction::Dec:
    case EditAction::Inc:
      if (hit.row >= EDIT_ROWS) return;
      *field(s, hit.row) = stepped(*field(s, hit.row), hit.action == EditAction::Inc, ROWS[hit.row]);
      break;
    case EditAction::Reset:
      s = defaults;
      break;
    default:
      break;
  }
}
```

- [ ] **Step 8: Run to verify it passes**

Run: `cd firmware/sim && make test`
Expected: every test prints `0 failed`, including `pomo_settings_test` and `pomo_editor_test`.

---

### Task 2: `Pomodoro::setConfig`

**Files:**
- Modify: `firmware/src/pomodoro.h`, `firmware/src/pomodoro.cpp`
- Test: `firmware/sim/tests/pomodoro_test.cpp`

**Interfaces:**
- Consumes: `PomoConfig` (existing).
- Produces: `void Pomodoro::setConfig(const PomoConfig &cfg)` -- replaces the config only while `POMO_IDLE`, and refreshes the idle countdown; ignored in every other state.

- [ ] **Step 1: Write the failing tests**

In `firmware/sim/tests/pomodoro_test.cpp`, add inside the anonymous namespace (before the closing `}  // namespace`):

```cpp
void testSetConfigWhileIdle() {
  Rig r;
  r.p.setConfig(PomoConfig{50 * MIN, 10 * MIN, 30 * MIN, 2});
  PomoView v = r.p.view();
  CHECK(v.state == POMO_IDLE);
  CHECK(v.displaySec == 3000);  // the idle clock shows the new focus length
  CHECK(v.fraction == 100);
  CHECK(v.sessions == 2);
  r.press();
  r.advance(50 * MIN - 1);
  CHECK(r.p.view().state == POMO_FOCUS);
  r.advance(1);
  CHECK(r.p.view().state == POMO_DONE);  // the new length is honoured
}

// A running phase is never resized under the user.
void testSetConfigIgnoredWhenNotIdle() {
  const PomoConfig other{50 * MIN, 10 * MIN, 30 * MIN, 2};

  Rig run;
  run.press();
  run.advance(MIN);
  run.p.setConfig(other);
  CHECK(run.p.view().displaySec == 24 * 60);
  CHECK(run.p.view().sessions == 4);

  Rig pau;
  pau.press();
  pau.advance(MIN);
  pau.press();  // paused
  pau.p.setConfig(other);
  CHECK(pau.p.view().state == POMO_PAUSED);
  CHECK(pau.p.view().displaySec == 24 * 60);

  Rig done;
  done.press();
  done.advance(25 * MIN);
  done.p.setConfig(other);
  CHECK(done.p.view().state == POMO_DONE);
  CHECK(done.p.view().sessions == 4);
  done.press();  // starts the break with the ORIGINAL 5 min
  CHECK(done.p.view().displaySec == 5 * 60);
}
```

and add the two calls to `main()` before `return checksDone(...)`:

```cpp
  testSetConfigWhileIdle();
  testSetConfigIgnoredWhenNotIdle();
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/sim && make build/pomodoro_test`
Expected: FAIL to compile, `no member named 'setConfig' in 'Pomodoro'`.

- [ ] **Step 3: Implement**

In `firmware/src/pomodoro.h`, in the public section after `reset()`:

```cpp
  // Replaces the durations. Only honoured while IDLE (the editor only opens
  // then); a running, paused or finished phase keeps the lengths it began
  // with. Refreshes the idle countdown so the card shows the new focus length.
  void setConfig(const PomoConfig &cfg);
```

In `firmware/src/pomodoro.cpp`, after `Pomodoro::reset()`:

```cpp
void Pomodoro::setConfig(const PomoConfig &cfg) {
  if (_state != POMO_IDLE) return;
  _cfg = cfg;
  _left = _cfg.focusMs;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd firmware/sim && make test`
Expected: all tests `0 failed`.

---

### Task 3: Vertical swipes and the touch start point

**Files:**
- Modify: `firmware/src/gesture.h`, `firmware/src/gesture.cpp`
- Test: `firmware/sim/tests/gesture_test.cpp`

**Interfaces:**
- Produces: `Gesture::SwipeUp` (finger moved up), `Gesture::SwipeDown`; `int16_t GestureTracker::startX() const`, `startY() const` (where the latest touch began, valid after it is released).

- [ ] **Step 1: Update the existing test, then write the failing ones**

In `firmware/sim/tests/gesture_test.cpp`, `testNonGestures()`: a mostly-vertical drag is now a vertical swipe. Replace

```cpp
  // A mostly-vertical drag is not a horizontal swipe.
  p.at(2000, true, 100, 200);
  p.at(2100, true, 102, 120);
  CHECK(p.at(2150, false, 0, 0) == Gesture::None);
```

with

```cpp
  // A mostly-vertical drag is not a horizontal swipe (it is a vertical one).
  p.at(2000, true, 100, 200);
  p.at(2100, true, 102, 120);
  CHECK(p.at(2150, false, 0, 0) == Gesture::SwipeUp);
```

Add these tests inside the anonymous namespace:

```cpp
void testVerticalSwipes() {
  Pad p;
  p.at(0, true, 100, 220);
  p.at(100, true, 101, 170);
  p.at(150, true, 100, 120);
  CHECK(p.at(200, false, 0, 0) == Gesture::SwipeUp);  // finger moved up the screen

  p.at(1000, true, 100, 60);
  p.at(1100, true, 99, 110);
  p.at(1150, true, 100, 160);
  CHECK(p.at(1200, false, 0, 0) == Gesture::SwipeDown);
}

// The dominant axis wins on a diagonal; a short or slow vertical drag is nothing.
void testVerticalClassification() {
  Pad p;
  p.at(0, true, 100, 200);
  p.at(100, true, 130, 130);  // dx 30, dy -70
  CHECK(p.at(150, false, 0, 0) == Gesture::SwipeUp);

  p.at(1000, true, 100, 100);
  p.at(1100, true, 190, 130);  // dx 90, dy 30
  CHECK(p.at(1150, false, 0, 0) == Gesture::SwipePrev);

  p.at(2000, true, 100, 200);
  p.at(2100, true, 100, 170);  // 30 px: more than a tap, less than a swipe
  CHECK(p.at(2150, false, 0, 0) == Gesture::None);

  p.at(3000, true, 100, 220);
  p.at(3400, true, 100, 170);
  p.at(3800, true, 100, 120);  // 100 px but 800 ms: too slow
  CHECK(p.at(3801, false, 0, 0) == Gesture::None);
}

// A vertical drag on the hold-enabled Pomodoro card must not become a hold.
void testVerticalSwipeOnHoldCard() {
  Pad p;
  p.at(0, true, 100, 220, true);
  p.at(100, true, 100, 170, true);
  p.at(200, true, 100, 120, true);
  CHECK(p.at(250, false, 0, 0) == Gesture::SwipeUp);
}

// Review Focus 5: a tap is positioned where the finger went down.
void testStartPoint() {
  Pad p;
  p.at(0, true, 120, 150);
  p.at(40, true, 125, 154);  // a little drift, still a tap
  CHECK(p.at(80, false, 0, 0) == Gesture::Tap);
  CHECK(p.t.startX() == 120);
  CHECK(p.t.startY() == 150);
  p.at(500, true, 30, 40);
  p.at(540, false, 0, 0);
  CHECK(p.t.startX() == 30);
  CHECK(p.t.startY() == 40);
}
```

Add to `main()` before `return checksDone`:

```cpp
  testVerticalSwipes();
  testVerticalClassification();
  testVerticalSwipeOnHoldCard();
  testStartPoint();
```

- [ ] **Step 2: Run to verify it fails**

Run: `cd firmware/sim && make build/gesture_test`
Expected: FAIL to compile, `no member named 'SwipeUp'`.

- [ ] **Step 3: Implement**

In `firmware/src/gesture.h`, add to the enum after `SwipePrev,`:

```cpp
  SwipeUp,     // finger moved up the screen (opens the Pomodoro editor)
  SwipeDown,   // finger moved down (closes it)
```

and in `GestureTracker`'s public section after `update(...)`:

```cpp
  // Where the latest touch began. A tap carries no coordinates of its own, so
  // the editor hit-tests here: a few pixels of drift between down and up must
  // not move the press onto a neighbouring button.
  int16_t startX() const { return _sx; }
  int16_t startY() const { return _sy; }
```

In `firmware/src/gesture.cpp`, replace the swipe block at the end:

```cpp
  if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy) && dt <= SWIPE_MAX_MS) {
    return dx < 0 ? Gesture::SwipeNext : Gesture::SwipePrev;
  }
```

with

```cpp
  if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy) && dt <= SWIPE_MAX_MS) {
    return dx < 0 ? Gesture::SwipeNext : Gesture::SwipePrev;
  }
  if (abs(dy) >= SWIPE_MIN_PX && abs(dy) > abs(dx) && dt <= SWIPE_MAX_MS) {
    return dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
  }
```

- [ ] **Step 4: Run to verify it passes**

Run: `cd firmware/sim && make test`
Expected: all tests `0 failed`.

---

### Task 4: Persistence

**Files:**
- Create: `firmware/src/pomo_defaults.h`
- Modify: `firmware/src/settings.h`, `firmware/src/settings.cpp`, `firmware/sim/settings_sim.cpp`, `firmware/src/main.cpp:21-45` (remove the moved defaults only)

**Interfaces:**
- Consumes: `PomoSettings`, `pomoClamp` (Task 1).
- Produces: `PomoSettings pomoDefaults()`; `const PomoSettings &pomoSettings()`; `bool pomoSettingsSave(const PomoSettings &)` (clamps, makes it current even if NVS fails, returns whether it persisted).

NVS and `config.h` are board-only, so this task is verified by compiling the simulator and by Task 6's on-hardware check, not by a host unit test (the clamp rules it relies on are tested in Task 1).

- [ ] **Step 1: Create `pomo_defaults.h`**

```cpp
#pragma once
// The compile-time Pomodoro defaults: what a cube with nothing stored in NVS
// uses, and what the editor's RESET returns to. Includes config.h, so only the
// settings layer (and its simulator stand-in) includes this.
#include "config.h"
#include "pomo_settings.h"

// Defaults, so a config.h from before the Pomodoro still builds.
#ifndef POMO_FOCUS_MIN
#define POMO_FOCUS_MIN 25
#endif
#ifndef POMO_BREAK_MIN
#define POMO_BREAK_MIN 5
#endif
#ifndef POMO_LONG_MIN
#define POMO_LONG_MIN 15
#endif
#ifndef POMO_SESSIONS
#define POMO_SESSIONS 4
#endif

static_assert(POMO_FOCUS_MIN >= 1 && POMO_FOCUS_MIN <= 99, "POMO_FOCUS_MIN must be 1..99");
static_assert(POMO_BREAK_MIN >= 1 && POMO_BREAK_MIN <= 99, "POMO_BREAK_MIN must be 1..99");
static_assert(POMO_LONG_MIN >= 1 && POMO_LONG_MIN <= 99, "POMO_LONG_MIN must be 1..99");
static_assert(POMO_SESSIONS >= 1 && POMO_SESSIONS <= 9, "POMO_SESSIONS must be 1..9");

constexpr PomoSettings POMO_DEFAULTS{POMO_FOCUS_MIN, POMO_BREAK_MIN, POMO_LONG_MIN, POMO_SESSIONS};
```

- [ ] **Step 2: Remove the moved block from `main.cpp`**

In `firmware/src/main.cpp` delete the four `#ifndef POMO_*` blocks and the four `static_assert` lines (the "Defaults, so a config.h from before..." comment through the `POMO_SESSIONS` assert). Keep the `POMO_TIME_DIV` comment and `#ifndef POMO_TIME_DIV` block.

- [ ] **Step 3: Extend `settings.h`**

Add `#include "pomo_settings.h"` after the existing includes, and at the end of the file:

```cpp

// Pomodoro durations, edited on the cube itself. Stored apart from the WiFi
// settings so saving one never rewrites the other; a never-stored key falls
// back to the config.h default (pomoDefaults()).
PomoSettings pomoDefaults();               // the compile-time values (RESET target)
const PomoSettings &pomoSettings();        // current, always within range
// Clamps and makes `s` current at once; returns whether it reached flash. If
// NVS is unavailable the new values still apply until the next reboot.
bool pomoSettingsSave(const PomoSettings &s);
```

- [ ] **Step 4: Implement in `settings.cpp`**

Add `#include "pomo_defaults.h"` after `#include "config.h"`. In the anonymous namespace add:

```cpp
PomoSettings g_pomo = POMO_DEFAULTS;

uint8_t loadU8(Preferences &p, const char *key, uint8_t fallback) {
  return p.isKey(key) ? p.getUChar(key, fallback) : fallback;
}
```

In `useDefaults()` add `g_pomo = POMO_DEFAULTS;`. In `settingsLoad()`, before `p.end();` add:

```cpp
  g_pomo = pomoClamp(PomoSettings{loadU8(p, "pf", POMO_DEFAULTS.focusMin),
                                  loadU8(p, "ps", POMO_DEFAULTS.shortMin),
                                  loadU8(p, "pl", POMO_DEFAULTS.longMin),
                                  loadU8(p, "pn", POMO_DEFAULTS.sessions)});
```

At the end of the file add:

```cpp
PomoSettings pomoDefaults() { return POMO_DEFAULTS; }

const PomoSettings &pomoSettings() { return g_pomo; }

bool pomoSettingsSave(const PomoSettings &s) {
  g_pomo = pomoClamp(s);  // applied even if flash is unavailable
  Preferences p;
  if (!p.begin(NS, false)) return false;
  p.putUChar("pf", g_pomo.focusMin);
  p.putUChar("ps", g_pomo.shortMin);
  p.putUChar("pl", g_pomo.longMin);
  p.putUChar("pn", g_pomo.sessions);
  p.end();
  return true;
}
```

- [ ] **Step 5: Implement the simulator stand-in**

In `firmware/sim/settings_sim.cpp` add `#include "../src/pomo_defaults.h"` after the existing includes, add `PomoSettings g_pomo = POMO_DEFAULTS;` inside the anonymous namespace, `g_pomo = POMO_DEFAULTS;` at the end of `settingsLoad()`, and at the end of the file:

```cpp
PomoSettings pomoDefaults() { return POMO_DEFAULTS; }

const PomoSettings &pomoSettings() { return g_pomo; }

bool pomoSettingsSave(const PomoSettings &s) {
  g_pomo = pomoClamp(s);
  Serial.println("[settings] (sim) pomodoro not persisted");
  return true;
}
```

- [ ] **Step 6: Verify the simulator still compiles**

Run: `cd firmware/sim && make build/settings_sim.o && make test`
Expected: compiles; all tests `0 failed`. (`main.cpp` is not recompiled yet; Task 6 makes it use the new API.)

---

### Task 5: Editor screen and the `@pomo-edit` shot

**Files:**
- Modify: `firmware/src/ui.h`, `firmware/src/ui.cpp`, `firmware/sim/shot.cpp`, `firmware/sim/Makefile`

**Interfaces:**
- Consumes: `editorRow/Minus/Plus/ResetBtn/DoneBtn`, `editorLabel`, `EDIT_ROWS` (Task 1); `PomoSettings`.
- Produces: `void uiPomodoroEditor(Display &lcd, const PomoSettings &s)` -- draws one full frame and pushes it.

- [ ] **Step 1: Declare it in `ui.h`**

Add `#include "pomo_settings.h"` with the other includes and, after `uiAlertStart()`:

```cpp
// The Pomodoro settings editor: four rows of - / + and a RESET / DONE bar.
// One static frame; hit-testing is in pomo_editor.h, which owns the layout.
void uiPomodoroEditor(Display &lcd, const PomoSettings &s);
```

- [ ] **Step 2: Draw it in `ui.cpp`**

Add `#include "pomo_editor.h"` after `#include "fonts_gen.h"`. In the anonymous namespace, just before the `// --- phase-end alert` comment, add:

```cpp
// --- the Pomodoro editor ---------------------------------------------------
// A button is its hit rectangle inset by 3 px, so neighbours never touch while
// the full rectangle stays the touch target.
void drawEditorButton(LovyanGFX *g, const EditRect &r, const char *text, uint16_t fill,
                      uint16_t ink) {
  constexpr int GAP = 3;
  g->fillRoundRect(r.x + GAP, r.y + GAP, r.w - 2 * GAP, r.h - 2 * GAP, 8, fill);
  g->setFont(text[1] == '\0' ? &V_B24.font : &V_B18.font);  // a lone - or + is drawn big
  g->setTextDatum(middle_center);
  g->setTextColor(ink, fill);
  g->drawString(text, r.x + r.w / 2, r.y + r.h / 2);
}
```

After `uiMessage(...)` add:

```cpp
void uiPomodoroEditor(Display &lcd, const PomoSettings &s) {
  g_animating = false;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  drawCaps(g, "Pomodoro", LCD_WIDTH / 2, 24, g_palette[ACC_ACCENT], middle_center);

  const uint8_t vals[EDIT_ROWS] = {s.focusMin, s.shortMin, s.longMin, s.sessions};
  for (int r = 0; r < EDIT_ROWS; r++) {
    const EditRect row = editorRow(r);
    drawEditorButton(g, editorMinus(r), "-", FAINT, INK);
    drawEditorButton(g, editorPlus(r), "+", FAINT, INK);
    drawCaps(g, editorLabel(r), LCD_WIDTH / 2, row.y + 12, DIM, middle_center);
    char buf[12];
    if (r == EDIT_ROWS - 1) snprintf(buf, sizeof(buf), "%u", (unsigned)vals[r]);
    else snprintf(buf, sizeof(buf), "%u min", (unsigned)vals[r]);
    g->setFont(&V_B24.font);
    g->setTextDatum(middle_center);
    g->setTextColor(INK, BG);
    g->drawString(buf, LCD_WIDTH / 2, row.y + 31);
  }
  drawEditorButton(g, editorResetBtn(), "RESET", FAINT, INK);
  drawEditorButton(g, editorDoneBtn(), "DONE", g_palette[ACC_ACCENT], BG);

  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}
```

- [ ] **Step 3: Add the shot and the build rules**

In `firmware/sim/shot.cpp`, in `renderSpecial`, insert before the `else if (!strncmp(name, "pomo-", 5))` branch:

```cpp
  } else if (!strcmp(name, "pomo-edit")) {
    uiPomodoroEditor(lcd, PomoSettings{30, 5, 15, 4});
```

and extend the usage comment's list (`@pomo-ready|focus|...`) with `|edit`.

In `firmware/sim/Makefile`: add `../src/pomo_editor.cpp` to `SRCS` (after `../src/pomodoro.cpp`) and `build/pomo_editor.o` to `SHOT_OBJ`.

- [ ] **Step 2b: Verify it builds and render it**

Run (needs one prior `pio run` for libdeps and `brew install sdl2`):

```sh
cd firmware/sim && make build/cube-shot && ./build/cube-shot build/shot @pomo-edit
```

Expected: prints `build/shot-pomo-edit.png`. Open it and check: title "POMODORO", four rows (FOCUS 30 min, SHORT BREAK 5 min, LONG BREAK 15 min, SESSIONS 4) each with `-` and `+` buttons at the sides, RESET and DONE (accent fill) at the bottom, nothing clipped at the 240x280 edges.

If a label or value collides, adjust the `row.y + 12` / `row.y + 31` offsets in `uiPomodoroEditor` only; the hit rectangles do not change.

---

### Task 6: Wire the editor into `main.cpp`

**Files:**
- Modify: `firmware/src/main.cpp`

**Interfaces:**
- Consumes: `pomoDefaults/pomoSettings/pomoSettingsSave` (Task 4), `pomoConfigFrom` (Task 1), `pomoEditorHit/Apply`, `editorMayOpen`, `EditAction` (Task 1), `Pomodoro::setConfig` (Task 2), `Gesture::SwipeUp/SwipeDown`, `gestures.startX/startY` (Task 3), `uiPomodoroEditor` (Task 5).

- [ ] **Step 1: Includes and the timer**

Add `#include "pomo_editor.h"` and `#include "pomo_settings.h"` with the other includes. Replace the `Pomodoro pomo(PomoConfig{...});` global with:

```cpp
// Built from the compile-time defaults only so the object exists; setup()
// applies the stored settings once NVS is read.
Pomodoro pomo(pomoConfigFrom(pomoDefaults(), POMO_TIME_DIV));
```

In `setup()`, right after `settingsLoad();` add:

```cpp
  pomo.setConfig(pomoConfigFrom(pomoSettings(), POMO_TIME_DIV));
```

- [ ] **Step 2: Editor state and helpers**

After `GestureTracker gestures;` add:

```cpp
// The Pomodoro settings editor (swipe up on the idle Pomodoro card). While it
// is open nothing else moves the deck, and holds are off so a long press
// cannot start a session behind it. `editSettings` is the working copy.
bool editing = false;
PomoSettings editSettings{};
```

Inside the anonymous namespace, before `pollTouch()`, add:

```cpp
void openEditor() {
  editSettings = pomoSettings();
  editing = true;
  dirty = true;
}

// DONE and swipe-down both land here: what is on screen is what is saved.
void closeEditor() {
  pomoSettingsSave(editSettings);
  pomo.setConfig(pomoConfigFrom(pomoSettings(), POMO_TIME_DIV));
  editing = false;
  uiReplayPomodoro();
  lastRotate = millis();
  dirty = true;
}

void editorGesture(Gesture g) {
  if (g == Gesture::SwipeDown) {
    closeEditor();
  } else if (g == Gesture::Tap) {
    // Hit-test where the finger went down, not where it came up.
    const EditHit hit = pomoEditorHit(gestures.startX(), gestures.startY());
    if (hit.action == EditAction::Done) {
      closeEditor();
    } else if (hit.action != EditAction::None) {
      pomoEditorApply(editSettings, hit, pomoDefaults());
      dirty = true;
    }
  }
}
```

- [ ] **Step 3: Route gestures in `pollTouch()`**

Replace the `switch (gestures.update(down, x, y, now, onPomodoro)) {` line and add the editor branch, so the top of the function reads:

```cpp
  const bool onPomodoro = cardIndex == uiPomodoroIndex(payload);
  const Gesture gesture = gestures.update(down, x, y, now, onPomodoro && !editing);
  if (editing) {
    editorGesture(gesture);
    return;
  }
  switch (gesture) {
```

and add this case to the switch, before `default:`:

```cpp
    case Gesture::SwipeUp:  // open the Pomodoro editor (idle timer only)
      if (editorMayOpen(onPomodoro, pomo.view().state)) openEditor();
      break;
```

(Keep the existing comment above `onPomodoro` that explains why holds only matter on the Pomodoro card.)

- [ ] **Step 4: Freeze the deck and draw the editor in `loop()`**

Change the auto-rotate condition to:

```cpp
  if (CARD_ROTATE_MS > 0 && !editing && now - lastRotate >= CARD_ROTATE_MS) {
```

Replace the `uiRender(lcd, payload, cardIndex, netOnline(), age, pv);` call with:

```cpp
    if (editing) uiPomodoroEditor(lcd, editSettings);
    else uiRender(lcd, payload, cardIndex, netOnline(), age, pv);
```

(The bridge poll needs no change: the editor opens only from the Pomodoro card, and `deckKeepIndex` already keeps a viewer on that card as the deck changes size.)

- [ ] **Step 5: Verify the simulator builds and behaves**

Run: `cd firmware/sim && make clean && make test && make run`

In the SDL window:
1. Go to the Pomodoro card (click through the deck). Drag the mouse **up** about 100 px: the editor opens, showing 25 / 5 / 15 / 4 (or your `config.h` values).
2. Click FOCUS `+`: 30 min. `-` three times: 15 min. Click the value area, the title and the margins: nothing changes. Hold the mouse still on the screen for 3 s: no session starts.
3. Click RESET: back to the defaults. Click FOCUS `+` once, then **DONE**: you return to the Pomodoro card and it reads 30:00 (idle clock).
4. Hold to start: the timer runs from 30:00. While running, drag up: the editor does not open. Hold 2 s to reset, drag up again: it opens and shows 30 min (the sim keeps it in memory for the session).
5. Drag down in the editor after a change: it closes and keeps the change.

Then: `make run POMO_FAST=60` -- set focus to 1 min, start: the phase lasts 1 s. Run `make clean` afterwards (it changed `-DPOMO_TIME_DIV`).

- [ ] **Step 6: Verify the firmware compiles**

Run: `cd firmware && pio run`
Expected: `SUCCESS`. (Needs `src/config.h`; copy `config.h.example` if absent.) If `pio` is not installed, say so in the hand-off rather than skipping silently.

- [ ] **Step 7: On hardware (when a board is at hand)**

Flash, then: swipe up on the idle Pomodoro card, set 50 / 10 / 20 / 2, DONE, power-cycle: the card reads 50:00 and the editor shows the same values. Check a swipe up near the panel's top and bottom edges (the CST816 can report edge touches differently); if it is unreliable, widen the allowed start region in `gesture.cpp` rather than changing the thresholds.

---

### Task 7: Documentation

**Files:**
- Modify: `README.md:168-170` (Pomodoro card section), `CLAUDE.md` (the Pomodoro paragraph under "Payload contract"), `firmware/src/config.h.example` (Pomodoro comment)

- [ ] **Step 1: README**

Append to the end of the "Pomodoro card" paragraph in `README.md`:

```
To change the lengths without reflashing, **swipe up** on the Pomodoro card while the timer is idle. A settings screen opens with a `-` and `+` for FOCUS (steps of 5 min), SHORT BREAK (1 min), LONG BREAK (5 min) and SESSIONS (1). RESET returns to the defaults; DONE (or a swipe down) saves them, and they survive power cycles. The values in `config.h` are only the starting point and what RESET returns to. A running, paused or finished phase is never resized: new lengths apply from the next one you start.
```

and change its last sentence "Durations are `POMO_FOCUS_MIN`, ... in `config.h`." to "The defaults are `POMO_FOCUS_MIN`, `POMO_BREAK_MIN`, `POMO_LONG_MIN` and `POMO_SESSIONS` in `config.h`."

- [ ] **Step 2: CLAUDE.md**

At the end of the "The deck is the payload cards **plus one local Pomodoro card...**" bullet, add:

```
A swipe up on the idle Pomodoro card opens an on-device settings editor (`pomo_editor.cpp` owns the layout, hit-testing and step/clamp rules, drawn by `uiPomodoroEditor`); values persist in NVS keys `pf/ps/pl/pn` (`pomoSettings()` in `settings.cpp`, falling back to `config.h` via `pomo_defaults.h`) and are applied with `Pomodoro::setConfig`, which only acts while idle. It is local-only: not in the payload contract and not mirrored in `preview.html`. Preview it with `./build/cube-shot build/shot @pomo-edit`.
```

- [ ] **Step 3: config.h.example**

Change the comment `// Pomodoro card (the last card in the deck). Minutes, 1-99.` to:

```
// Pomodoro card (the last card in the deck). Minutes, 1-99. These are the
// defaults: swipe up on the idle card to change them on the device itself.
```

- [ ] **Step 4: Final check**

Run: `cd firmware/sim && make test` and `grep -n "POMO_FOCUS_MIN\|static_assert" firmware/src/main.cpp`
Expected: all tests `0 failed`; `main.cpp` no longer mentions `POMO_FOCUS_MIN` or the `POMO_*` `static_assert`s.

---

## Self-review

- **Spec coverage:** entry gesture and idle-only (Tasks 1, 3, 6); rows, steps, ranges, snapping (1); RESET/DONE/swipe-down save semantics (1, 6); persistence, fallback, clamping (1, 4); `setConfig` idle-only (2); holds off and deck frozen while editing (6); sim, shot and tests (1-3, 5, 6); docs (7). The spec said `pomo_defaults.h` shares defaults: done. Two additions beyond the spec, both needed: `setConfig` also refreshes the idle countdown (else the card would keep showing the old focus length), and the tracker exposes `startX/startY` (a tap has no coordinates otherwise). The old gesture test that required a vertical drag to be `None` is updated, as vertical drags now mean something.
- **Placeholders:** none; every code step has its code.
- **Type consistency:** `EditRect`, `EditHit{action,row}`, `pomoEditorApply(s, hit, defaults)`, `editorMayOpen(bool, PomoState)`, `pomoConfigFrom(s, timeDiv)`, `pomoDefaults()/pomoSettings()/pomoSettingsSave()` are used with the same signatures in every task.
- **Risk carried:** edge-swipe reliability on the CST816 (Task 6 step 7) and the sim's in-memory-only persistence (NVS verified only on hardware).
