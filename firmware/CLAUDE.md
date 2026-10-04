# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Scope: `firmware/`. The repo-root `CLAUDE.md` already describes each feature (payload contract, Pomodoro, editors, BLE, portal, auto-rotate, sleep, the three renderers). This file covers how the firmware tree is built, tested and split, so that a change reaches every file it has to.

## Commands

Run these from `firmware/`. `src/config.h` must exist (copy `src/config.h.example`) for both the board build and the sim build.

```sh
pio run                                  # board build; also fetches LovyanGFX / ArduinoJson / NimBLE into .pio/libdeps, which sim/ compiles against
pio run -t upload && pio device monitor  # USB flash + serial (115200, exception decoder)
python3 tools/gen_fonts.py               # regenerate src/fonts_gen.h from tools/Inter.ttf (needs Pillow)
```

Run these from `sim/`:

```sh
make test                                          # every host test; stops at the first failing binary
make build/pomodoro_test && ./build/pomodoro_test  # one test
./build/cube-shot build/ring fixtures/ring.json    # render a saved payload (fixtures/: every ring level, the usage card)
make clean                                         # required after changing SCALE or POMO_FAST: the Makefile tracks headers, not flags
```

Run tests from `sim/`, because `ble_frame_test` opens `fixtures/ble-frames.txt` relative to the working directory. `settings_json_test` needs ArduinoJson from `../.pio/libdeps`, so run `pio run` once first, or set `LIBDEPS=`. A test prints `<name>: N checks, M failed` and exits non-zero when anything failed.

## How `src/` is split

The sources fall into three kinds. This split is what makes host tests and the simulator possible, so keep new code in the right kind.

- **Pure logic.** Header-only (`transport_policy.h`, `idle_sleep.h`, `orientation.h`, `ble_auth.h`, `ble_frame.h`, `age_label.h`, `touch_map.h`, `deck_util.h`, `battery_util.h`, `portal_util.h`, `*_settings.h`) or a small `.cpp` (`pomodoro`, `gesture`, `pomo_editor`, `dev_editor`, `settings_json`). These files make no Arduino or hardware calls and never read `millis()`. The caller passes `now`, and elapsed time is computed by unsigned subtraction so a `millis()` wrap is harmless. Each one has a `sim/tests/<name>_test.cpp`.
- **Hardware layers.** `net.cpp`, `net_ble.cpp`, `touch.cpp`, `imu.cpp`, `battery.cpp`, `settings.cpp` (NVS), `portal.cpp`, `ota.cpp`. Each one implements a header (`net.h`, `ble.h`, `settings.h`, ...) that `sim/` implements again as a `*_sim.cpp` stand-in. When you change one of these headers, update its stand-in too.
- **Shared app code.** `main.cpp`, `ui.cpp` and `payload.cpp` compile unchanged for both the board and the SDL simulator. They may only use what `sim/Arduino.h` provides: `millis`, `delay`, `Serial.print/println/printf`, `min/max/constrain`, `ESP.restart` and `setCpuFrequencyMhz`. `display.h` picks the panel with `#ifdef LGFX_SDL`.

PlatformIO compiles everything in `src/`, but the simulator compiles only the files its Makefile lists. So:
- A new `.cpp` that `main.cpp` or `ui.cpp` uses must be added to `SRCS` in `sim/Makefile`, and also to `SHOT_OBJ` if `ui.cpp` or `shot.cpp` needs it. If it touches hardware, put its API in a header and add a `sim/*_sim.cpp` stand-in instead.
- A new test needs `build/<name>_test` added to `TESTS`, plus a `build/<name>_test: ../src/<name>.cpp` rule for each `.cpp` it links. Tests use `tests/check.h` (`CHECK(cond)`, then `return checksDone("<name>");`), not a framework.
- A new screen that is not a card, if you want a PNG of it, needs a case in `renderSpecial` in `sim/shot.cpp`. That case is what the `@name` argument selects.

## `main.cpp`

The firmware runs as one cooperative loop with no tasks of its own. NimBLE runs its callbacks on its own task, and `bleTake()` / `bleTakeSettings()` hand those results to the loop, so both must be called on every pass. Each pass runs these steps in order:

1. Pomodoro tick and phase-end alert
2. Touch and gestures
3. IMU
4. Battery
5. Transport policy (start or stop WiFi and OTA)
6. Payload intake over BLE or WiFi
7. Settings writes from BLE
8. Auto-advance
9. Screen sleep
10. Draw

A frame is drawn only when `dirty` is set, while `uiAnimating()` is true, or once a second (for the age counter). After any state change that should show on screen, set `dirty = true`. Each frame draws exactly one screen, chosen in this priority order: BLE passkey, then "waiting for a Mac", then an open editor, then the deck (`uiRender`).

Boot order matters:
- `settingsLoad()` runs before anything reads settings.
- `touch.begin()` starts the shared I2C bus, so `imuBegin()` comes after it.
- `bleBegin()` runs before any call to `bleBonded()`.
- WiFi is joined at boot only when no Mac is bonded.

## Settings and `config.h`

`config.h` only holds compile-time defaults. At runtime, code reads `settings()`, `deviceSettings()` and `pomoSettings()`. These come from NVS, falling back per key to the defaults in `device_defaults.h` / `pomo_defaults.h`. When you add a `config.h` macro, give it an `#ifndef` fallback where it is read. Users keep their own gitignored `config.h`, and an older copy must still build.

Adding a persisted setting touches all of these:
- the struct and its clamp in `*_settings.h`
- the NVS key in `settings.cpp`, and `sim/settings_sim.cpp`
- the portal form in `portal.cpp`
- the BLE JSON: `settings_json.cpp`, `docs/ble-protocol.md`, and `CubeSettings.swift` in `mac-helper/`
- the on-device editor, if the setting gets a row there

## Gotchas

- `src/fonts_gen.h` (~680 KB) is generated, so don't hand-edit it. Its fonts cover only printable ASCII (0x20–0x7E), at the sizes listed in `FONTS` in `tools/gen_fonts.py`. `bridge/cards.mjs` folds payload strings down to that range already. Text the firmware draws itself must stay inside it too, unless you widen `CHARS` and regenerate.
- `board_build.arduino.memory_type = qio_opi` in `platformio.ini` is required, because the PSRAM is octal. With `qio_qspi`, the PSRAM is not found. Starting the portal AP while BLE is up then runs out of memory and crashes in the WiFi driver.
- Serial logs use `[tag]` prefixes (`[boot]`, `[net]`, `[ble]`, `[imu]`, `[settings]`). The simulator prints the same lines to stdout, so `make run` output can be compared with `pio device monitor`.
