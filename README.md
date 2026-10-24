# Claude Status Cube

A desk display that shows how much of your Claude rate limits you have left.
Runs on a **Waveshare ESP32-S3-Touch-LCD-1.69** (240x280 ST7789V2, CST816
touch).

One card with both rate-limit windows — the five-hour session and the
seven-day week — as two concentric rings: outer is the session, inner is the
week. The middle holds the one thing a ring cannot show (time until the session
resets), a legend line per window sits underneath with its exact percentage and
reset time, and your unread-mail count rides in the gap of the rings next to an
envelope.

```
┌─────────────────────┐
│ 78%    BLE      4s● │
│      ╭───────╮      │
│    ╭─╯ ╭───╮ ╰─╮    │
│   │  │ 4h 09m │ │   │
│   │  │TO RESET│ │   │
│    ╰─╮╰─ ✉ 12 ─╯╭╯  │
│      ╰──   ──╯      │
│  ■ 5H  14%   4h 09m │
│  ■ 7D  53%   1d 1h  │
│        ● ○          │
└─────────────────────┘
```

Two notches are cut into each ring at 60% and 85%, where it turns amber and
then red, so the arcs read against their own thresholds with no legend of
colours anywhere on screen.

## How it fits together

```
Claude Code login (OAuth)   ─┐                       ┌─HTTP (WiFi)──────────────→┐
~/.claude/projects/*.jsonl  ─┼─→  bridge (Go)    ────┤                           ├─→ ESP32-S3
Anthropic Usage & Cost API  ─┤     aggregates,       └─→ ClaudeCubeLink.app ─BLE─→┘   draws cards
Gmail (IMAP, optional)      ─┘     formats strings       (Mac menu bar)
```

The cube takes the same payload either way: it polls the bridge over WiFi, or
the Mac helper app pushes it over Bluetooth (preferred; then the cube needs no
WiFi at all). The device never talks to Anthropic. A host-side bridge does the aggregation
and serves one small, pre-formatted JSON document over the LAN. That keeps
credentials off the microcontroller, keeps TLS and pagination off the
microcontroller, and — most usefully — means **you redesign the dashboard by
editing `bridge/internal/cards` and rebuilding the bridge, with no reflash.**

## The gauges

The bridge sends one card, because the only numbers that change what you do
next are the two rate-limit windows. The cube adds its own Volume card after it
while a Mac is paired. Until the first payload arrives, a "No data yet" card
stands in for the bridge's.

Both come from the same place Claude Code's own status line gets them:
`GET https://api.anthropic.com/api/oauth/usage`, which returns each window's
utilization as a whole percentage plus the instant it resets. That is the
figure the server is actually enforcing, so there is nothing to estimate and no
error bar. Green under 60%, amber to 85%, red above.

Authentication reuses the Claude Code login already on this machine -- the
macOS Keychain item `Claude Code-credentials`, falling back to
`~/.claude/.credentials.json` elsewhere. The token never leaves the host; the
device only ever receives the rendered percentage. `bridge/internal/limits` caches
the response for 60s and fails soft: a failed read keeps the last good one for
up to 10 minutes, and after that (or with no login, no network, or an endpoint
that has moved) the ring keeps its empty track and a short reason (`no login`,
`timeout`, `HTTP 401`, `offline`...) goes under the countdown. A window whose
reset time has passed shows the empty track too, until the next read. An
unfilled ring reads as a legible nothing; a missing ring reads as a bug.

The spend and token breakdowns (`TODAY`, `THIS MONTH`, `TOKENS TODAY`,
`TOP MODEL`, `TOP PROJECT`) are still built in `bridge/internal/cards` and are one
flag away -- set `"extraCards": true` in `bridge/config.json`, or run with
`CUBE_EXTRA_CARDS=1` (with the Mac helper: **Bridge settings…**), to swipe through them as well. They are off by default,
and they have no percentage of anything to fill a ring with, so they keep the
older big-number layout (see **Adding a card**).

