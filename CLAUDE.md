# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A desk display for Claude rate-limit usage. Two halves that talk over plain HTTP on the LAN:

- `bridge/` — Node ≥20 ESM, **zero npm dependencies**. Gathers data on the host and serves one small, fully pre-formatted JSON document at `GET /api/status` (plus `/` = browser mock, `/health`).
- `firmware/` — PlatformIO / Arduino for a Waveshare ESP32-S3-Touch-LCD-1.69 (240x280 ST7789V2, CST816 touch), drawn with LovyanGFX (no LVGL) + ArduinoJson. It only polls the bridge (or takes the same payload over BLE) and draws strings; it never talks to Anthropic.
- `mac-helper/` — Swift package building `ClaudeCubeLink.app`: supervises the bridge (`node bridge/server.mjs`) and pushes `/api/status` to the cube over BLE (CoreBluetooth). Optional; BLE users run its `install.sh` instead of `bridge/agent.sh`. It shows a menu bar item (`StatusMenu.swift`: 5h percent tinted at 60/85%, dropdown of cards, bridge/cube status, Send now / Restart bridge / Cube settings… / Forget cube / a "Pomodoro notifications" checkbox / Quit, which also stops the bridge child it started; the checkbox gates the macOS banner `Notifier.swift` posts when the cube reports a finished phase, wording in `PomodoroNotice`); what it displays is derived by the pure `MenuModel` in `CubeLinkCore`.

The README is detailed and authoritative for setup, troubleshooting and design rationale.

## Commands

Bridge (from `bridge/`):
```sh
node server.mjs                      # or npm start; CUBE_SOURCE=admin for the Admin API source
CUBE_EXTRA_CARDS=1 node server.mjs   # also include the spend/token cards
./agent.sh install|status|logs|restart|uninstall   # macOS LaunchAgent
```
Config: optional `bridge/config.json` (copy `config.example.json`, gitignored); env vars override it (`CUBE_SOURCE`, `CUBE_PORT`, `CUBE_HOST`, `CUBE_REFRESH_MS`, `CUBE_EXTRA_CARDS`, `ANTHROPIC_ADMIN_KEY`, `ANTHROPIC_ADMIN_OAUTH_TOKEN`). Env booleans override both ways (`CUBE_EXTRA_CARDS=0` beats `extraCards: true`); a bad number falls back to the default with a warning; refresh is at least 1 s. `host` unset = every interface (WiFi cubes need it); `127.0.0.1` is fine for BLE-only. The Mac helper always reads `127.0.0.1`, so a `host` pinned to a LAN address gets a second loopback listener (`server.mjs`).

Firmware (from `firmware/`):
```sh
cp src/config.h.example src/config.h  # WiFi + BRIDGE_URL, gitignored
pio run                               # build (also fetches libdeps the simulator needs)
pio run -t upload && pio device monitor
```

Mac helper (from `mac-helper/`):
```sh
swift test                                          # frame encoding, push policy, supervisor decisions, shared fixture
./install.sh install|status|logs|restart|uninstall  # LaunchAgent; install needs a terminal (Bluetooth-permission prompt)
```
`install.sh` also records `bridgeDir`/`nodePath` in the `com.claude-cube.link` defaults, used after env and before searching near the cwd / app bundle.
App env: `CUBE_PORT`, `CUBE_BRIDGE_DIR`, `CUBE_NODE`; flag `--port`.

Desktop simulator (from `firmware/sim/`, needs `brew install sdl2` and one prior `pio run`):
```sh
make run                                   # SDL window; SCALE=3, CUBE_BRIDGE_URL=... to override
make shot                                  # headless: one PNG per card of the live bridge payload -> build/shot-N.png
./build/cube-shot build/state states.json  # render a saved payload (e.g. red ring, empty track)
./build/cube-shot build/shot @portal       # a screen that is not a payload card: @portal, @ota, @ble-pair|@ble-wait, @pomo-ready|focus|paused|break|done|long
CUBE_BLE=live|stale|pair|none make run     # what the sim cube believes about Bluetooth (default none); also @ble-pair, @ble-wait shots
CUBE_ORIENT=2 make run                     # sim cube is upside down: flips ~1 s after start (default 0); the window is the only place the flip shows, `make shot` reads back the logical frame and is unaffected
make test                                  # host-side unit tests (pomodoro, gesture, portal helpers); no SDL or board needed
make run POMO_FAST=60                      # Pomodoro minutes become seconds: watch a full cycle and the phase-end alert (make clean after)
```

