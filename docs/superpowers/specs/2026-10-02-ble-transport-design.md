# BLE-first transport with WiFi fallback

Date: 2026-10-02
Status: design, awaiting review

## Goal

Pair the cube with a Mac the way an Apple Watch pairs with a phone: pick it
once with a passkey, then it reconnects by itself. The Mac pushes the finished
payload to the cube over BLE (CoreBluetooth). When there is no live BLE link,
the cube falls back to polling the bridge over WiFi, as it does today.

Unchanged: the payload contract (`{v, ts, src, est, cards}`), `cards.mjs` as the
only place that formats, `payloadFromJson()` as the only parser, and the
bridge's HTTP endpoints (`/api/status`, `/`, `/health`).

## Decisions made

- BLE-only is a valid setup. A cube with no WiFi credentials boots and pairs
  over BLE. WiFi is an optional fallback added later through the portal.
- The Mac pushes (cube is the GATT server, Mac is the central).
- One Mac bonded at a time.
- One Mac app, `ClaudeCubeLink.app`, supervises the existing Node bridge as a
  child process. The bridge is not rewritten.
- Non-goals: several Macs, iOS or other hosts, Windows/Linux, sending touch or
  Pomodoro events back to the Mac.

## 1. BLE protocol and pairing

The cube advertises as "Claude Cube" with one custom service (128-bit UUIDs
fixed in `docs/ble-protocol.md`). Characteristics:

| Name    | Properties        | Purpose |
|---------|-------------------|---------|
| Payload | write, encrypted  | Payload chunks from the Mac. Refused until the link is bonded and encrypted. |
| Control | notify            | Cube to Mac: "send now" on connect, and an ACK carrying the `seq` of each completed payload. |
| Info    | read              | Protocol version and firmware version, so the Mac can refuse an incompatible cube. |

**Framing.** Each write is a 4-byte header `ver, seq, idx, total` followed by
JSON bytes sized to the negotiated MTU (about 500 bytes). A full 8-card payload
is about 1.3 KB, so 3 to 4 writes. The cube reassembles into a 2 KB buffer and
runs the result through `payloadFromJson()`. The payload replaces the displayed
one only if it parses, so a dropped or garbled frame never blanks the screen.
Limits: at most 16 chunks; a new `seq` discards a half-finished one; duplicate
`idx` values are ignored.

**Pairing.** NimBLE-Arduino with display-only IO capability, MITM, Secure
Connections and bonding.
1. A cube with no bond advertises in pairing mode. The screen shows
   "PAIR WITH MAC" and a fresh random 6-digit passkey.
2. The first encrypted access makes macOS show its standard code dialog. The
   user types the code from the cube.
