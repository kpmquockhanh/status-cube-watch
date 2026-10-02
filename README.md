# Claude Status Cube

A desk display that shows how much of your Claude rate limits you have left.
Runs on a **Waveshare ESP32-S3-Touch-LCD-1.69** (240x280 ST7789V2, CST816
touch).

Two swipeable cards — the five-hour session window and the seven-day window —
and nothing else. Each one is a dial: the ring is the percentage, the middle
of the ring holds the one thing a ring cannot show, and the exact number drops
to a single line in the gap at the bottom.

```
┌─────────────────────┐
│ SESSION          4s●│
│      ╭───────╮      │
│    ╭─╯       ╰─╮    │
│   │   4h 09m    │   │
│   │ UNTIL RESET │   │
│    ╰─╮       ╭─╯    │
│      ╰──   ──╯      │
│    14% OF 5H LIMIT  │
│        ● ○          │
└─────────────────────┘
```

Two notches are cut into the ring at 60% and 85%, where it turns amber and
then red, so the arc reads against its own thresholds with no legend anywhere
on screen.

## How it fits together

```
~/.claude/projects/*.jsonl  ─┐
                             ├─→  bridge (Node)  ──HTTP──→  ESP32-S3
Anthropic Usage & Cost API  ─┘     aggregates,              draws cards
                                   formats strings
```

The device never talks to Anthropic. A host-side bridge does the aggregation
and serves one small, pre-formatted JSON document over the LAN. That keeps
credentials off the microcontroller, keeps TLS and pagination off the
microcontroller, and — most usefully — means **you redesign the dashboard by
editing `bridge/cards.mjs` and reloading, with no reflash.**

## The gauges

The whole deck is two cards, because the only numbers that change what you do
next are the two rate-limit windows.

Both come from the same place Claude Code's own status line gets them:
`GET https://api.anthropic.com/api/oauth/usage`, which returns each window's
utilization as a whole percentage plus the instant it resets. That is the
figure the server is actually enforcing, so there is nothing to estimate and no
error bar. Green under 60%, amber to 85%, red above.

Authentication reuses the Claude Code login already on this machine -- the
macOS Keychain item `Claude Code-credentials`, falling back to
`~/.claude/.credentials.json` elsewhere. The token never leaves the host; the
device only ever receives the rendered percentage. `bridge/limits.mjs` caches
the response for 60s and fails soft: with no login, no network, or an endpoint
that has moved, the ring keeps its empty track and the reason goes on the line
underneath. An unfilled ring reads as a legible nothing; a missing ring reads
as a bug.

The spend and token breakdowns (`TODAY`, `THIS MONTH`, `TOKENS TODAY`,
`TOP MODEL`, `TOP PROJECT`) are still built in `bridge/cards.mjs` and are one
flag away -- set `"extraCards": true` in `bridge/config.json`, or run with
`CUBE_EXTRA_CARDS=1`, to swipe through them as well. They are off by default,
and they have no percentage of anything to fill a ring with, so they keep the
older big-number layout (see **Adding a card**).

### Unread mail card

Set `"gmail": {"user": "you@gmail.com", "appPassword": "xxxx xxxx xxxx xxxx"}`
in `bridge/config.json` (or `CUBE_GMAIL_USER` / `CUBE_GMAIL_PASSWORD`) and a
`MAIL` card with your unread inbox count follows the two rings. The bridge
reads it over IMAP (`STATUS INBOX (UNSEEN)`, at most once a minute), so the
device never sees a credential. Create the app password at
<https://myaccount.google.com/apppasswords> (needs 2-step verification; some
Workspace admins disable them). If a fetch fails the last count stays on screen
marked `stale: ...`; with no count yet the card says why. Leave the settings
empty and there is no card.

## Two data sources