### Unread mail card

Set `"gmail": {"user": "you@gmail.com", "appPassword": "xxxx xxxx xxxx xxxx"}`
in `bridge/config.json` (or `CUBE_GMAIL_USER` / `CUBE_GMAIL_PASSWORD`; with the Mac helper, the
Mail fields in **Bridge settings…**) and a
number appears next to an envelope in the gap of the rings. The bridge
reads it over IMAP (`STATUS INBOX (UNSEEN)`, at most once a minute), so the
device never sees a credential. Create the app password at
<https://myaccount.google.com/apppasswords> (needs 2-step verification; some
Workspace admins disable them). If a fetch fails the last count stays on screen
shown in red; with no count yet it reads `--`. Leave the settings empty and
there is no mail badge.

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
go build -o cube-bridge ./cmd/cube-bridge && ./cube-bridge
```

Install Go 1.24 or newer first (`brew install go`); there is nothing else to
install, since the one module dependency (`golang.org/x/text`) is fetched by the
build. The result is a single binary. It prints the URL to put in the firmware config:

```
claude-status-cube bridge  source=local  refresh=5000ms
  preview   http://localhost:8787/
  device    http://192.168.1.50:8787/api/status
```

Open the **preview** URL in a browser: it renders the same payload as a
240x280 device mock, so you can build and tune the whole dashboard before the
hardware arrives. Click the screen or use the arrow keys to change cards.

The server answers at once and fills in the data in the background (the card
says `starting` until the first read lands). `GET /health` is always 200 while
it runs; read `ok` in the body (`ok:false` with `error` / `limitsError` when
degraded). It listens on every interface, which the cube needs to poll over
WiFi; set `"host": "127.0.0.1"` in `config.json` (or `CUBE_HOST`) to keep it off
the LAN when the cube only uses Bluetooth. Pinned to one LAN address, it also
listens on `127.0.0.1` for the Mac helper. A bad value in `config.json` or the
environment is ignored with a warning, and an environment variable wins over
the file both ways (`CUBE_EXTRA_CARDS=0` turns off `"extraCards": true`).

`cube-bridge` reads `config.json` from its working directory, so run it from `bridge/`
or pass `-config <path>`.

To keep it running without a terminal open, install it as a LaunchAgent:

> **Upgrading from the Node bridge:** run `./agent.sh install` (or `./agent.sh restart`) once
> after pulling. An installed LaunchAgent still runs `node .../bridge/server.mjs`, which no
> longer exists, so launchd would respawn-loop it at the next login and the cube would go dark.
> `./agent.sh status` warns when the agent is still the old one. The Mac app keeps its bundled
> Node copy until you run `mac-helper/install.sh install` again, which switches it to the binary.

```bash
./agent.sh install      # builds the binary; starts now, and at every login
./agent.sh status       # loaded? answering? prints /health (curl /api/status for the cards)
./agent.sh logs         # tail ~/Library/Logs/claude-status-cube/bridge.log
./agent.sh restart      # rebuild and reload, after editing the Go code
./agent.sh uninstall
```

An *agent* rather than a *daemon* on purpose: it runs as you, inside your GUI
login session, which is what lets it read the Claude Code token from your login
Keychain. A daemon starts before login, has no Keychain, and could not
authenticate. launchd restarts it if it crashes but leaves it alone if you stop
it deliberately (`SuccessfulExit=false`), and backs off 10s between respawns so
a bad config cannot spin the CPU.

The LaunchAgent runs the compiled `bridge/cube-bridge` by absolute path, so there is no
interpreter to find and launchd's missing shell profile does not matter. `./agent.sh restart`
rebuilds the binary first, and also switches an install left over from the Node bridge to it.

This keeps the cube alive as long as the Mac is awake. It still goes dark when
the Mac sleeps; for a display that is genuinely independent, run the bridge on
a Raspberry Pi or another always-on box instead.

For Admin API mode:

```bash
export ANTHROPIC_ADMIN_KEY=sk-ant-admin01-...
CUBE_SOURCE=admin ./cube-bridge
```

(or copy `config.example.json` to `config.json` and set it there).

### Claude agents (optional, off by default)

The bridge can also follow your running Claude Code sessions (which are working, which wait for you)
and put an AGENTS card in the payload. It is off until the firmware that draws it ships, and nothing
changes on the cube while it is off. To try the data:

```sh
CUBE_AGENTS=1 ./cube-bridge          # or "agents": true in config.json
```

The bridge learns about sessions from hooks. `./agent.sh install` and `mac-helper/install.sh install`
add the bridge's own entries to `~/.claude/settings.json` (and `uninstall` removes them); you can also run
`cube-bridge hooks install|uninstall [--port N] [--settings PATH]` yourself. Each entry runs
`cube-bridge hook`, which posts the event to the bridge on `127.0.0.1` and always exits 0, so it never slows
Claude down, even with the bridge off. Nothing else in the file is touched: a one-time backup is kept as
`settings.json.cube-bridge.bak`, and a file that is not valid JSON is left alone. Sessions are also
discovered with `claude agents --json` and tmux, and a prompt can be answered only for an agent in a tmux
pane. The prompt text is served only to requests from the Mac itself; the cube and anything else on the
LAN get the agent names and states. When the Mac app runs the bridge, its Bridge settings do not have
this switch yet. The agent tracking code is adapted from
[agents-deck](https://github.com/brushknight/agents-deck); see `bridge/internal/agents/NOTICE.md`.

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
4. From then on it reconnects by itself. The app also runs the bridge for you
   (the compiled `cube-bridge` from a copy of `bridge/` inside the app, restarted if it dies); if something
   already serves the port, such as a running `bridge/agent.sh` job, the app adopts that bridge
   instead of starting its own. `build-app.sh` builds the binary into the app, so Go 1.24+ is needed
   to install, and nothing but the app to run.

**`install.sh` replaces `bridge/agent.sh`.** BLE users run `mac-helper/install.sh` instead of
`bridge/agent.sh install`. If the `agent.sh` job is already installed, run
`bridge/agent.sh uninstall` first; otherwise both LaunchAgents fight for the same port.
**The bridge ships inside the app.** `build-app.sh` compiles `cube-bridge` (no `config.json`)
into `ClaudeCubeLink.app/Contents/Resources/bridge`, so the app runs the same way from the
LaunchAgent, Finder or `open`, wherever the repo is. A change to `bridge/` reaches the app only
after `./install.sh install` again.

**Bridge settings live in the app**, not in `bridge/config.json`: menu **Bridge settings…** edits
the usage source (local logs or Admin API, with its key or OAuth token), the spend/token cards,
the refresh interval, Gmail (address + app password) and whether the bridge serves the LAN (WiFi
cubes need it; off = `127.0.0.1` only). Apply saves and restarts the bridge, which gets them as the
bridge's environment variables (`CUBE_SOURCE`, `CUBE_EXTRA_CARDS`, `CUBE_REFRESH_MS`,
`CUBE_HOST`, `ANTHROPIC_ADMIN_*`, `CUBE_GMAIL_*`), which win over any `config.json`. The secrets are one Keychain item ("Claude Cube Link
bridge settings"); the rest is in the app's defaults (`bridgeSettings`). On its first launch after
`./install.sh install` the app imports `bridge/config.json` once, if it has no settings of its own
yet. Each reinstall re-signs the app, so macOS asks once to let the new build read that Keychain
item: choose **Always Allow**. A bridge the app adopted (say, a running `agent.sh` job) keeps its own
config; the window says so.
The app reads `CUBE_PORT` (default 8787), `CUBE_BRIDGE_DIR` (a directory holding `cube-bridge`) from
the environment, and takes `--port N` on its command line; `install.sh` writes the port
into the LaunchAgent, which restarts the app only after a crash (`KeepAlive` with
`SuccessfulExit=false`). Its other state is in the app's defaults
(`defaults read com.claude-cube.link`). Bridge order:
`CUBE_BRIDGE_DIR` (to run the repo's bridge while working on it), a `bridgeDir` default left by an
older install (`install.sh install` removes it), the bundled copy, then a `bridge/` next to the
working directory (`swift run`).
The app talks to the bridge on `127.0.0.1`; a bridge whose `host` is pinned to a LAN address
listens on `127.0.0.1` as well, so any `host` works.

**Menu bar.** The item shows the 5-hour percentage, tinted amber at 60% and red at 85%; the dropdown
repeats the cards and shows whether the bridge and the cube are up. **Send now** pushes the payload
at once, **Restart bridge** restarts the bridge the app runs, and **Show log** opens the log.
**Bridge settings…** edits the bridge's own settings (above), and **Unlock Mac with cube…** is
described [below](#unlocking-the-mac-from-the-cube).
**Cube settings…** edits what the setup portal
can; changing the WiFi network with the password left blank asks first, because the cube then joins
it as an open network and forgets the stored password. **Forget cube** drops the stored cube and
scans again, for when you replace it (also remove the old "Claude Cube" in System Settings >
Bluetooth). **Quit** also stops the bridge the app started.
For verbose diagnostics (BLE state changes, frames and ACKs, bridge health checks and child exits,
HTTP timings, push decisions) set `CUBE_TRACE=1`, pass `--trace`, or tick **Verbose trace** in the menu (until the app quits); lines tagged `[trace:...]`
go to stderr, i.e. `./install.sh logs`.

While a Mac is paired and data arrives over Bluetooth (a payload every 5 s) the cube turns WiFi off
(with no paired Mac, or with `WIFI_ALWAYS_ON 1`, WiFi stays on even when Bluetooth is live). If
nothing arrives for 15 s and WiFi is configured it polls the bridge instead, and goes back to
Bluetooth after it has been steady for 30 s. `WIFI_ALWAYS_ON 1` in `config.h` keeps WiFi up
(needed for OTA while Bluetooth is working). With no WiFi configured and the Mac away, the last
cards stay on screen and the freshness counter keeps counting; the setup portal does not open by
itself unless the cube has no paired Mac.

**Pairing again.** Hold the screen at boot, join the cube's `claude-cube-XXXX` network, press
**Forget Mac** (or **Forget iPhone**), then also remove "Claude Cube" in the device's Bluetooth
settings. If the cube's flash was erased the Mac keeps the old bond and the log says so
(`./install.sh logs`). The cube keeps one Mac and one iPhone; see
[Motorbike navigation](#motorbike-navigation-iphone) for pairing the phone.

The protocol is in `docs/ble-protocol.md`. The manual hardware checklist (nothing in it has been run
on a real cube yet) is `docs/ble-acceptance.md`.

### Changing WiFi without reflashing (setup portal)

The cube shows a setup screen when it cannot join the stored network within ~20 s and has no paired Mac (a cube with no WiFi and no Mac shows "Waiting for a Mac" instead, see Pairing over Bluetooth), or when you touch the screen while the boot screen says "HOLD SCREEN FOR SETUP" (the first 3 s after power-up) and keep holding for 5 s. Join the open `claude-cube-XXXX` network (scan the QR on the screen); the setup page opens by itself, or browse to `192.168.71.1`. Enter the WiFi name and password, the bridge URL and, optionally, an OTA password. A WiFi password must be 8 to 63 characters (or empty for an open network). A blank password keeps the saved one unless you change the network; a blank OTA password keeps the saved one too, and **Remove the OTA password** clears it. While nobody is on the setup network the cube keeps trying the stored network (a 10 s attempt every 30 s). Once it joins, the screen also shows the cube's address on that network, where the same page works from any computer on your LAN. If the setup screen came up because WiFi was down (not because you held the screen), the cube goes back to normal by itself the moment the stored network answers, so a router that was slow to come back after a power cut does not strand it. The setup network is open by design: while it is up (setup screen showing), anyone in radio range, and once it has joined, anyone on your LAN, can change the cube's settings, including the OTA password. (The page refuses requests that do not name the cube by address or that do not come from the form it served, so another web page cannot submit it for you.) Only reconfigure when you are at the cube, and set an OTA password. The page also has **Pair new device**, **Forget Mac**, **Forget iPhone** and, when a pairing was left half-done, **Forget unknown devices**.

### Updating over WiFi (OTA)

After the first USB flash the cube is reachable as `claude-cube.local`. In `firmware/platformio.ini` uncomment `upload_protocol = espota`, `upload_port = claude-cube.local` and (if you set an OTA password) `upload_flags = --auth=...`, then run `pio run -t upload` as usual. The screen shows a progress bar. OTA runs under WiFi modem sleep (Bluetooth is always on, and the ESP32 does not allow WiFi power-save off alongside it), so an upload may occasionally time out: just retry. Without an OTA password anyone on your network can flash the cube; set one in the portal.

### Volume card

While a Mac is paired, the deck ends with a Volume card: the output's name and its volume as a large number on the left, a slim bar on the right that fills from the bottom. **Drag** up or down anywhere on the card to raise or lower the volume by how far your finger moves; touching or tapping never changes it. **Tap the number** to mute or unmute (any volume change also unmutes). The row of buttons at the bottom is **previous**, **play/pause** and **next**: they act on whichever app is playing (as Control Center's buttons do; no permission needed), and the middle button shows ▶ while paused and ❙❙ while playing. Left and right swipes still change card; swipe down does not open the display panel on this card. Changes made on the Mac (keyboard, menu bar, switching to AirPods) show on the cube within about a third of a second. An output whose volume the Mac cannot set (HDMI, some USB devices) shows `FIXED`, and only mute works there; with the Mac's app not connected the card shows `NO MAC`. It needs the Mac app and firmware from the same release: an older app leaves the card on `NO MAC`, and an older cube has no Volume card. After a firmware update the cube tells the paired Mac its services changed (Service Changed, sent once per new firmware revision), and the Mac app rediscovers them on the next connection. If the card still stays on `NO MAC` while the top bar says `BLE`, remove the cube in System Settings > Bluetooth and pair again (macOS kept the old services).

### Unlocking the Mac from the cube

Turn on **Unlock Mac with cube…** in the menu. It asks for your login password once, checks it, and keeps it in the Mac's Keychain; macOS also asks you to allow Claude Cube Link under Privacy & Security > Accessibility, which it needs to type. From then on, when the Mac locks, the cube shows **Tap to unlock**. Tap it, and the Mac wakes the display and types the password into the lock screen. The cube only asks; the password never leaves the Mac. A swipe puts the prompt away to show the cards, and it comes back after 30 s without a touch.

It works on a locked session only. After a restart, the FileVault login comes before the app runs, so type the password there as usual. Anyone holding your paired cube can unlock the Mac while it is in Bluetooth range, so leave this off if the cube sits where others can reach it. `build-app.sh` signs with your Apple Development certificate when the keychain has one (a free Apple ID in Xcode > Settings > Accounts creates it), so macOS keeps the Accessibility permission and Keychain access across rebuilds. Without one the app is signed ad-hoc, and macOS forgets both after every rebuild; until you allow them again, the log says why a tap did nothing. Turning the item off deletes the stored password.

### Motorbike navigation (iPhone)

Mounted upright on the handlebar, the cube can show a heading-up map while you ride: your route as a
thick blue line, the streets around it in grey, you as a white arrow, the next turn and its distance
on top, and the street you turn onto with the time left at the bottom (Vietnamese accents kept). An
iPhone app, `ios-nav/`, does all the work and streams ready-to-draw frames to the cube over
Bluetooth; routes are motorbike routes for Vietnam (no expressways) from your own GraphHopper server,
`nav-server/`. The Mac is not involved: the cube keeps one Mac and one iPhone paired at the same
time, and the Mac's cards come back when the ride ends.

**Set up once.**
1. Server: `nav-server/README.md` (deploy, the key, the monthly map refresh).
2. Cube: flash this firmware (fw_rev 8 or later).
3. Phone: build and install the app from Xcode as `ios-nav/README.md` says (free Apple ID: rebuild
   once a week), then enter the server URL and key in the app's Settings.
4. Pair the phone. With a Mac already paired the cube refuses new devices, so first hold the
   screen at boot, open the setup page and press **Pair new device**: the cube restarts with pairing
   open for 2 minutes (the screen says PAIRING OPEN; a tap hides the message). Open the app; iOS
   asks for the 6-digit code the cube shows. A cube with nothing paired yet accepts the first device
   without this step.

**Riding.** Search a place, check the route, tap Start, allow location "Always", lock the phone and
go. Start downloads the route and the streets along it first, so losing mobile signal later costs
nothing but rerouting: off the route without signal the cube shows OFF ROUTE · NO SIGNAL with an
arrow back to the route, and the phone reroutes when the signal returns. During a ride the map owns
the screen: full brightness, no sleep, no auto-advance or auto-rotate, touch ignored, WiFi off (it
shares the radio with Bluetooth). If the phone goes quiet the cube shows NO PHONE after 3 s and
returns to its cards after 20 s. On arrival it shows Arrived for 8 s, then the cards come back
where you left them. The battery percent appears in the banner when it drops under 15%.

A cube paired only with the iPhone no longer joins WiFi at boot or opens the setup page by itself,
so it starts straight away on the road.

Nothing here has been tried on a real bike yet: `docs/nav-acceptance.md` is the checklist. In the
simulator, `CUBE_BLE=live CUBE_NAV=turn make run` shows the map (`CUBE_NAV_STOP_MS=5000` to watch
NO PHONE), and `./build/cube-shot build/shot @nav-turn` renders it (also `vn`, `long`, `reroute`,
`offroute`, `arrived`, `lost`, `busy`).

### Sound

The cube has a passive buzzer. For now it only beeps when you change its level; it is there for alerts to come. The level is OFF, LOW, MED (the default) or HIGH, and you can set it in three places:
- **On the cube:** swipe down on any card and use the SOUND row. A tap on its label switches it off or back on, and each change plays a beep at the new level.
- **From the Mac:** **Cube settings…** > Sound.
- **In the setup portal:** Sound, from 0 to 3.

The default is `BUZZER_LEVEL` in `config.h`. SOUND took the place of WiFi refresh in the on-device panel. WiFi refresh is still in the portal and on the Mac.

### On the cube

| Gesture | Where | What it does |
|---|---|---|
| Swipe left / right | any card | next / previous card |
| Swipe down | any card but Volume | display panel: BRIGHTNESS, SLEEP, ADVANCE, SOUND |
| Vertical drag, tap | Volume | set the level, mute, media keys |
| Tap | lock prompt | unlock the Mac |

**Display panel.** Each row has a `-` and `+`; SLEEP (screen off after N minutes), ADVANCE (auto-advance every N seconds) and SOUND also switch off and back on with a tap on their label. Brightness changes as you step it. DONE or a swipe up saves; RESET returns to the `config.h` defaults (`BACKLIGHT`, `SCREEN_SLEEP_MS`, `CARD_ROTATE_MS`, `BUZZER_LEVEL`). The same settings are in the portal and in the Mac's **Cube settings…**.

**Top bar.** Battery level on the left (measured on the board; a cube with no cell is on USB and shows 100%), the transport that delivered the data in the middle (`BLE`, `WIFI`, blank when offline), and the age of the data on the right (`4s`, `2m`, … `OLD`, or `OFF` when nothing has arrived).

**Screen sleep.** After the SLEEP time (default 15 min) with no touch and no new readings, the backlight and panel turn off. A touch wakes it and is otherwise ignored; new data alone does not wake it. The Mac's 5-second resend and the ticking countdown are not new readings, so a cube on a live Bluetooth link still sleeps. It never sleeps while a settings screen is open or a pairing screen is up.

**Auto-rotate.** Stand the cube on its other end and the display flips 180° after about a second (touch follows). Flat or tilted, it keeps its orientation. The sign of the accelerometer axis is not yet confirmed on hardware; `docs/imu-acceptance.md` is the checklist, and `IMU_UP_SIGN` in `board_pins.h` is the one value to flip if it turns the wrong way.

### Stand

`3d-printing/` has two parametric OpenSCAD desk stands that hold the board and a LiPo (a wedge and an easel), with STLs and a geometry test; see `3d-printing/README.md`.

## Layout

```
bridge/
  cmd/cube-bridge/    main: config, wiring, signals
  internal/server/    HTTP server + refresh loop
  internal/cards/     events → the card deck (edit this to redesign); golden tests in testdata/
  internal/pricing/   per-model rates and cache multipliers
  internal/limits/    rate-limit windows from the OAuth usage endpoint
  internal/mail/      unread count over IMAP (Gmail)
  internal/config/    config.json + environment variables
  internal/sources/   local.go: incremental ~/.claude transcript reader; admin.go: Usage & Cost Admin API client
  agent.sh            build + install/remove the macOS LaunchAgent
  preview.html        browser mock of the device (also embedded in the binary, assets.go)
