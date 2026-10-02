# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A desk display for Claude rate-limit usage. Two halves that talk over plain HTTP on the LAN:

- `bridge/` — Node ≥20 ESM, **zero npm dependencies**. Gathers data on the host and serves one small, fully pre-formatted JSON document at `GET /api/status` (plus `/` = browser mock, `/health`).
- `firmware/` — PlatformIO / Arduino for a Waveshare ESP32-S3-Touch-LCD-1.69 (240x280 ST7789V2, CST816 touch), drawn with LovyanGFX (no LVGL) + ArduinoJson. It only polls the bridge (or takes the same payload over BLE) and draws strings; it never talks to Anthropic.
- `mac-helper/` — Swift package building `ClaudeCubeLink.app`: supervises the bridge (`node bridge/server.mjs`) and pushes `/api/status` to the cube over BLE (CoreBluetooth). Optional; BLE users run its `install.sh` instead of `bridge/agent.sh`.

The README is detailed and authoritative for setup, troubleshooting and design rationale.

## Commands

Bridge (from `bridge/`):
```sh
node server.mjs                      # or npm start; CUBE_SOURCE=admin for the Admin API source
CUBE_EXTRA_CARDS=1 node server.mjs   # also include the spend/token cards
./agent.sh install|status|logs|restart|uninstall   # macOS LaunchAgent
```
Config: optional `bridge/config.json` (copy `config.example.json`, gitignored); env vars override it (`CUBE_SOURCE`, `CUBE_PORT`, `CUBE_REFRESH_MS`, `CUBE_EXTRA_CARDS`, `ANTHROPIC_ADMIN_KEY`, `ANTHROPIC_ADMIN_OAUTH_TOKEN`).

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
App env: `CUBE_PORT`, `CUBE_BRIDGE_DIR`, `CUBE_NODE`; flag `--port`.

Desktop simulator (from `firmware/sim/`, needs `brew install sdl2` and one prior `pio run`):
```sh
make run                                   # SDL window; SCALE=3, CUBE_BRIDGE_URL=... to override
make shot                                  # headless: one PNG per card of the live bridge payload -> build/shot-N.png
./build/cube-shot build/state states.json  # render a saved payload (e.g. red ring, empty track)
./build/cube-shot build/shot @portal       # a screen that is not a payload card: @portal, @ota, @ble-pair|@ble-wait, @pomo-ready|focus|paused|break|done|long
CUBE_BLE=live|stale|pair|none make run     # what the sim cube believes about Bluetooth (default none); also @ble-pair, @ble-wait shots
make test                                  # host-side unit tests (pomodoro, gesture, portal helpers); no SDL or board needed
make run POMO_FAST=60                      # Pomodoro minutes become seconds: watch a full cycle and the phase-end alert (make clean after)
```

There is no linter, and the only automated tests are the host-side unit tests above. Verification is: run the bridge, check `/api/status` / the preview page, and use `make shot` to see what the real firmware renderer draws.

## Architecture

**Data flow.** `server.mjs` runs a refresh loop: `limits.mjs` fetches the two rate-limit windows from `GET https://api.anthropic.com/api/oauth/usage` using the local Claude Code login (macOS Keychain `Claude Code-credentials`, else `~/.claude/.credentials.json`), cached 60s and fail-soft. Only when `extraCards` is on does it also call a source (`sources/local.mjs` — incremental byte-offset reader of `~/.claude/projects/*.jsonl`, dedup on `requestId`, priced via `pricing.mjs`; or `sources/admin.mjs` — Usage & Cost Admin API, refresh floored at 60s). `cards.mjs::buildPayload` turns it all into the payload.

**All formatting lives in `bridge/cards.mjs`.** Redesigning the dashboard means editing that file and reloading — no reflash. Keep it that way: don't add number/unit formatting to the firmware.

**Payload contract** (produced by `cards.mjs`, parsed by `firmware/src/payload.cpp` into structs in `payload.h`): `{v, ts, src, est, cards: [{t, v, s1, s2, c, g?}]}`.
- `g` present (0–100) → ring card: `g` fills the arc, `v` goes in the ring center, `s1` captions `v`, `s2` labels the percentage below. `g: -1` = empty track (no reading). No `g` → big-number text card.
- `c` is an accent name: `accent|blue|green|amber|violet|red` (anything else → ink).
- Firmware limits: `MAX_CARDS = 8`; fixed char buffers (`title`/`value` 24, `sub1`/`sub2` 40, `src` 12) truncate silently. Ring cards share one font size across the deck, sized to the longest `v` inside the ~130px ring center, so keep `v` short.
- Changing the contract means updating `cards.mjs`, `payload.h/.cpp`, `ui.cpp`, and `preview.html` together.
- The deck is the payload cards **plus one local Pomodoro card, always last** (`uiDeckSize` / `uiPomodoroIndex` in `ui.cpp`). It is not in the contract, not counted against `MAX_CARDS`, and exists even with no payload. It is the one place the firmware formats text itself (`MM:SS`, `pomoFormatTime`); everything else stays formatted by `cards.mjs`. Its state machine (`pomodoro.cpp`) and the touch gesture classifier (`gesture.cpp`) are pure and host-tested (`make test` in `firmware/sim/`). Long-press (600 ms) on that card starts/pauses/resumes, a hold to 2 s resets; tap and swipe never change. A swipe up on the idle Pomodoro card opens an on-device settings editor (`pomo_editor.cpp` owns the layout, hit-testing and step/clamp rules, drawn by `uiPomodoroEditor`); values persist in NVS keys `pf/ps/pl/pn` (`pomoSettings()` in `settings.cpp`, falling back to `config.h` via `pomo_defaults.h`) and are applied with `Pomodoro::setConfig`, which only acts while idle. It is local-only: not in the payload contract and not mirrored in `preview.html`. Preview it with `./build/cube-shot build/shot @pomo-edit`.