3. Both sides store the bond (the cube's keys go to NVS). Later connections are
   silent.
4. A bonded cube accepts only its own Mac. To re-pair, the bond is forgotten
   through a new "forget Mac" action in the setup portal.

**Freshness.** The Mac sends a heartbeat every 5 s even when nothing changed.
BLE counts as live while a complete payload arrived in the last 15 s.

## 2. Firmware transport selection

New files:
- `firmware/src/net_ble.cpp/.h` (hardware-only): the GATT server, pairing,
  reassembly and ACKs. Exposes `bleBegin()`, `bleTake(Payload&)`,
  `bleLastGood()` and a state (`Advertising`, `Pairing` with passkey,
  `Connected`).
- `firmware/src/ble_frame.h` (pure): header parsing and the reassembler.
- `firmware/src/transport_policy.h` (pure): decides WiFi on or off and which
  source the freshness counter follows, from `now`, each source's last good time
  and whether WiFi is configured and joined.

Policy:
- BLE live: WiFi stays off.
- BLE stale for 15 s: bring WiFi up if credentials exist and poll the bridge
  every `POLL_INTERVAL_MS` exactly as today. BLE keeps advertising and
  accepting writes throughout.
- A fresh BLE payload makes BLE the source again; WiFi shuts down only after
  BLE has stayed live for 30 s, to avoid flapping the radio.
- No WiFi configured: BLE is the only source. A stale link shows "NO MAC" in the
  style of "NO BRIDGE" and keeps the last card.

`main.cpp`:
- The `netFetch()` block in `loop()` becomes one `transportPoll()` call.
  `lastGood` tracks whichever source delivered last. The online indicator means
  "any live source".
- Boot: having no SSID no longer forces the setup portal. With no bond and no
  SSID the cube shows the pairing screen. The 3 s boot-hold still opens the
  portal; its hint changes from "WIFI SETUP" to "SETUP".
- Behavior change: failing to join WiFi for about 20 s no longer opens the
  portal on its own, except when there is also no bond (then the cube has no
  other way to get data). Otherwise the portal is reached by the boot-hold.

Simulator: `firmware/sim/ble_sim.cpp` stands in for `net_ble.cpp`, driven by
`CUBE_BLE=live|stale|pair`, plus a `@ble-pair` screenshot target.

`platformio.ini` gains `h2zero/NimBLE-Arduino`.

## 3. The Mac app (`mac-helper/`)

A SwiftPM package that a build script wraps into `ClaudeCubeLink.app`
(`Info.plist` with `NSBluetoothAlwaysUsageDescription` and `LSUIElement`,
ad-hoc signed). `install.sh` mirrors `bridge/agent.sh`: it builds, copies the
app to `~/Applications`, writes a LaunchAgent with `KeepAlive`, and supports
`status|logs|restart|uninstall`. For BLE users it replaces `agent.sh`, which
stays for the WiFi-only setup.

Pieces:
- `BridgeSupervisor`: launches `node bridge/server.mjs` as a child process,
  restarts it if it exits (with backoff), locates `node`, and passes the config
  through the environment. The signed app is the parent, so the app owns the
  Bluetooth permission. The bridge keeps serving HTTP on the LAN, so WiFi
  fallback, the `/` preview and `/health` work unchanged.
- `BridgeClient`: fetches `http://127.0.0.1:<port>/api/status` every 5 s and
  passes the response bytes through unchanged, never re-encoding them.
- `CubeLink`: a `CBCentralManager` that scans for the service UUID, connects,
  discovers characteristics and subscribes to Control. It chunks to
  `maximumWriteValueLength(for: .withResponse)`, waits for the ACK of each
  `seq` and retries once. After the first pairing it reconnects by the stored
  peripheral identifier. On disconnect it backs off from 1 s to 30 s, and it
  recovers after sleep/wake and after Bluetooth is toggled.
- Run loop: send when the body changed or the 5 s heartbeat is due, and at once
  when the cube sends "send now".

Behaviors:
- If the bridge is down, the app stops heartbeats, so the cube sees stale data
  and falls back, instead of being fed old data that looks fresh.
- Pairing uses macOS's own dialog on the first encrypted write. The app has no
  UI and only logs "waiting for pairing, enter the code shown on the cube".
- A lock file keeps a second instance from running.
- Config: `--bridge` / `CUBE_PORT`, defaulting to the bridge's default (8787).

## 4. Testing, rollout and risks

Automated:
- `make test` in `firmware/sim/`: `ble_frame.h` (out-of-order and duplicate
  chunks, new `seq` discarding a partial payload, the 16-chunk cap, bad `total`)
  and `transport_policy.h` (15 s and 30 s thresholds, no flapping, no-WiFi mode,
  portal only when no bond).
- `docs/ble-protocol.md` plus the fixture `firmware/sim/fixtures/ble-frames.json`
  are read by both the C++ tests and the Swift tests, so the two ends cannot
  drift.
- Swift tests: the chunker at several MTUs, ACK and retry against a fake
  peripheral protocol.
- Simulator states and the `@ble-pair` screenshot cover the visual side.

Manual, on hardware: first pairing including a wrong passkey; reconnect after
cube reboot and after Mac sleep/wake; out of range, WiFi fallback, return to
BLE; bridge down and back; forgetting the bond; OTA still working with BLE on;
free heap with BLE and WiFi both up; battery drain BLE-only versus WiFi-only.

Rollout order (each step verifiable on its own):
1. Spike: Mac Bluetooth permission for a LaunchAgent-started `.app`, and a bare
   BLE round trip (throwaway firmware stub and throwaway Swift tool).
2. Protocol doc, `ble_frame.h` and its tests.
3. Firmware `net_ble`, the policy and the `main.cpp` wiring.
4. Simulator stand-ins and screenshots.
5. The full Mac app and `install.sh`.
6. README and `CLAUDE.md` updates.

Risks:
- Bluetooth permission for a LaunchAgent-started app. The spike settles it. If
  it fails, the fallback is a menu bar app started at login.
- Heap with WiFi and BLE both up. NimBLE is much lighter than Bluedroid; measure
  it, and PSRAM is available if needed.
- A stale bond after a flash erase: the erase wipes the cube's NVS but macOS
  keeps the old bond, so encryption fails. The app detects this and logs the fix
  (forget the device in Bluetooth settings); the pairing screen mentions it.
- New dependency: NimBLE-Arduino.
