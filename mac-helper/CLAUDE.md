# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Scope: `mac-helper/`. The repo-root `CLAUDE.md` describes what the app does: it runs the bridge, shows the menu bar item and pushes payloads over BLE. The wire format is `docs/ble-protocol.md`. This file covers how the package is built, tested and wired together.

## Commands

```sh
swift test                                  # all tests (Swift Testing, free @Test functions)
swift test --filter encodesTheGoldenFrames  # one test, by function name
swift test --filter SettingsTests           # one file's tests, by file name
./build-app.sh                              # release build -> build/ClaudeCubeLink.app (adds Info.plist + icon, ad-hoc signed)
./install.sh install                        # rebuild, stop the running copy, reinstall to ~/Applications, reload the LaunchAgent
./install.sh logs                           # tail ~/Library/Logs/claude-cube-link.log
swift icon/make-icon.swift X.iconset && iconutil -c icns X.iconset -o icon/AppIcon.icns   # regenerate the committed icon
```

When you try a new build by hand, stop the installed copy first (`./install.sh uninstall`, or just run `./install.sh install`). The app takes a single-instance `flock` on `~/Library/Application Support/ClaudeCubeLink/lock`, and a second copy exits 0 without saying anything. For verbose `[trace:<tag>]` lines, set `CUBE_TRACE=1` or pass `--trace`.

Running unbundled (`swift run`) works for the bridge and the menu, but posts no notifications: `UNUserNotificationCenter` traps without a bundle, so `Notifier` turns itself off. For anything that depends on `Info.plist`, use `build-app.sh`. That covers `LSUIElement`, the Bluetooth usage string, and the bundle id that doubles as the defaults domain.

## Two targets

- **`CubeLinkCore`** (library, no AppKit) holds every decision, written as pure functions or value types. Inputs are injected: env dictionaries, `exists:` closures in place of the filesystem, `now` in place of the clock. The tests in `Tests/CubeLinkCoreTests` cover these:
  - `BridgeSupervisor.decide`, `findNode`, `findBridgeDir`, `announcement` and `shouldResetBackoff`
  - `PushPolicy`, `Backoff`, `encodeFrames`, `ControlMessage.parse`, `encodeVolume` and `foldVolumeName`
  - `CubeSettings` (`parse`, `patch`, `rebase`, `needsReboot`), `MenuModel` and `PomodoroNotice`
  - `validatePayloadBody`

  The stateful halves are not tested: `CubeLink` (CoreBluetooth) and the `Process` side of `BridgeSupervisor`. Put new logic in a testable static or struct, and have those classes call it.
- **`ClaudeCubeLink`** (executable) is AppKit glue. `main.swift` is a top-level script that builds every object and connects them through `onX` callback closures, so no component holds a reference to another. `StatusMenu` and `MenuRows` render a `MenuModel`, `SettingsWindow` edits a `CubeSettings`, and `Notifier` posts banners; `SystemVolume` reads and sets the default output through CoreAudio and reports changes (coalesced 30 ms), and `main.swift` writes them to the cube with `CubeLink.writeVolume` (also once on `onReady`) and applies `onVolumeRequest`.

Everything runs on the main queue. `CBCentralManager` is created with `queue: .main`, and `BridgeClient` and `Process` callbacks hop to main before touching state.

## Runtime flow

- **Tick.** A 5 s `Timer` runs in `.common` run-loop mode, which keeps it firing while the status menu is open. If it stopped, the cube would call the link stale after 15 s and fall back to WiFi. Each tick:
  1. `BridgeClient.fetch` reads `127.0.0.1:<port>/api/status`.
  2. The menu is updated with `MenuModel.make`.
  3. If the link is ready, `PushPolicy` decides whether to send (on change, every 5 s, or when forced), and `CubeLink.send` sends.

  The cube's Control `01` ("send now", sent on subscribe) and the menu's Send now both force a tick.
