# BLE transport: hardware acceptance checklist

Everything automated (host tests, simulator, `swift test`, `pio run`) passes, but none of the
Bluetooth path has run on a real cube and a real Mac. Work through this with the board and the Mac.
Tick an item only when you observed it. Logs: `./install.sh logs` (Mac, from `mac-helper/`) and
`pio device monitor` (cube).

## Setup

- [ ] If `bridge/agent.sh` is installed, run `bridge/agent.sh uninstall` first (both jobs would fight for the port).
- [ ] `cd mac-helper && ./install.sh install` from a terminal; the Bluetooth permission prompt appears and Enter continues.

## Pairing

- [ ] First pairing: forget the bond (portal "Forget paired Mac"), install, the code on the cube matches the Mac prompt, cards appear.
- [ ] Wrong passkey: type a wrong code. The cube stays unpaired and shows a fresh code on the next attempt. Note whether `onAuthenticationComplete` fires on a wrong passkey (the cube's pairing screen must not get stuck).
- [ ] Passkey display and subscribe: the code shows while pairing and clears afterwards; on subscribe the cube asks for "send now" and the first cards arrive without waiting for the 5 s heartbeat.
- [ ] Stranger-bond check: a second Mac or phone (nRF Connect) cannot pair while a bond exists; the cube log shows "second Mac refused". NimBLE's bond-count timing may make this check fire too early or late; if so, compare bond addresses or check on the first write / Info read instead.
- [ ] The app's cancel-on-Info-read-encryption-error path in `CubeLink` does not abort a macOS passkey prompt that is still pending.

## Reconnect and range

- [ ] Reconnect after a cube reboot, with no dialog.
- [ ] Reconnect after Mac sleep/wake, with no dialog.
- [ ] Out of range or Mac Bluetooth off: after about 15 s, with WiFi configured, the cube fills from WiFi; back in range it returns to BLE and WiFi drops after about 30 s.
- [ ] No WiFi configured and the Mac away: the last cards stay, the freshness counter ticks, no portal appears.

## Bridge

- [ ] Bridge down (`pkill -f server.mjs` while the app supervises it): the app restarts it. With the bridge held down the cube goes stale and falls back.
- [ ] External bridge: start `bridge/agent.sh install` first, then the app; the log says it adopted that bridge.

## Bond management

- [ ] Forget the bond through the portal (hold the screen at boot, "Forget paired Mac"); the cube advertises for pairing again.
- [ ] Stale macOS bond: erase flash (`pio run -t erase`), re-flash, run. The app's log shows the "remove Claude Cube in Bluetooth settings" message; after doing so, pairing works.

## Radio and power

- [ ] OTA with `WIFI_ALWAYS_ON 1` works. OTA runs under WiFi modem sleep (BLE forbids power-save NONE), so it may be slightly less reliable: retry espota if it times out. On the first hardware boot, watch serial for the coexistence abort message ("Should enable WiFi modem sleep when both WiFi and Bluetooth are enabled"); it must not appear. With 0 and BLE live, confirm whether OTA is unreachable (WiFi is off) and note it in the README.
- [ ] Free heap with BLE and WiFi both up: log `ESP.getFreeHeap()` once from `loop()` temporarily, note the number, remove the line.
- [ ] Idle draw, BLE-only versus WiFi-only (USB meter readings, if available).

## macOS app environment

- [ ] `RunLoop.main.run()` is enough for CoreBluetooth in the LaunchAgent-started `.app`; if no callbacks arrive, `NSApplication` is needed (the Task 1 spike was skipped, so this is unverified).
- [ ] The Bluetooth permission granted in `install.sh` applies to the LaunchAgent-started `.app`. If launchd-started instances are denied, fall back to a menu-bar app started at login.

## Pomodoro notification

- [ ] With the cube connected over BLE, let a focus phase end (set a 1 minute focus in the Pomodoro editor): a "Focus done" banner appears on the Mac as the cube alerts.
- [ ] The break ending gives "Break over / Time to focus"; the fourth focus gives "Take a long break".
- [ ] Unticking "Pomodoro notifications" in the menu silences it; the cube alert still happens.
- [ ] Mac asleep or out of range when the phase ends: nothing is queued and nothing crashes on either side.
- [ ] With notifications switched off for Claude Cube Link in System Settings, the menu item reads "(off in System Settings)" and is greyed out.
