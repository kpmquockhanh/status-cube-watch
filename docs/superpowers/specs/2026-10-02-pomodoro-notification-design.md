# Pomodoro phase-end notification on the Mac

## Goal

When a Pomodoro phase ends on the cube, `ClaudeCubeLink.app` shows a macOS notification, so the
user notices at the Mac even when not looking at the cube.

## Decisions (agreed)

- Notify on **every phase end** (focus, short break, long break), each with its own text.
- The cube announces the event over BLE (approach A). The Mac does not mirror the timer.
- Works over BLE only. WiFi-only setups have no cube-to-Mac path and are out of scope.
- A mute checkbox lives in the **menu bar dropdown**, stored in the Mac's `UserDefaults`
  (default on). It is not a cube setting and not part of the Settings characteristic.
- The cube's own on-screen alert is unchanged.

## Protocol

New Control (cube -> Mac) message, additive; protocol version stays 1:

| Bytes | Meaning |
|-------|---------|
| `04 <ended> <next>` | a Pomodoro phase just ended. `<ended>`/`<next>`: `00` focus, `01` short break, `02` long break |

- Fire-and-forget Control notify, no ack, no retry. If no Mac is subscribed it is dropped.
- A Mac that does not know `04` ignores it (`ControlMessage.parse` returns nil).
- Documented in `docs/ble-protocol.md`; a frame is added to `firmware/sim/fixtures/ble-frames.txt`
  so `make test` and `swift test` both pin the encoding.

## Firmware

- `net_ble.cpp`: `bleNotifyPomodoro(uint8_t ended, uint8_t next)` notifies Control. No-op when
  there is no connected, subscribed Mac. Stubbed in `net_ble_off.cpp` and `sim/ble_sim.cpp`.
- `main.cpp`: inside the existing `if (pomo.takeAlert())` block, read `pomo.view()` (state DONE:
  `phase` = ended phase, `next` = what a long-press starts) and call `bleNotifyPomodoro`.
- The frame encoding lives in a pure helper next to `ble_frame.h` so it is host-tested.
- Bump `fw_rev` per the Settings precedent so the Mac can tell the event exists (informational;
  the Mac does not gate on it).

## Mac helper

- `Protocol.swift`: `ControlMessage.pomodoroEnded(ended: PomoPhase, next: PomoPhase)`. A frame
  shorter than 3 bytes or with a phase code above 2 parses to nil.
- `CubeLinkCore`: pure `PomodoroNotice` mapping (ended, next) to title/body:

  | Ended | Next | Title | Body |
  |-------|------|-------|------|
  | focus | short break | Focus done | Take a short break |
  | focus | long break | Focus done | Take a long break |
  | short/long break | focus | Break over | Time to focus |

  Any other combination falls back to title "Pomodoro", body "Phase finished".
- `CubeLink`: new `onPomodoroEnded` callback invoked from the Control handler (next to the
  existing `Trace.log("ble", "control: ...")`).
- `ClaudeCubeLink` target: a `Notifier` posts through `UNUserNotificationCenter`; it requests
  authorization once at launch. `StatusMenu.swift` wires `onPomodoroEnded` to it, gated by the
  mute flag.
- Menu: a checkable item "Pomodoro notifications" in the dropdown, backed by `UserDefaults`
  (default on). If authorization is denied the item shows as disabled with the hint
  "Notifications off in System Settings".
- No queueing: an event that arrives while the Mac is disconnected is lost.

## Testing

- Host: frame encoding fixture (`make test`), `ControlMessage.parse` cases including malformed
  frames, and the `PomodoroNotice` mapping (`swift test`).
- Hand: finish a phase on the cube with `POMO_FAST` firmware or a short duration set in the
  Pomodoro editor; confirm the notification, the mute checkbox, and the denied-permission state.
- Add an item to `docs/ble-acceptance.md` for the on-hardware check (the BLE path has not yet
  been run on a real cube).

## Out of scope

WiFi delivery, notifications for pause/start/reset, per-phase mute, a notification sound
setting, a cube-side toggle, and queueing missed events.

## Docs to update on implementation

`CLAUDE.md` (Transports paragraph), `docs/ble-protocol.md`, `docs/ble-acceptance.md`, `README.md`
if it lists the menu items.
