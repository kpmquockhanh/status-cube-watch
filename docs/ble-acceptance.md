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
- [ ] Connection parameters: after `[ble] encrypted`, serial shows `[ble] conn params: interval 30 ms` or `45 ms`, `latency 6, timeout 4000 ms` (`BLE_IDLE_PARAMS` in `ble_conn.h`). If nothing follows, macOS kept the interval printed on the `[ble] connect` line. Note which one, and compare the idle draw against a build without the `updateConnParams` call. Settings writes and "Send now" from the menu still arrive within half a second, and a Mac reboot or walking out of range still drops the link within about 4 s.

## macOS app environment

- [ ] `RunLoop.main.run()` is enough for CoreBluetooth in the LaunchAgent-started `.app`; if no callbacks arrive, `NSApplication` is needed (the Task 1 spike was skipped, so this is unverified).
- [ ] The Bluetooth permission granted in `install.sh` applies to the LaunchAgent-started `.app`. If launchd-started instances are denied, fall back to a menu-bar app started at login.

## Pomodoro notification

- [ ] With the cube connected over BLE, let a focus phase end (set a 1 minute focus in the Pomodoro editor): a "Focus done" banner appears on the Mac as the cube alerts.
- [ ] The break ending gives "Break over / Time to focus"; the fourth focus gives "Take a long break".
- [ ] Unticking "Pomodoro notifications" in the menu silences it; the cube alert still happens.
- [ ] Mac asleep or out of range when the phase ends: nothing is queued and nothing crashes on either side.
- [ ] With notifications switched off for Claude Cube Link in System Settings, the menu item reads "(off in System Settings)" and is greyed out.
- [ ] Dismiss the first permission prompt without answering: the item stays enabled with no hint, and opening the menu asks again.

## Unlock with cube

- [ ] Menu > Unlock Mac with cube…: a wrong password is refused and asked again; the right one turns the item on, and macOS offers the Accessibility pane.
- [ ] Lock the Mac (Ctrl-Cmd-Q): the cube shows Tap to unlock within a second. A tap: UNLOCKING, then the Mac is unlocked and the cube is back on its cards.
- [ ] Lock, let the Mac's display sleep, then tap: the display wakes and the password still lands whole (no first characters lost).
- [ ] Without the Accessibility permission: a tap does nothing on the Mac, the log says why, and the cube asks again after 6 s.
- [ ] Two quick taps after the prompt returns: the password is typed once (5 s gap).
- [ ] Change the login password: a tap types nothing and the log says the stored password no longer works.
- [ ] Item off: locking the Mac shows no prompt on the cube, and the Keychain item `com.claude-cube.link` / `unlock` is gone.
- [ ] Swipe on the prompt: the cards show; after 30 s untouched the prompt returns. A Pomodoro phase end while locked shows the alert, not the prompt.
- [ ] Lock screens on recent macOS accept the synthetic keystrokes (note the macOS version tested).

## Volume card

- [ ] `AudioObjectGetPropertyData` reads `kAudioHardwareServiceDeviceProperty_VirtualMainVolume` on the built-in speakers: `CUBE_TRACE=1` shows `[trace:volume] mac N` matching the menu bar slider.
- [ ] Paired cube: the Volume card sits before the Pomodoro and shows the output name and level within a second of connecting.
- [ ] Drag up and down: the fill follows the finger with no lag, the Mac's volume follows within a few hundred ms, and the fill does not jump back after the lift.
- [ ] A touch or tap anywhere never changes the level; a drag moves it by the finger's travel. Previous / play-pause / next control the playing app; a tap on the number mutes, a second unmutes; a drag on a muted output unmutes it.
- [ ] Keyboard volume keys and mute on the Mac: the card follows within about 300 ms.
- [ ] Switch the output to AirPods and back: the name and level change to that device's.
- [ ] An HDMI or other fixed output: `FIXED`, full grey fill, drags do nothing, a tap on the number mutes if the device allows it.
- [ ] Turn the Mac's Bluetooth off mid-drag: the card shows `NO MAC` at once and nothing is sent after; back on, the state returns.
- [ ] Horizontal swipes on the card still change card; a swipe down does not open the display panel there.
- [ ] An older Mac app (before this change) with this firmware: the card stays `NO MAC`, nothing else changes.
- [ ] This Mac app with older firmware (fw_rev 4): the log says "cube has no Volume characteristic" once per connection, and nothing else changes.
- [ ] OTA a bonded fw_rev 4 cube to 5 without re-pairing: the Volume card fills (or the Mac log names the cached GATT table).
- [ ] A slightly wobbly tap on the number (10-15 px vertical drift) becomes a drag and moves the level instead of muting; note whether this happens in practice. Play/pause shows ❙❙ within a second of starting playback on the Mac and ▶ after pausing.
