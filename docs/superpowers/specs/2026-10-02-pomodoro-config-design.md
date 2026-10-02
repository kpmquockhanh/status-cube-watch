# On-device Pomodoro configuration

Date: 2026-10-02 · Status: draft, awaiting review

## Goal

Change the Pomodoro focus length, short break, long break and sessions-before-long-break from the cube's touchscreen, with no host, bridge or reflash involved. Values persist across reboots.

## Non-goals

- No bridge config, payload field or setup-portal fields (the bridge never learns about the Pomodoro).
- No editing while a phase is running or paused.
- No change to how phases start (still long-press only) or to the alert.
- No `preview.html` change: the Pomodoro card and its editor are local-only.

## Behavior

**Opening.** On the Pomodoro card, while the timer is idle (`POMO_IDLE`), a swipe up opens the editor. In every other timer state, and on every other card, a swipe up does nothing (as vertical travel does today). Tap and horizontal swipes keep their current meaning on the Pomodoro card.

**Editor screen** (240x280). From the top: a title, four rows, then a button row.

| Row      | Step | Range | Default |
|----------|------|-------|---------|
| FOCUS    | 5 min | 1-99 | 25 |
| SHORT    | 1 min | 1-99 | 5 |
| LONG     | 5 min | 1-99 | 15 |
| SESSIONS | 1    | 1-9  | 4 |

Each row shows its label, the value, and a `-` and `+` touch target of at least 44x44 px. Stepping by 5 snaps to multiples of 5, with 1 as the lower bound: `+` goes to the next multiple of 5 above the value and `-` to the next one below it (1 +> 5 +> 10, 10 -> 5 -> 1, 27 +> 30, 27 -> 25). Values clamp at the range ends. The button row has **RESET** (left) and **DONE** (right), also at least 44 px tall.

**Leaving.** DONE or a swipe down saves and returns to the Pomodoro card. RESET sets all four rows to the compile-time defaults (`config.h` values, else 25/5/15/4); it only changes the rows on screen, and DONE (or swipe down) then saves, so there is no separate undo. There is no cancel: every change is visible as it is made, and RESET is the way back.

**While open.** Auto-rotate and bridge polls still run, but nothing may move the deck off the editor: `step()` and the poll's `deckKeepIndex` are bypassed while it is open. The phase-end alert cannot fire because the timer is idle.

## Components

### Persistence: `settings.h/.cpp`

```cpp
struct PomoSettings { uint8_t focusMin, shortMin, longMin, sessions; };
const PomoSettings &pomoSettings();
bool pomoSettingsSave(const PomoSettings &s);
```

NVS namespace `cube`, keys `pf`, `ps`, `pl`, `pn` (u8). A key that was never stored falls back to `POMO_FOCUS_MIN` / `POMO_BREAK_MIN` / `POMO_LONG_MIN` / `POMO_SESSIONS` from `config.h`, else the defaults above, so a fresh or older cube behaves as it does now. Loaded values are clamped to the ranges above (a bad NVS value never reaches the timer). `settingsLoad()` loads it with the rest. The `POMO_*` defaults move from `main.cpp` into a shared header (`pomo_defaults.h`) so `settings.cpp`, the editor's RESET and `main.cpp` share one source. `sim/settings_sim.cpp` gets an in-memory stand-in.

### Timer: `pomodoro.h/.cpp`

`void Pomodoro::setConfig(const PomoConfig &cfg)` replaces the config and is ignored unless the state is `POMO_IDLE`. It does not touch the completed-session count. `main.cpp` builds the `PomoConfig` from `pomoSettings()` using the existing `* 60000UL / POMO_TIME_DIV` conversion, so `make run POMO_FAST=60` is unchanged.

### Gesture: `gesture.h/.cpp`

Add `SwipeUp` and `SwipeDown`. A touch is vertical when travel is at least `SWIPE_MIN_PX`, within `SWIPE_MAX_MS`, and `|dy| > |dx|`. Horizontal classification is unchanged, and a diagonal swipe goes to whichever axis dominates. `SwipeUp`/`SwipeDown` are reported on every card; `main.cpp` acts on them only as above.

### Editor logic: `pomo_editor.h/.cpp` (new, pure)

No Arduino or LovyanGFX dependency, so it builds in `make test`.

```cpp
enum class EditAction { None, Dec, Inc, Reset, Done };
struct EditHit { EditAction action; uint8_t row; };  // row valid for Dec/Inc
EditHit pomoEditorHit(int16_t x, int16_t y);          // layout constants live here
void pomoEditorApply(PomoSettings &s, EditHit hit);   // step/snap/clamp/reset
```

The layout rectangles are constants shared with the renderer so hit-testing and drawing cannot drift apart.

### Screen: `ui.cpp` / `ui.h`

`void uiPomodoroEditor(Display &lcd, const PomoSettings &s)` draws the editor into the same full-screen sprite as the cards, using the existing palette, fonts and `fitFont`. No animation. The editor adds no new state to `uiRender`; `main.cpp` calls `uiPomodoroEditor` instead of `uiRender` while open.

### Wiring: `main.cpp`

An `editing` flag plus a working `PomoSettings` copy.
- `pollTouch`: on the idle Pomodoro card, `SwipeUp` sets `editing`, copies the current settings, and marks dirty. While `editing`, `Tap` is hit-tested and applied, `SwipeDown` and DONE call `pomoSettingsSave`, `pomo.setConfig`, clear `editing`, and `uiReplayPomodoro()`. Holds are disabled while editing (`holdEnabled` false), so a long press cannot start a session behind the editor.
- `loop`: skip `step()` and the poll's index adjustment while `editing`.
- `setup`: construct `pomo` from `pomoSettings()` after `settingsLoad()` (the global initialiser moves into `setup()` or uses `setConfig` once settings are loaded).

## Verification

Host tests (`make test` in `firmware/sim/`):
- Gesture: vertical swipe up/down, a diagonal resolved by dominant axis, and a short drag below `SWIPE_MIN_PX` stays a tap or nothing.
- Editor: step, snap-to-5 (including 1<->5), clamping at both ends, RESET, hit-testing for every row and button, and a miss returning `None`.
- Pomodoro: `setConfig` applies while idle and is ignored while running, paused or done.
- Settings: clamping and fallback are checked through the pure clamp helper (NVS itself is board-only).

Simulator: `make run` (open with a vertical mouse drag, adjust with clicks); `./build/cube-shot build/shot @pomo-edit` renders the editor headlessly; `make run POMO_FAST=60` confirms a changed length is honoured by the next focus phase. On hardware: change values, reboot, confirm they persist and that a fresh flash with an empty NVS still uses `config.h`.

## Docs

README: a short "Configuring the Pomodoro" section. CLAUDE.md: extend the Pomodoro paragraph (swipe up on the idle card opens the on-device editor, persisted in NVS keys `pf/ps/pl/pn`; the gesture and editor logic are pure and host-tested; the editor is not in the payload contract).

## Risks and open points

- Vertical swipe on the 240x280 panel near the edges may be affected by the CST816's edge behavior; verify on hardware, and if it is flaky, widen the allowed start region rather than the gesture.
- `main.cpp` currently constructs `pomo` as a global from compile-time values; moving construction after `settingsLoad()` must keep the sim (`sdl_main.cpp`) building.
