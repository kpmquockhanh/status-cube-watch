# Pomodoro card, WiFi config portal and OTA: design

Date: 2026-10-02
Status: draft, awaiting user review
Scope: `firmware/` (plus `bridge/preview.html`, `firmware/sim/`, CLAUDE.md note)

## Goal

Make the cube usable as a standalone desk device:

1. **WiFi config portal** (idea #10): change WiFi and bridge settings without editing `config.h` or reflashing.
2. **OTA updates** (idea #9): flash over WiFi with the normal PlatformIO workflow.
3. **Pomodoro timer card**: a focus/break timer that lives on the device and is controlled by touch.

These are three independent pieces. Each gets its own implementation plan and is built in this order: **settings + portal, then OTA, then Pomodoro**. The portal comes first because OTA's password and everything network-related depend on stored settings.

## Decisions (agreed in brainstorming)

| Topic | Decision |
|---|---|
| Pomodoro state | On the device (approach A). Bridge and `cards.mjs` are unchanged. |
| Pomodoro control | Dedicated card. Long-press (~600 ms) toggles start/pause. Very long press (~2 s) resets. Tap and swipe unchanged. |
| Pomodoro cycle | 25 min focus, 5 min break, 15 min long break after 4 focus sessions. Each phase starts on a long-press, never automatically. Durations are constants in `config.h`. |
| Phase-end alert | Jump to the Pomodoro card, flash the ring and pulse the backlight, then hold on a "done" message until long-press. |
| Portal | Captive portal AP with QR code. Settings stored in NVS. `config.h` stays as the default. |
| OTA | ArduinoOTA push. No browser upload, no pull-from-bridge. |
| Hardware | ESP32-S3R8 (8 MB octal PSRAM, 16 MB flash). `platformio.ini` comment saying "R2" is stale. |

## Assumptions

- The Pomodoro card is always the **last** card and shows even when the bridge is offline.
- The timer uses `millis()`. A reboot resets it. The RTC is not used.
- The timer keeps running while another card is showing.
- The portal and OTA are optional: with no NVS data and a working `config.h`, behaviour is the same as today.

## Non-goals

- Pomodoro durations editable on the device (could later be added to the portal page).
- Bridge-driven or hook-driven timer start.
- Browser upload page or pull-based OTA.
- Using PSRAM. If it is ever needed, verify the memory type first (see Risks).
- Sound. The board has no speaker.

## 1. Settings storage

New `src/settings.h/.cpp`, a thin wrapper over ESP32 `Preferences` (NVS).

- Keys: `ssid`, `pass`, `bridge` (URL), `otapass`.
- API: getters that return the NVS value if set, else the `config.h` default (`WIFI_SSID`, `WIFI_PASSWORD`, `BRIDGE_URL`; OTA password default is empty meaning "none configured"); a `settingsSave(...)` for the portal; `settingsHaveWifi()` to report whether any SSID exists.
- `net.cpp` and `main.cpp` stop reading the macros directly and use `settings`.
- `POLL_INTERVAL_MS`, `CARD_ROTATE_MS`, `BACKLIGHT` and the new `POMO_*` constants remain compile-time in `config.h`.
- The simulator does not use NVS. It links a stub `settings_sim.cpp` that returns the `config.h` defaults, in the same way `net_sim.cpp` replaces `net.cpp`.

## 2. WiFi config portal

New `src/portal.h/.cpp`. Entry point `portalRun()` blocks until settings are saved, then reboots.

**Triggers**
- No stored credentials and no usable `config.h` SSID.
- WiFi still not connected about 20 s after boot.
- Screen held about 5 s during boot (forces the portal, for reconfiguring a working cube).

**Behaviour**
- Start a SoftAP named `claude-cube-XXXX` (last 2 bytes of the MAC), open (no password).
- A DNS catch-all answers every query with the AP address so phones open the page automatically. A small HTTP server serves one page.
- The page has fields: SSID, password, bridge URL, optional OTA password. A POST saves them through `settings` and the cube reboots.
- The portal has no timeout. It stays up until settings are saved.

**Screen**
- `ui.cpp` gets `uiPortal(lcd, apName)`, drawing the AP name, a "join this network" line and a WiFi QR code (`WIFI:T:nopass;S:<ap>;;`) using LovyanGFX's QR support.
- Wrong password: connection fails, the 20 s timeout triggers the portal again.

**Simulator**
- Portal code is excluded from the sim build. `uiPortal` is compiled, so `make shot` can render it from a fixed call.

## 3. OTA (ArduinoOTA)

- After WiFi connects, start `ArduinoOTA` with hostname `claude-cube` and the password from `settings` (skipped if none is set, with a one-line warning on serial).
- `ArduinoOTA.handle()` runs in `loop()`.
- On start, polling and normal drawing pause and the screen shows an update progress bar (`uiOta(lcd, percent)`). On error or end, it returns to the normal loop or reboots.
- `platformio.ini`: add a commented `upload_protocol = espota` / `upload_port = claude-cube.local` example. Correct the "R2 / 2 MB PSRAM" comment to R8.
- The existing `default_16MB.csv` partition table already has two OTA app slots, so no partition changes.
- Not built in the simulator.

## 4. Pomodoro card

### State machine

New `src/pomodoro.h/.cpp`, plain C++ with no hardware dependency, taking the current time as an argument so it can be tested with a fake clock.

States: `IDLE`, `FOCUS`, `BREAK`, `PAUSED`, `DONE`.
Tracked: current phase kind (focus / short break / long break), time left in the phase, completed focus count (0 to 4), and a `pendingAlert` flag.

Transitions:
- `IDLE` + long-press: start `FOCUS`.
- `FOCUS` / `BREAK` + long-press: `PAUSED` (remembers the phase). `PAUSED` + long-press: resume.
- Any state + very long press: back to `IDLE`, focus count reset.
- A running phase reaching zero: enter `DONE`, set `pendingAlert`, increment the focus count if it was a focus phase.
- `DONE` + long-press: start the next phase. After a focus phase that is the 4th, the next phase is the long break and the count resets after it. Otherwise: focus is followed by a short break, a break is followed by focus.

Durations: `POMO_FOCUS_MIN` (25), `POMO_BREAK_MIN` (5), `POMO_LONG_MIN` (15), `POMO_SESSIONS` (4), all in `config.h`.

API (sketch): `pomoTick(nowMs)`, `pomoLongPress(nowMs)`, `pomoReset()`, `pomoView()` returning a struct the UI renders (state, phase, secondsLeft, fraction, sessionIndex, alertActive).

### Deck integration

- The Pomodoro card is virtual. The deck length becomes `payload.nCards + 1` and the Pomodoro card sits at the last index. It does not count against `MAX_CARDS` and does not appear in the payload, so `payload.h/.cpp`, `cards.mjs` and the bridge are unchanged.
- `main.cpp` navigation (`step()`, card index bounds, `uiReplay`) uses the combined deck length.
- It is present even with zero payload cards or when the bridge is unreachable. In that case `step()` must not early-return as it does today for `nCards == 0`.

### Appearance

A ring card in the existing style, rendered by `ui.cpp` from `pomoView()` rather than from payload data:
- Ring fill: time left in the phase as a fraction.
- Ring colour: amber for focus, green for breaks (crossfades using the existing colour animation), muted while `IDLE` or `PAUSED`.
- Centre: `MM:SS`. Caption: phase name (FOCUS, BREAK, LONG BREAK, PAUSED, READY). Label below: session count (`2/4`) and a hint (`HOLD TO START`, `HOLD TO PAUSE`, `HOLD FOR NEXT`).
- Notches: no notches on this card.
- Because the card is rendered every second while running, it hooks into the existing once-per-second redraw. No extra animation loop is needed except during the alert.

Documented exception: this card formats `MM:SS` on the device. CLAUDE.md's "no number/unit formatting in the firmware" rule gets a note that the Pomodoro card is the one local card.

### Touch

`main.cpp`'s `pollTouch()` already tracks down-time. Extend it:
- While the finger is still down and has not moved (within `TAP_MAX_PX`), reaching about 600 ms on the Pomodoro card triggers `pomoLongPress()` once. Reaching about 2 s triggers `pomoReset()`. The release afterwards must not also count as a tap.
- Long-press only acts on the Pomodoro card. On other cards, a held touch is ignored on release, as a slow touch is today.
- Tap and swipe behaviour is unchanged everywhere.

### Alert

When `pendingAlert` is set:
- `main.cpp` calls `step` to the Pomodoro index (`uiReplay`).
- `ui.cpp` runs a short pulse: the ring flashes between the phase colour and white, and the backlight ramps up and down a few times (about 3 pulses over about 2 s), then settles on the "done" view. `uiAnimating()` returns true during it so the loop keeps drawing.
- The "done" view reads "FOCUS DONE" or "BREAK DONE" with the hint to hold for the next phase.
- A tap or swipe leaves the card. `pendingAlert` is cleared once the jump has happened, so it does not fire again. The `DONE` state stays until long-pressed.
- The backlight returns to `BACKLIGHT` afterwards.

### Mirrors

- `bridge/preview.html`: add a Pomodoro card with fixed fake timer data. It mirrors the layout, not the timing, as with the other previews.
- Simulator: it compiles `main.cpp` and `ui.cpp`, so the card renders. `shot.cpp` gets a way to render a fixed Pomodoro view (focus, break, paused, done) so `make shot` can produce a PNG per state.

## 5. Verification

No test suite exists in the repo, so verification follows the project's normal approach plus one small addition.

- **Pomodoro state machine:** a host-side check (a small standalone C++ file built with the sim's toolchain, run by `make`) that drives `pomodoro.cpp` with a fake clock and asserts the cycle, the 4-session long break, pause/resume, reset and the alert flag. This is the only new automated check.
- **Pomodoro rendering:** `make shot` PNGs for focus, break, paused, done and the alert frame, checked by eye. `make run` for touch behaviour in the SDL window (mouse hold).
- **Portal:** on hardware. Manual steps: boot with no credentials, join the AP from a phone, submit settings, confirm the cube reboots and connects. Repeat with a wrong password (expect the portal after about 20 s) and with a 5 s hold at boot on a working cube.
- **OTA:** on hardware. Run `pio run -t upload --upload-port claude-cube.local`, confirm the progress bar and that the new build boots. Check that a wrong OTA password is rejected.
- **Regression:** with an empty NVS and a populated `config.h`, boot behaviour matches today's.

## Risks

- **PSRAM memory type:** `platformio.ini` sets `qio_qspi`, which is for quad PSRAM. The R8 has octal PSRAM and needs `qio_opi`. Nothing here uses PSRAM, so this does not block the work, but it should be verified and corrected if PSRAM is ever used. Not changed by this spec beyond the comment.
- **Library size and RAM:** WebServer, DNSServer and ArduinoOTA together are small against 16 MB flash and the existing sprite. Check free heap after the full-screen sprite is allocated.
- **Long-press vs tap:** the long-press must not fire a trailing tap or swipe on release. The plan must cover this explicitly.
- **Portal reachability:** some phones drop an AP that has no internet. The page is also reachable by typing `192.168.4.1`, and the screen should show that address.

## File summary

New: `src/settings.h/.cpp`, `src/portal.h/.cpp`, `src/pomodoro.h/.cpp`, `sim/settings_sim.cpp`, a host-side Pomodoro check.
Changed: `src/main.cpp`, `src/net.cpp`, `src/ui.h/.cpp`, `src/config.h.example`, `platformio.ini`, `sim/Makefile`, `sim/shot.cpp`, `bridge/preview.html`, `CLAUDE.md`.
Unchanged: `bridge/cards.mjs`, the payload contract, `payload.h/.cpp`.