mac-helper/           Swift app: runs the bridge, pushes /api/status to the cube over BLE
  install.sh          install/remove the LaunchAgent
  build-app.sh        builds ClaudeCubeLink.app with the bridge inside
  Sources/CubeLinkCore    pure logic (framing, push policy, menu model), unit-tested
  Sources/ClaudeCubeLink  the app: menu bar, settings windows, volume, unlock
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
  src/*.h             most logic is pure headers, tested on the host
  sim/                desktop simulator — runs the UI without hardware
  sim/shot.cpp        headless frame grab: one PNG per card
  sim/tests/          host-side unit tests (make test)
  sim/bench.cpp       performance/energy model (make bench)
docs/                 BLE protocol, hardware checklists, board reference
3d-printing/          OpenSCAD desk stands + STLs
```

## Testing without the board

Two ways, in increasing fidelity.

**Browser mock.** Start the bridge and open its address. `preview.html` renders
the same 240×280 layout and polls the same endpoint the device will. Good for
iterating on `internal/cards` — edit, rebuild, reload, see it. It is a JavaScript
reimplementation of the layout, so it tells you whether the *data* reads well,
not whether the firmware draws it correctly.

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
it somewhere other than the `BRIDGE_URL` in `config.h`. Other knobs:
`CUBE_BATTERY=30` (or `none`), `CUBE_ORIENT=2` (the cube is upside down),
`CUBE_BLE=live CUBE_VOLUME=40|muted|fixed` (Volume card),
and `CUBE_BLE=live CUBE_LOCKED=1` (unlock prompt).

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
./build/cube-shot build/shot @dev-edit     # a local screen: @portal, @ota, @waiting, @vol-56, @unlock, ...
```

The full list of `@` screens is in `CLAUDE.md`.

**Unit tests and the bench.** The firmware's logic (gestures,
transport policy, BLE framing, sleep, orientation, volume, unlock…) lives in
pure headers tested on the host, and the Mac app's in `CubeLinkCore`:

```sh
cd firmware/sim && make test     # host-side unit tests, no SDL or board
cd firmware/sim && make bench    # real setup()/loop() on a virtual clock; fails on a stale panel,
                                 # a screen left on, or a stale Settings read
cd mac-helper && swift test      # frame encoding, push policy, supervisor, shared BLE fixture
```

`firmware/sim/fixtures/ble-frames.txt` is read by both `make test` and
`swift test`, so the two ends of the BLE protocol cannot drift apart.

What it still cannot tell you: whether the pins in `board_pins.h` match your
revision, how the ST7789 renders these colours, and how the panel behaves at its
real refresh rate.

## Adding a card

Append one more `Card` to the deck in `bridge/internal/cards/cards.go` (`spendCards`
shows how). Without a `G`, you get the big-number layout:

```go
Card{T: "REQUESTS", V: itoa(dayT.count), S1: "today", S2: "", C: "blue"}
```

With a `G` — a whole percentage — you get a ring instead, and the other fields
change jobs: `G` fills the arc, `V` goes in the middle of it, `S1` captions
`V`, and `S2` labels the percentage in the gap at the bottom:

```go
Card{T: "CONTEXT", V: "38k", S1: "tokens left", S2: "of 200k", C: "green", G: intp(81)}
```

`G: -1` means *no reading* and draws the empty track. The threshold colours
are yours to pick through `c`; the notches at 60% and 85% are fixed, so a ring
whose meaning does not break there wants a different accent than green/amber/
red so it doesn't imply one.

Rebuild and restart the bridge, then reload the page. The firmware sizes the deck from the payload and the page
dots follow — nothing to change on the device. Accent names are `accent`,
`blue`, `green`, `amber`, `violet`, `red`.

Keep `v` short. A text card shrinks its own value to fit 220px. Ring cards
instead pick one size for the whole deck — the largest that fits the longest
value inside the ring's 130px middle — so the countdown does not change size
as you swipe. One long value therefore shrinks every ring, which is why
`until()` drops minutes once the horizon passes ten hours.

## Notes on the implementation

- **Incremental reads.** The local source reads every `.jsonl` under
  `~/.claude/projects` (subagent transcripts included), keeps a byte offset per
  file and only parses appended bytes, moving the offset only past complete
  lines, so a half-written line is read again next time. A file whose size and
  modified time have not changed is skipped, so a 5-second refresh over a busy
  `~/.claude` stays cheap. Events are de-duplicated on `requestId`, because
  the same request is often written to the log more than once.
- **Pricing.** `bridge/internal/pricing` matches model ids on the longest prefix,
  prices cache reads and writes per model, and prices fast mode and US-only
  inference (`inference_geo: "us"`, 1.1x) when the usage record says so.
- **Admin polling.** The Usage & Cost API documents sustained polling at once
  per minute; the bridge enforces that floor in `admin` mode regardless of
  what you configure.
- **Cost vs. usage endpoints.** In `admin` mode the usage report gives hourly
  granularity per model and the cost report gives authoritative dollars in
  daily buckets (UTC days, shown under the local date). Both only matter with
  `extraCards` on; the default gauges come from the OAuth usage endpoint instead.
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
| Top bar shows `OFF` | Bridge not reachable — check it's bound to the LAN address it printed, and that the host firewall allows the port |
| Costs look too low | An unrecognised model falls back to Sonnet-tier rates; add it to `bridge/internal/pricing` |
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

The IMU drives auto-rotate; the buzzer only beeps on a SOUND change so far, and the
PCF85063 RTC is still unused:

- Tilt or pick-up to wake, via the IMU
- An `Attention` tune (already defined in `melody.h`) when Claude is waiting for you
- Buzzer when the daily spend crosses a threshold
- Deep sleep between polls, with the RTC as the wake source, for battery use
- A "burn rate" card: tokens per minute over the last 10 minutes