**Battery indicator.** The top bar of every card shows a battery glyph + percent, read on-device from `PIN_BAT_ADC` (`battery.cpp`; logic in `battery_util.h`, host-tested). It is local-only: not in the payload contract and not in `preview.html`. With no cell fitted (mV < 2500) the cube is on USB and shows a full 100%. The glyph is dropped (number only) when the card name would not fit beside it. In the simulator set `CUBE_BATTERY=<0..100>|none` (default 78).

**Three renderers of the same layout.**
- `firmware/src/ui.cpp` — the real one: whole frame composed in one full-screen sprite then pushed. Ring notches at 60%/85% (`NOTCHES`), animations (700ms first sweep, 260ms retarget, 400ms colour crossfade); `uiAnimating()` tells `main.cpp` to keep drawing frames.
- `bridge/preview.html` — a separate JS/SVG reimplementation for browser previewing; it shows whether the data reads well, not whether the firmware draws it correctly. Layout/threshold changes in `ui.cpp` must be mirrored here by hand.
- `firmware/sim/` — compiles the real `main.cpp`, `ui.cpp`, `payload.cpp` against LovyanGFX's SDL backend (`-DLGFX_SDL`). Only hardware layers are swapped: `net_sim.cpp` (socket HTTP GET) for `net.cpp`, `touch_sim.cpp` (mouse) for `touch.cpp`, and `sim/Arduino.h` is a tiny shim (millis/delay/Serial/min/max/constrain). Firmware code shared with the sim must stay within that shim; `display.h` has an `#ifdef LGFX_SDL` branch for the sim panel.

**Transports.** The cube takes the same payload over BLE (preferred) or WiFi polling. BLE: `net_ble.cpp` is a NimBLE GATT server (Payload write / Control notify / Info read, passkey pairing, one bond); `ble_frame.h` (`FrameAssembler`) reassembles chunked JSON and `bleTake()` feeds it to the unchanged `payloadFromJson()`. `transport_policy.h` (pure, host-tested) decides when WiFi runs: off while a BLE payload arrived in the last 15 s, on when it goes stale (or there is no bond), off again after 30 s of steady BLE; `WIFI_ALWAYS_ON` (`config.h`) keeps it up for OTA. Boot no longer forces the setup portal when there is no SSID; the portal auto-opens on a failed join only with no bond; "Forget paired Mac" (`bleForgetBonds()`) lives in the portal. The wire format is `docs/ble-protocol.md`, pinned by `firmware/sim/fixtures/ble-frames.txt`, which both `make test` and `swift test` read. The Mac side is `mac-helper/` (`BridgeSupervisor` runs `node bridge/server.mjs` or adopts one already serving the port, `CubeLink` is CoreBluetooth, `PushPolicy` sends on change / every 5 s / on "send now" and never when the bridge is down). In the simulator `ble_sim.cpp` stands in for `net_ble.cpp` (`CUBE_BLE`). The BLE path has not been run on a real cube; `docs/ble-acceptance.md` is the open checklist. The payload contract and `cards.mjs` are unchanged.

**Hardware specifics** are isolated in `firmware/src/board_pins.h` (all pins, `LCD_OFFSET_Y`) and `firmware/src/display.h` (LovyanGFX panel config, `cfg.invert`). Board reference (peripherals, full GPIO map, I2C addresses) is in `docs/ESP32-S3-Touch-LCD-1.69.md`.

**Settings, portal and OTA.** WiFi SSID/password, bridge URL and OTA password live in NVS (`settings.cpp`, namespace `cube`), falling back to `config.h` for any key never stored, so an empty NVS behaves exactly like before. The setup portal (`portal.cpp`: SoftAP `claude-cube-XXXX` + DNS catch-all + one form at 192.168.4.1) starts when there is no SSID, when WiFi fails to join for ~20 s (it reboots after 60 s with nobody joined, to retry), or when the screen is touched in the 3 s boot window (`SETUP_WINDOW_MS`) and held for 5 s. OTA is ArduinoOTA as `claude-cube.local` (see the commented `upload_protocol = espota` lines in `platformio.ini`). `settings.cpp`, `portal.cpp` and `ota.cpp` are hardware-only and have stand-ins in `firmware/sim/`.