| Source | What it reads | Who it works for |
|---|---|---|
| `local` *(default)* | Claude Code's own transcripts in `~/.claude/projects` | Anyone. Works on a personal account, no API key, no network call. |
| `admin` | The [Usage & Cost Admin API](https://platform.claude.com/docs/en/manage-claude/usage-cost-api) | Organizations only. Needs an Admin API key or an `org:admin` OAuth token. |

**Start with `local`** unless you're on an org plan. The Admin API is
unavailable for individual accounts, and `local` covers the machine you
actually work on.

A caveat worth knowing for `local`: the transcripts record tokens, not
billing. The bridge prices them with the published per-model rates, so the
dollar figures are an *equivalent API cost*. If you're on a Claude subscription
you aren't billed per token, and the number is a usage proxy, not an invoice.
Cards show a `~` in the top bar whenever the figure is an estimate. In `admin`
mode the cost report supplies real billed dollars and the `~` disappears.

## Quick start

### 1. Bridge

```bash
cd bridge
node server.mjs
```

No dependencies to install — it's plain Node ≥20 with zero packages. It prints
the URL to put in the firmware config:

```
claude-status-cube bridge  source=local  refresh=5000ms
  preview   http://localhost:8787/
  device    http://192.168.1.50:8787/api/status
```

Open the **preview** URL in a browser: it renders the same payload as a
240x280 device mock, so you can build and tune the whole dashboard before the
hardware arrives. Click the screen or use the arrow keys to change cards.

To keep it running without a terminal open, install it as a LaunchAgent:

```bash
./agent.sh install      # starts now, and at every login
./agent.sh status       # loaded? answering? what's on the cards?
./agent.sh logs         # tail ~/Library/Logs/claude-status-cube/bridge.log
./agent.sh restart      # after editing cards.mjs
./agent.sh uninstall
```

An *agent* rather than a *daemon* on purpose: it runs as you, inside your GUI
login session, which is what lets it read the Claude Code token from your login
Keychain. A daemon starts before login, has no Keychain, and could not
authenticate. launchd restarts it if it crashes but leaves it alone if you stop
it deliberately (`SuccessfulExit=false`), and backs off 10s between respawns so
a bad config cannot spin the CPU.

The interpreter is pinned to an absolute path — launchd does not read your
shell profile — and the script prefers `/opt/homebrew/bin/node` over an nvm
build, whose path disappears when you clean up a Node version. Override with
`CUBE_NODE=/path/to/node ./agent.sh install`.

This keeps the cube alive as long as the Mac is awake. It still goes dark when
the Mac sleeps; for a display that is genuinely independent, run the bridge on
a Raspberry Pi or another always-on box instead.

For Admin API mode:

```bash
export ANTHROPIC_ADMIN_KEY=sk-ant-admin01-...
CUBE_SOURCE=admin node server.mjs
```

(or copy `config.example.json` to `config.json` and set it there).

### 2. Firmware

```bash
cd firmware
cp src/config.h.example src/config.h   # fill in SSID, password, bridge URL
pio run -t upload && pio device monitor
```

**Check `src/board_pins.h` before the first flash.** Waveshare ships several
1.69" variants and the revisions don't all share a pinout. Every
board-specific value is in that one file; cross-check it against the pin table
on your board's product wiki.

### 3. Pairing over Bluetooth (optional, recommended)

The Mac can push the data to the cube over Bluetooth, so the cube needs no WiFi at all. With WiFi
also configured, it is the automatic fallback when the Mac is out of range or asleep.

```sh
cd mac-helper
./install.sh install      # builds the app, asks for Bluetooth permission, installs the LaunchAgent
./install.sh status|logs|restart|uninstall
```

1. Flash the cube. With no WiFi configured and no Mac paired it shows **Waiting for a Mac** until a payload arrives (a touch dismisses it).
2. `./install.sh install`, click **Allow** on the macOS Bluetooth prompt. The script pauses
   (`read`) until you press Enter, so run it from a terminal, not a script without stdin.
3. The cube shows a 6-digit code; macOS asks for it. Type it. That is the whole pairing.
4. From then on it reconnects by itself. The app also runs the Node bridge for you
   (`node bridge/server.mjs`, restarted if it dies); if something already serves the port, such as
   a running `bridge/agent.sh` job, the app adopts that bridge instead of starting its own.

**`install.sh` replaces `bridge/agent.sh`.** BLE users run `mac-helper/install.sh` instead of
`bridge/agent.sh install`. If the `agent.sh` job is already installed, run
`bridge/agent.sh uninstall` first; otherwise both LaunchAgents fight for the same port.
The app reads `CUBE_PORT` (default 8787), `CUBE_BRIDGE_DIR` and `CUBE_NODE` (path to `node`) from
the environment, and takes `--port N` on its command line; `install.sh` writes them into the
LaunchAgent, which restarts the app only after a crash (`KeepAlive` with `SuccessfulExit=false`).

While a Mac is paired and data arrives over Bluetooth (a payload every 5 s) the cube turns WiFi off
(with no paired Mac, or with `WIFI_ALWAYS_ON 1`, WiFi stays on even when Bluetooth is live). If
nothing arrives for 15 s and WiFi is configured it polls the bridge instead, and goes back to
Bluetooth after it has been steady for 30 s. `WIFI_ALWAYS_ON 1` in `config.h` keeps WiFi up
(needed for OTA while Bluetooth is working). With no WiFi configured and the Mac away, the last
cards stay on screen and the freshness counter keeps counting; the setup portal does not open by
itself unless the cube has no paired Mac.

**Pairing again.** Hold the screen at boot, join the cube's `claude-cube-XXXX` network, press
**Forget paired Mac**, then also remove "Claude Cube" in System Settings > Bluetooth. If the cube's
flash was erased the Mac keeps the old bond and the log says so (`./install.sh logs`).

The protocol is in `docs/ble-protocol.md`. The manual hardware checklist (nothing in it has been run
on a real cube yet) is `docs/ble-acceptance.md`.

### Changing WiFi without reflashing (setup portal)

The cube shows a setup screen when it cannot join the stored network within ~20 s and has no paired Mac (a cube with no WiFi and no Mac shows "Waiting for a Mac" instead, see Pairing over Bluetooth), or when you touch the screen while the boot screen says "HOLD SCREEN FOR SETUP" (the first 3 s after power-up) and keep holding for 5 s. Join the open `claude-cube-XXXX` network (scan the QR on the screen); the setup page opens by itself, or browse to `192.168.4.1`. Enter the WiFi name and password, the bridge URL and, optionally, an OTA password. A WiFi password must be 8 to 63 characters (or empty for an open network). A blank password keeps the saved one unless you change the network. If nobody joins within a minute the cube reboots and retries the stored network, so a router that was slow to come back after a power cut does not strand it. The setup network is open by design: while it is up (setup screen showing), anyone in radio range can change the cube's settings, including the OTA password. Only reconfigure when you are at the cube, and set an OTA password. The page also has a **Forget paired Mac** button.

### Updating over WiFi (OTA)

After the first USB flash the cube is reachable as `claude-cube.local`. In `firmware/platformio.ini` uncomment `upload_protocol = espota`, `upload_port = claude-cube.local` and (if you set an OTA password) `upload_flags = --auth=...`, then run `pio run -t upload` as usual. The screen shows a progress bar. OTA runs under WiFi modem sleep (Bluetooth is always on, and the ESP32 does not allow WiFi power-save off alongside it), so an upload may occasionally time out: just retry. Without an OTA password anyone on your network can flash the cube; set one in the portal.

### Pomodoro card

The last card in the deck is a Pomodoro timer that runs on the cube itself (so it works with the bridge down). **Hold** the screen for about half a second to start, pause or resume; keep holding for two seconds to reset. It runs 25 min focus, 5 min break, and a 15 min break after four focus sessions, each phase started by you. When a phase ends the cube jumps to this card and pulses the ring and backlight until you hold to start the next phase. To change the lengths without reflashing, **swipe up** on the Pomodoro card while the timer is idle. A settings screen opens with a `-` and `+` for FOCUS (steps of 5 min), SHORT BREAK (1 min), LONG BREAK (5 min) and SESSIONS (1). RESET returns to the defaults; DONE (or a swipe down) saves them, and they survive power cycles. A running, paused or finished phase is never resized: new lengths apply from the next one you start. The defaults are `POMO_FOCUS_MIN`, `POMO_BREAK_MIN`, `POMO_LONG_MIN` and `POMO_SESSIONS` in `config.h`.

## Layout

```
bridge/
  server.mjs          HTTP server + refresh loop
  cards.mjs           events → the card deck (edit this to redesign)
  pricing.mjs         per-model rates and cache multipliers
  limits.mjs          rate-limit windows from the OAuth usage endpoint
  agent.sh            install/remove the macOS LaunchAgent
  preview.html        browser mock of the device
  sources/local.mjs   incremental ~/.claude transcript reader
  sources/admin.mjs   Usage & Cost Admin API client
mac-helper/           Swift app: runs the bridge, pushes /api/status to the cube over BLE
  install.sh          install/remove the LaunchAgent
firmware/
  src/board_pins.h    all board-specific pins — check this first
  src/config.h        your WiFi + bridge URL (gitignored)
  src/display.h       LovyanGFX panel definition
  src/touch.cpp       CST816 driver
  src/ui.cpp          card rendering
  src/payload.cpp     bridge JSON → Payload (shared with the simulator)
  src/net.cpp         WiFi transport
  src/net_ble.cpp     BLE transport (NimBLE GATT server)
  src/ble_frame.h     chunk reassembly for the BLE wire format
  src/transport_policy.h  when WiFi runs versus BLE (pure, host-tested)
  src/main.cpp        poll loop and swipe handling
  sim/                desktop simulator — runs the UI without hardware
  sim/shot.cpp        headless frame grab: one PNG per card
```

## Testing without the board

Two ways, in increasing fidelity.

**Browser mock.** Start the bridge and open its address. `preview.html` renders
the same 240×280 layout and polls the same endpoint the device will. Good for
iterating on `cards.mjs` — edit, reload, see it. It is a JavaScript
reimplementation of the layout, so it tells you whether the *data* reads well,
not whether the firmware draws it correctly.

**Battery indicator.** Every card's top bar shows the battery level, measured on the board's ADC and drawn on-device (it is not part of the bridge payload). With no battery connected the cube is running from USB and shows a full 100%. In the simulator, `CUBE_BATTERY=30 make run` (or `none` for the no-battery case) picks the level.

**Desktop simulator.** `firmware/sim/` compiles the actual firmware sources —
`main.cpp`, `ui.cpp`, `payload.cpp` — against LovyanGFX's SDL backend and opens
a window instead of driving the ST7789. Only the two layers that touch hardware
are swapped: `net_sim.cpp` does a socket HTTP GET in place of WiFi, and
`touch_sim.cpp` feeds mouse events in place of the CST816 over I²C. The gesture
thresholds, the font-fitting ladder, the sprite compositing and the JSON parsing
are the real ones.

```sh
brew install sdl2
cd firmware && pio run          # once, to fetch LovyanGFX + ArduinoJson
cd sim && make run
```

Drag across the window to swipe, click to tap, `SCALE=3 make run` for a bigger
window, and `CUBE_BRIDGE_URL=http://localhost:8787/api/status make run` to point
it somewhere other than the `BRIDGE_URL` in `config.h`.

**Bluetooth in the simulator.** There is no BLE radio on the desktop, so `CUBE_BLE` picks what the
cube believes: `none` (default, WiFi-only as before), `live` (bonded, a payload arrives every 5 s),
`stale` (bonded, nothing arrives, so WiFi takes over after 15 s), `pair` (the passkey screen).
`./build/cube-shot build/shot @ble-pair` and `@ble-wait` render the passkey and "Waiting for a Mac"
screens.

**Frame grabs.** `make shot` renders every card of the live payload through the
same `ui.cpp` and writes one PNG per card to `build/`, with no window and no
screen-recording permission involved. It waits for the entry animation to
settle first, so what you get is the resting layout. Point it at a saved
payload to shoot states you cannot reach on demand -- the red ring, the
empty track, a card that does not exist yet:

```sh
cd firmware/sim
make shot                                  # the live bridge
./build/cube-shot build/state states.json  # build/state-0.png, -1.png, ...
```

What it still cannot tell you: whether the pins in `board_pins.h` match your
revision, how the ST7789 renders these colours, and how the panel behaves at its
real refresh rate.

## Adding a card

Push one more object onto the `cards` array in `bridge/cards.mjs`. Without a
`g`, you get the big-number layout:

```js
{ t: 'WEB SEARCHES', v: String(dayT.webSearch), s1: 'today', s2: '', c: 'blue' }
```

With a `g` — a whole percentage — you get a ring instead, and the other fields
change jobs: `g` fills the arc, `v` goes in the middle of it, `s1` captions
`v`, and `s2` labels the percentage in the gap at the bottom:

```js
{ t: 'CONTEXT', v: '38k', s1: 'tokens left', s2: 'of 200k', c: 'green', g: 81 }
```

`g: -1` means *no reading* and draws the empty track. The threshold colours
are yours to pick through `c`; the notches at 60% and 85% are fixed, so a ring
whose meaning does not break there wants a different accent than green/amber/
red so it doesn't imply one.

Reload the page. The firmware sizes the deck from the payload and the page
dots follow — nothing to change on the device. Accent names are `accent`,
`blue`, `green`, `amber`, `violet`, `red`.

Keep `v` short. A text card shrinks its own value to fit 220px. Ring cards
instead pick one size for the whole deck — the largest that fits the longest
value inside the ring's 130px middle — so the countdown does not change size
as you swipe. One long value therefore shrinks every ring, which is why
`until()` drops minutes once the horizon passes ten hours.

## Notes on the implementation

- **Incremental reads.** The local source keeps a byte offset per transcript
  and only parses appended bytes, so a 5-second refresh over a busy
  `~/.claude` stays cheap. Events are de-duplicated on `requestId`, because
  the same request is often written to the log more than once.
- **Admin polling.** The Usage & Cost API documents sustained polling at once
  per minute; the bridge enforces that floor in `admin` mode regardless of
  what you configure.
- **Cost vs. usage endpoints.** In `admin` mode the usage report gives hourly
  granularity per model and the cost report gives authoritative dollars in
  daily buckets. Both only matter with `extraCards` on; the default gauges come
  from the OAuth usage endpoint instead.
- **No LVGL.** Two dials don't need a widget toolkit. LovyanGFX with one
  full-screen sprite is simpler to build, has no `lv_conf.h` to maintain, and
  leaves most of RAM free. LVGL 9 is the upgrade path if the UI grows.
- **Motion.** A ring sweeps up from zero the first time you see a card after
  boot (700ms), retargets from wherever it currently is when the number moves
  under it (260ms), and crossfades through a threshold colour over 400ms.
  Everything eases out. It is deliberately per-card and once-per-boot: an
  animation replayed on every swipe is one you would see dozens of times a
  day, and there is no pulse or blink on the red state for the same reason.
  `uiAnimating()` is what tells `main.cpp` to keep drawing frames; the rest of
  the time the loop still redraws once a second for the freshness counter.

## Troubleshooting

| Symptom | Cause |
|---|---|
| White or blank screen | Pins in `board_pins.h` don't match your revision |
| Image shifted vertically | Change `LCD_OFFSET_Y` (20 ↔ 0) |
| Colours inverted | Flip `cfg.invert` in `src/display.h` |
| `[touch] not responding` | `PIN_I2C_SDA` / `PIN_I2C_SCL` / `PIN_TP_RST` wrong |
| Device shows `OFFLINE` | Bridge not reachable — check it's bound to the LAN address it printed, and that the host firewall allows the port |
| Costs look too low | An unrecognised model falls back to Sonnet-tier rates; add it to `bridge/pricing.mjs` |
| Cube never pairs | Check `./install.sh logs`; the app needs the Bluetooth permission (System Settings > Privacy & Security > Bluetooth) |
| `pio run` stalls at `Downloading 0%` | PlatformIO's CDN, not your network. See below. |

### When the PlatformIO registry is down

`dl.registry.platformio.org` occasionally serves at a crawl, which strands
`pio run` on `Downloading 0%` with an empty `~/.platformio/packages/`. Confirm
it's the CDN and not you by timing the same toolchain from GitHub:

```sh
curl -o /dev/null -w '%{speed_download} B/s\n' -r 0-8000000 \
  https://github.com/espressif/crosstool-NG/releases/download/esp-2021r2-patch5/xtensa-esp32s3-elf-gcc8_4_0-esp-2021r2-patch5-macos-arm64.tar.gz
```

If GitHub is fast, install the packages by hand into `~/.platformio/packages/`
from their upstream releases — the two xtensa toolchains and the RISC-V one
from [crosstool-NG](https://github.com/espressif/crosstool-NG/releases/tag/esp-2021r2-patch5),
the framework from [arduino-esp32 2.0.17](https://github.com/espressif/arduino-esp32/releases/tag/2.0.17),
and [esptool 4.11.0](https://github.com/espressif/esptool/releases/tag/v4.11.0).
Each directory needs a `package.json` (`name` and `version` matching what
`~/.platformio/platforms/espressif32/platform.json` asks for) and a `.piopm`
naming the owner — the registry adds both during packaging, so the raw
archives lack them. esptool from source also needs `pip install intelhex`
in the PlatformIO Python environment.

## Ideas from here

The board also has a QMI8658 IMU, a PCF85063 RTC, a buzzer, and battery
charging, none of which this scaffold uses yet:

- Wrist-raise or tilt to wake, via the IMU
- Buzzer when the daily spend crosses a threshold
- Deep sleep between polls, with the RTC as the wake source, for battery use
- A "burn rate" card: tokens per minute over the last 10 minutes
# status-cube-watch
# status-cube-watch
# status-cube-watch