There is no linter, and the only automated tests are the host-side unit tests above. Verification is: run the bridge, check `/api/status` / the preview page, and use `make shot` to see what the real firmware renderer draws.

## Architecture

**Data flow.** `server.mjs` runs a refresh loop: `limits.mjs` fetches the two rate-limit windows from `GET https://api.anthropic.com/api/oauth/usage` using the local Claude Code login (macOS Keychain `Claude Code-credentials`, else `~/.claude/.credentials.json`), cached 60s and fail-soft (a failed read keeps the last good one for up to 10 min, then the rings show an empty track and a short reason). Only when `extraCards` is on does it also call a source (`sources/local.mjs` — incremental byte-offset reader of every `.jsonl` under `~/.claude/projects` (recursive, subagents included; the offset only advances past complete lines; unchanged size+mtime is skipped), dedup on `requestId`, priced via `pricing.mjs`; or `sources/admin.mjs` — Usage & Cost Admin API, refresh floored at 60s). `cards.mjs::buildPayload` turns it all into the payload.

**All formatting lives in `bridge/cards.mjs`.** Redesigning the dashboard means editing that file and reloading — no reflash. Keep it that way: don't add number/unit formatting to the firmware.

**Payload contract** (produced by `cards.mjs`, parsed by `firmware/src/payload.cpp` into structs in `payload.h`): `{v, ts, src, cards: [{t, v, s1, s2, c, g?, g2?, c2?, rows?, m?, mc?}]}`. `ts` (build time, epoch s) and `src` (source label) are for people reading the JSON; the firmware ignores them.
- `g` present (0–100) → ring card: `g` fills the arc, `v` goes in the ring center, `s1` captions `v`, `s2` labels the percentage below. `g: -1` = empty track (no reading). No `g` → big-number text card.
- `g2` present (0–100, `-1` = empty track) → dual-ring card, which is what `cards.mjs` emits by default (one card, `usageCard`): `g`/`c` are the outer ring (5h), `g2`/`c2` the inner (7d), `v`/`s1` the centre countdown and caption (sized per card, inside the inner ring), `rows` = two `{k, p, r}` legend lines (`drawDualCard` in `ui.cpp`), and `m`/`mc` the unread-mail count + accent, drawn with an envelope in the ring gap (absent = no badge). All optional; a card without `g2` renders as a single ring.
- `c` is an accent name: `accent|blue|green|amber|violet|red` (anything else → ink).
- Firmware limits: `MAX_CARDS = 8`; fixed char buffers (`title`/`value` 24, `sub1`/`sub2` 40, `src` 12) truncate silently. Ring cards share one font size across the deck, sized to the longest `v` inside the ~130px ring center, so keep `v` short.
- Changing the contract means updating `cards.mjs`, `payload.h/.cpp`, `ui.cpp`, and `preview.html` together.
- The deck is the payload cards **plus one local Pomodoro card, always last** (`uiDeckSize` / `uiPomodoroIndex` in `ui.cpp`). It is not in the contract, not counted against `MAX_CARDS`, and exists even with no payload. It is the one place the firmware formats text itself (`MM:SS`, `pomoFormatTime`); everything else stays formatted by `cards.mjs`. Its state machine (`pomodoro.cpp`) and the touch gesture classifier (`gesture.cpp`) are pure and host-tested (`make test` in `firmware/sim/`). A double tap on that card starts/pauses/resumes, a triple tap resets; swipes never change it. `GestureTracker::update` groups taps only when its `multiTap` flag is set (Pomodoro card, no editor open): a tap waits `MULTI_TAP_GAP_MS` (350 ms) for the next one, so a double tap acts that long after the second lift, a third tap inside the gap fires at once, and a swipe, drag, long press or card change drops the pending taps. Everywhere else a tap is a plain `Tap`, delivered immediately (the editors hit-test it), and `update` must be called every loop, finger down or not, so the pending taps settle. There is no on-screen feedback for the tap count. A swipe up on the idle Pomodoro card opens an on-device settings editor (`pomo_editor.cpp` owns the layout, hit-testing and step/clamp rules, drawn by `uiPomodoroEditor`); values persist in NVS keys `pf/ps/pl/pn` (`pomoSettings()` in `settings.cpp`, falling back to `config.h` via `pomo_defaults.h`) and are applied with `Pomodoro::setConfig`, which acts while idle or between phases (DONE) and returns whether it did; a change from the Mac that arrives mid-phase waits in `pomoConfigPending` (`main.cpp`). It is local-only: not in the payload contract and not mirrored in `preview.html`. Preview it with `./build/cube-shot build/shot @pomo-edit`.