- **Payload bytes are passed through untouched.** `validatePayloadBody` only rejects an empty body, a body over 2048 bytes, or one that is not a JSON object. The helper never re-encodes the payload. `MenuModel` parses it separately for the menu: it reads `t`, `g`, `v`, `s1`, `s2`, `rows[{k,p,r}]` and `m`, and mirrors the 60/85 notches. A change to the payload contract in `bridge/cards.mjs` can therefore need a matching change in `MenuModel`.
- **`CubeLink` connection sequence.**
  1. Scan by service UUID, or reconnect by the identifier stored in defaults (`cubeIdentifier`).
  2. Connect and discover characteristics.
  3. Read Info. Info is encrypted, so this read is what makes macOS show the passkey prompt.
  4. Check `CubeProtocol.version`, then subscribe to Control.
  5. Mark the link ready and read Settings. The Settings characteristic is optional, since older firmware lacks it.

  Any failure during setup cancels the connection. The disconnect callback then reconnects with `Backoff` (1 s, doubling, capped at 30 s). `isCurrent` ignores late callbacks from a cube dropped by Forget cube.
- **Sending.** Each payload goes out as `[ver, seq, idx, total]` frames, written with response. Every write, retries included, gets a new `seq`, and `seq` resets to 0 on connect, matching the cube. If no ACK arrives within 2 s, the app retries once; after that, the next heartbeat tries again.
- **Bridge supervision.** `BridgeSupervisor` checks `/health`. If something already answers, it adopts that bridge and re-checks every 30 s. Otherwise it spawns `node server.mjs` with `cwd` set to the bridge directory and `CUBE_PORT` set. A child that dies is restarted with backoff, and the backoff resets after 60 s of uptime. Quit, `applicationWillTerminate` and the SIGTERM/SIGINT handlers all call `stop()`. Without that, an orphaned child would be adopted by the next launch, stale code and config included.

## Keeping the two sides in step

- `Protocol.swift` mirrors `firmware/src/ble_frame.h` and `docs/ble-protocol.md`: UUIDs, header size, 16 chunks, 2048-byte payload limit, Control opcodes `01` to `05`, and the optional Volume characteristic (fw_rev 5; a cube without it is logged once per connection). `FrameTests` reads `firmware/sim/fixtures/ble-frames.txt` through a path built from `#filePath` (four levels up), so that fixture must stay where it is. A wire change updates the fixture, the firmware, the Swift code and the doc together, and both `make test` (in `firmware/sim/`) and `swift test` must pass.
- `CubeSettings` mirrors the keys and ranges in `firmware/src/settings_json.cpp` (`bl sl rt pi sd pf ps pl pn ssid bridge pass otapass`). A read returns only booleans for the passwords (`wifiPass`, `otaPass`). `sound` (`sd`) is `Int?`: nil means the cube's read had none (fw_rev < 4), so `SettingsWindow` hides its row and `patch` never sends it.
  - `patch` sends only the keys that changed, so a stale window can't overwrite edits made on the cube.
  - `rebase` keeps fields the user is editing when a fresh read arrives.
  - Network keys make the cube reply `okReboot`, and the app does not read back after that reply.
  - A new setting has to be added on both sides, plus in `SettingsWindow`.

## Gotchas

- In the LaunchAgent, `KeepAlive` is `SuccessfulExit=false`, so launchd restarts the app only after a non-zero exit. Intentional exits must return 0: SIGTERM, and a second instance finding the lock held. Anything else makes launchd respawn the app in a loop.
- The app is an accessory app (`LSUIElement`, `.accessory`). It still sets a main menu with an Edit menu, because without one Cmd-C/V/X/A/Z do nothing in the settings window's text fields.
- The defaults domain is `com.claude-cube.link` (the bundle id). It holds `bridgeDir` and `nodePath` (written by `install.sh`), `cubeIdentifier`, and `pomodoroNotifications`. The lookup order for the bridge directory and node is: env, then defaults, then a search near the working directory or app bundle.
- The package uses Swift 6 tools with the Swift 5 language mode (`swiftLanguageModes: [.v5]`) and targets macOS 13.
- `log()` in `main.swift` is `Trace.info` and always writes. `Trace.log` writes only when tracing is on. Both use one serial queue, so lines stay in order. When stderr is not a regular file (a launch from Finder or `open`), `main.swift` redirects it to the log file.
