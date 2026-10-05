# Buzzer

## Goal
Give the cube a voice. Make the on-board passive buzzer play a short tune when a Pomodoro phase ends, with an on/off switch and three volume levels you can set on the cube, from the Mac and in the portal. Also hold the buzzer pin low from the first instant of boot. Today the firmware never touches it, and Waveshare's FAQ for this board warns that an undriven buzzer pin keeps drawing current through the 3.3 V LDO and heats it.

This is sub-project A of six (A buzzer, B Claude session status, C answer from the cube, D IMU gestures, E next meeting, F now playing). B will use the `Attention` sound defined here when Claude Code is waiting for you, so that sound is in the tables now even though nothing in A triggers it.

## Non-goals
- Sounds triggered by the bridge: no payload field and no contract change. B adds the payload event that plays `Attention`.
- Usage-threshold beeps and a limit-reset chime. These were offered and not wanted.
- Quiet hours. The cube has no local time of day yet (the RTC is unused), so there is nothing to compare against.
- A touch-click sound, or sound for swipes and taps.
- Mirroring in `preview.html` or the Mac's menu bar. Sound is local, like the battery and the Pomodoro card.
- Real audio in the simulator. It logs which sound would play.

## Hardware facts
- The buzzer is **passive**: a PWM square wave sets its pitch, and the duty cycle sets its loudness (non-linearly; 50% is loudest).
- Current boards wire it to **GPIO42** (`docs/ESP32-S3-Touch-LCD-1.69.md`). Waveshare's FAQ says an older revision used GPIO33.
- The Arduino core is 2.0.17 (`framework-arduinoespressif32` 4.20017.0), so the LEDC API is `ledcSetup` / `ledcAttachPin` / `ledcWrite` / `ledcChangeFrequency`. On the S3, channel *n* runs on timer `(n / 2) % 4`.
- The backlight is LovyanGFX `Light_PWM` on LEDC **channel 7** (timer 3) at 12 kHz (`display.h`). The buzzer must use a channel on another timer, so that changing the buzzer's frequency can never retime the backlight. Channel 6 would share timer 3, so it is ruled out.
- Arduino's `tone()` is not used. It claims LEDC channel 0 and spawns its own FreeRTOS task.

## Design

### Pin: `board_pins.h`
`#define PIN_BUZZER 42`, with a comment that older board revisions use 33.

### Sequencer: `firmware/src/melody.h` (pure, header-only, host-tested)
- `struct Note { uint16_t hz; uint16_t ms; }`, where `hz == 0` is a rest.
- `enum class Sound : uint8_t { FocusDone, BreakDone, Attention, Preview }` and `const Note *melodyFor(Sound, uint8_t &len)`. Each melody is a `constexpr` table and lasts under 0.5 s.

| Sound | Played when | Shape (starting pitches; tuned by ear on the board) |
|---|---|---|
| `FocusDone` | a focus phase ends: break time | three rising notes, ~2.1 / 2.6 / 3.1 kHz, ~110 ms each |
| `BreakDone` | a short or long break ends: back to work | three notes ending high and held longer, distinct from `FocusDone` |
| `Attention` | (B) Claude Code is waiting for you | two ~70 ms beeps near the buzzer's loudest pitch (~2.7 kHz), 70 ms apart |
| `Preview` | the SOUND level changes in the on-device panel | one ~90 ms beep |