**Display panel.** A swipe down on any card (when no editor is open) slides a second settings panel in from the top: brightness, screen sleep, auto-advance, WiFi refresh, same four-row layout and RESET/DONE bar as the Pomodoro editor (`dev_editor.cpp` owns labels, value text and preset stepping; hit-testing is the shared `pomoEditorHit`; drawn by `uiDeviceEditor`/`uiDeviceSlide`). Swipe up or DONE closes and saves (`DeviceSettings`, NVS `bl/sl/rt/pi`); brightness previews live. Local-only, not in `preview.html`. Preview: `./build/cube-shot build/shot @dev-edit`.

**Battery indicator.** The top bar of every card shows a battery glyph + percent, read on-device from `PIN_BAT_ADC` (`battery.cpp`; logic in `battery_util.h`, host-tested). It is local-only: not in the payload contract and not in `preview.html`. With no cell fitted (mV < 2500) the cube is on USB and shows a full 100%. The top bar's right side shows the age of the data (`age_label.h`, pure, host-tested: `OFF` / `Ns` / `Nm` / `Nh` / `OLD`), mirrored in `preview.html`. In the simulator set `CUBE_BATTERY=<0..100>|none` (default 78).

**Screen sleep.** After `SCREEN_SLEEP_MS` (`config.h`, default 15 min, 0 = never) with no touch and no fresh data, `main.cpp` turns the backlight off and puts the panel to sleep; a touch wakes it (and is swallowed), data alone does not. Never while a Pomodoro session runs/pauses, the editor is open, or a pairing screen is up. Logic is `idle_sleep.h` (pure, host-tested). Local-only, not in the payload or `preview.html`; the sim ignores brightness, so only the state machine is testable off-device.

**Auto-rotate.** The cube flips its display 180° when stood on its other end, always on (no setting). `imu.cpp` reads the QMI8658C accelerometer (I2C `0x6B`, shared bus) and `main.cpp` polls it at 5 Hz into `Orientation` (`orientation.h`, pure, host-tested: `|up| > 0.6 g`, `|up| > |az|`, held 1 s, deferred while a finger is down; flat or tilted never flips). The result goes to `lcd.setRotation(0|2)`, so `ui.cpp` and `preview.html` are untouched; raw CST816 coordinates are mirrored by `touchToScreen` (`touch_map.h`) before gesture code sees them. The panel is not written while the screen sleeps; the rotation is applied on wake before the first frame. Local-only: not in the payload contract. `IMU_UP_SIGN` in `board_pins.h` is the sign of IMU X when the screen top is up (X is the vertical axis with the cube on its edge, measured; the sign is still to be confirmed on hardware); `docs/imu-acceptance.md` is the open checklist. The simulator stand-in is `imu_sim.cpp` (`CUBE_ORIENT`); `touch_sim.cpp` undoes the mirror so `main.cpp` sees raw coordinates as on the board.

**Three renderers of the same layout.**
- `firmware/src/ui.cpp` — the real one: whole frame composed in one full-screen sprite then pushed. Ring notches at 60%/85% (`NOTCHES`), animations (700ms first sweep, 260ms retarget, 400ms colour crossfade); `uiAnimating()` tells `main.cpp` to keep drawing frames.
- `bridge/preview.html` — a separate JS/SVG reimplementation for browser previewing; it shows whether the data reads well, not whether the firmware draws it correctly. Layout/threshold changes in `ui.cpp` must be mirrored here by hand.
- `firmware/sim/` — compiles the real `main.cpp`, `ui.cpp`, `payload.cpp` against LovyanGFX's SDL backend (`-DLGFX_SDL`). Only hardware layers are swapped: `net_sim.cpp` (socket HTTP GET) for `net.cpp`, `touch_sim.cpp` (mouse) for `touch.cpp`, and `sim/Arduino.h` is a tiny shim (millis/delay/Serial/min/max/constrain). Firmware code shared with the sim must stay within that shim; `display.h` has an `#ifdef LGFX_SDL` branch for the sim panel.