- `class MelodyPlayer`:
  - `play(Sound)` replaces whatever is playing. Timing starts at the next `update()`, so a play requested after this pass read `now` (the panel's preview, from the touch handler) keeps its first note.
  - `stop()`.
  - `uint16_t update(now)` returns the pitch to output now, or 0 for silence (a rest, or finished). A late call skips the notes it missed, so a tune keeps its length.
  - `bool playing() const`.
- Elapsed time uses unsigned subtraction, so a `millis()` wrap mid-melody is harmless. The caller passes `now`, as with every pure header.
- C++11-safe for the board build: every `constexpr` function is a single `return`.

### Driver: `firmware/src/buzzer.{h,cpp}` (hardware layer)
API:
```cpp
bool buzzerBegin();                 // first thing in setup(); false = LEDC setup failed
void buzzerSetLevel(uint8_t level); // 0 = off, 1..3 = LOW / MED / HIGH
void buzzerPlay(Sound s);           // no-op at level 0
void buzzerUpdate(uint32_t now);    // every loop pass
```
- `buzzerBegin()`:
  1. `pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW)` before anything else in `setup()`, so the pin no longer floats from boot.
  2. Then `ledcSetup(BUZZ_CH = 0, 2000, 10)` (10-bit duty), `ledcAttachPin`, and `ledcWrite(BUZZ_CH, 0)`. Channel 0 runs on timer 0, away from the backlight's timer 3.
- Silence is always duty 0, which keeps the output low.
- `buzzerUpdate` asks the `MelodyPlayer` for the current pitch and only touches LEDC when the pitch changes: `ledcChangeFrequency` followed by `ledcWrite(duty)`, or duty 0 for a rest or the end.
- Notes are timed by the main loop pass (15 ms, or `FRAME_MS` = 16 ms while animating), so durations are accurate to about one pass. Every note is ≥ 60 ms, so that is inaudible. Screen-off passes are 50 ms, but nothing in A plays with the screen off: a running Pomodoro keeps it on, and the panel is on screen.
- Volume is a duty per level: `{0, LOW ≈ 2%, MED ≈ 10%, HIGH = 50%}` of the 10-bit range. These are constants in `buzzer.cpp`, tuned by ear on the board, because a passive transducer's loudness is not linear in duty.
- A level change applies on the next loop pass. Level 0 also stops the tune, so it is silent at once.
- `ledcSetup` returning 0 makes `buzzerBegin()` return false. `setup()` logs `[buzz] ledc setup failed` once Serial is up, and the cube stays silent. The pin is still held LOW by step 1.
- Serial log: `[buzz] <sound> @<LOW|MED|HIGH>` on each play.

Simulator and bench:
- `firmware/sim/buzzer_sim.cpp` implements the same header. It only prints the `[buzz]` line: no player and no audio.
- `sim/bench.cpp` includes `../src/buzzer.h` and defines no-op stand-ins, as it already does for the battery and the IMU.
- `buzzer_sim.cpp` goes in `SRCS`. `melody.h` is header-only, so `melody_test` only needs adding to `TESTS`.

### Setting: `DeviceSettings.sound`
- New field `uint8_t sound; // 0 = off, 1..3 = LOW / MED / HIGH`. It is clamped to 0..3 in `deviceClamp` and compared in `operator==`.
- Default: `BUZZER_LEVEL` from `config.h` (MED = 2), with an `#ifndef` fallback in `device_defaults.h` so an older `config.h` still builds. `config.h.example` documents it.
- NVS key `sd` (`settings.cpp`), falling back to the default when never stored. `sim/settings_sim.cpp` has no NVS and copies `DEVICE_DEFAULTS` whole, so it needs no change.
- Every brace-initialised `DeviceSettings{...}` gains the fifth value: `DEVICE_DEFAULTS`, `portal.cpp`'s save (`DeviceSettings{bl, sl, rt, pi}` today), and the tests. A missed one compiles fine and silently stores sound = 0.
- `buzzerSetLevel` is called:
  - in `setup()`, with `deviceSettings().sound`;
  - in `closeDeviceEditor()`, next to the brightness restore;
  - in `handleBleSettings()`, next to its `setBrightness`.

  The portal saves and reboots, so `setup()` covers it.

Everywhere it can be edited:
- **On-device panel** (`dev_editor.cpp`): row 3 becomes **SOUND** in place of REFRESH. It is a switch row, like SLEEP and ADVANCE.
  - Presets are `{1, 2, 3}`, shown as `LOW` / `MED` / `HIGH`, or `OFF`.
  - A tap on the lead toggles it. Turning it back on restores the saved level, else the default, else MED.
  - The steppers move between presets and do nothing while it is off.
  - Each change plays `Preview` at the new level, as brightness previews live. Turning it off is silent.
  - This lives in `main.cpp`, beside the existing live `lcd.setBrightness(editDevice.backlight)` after `devEditorApply`. When `editDevice.sound` changed, it calls `buzzerSetLevel(editDevice.sound)`, and then `buzzerPlay(Sound::Preview)` if the new level is non-zero. `dev_editor.cpp` stays pure.
  - Closing the panel always saves, so there is no cancel path that would need to revert the level.
  - REFRESH leaves the cube's panel but stays editable in the portal and the Mac window.
- **BLE Settings JSON** (`settings_json.cpp`, `docs/ble-protocol.md`): key `sd`, an integer from 0 to 3. As for every key, a missing `sd` leaves the level unchanged, so an older Mac helper keeps working. `BLE_FW_REV` goes from 3 to 4.
- **Mac helper** (`CubeSettings.swift`, `SettingsWindow.swift`): a `sound` field and a **Sound** popup (Off / Low / Medium / High). The popup is shown only when the cube's settings read contains `sd`, so an older cube never shows a control it would ignore. The write sends `sd` only when it changed (the existing diff rule).
- **Portal** (`portal.cpp`): a numeric field "Sound" (hint "0 = off, 1 = low, 2 = medium, 3 = high"), key `sd`, using the existing `numField` / `portalParseInt` pattern. A blank field keeps the current value.

### Pomodoro hookup: `main.cpp`
In the existing `pomo.takeAlert()` block, next to `bleNotifyPomodoro` and `uiAlertStart`:
```cpp
buzzerPlay(ended.phase == PHASE_FOCUS ? Sound::FocusDone : Sound::BreakDone);
```
`buzzerUpdate(now)` runs on every loop pass, right after the Pomodoro tick. The screen never sleeps while a Pomodoro runs, so this needs no sleep interaction. Nothing here changes the CPU clock policy: a playing tone costs LEDC time, not CPU.

## Testing
- `make test`:
  - New `melody_test`: note boundaries, a rest outputs 0, the end gives 0 and `playing() == false`, `play` replaces a running melody, `stop`, timing starts at the first `update`, a late pass skips ahead and still ends on time, a `millis()` wrap mid-note, and every table is non-empty and under 500 ms.
  - Updated `dev_editor_test`: row 3 is SOUND, with a switch; toggle/restore order; stepping LOW↔HIGH and clamping at the ends; steppers inert while off; RESET restores the defaults but keeps WiFi refresh, which has no row in the panel.
  - Updated `settings_json_test`: `sd` is accepted for 0..3, rejected for 4, -1 and a string, and absent means unchanged.
  - Updated `device_settings_test`: `deviceClamp` holds `sound` to 0..3, and `operator==` sees it.
- `pio run`: the board build (catches C++11 issues in `melody.h`).
- `make bench`: still passes, with the buzzer stand-in.
- `./build/cube-shot build/shot @dev-edit`: the panel shows the SOUND row, switched on at MED. ADVANCE in the same shot shows what a switched-off row looks like.
- `swift test`: `CubeSettings` round-trips `sd`, and `sound` is nil when `sd` is absent, so it is never sent. That the popup is then hidden is checked by hand: the window is AppKit glue.
- Hardware (new `docs/buzzer-acceptance.md`, a checklist like the IMU and BLE ones):
  - [ ] With the cube idle and silent, the board near the LDO stays cool (compare with before the flash).
  - [ ] Each level is audible, with LOW clearly quieter than HIGH; the duties are tuned until it is.
  - [ ] A focus phase ending plays `FocusDone`, and a break ending plays `BreakDone`.
  - [ ] Sound OFF is silent everywhere.
  - [ ] The backlight does not flicker or change brightness while a tune plays (separate LEDC timers).

## Docs
- Root `CLAUDE.md` and `firmware/CLAUDE.md`:
  - A short Buzzer paragraph covering the pin, the separate LEDC timer, that `melody.h` is pure and host-tested, the `sd` setting, and that it is local-only.
  - The "Display panel" paragraph's list of rows changes (WiFi refresh becomes sound), and its NVS keys gain `sd`.
- `mac-helper/CLAUDE.md`: `sd` in the mirrored key list, and that `sound` is optional.
- README: a sound line in the device features and settings. `docs/ble-protocol.md`: the `sd` key and fw_rev 4.

## Risks
- **Loudness.** A passive buzzer behind a 3D-printed bezel may be quiet at LOW, or shrill at HIGH. The duty table and the pitches are tuned on the board; the structure does not depend on the values.
- **Board revision.** Only GPIO42 is supported. If an older GPIO33 board turns up, `PIN_BUZZER` is a one-line change, to be checked against that board's schematic first.
- **Losing REFRESH from the on-device panel.** It is still in the portal and on the Mac. With BLE as the main link it is rarely changed.