**Transports.** The cube takes the same payload over BLE (preferred) or WiFi polling. BLE: `net_ble.cpp` is a NimBLE GATT server (Payload write / Control notify / Info read, passkey pairing, one bond; who may stay connected is `ble_auth.h`, pure, host-tested: Just Works links, any second device, and a link not secured within 60 s (`bleUnsecuredExpired`) are dropped); `ble_frame.h` (`FrameAssembler`) reassembles chunked JSON and `bleTake()` feeds it to the unchanged `payloadFromJson()`. `transport_policy.h` (pure, host-tested) decides when WiFi runs: off while a BLE payload arrived in the last 15 s, on when it goes stale (or there is no bond), off again after 30 s of steady BLE; `WIFI_ALWAYS_ON` (`config.h`) keeps it up for OTA. Boot no longer forces the setup portal when there is no SSID; the portal auto-opens on a failed join only with no bond; "Forget paired Mac" (`bleForgetBonds()`) lives in the portal. The wire format is `docs/ble-protocol.md`, pinned by `firmware/sim/fixtures/ble-frames.txt`, which both `make test` and `swift test` read. The Settings characteristic (`settings_json.cpp`, host-tested; `CubeSettings.swift` on the Mac, edited via the menu's "Cube settings…" window) lets the Mac change everything the portal can, over the same encrypted link; only the groups that changed are written, network last (`Ok` can mean a partial save, so the Mac reads back), and a rejected write is republished so a read never returns it; see the protocol doc. The cube also notifies Control `04 <ended> <next>` when a Pomodoro phase ends (`bleNotifyPomodoro`, called from the `takeAlert()` block in `main.cpp`); the Mac turns it into a banner, BLE only, fire and forget. The Mac side is `mac-helper/` (`BridgeSupervisor` runs `node bridge/server.mjs` or adopts one already serving the port, `CubeLink` is CoreBluetooth, `PushPolicy` sends on change / every 5 s / on "send now" and never when the bridge is down). The top bar shows which transport delivered the data (`BLE` or `WIFI`, `UiLink` passed to `uiRender`; blank when offline); like the battery it is local-only, not in the payload and not in `preview.html`. In the simulator `ble_sim.cpp` stands in for `net_ble.cpp` (`CUBE_BLE`) and `CUBE_LINK=ble|wifi|none` picks the marker in `make shot`. The BLE path has not been run on a real cube; `docs/ble-acceptance.md` is the open checklist.

**Hardware specifics** are isolated in `firmware/src/board_pins.h` (all pins, `LCD_OFFSET_Y`) and `firmware/src/display.h` (LovyanGFX panel config, `cfg.invert`). Board reference (peripherals, full GPIO map, I2C addresses) is in `docs/ESP32-S3-Touch-LCD-1.69.md`.

**Settings, portal and OTA.** The portal form also edits display/behaviour knobs (`DeviceSettings` in `device_settings.h`: backlight, screen-sleep minutes, auto-advance seconds, WiFi poll seconds; NVS keys `bl/sl/rt/pi`, defaults from the old `config.h` macros via `device_defaults.h`) and the Pomodoro durations; a blank numeric field keeps the current value. WiFi SSID/password, bridge URL and OTA password live in NVS (`settings.cpp`, namespace `cube`), falling back to `config.h` for any key never stored, so an empty NVS behaves exactly like before. The setup portal (`portal.cpp`: SoftAP `claude-cube-XXXX` + DNS catch-all + one form at 192.168.71.1, not .4.1 so the station side can join a 192.168.4.x LAN) starts when the screen is touched in the 3 s boot window (`SETUP_WINDOW_MS`) and held for 5 s, or when WiFi fails to join for ~20 s and no Mac is bonded. It runs AP+STA: while nobody is on the AP it retries the stored network (10 s every 30 s); once joined it shows the LAN address and serves the form there too. Every request must name the cube by address and every save/forget carry a per-boot token (`portalHostIs` / `portalTokenOk` in `portal_util.h`, host-tested), so other web pages cannot post to it. Opened by a failed join (`autoRetry`), it reboots to normal as soon as the stored network answers (`portalStaRecoveredReboot`); there is no idle reboot. A blank OTA password keeps the stored one; a checkbox clears it. With no SSID and no bond the cube shows "Waiting for a Mac" instead (a touch dismisses it). OTA is ArduinoOTA as `claude-cube.local` (see the commented `upload_protocol = espota` lines in `platformio.ini`). `settings.cpp`, `portal.cpp` and `ota.cpp` are hardware-only and have stand-ins in `firmware/sim/`.
