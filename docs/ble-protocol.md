# BLE protocol (version 1)

The cube is a BLE peripheral advertising as **Claude Cube**; the Mac app is the central.
Everything here is pinned by `firmware/src/ble_frame.h` and `firmware/sim/fixtures/ble-frames.txt`
(read by both the C++ and the Swift tests).

## Service

| Name    | UUID                                   | Properties                              |
|---------|----------------------------------------|------------------------------------------|
| Service | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a01` | advertised                               |
| Payload | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a02` | write (encrypted + authenticated)        |
| Control | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a03` | notify                                   |
| Info    | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a04` | read (encrypted + authenticated)         |
| Settings| `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a05` | read + write (encrypted + authenticated) |
| Volume  | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a06` | write (encrypted + authenticated), fw_rev 5+ |

Info is 2 bytes: `[proto_ver, fw_rev]`. Reading it is what triggers pairing, so the Mac reads
Info first, checks `proto_ver == 1`, then subscribes to Control, then writes payloads.

## Payload frames

Each write: `[ver=1][seq][idx][total]` + JSON bytes. The JSON is the bridge's `/api/status`
response, unchanged. At most 16 chunks and 2048 payload bytes in total. Chunks are sized to the
negotiated write length (`maximumWriteValueLength(for: .withResponse)`, about 500).

- `seq` is chosen by the Mac, +1 per payload, wrapping 255 -> 0. **A retry uses a new seq.**
- `idx` runs 0..total-1 and must arrive in order. A repeated idx is ignored; a gap, an unknown
  `ver`, `total` 0 or > 16, or a payload over 2048 bytes drops the partial payload.
- A new `seq` discards a half-finished payload. A retransmit of the seq that just completed is
  ignored. Both sides reset this state on every new connection.
- The cube parses the reassembled JSON with the normal payload parser and swaps it in only if it
  parses, so a bad payload never blanks the screen.

## Control (cube -> Mac)

| Bytes        | Meaning |
|--------------|---------|
| `01`         | send now: sent when the Mac subscribes, so the screen fills at once |
| `02 <seq>`   | the payload with that seq was reassembled (not necessarily valid JSON) |
| `03 <result>`| a Settings write was handled: `00` saved, `01` rejected (nothing changed), `02` saved and the cube is rebooting |
| `04 <ended> <next>` | a Pomodoro phase just ended (added in fw_rev 3, protocol still 1). `<ended>` is the phase that finished and `<next>` the one a double tap would start: `00` focus, `01` short break, `02` long break. Fire and forget: no ack, and it is dropped if no Mac is subscribed |
| `05 <level> <muted>` | set the Mac's output to `<level>` (0..100) and mute it when `<muted>` is `01` (added in fw_rev 5, protocol still 1). An absolute target, so a lost or repeated message cannot drift the volume. Fire and forget: no ack, and it is dropped if no Mac is subscribed. A Mac that does not know `05` ignores it |

## Liveness

The Mac sends a heartbeat every 5 s even if the payload did not change. The cube treats BLE as
live while a complete, valid payload arrived in the last 15 s; if none arrives for 15 s it starts
WiFi (if configured) and polls the bridge. If the bridge is down the Mac stops sending, so the
cube sees honest staleness.

## Pairing

Display-only IO capability, MITM, Secure Connections, bonding. A cube with no bond shows a random
6-digit passkey; macOS asks for it. Only a passkey pairing counts: a link that ends up encrypted but
not authenticated (Just Works, from a central with no input) is dropped along with any bond it
stored. One bond at a time: while a bond exists, only that bond gets in; a second device that
completes pairing (bonding or not) is disconnected and its new bond deleted. A link that is not
secured within 60 s of connecting is dropped, so a central that connects and never pairs cannot
hold the cube's only connection slot (it stops advertising while connected). To pair again, use "Forget paired
Mac" in the setup portal and also remove "Claude Cube" in the Mac's Bluetooth settings.

## Settings (added in fw_rev 2, protocol still 1)

Optional: a Mac that does not find this characteristic simply has no settings UI. It carries what
the setup portal edits, as one JSON object of at most 512 bytes (a single ATT value, no framing).

| Key | Meaning | Range |
|-----|---------|-------|
| `bl` | backlight | 10..255 |
| `sl` | screen sleep, minutes (0 = never) | 0..240 |
| `rt` | auto-advance cards, seconds (0 = off) | 0..255 |
| `pi` | WiFi poll interval, seconds | 2..60 |
| `sd` | buzzer level: 0 off, 1 low, 2 medium, 3 high (added in fw_rev 4) | 0..3 |
| `pf` `ps` `pl` | Pomodoro focus / short / long break, minutes | 1..99 |
| `pn` | Pomodoro sessions before the long break | 1..9 |
| `ssid` `bridge` | WiFi network, bridge URL | portal rules |
| `pass` `otapass` | WiFi / OTA password (write only) | portal rules |

**Read** returns every key above except `pass` / `otapass`, plus `v` and booleans `wifiPass` /
`otaPass` (whether one is set). **Write** is a partial object: only the keys present change, and one
bad value rejects the whole write. A new `ssid` without a `pass` means an open network, as in the
portal. The cube answers with Control `03 <result>`; it saves on its main loop, and reboots (after
the reply) when a network key changed, since those are only read at boot. Only the groups the write
actually changes are stored (display, Pomodoro, then network last). If a later group fails to
store after an earlier one did, the result is `00` (Ok) rather than an error, so after a non-reboot
result the Mac reads Settings again to see what was stored. A rejected write is never what a read
returns: the cube republishes the stored values. Settings saved on the cube itself (the display
panel or the Pomodoro editor) are republished as the editor closes, so the next read returns them;
nothing is notified.

## Volume (added in fw_rev 5, protocol still 1)

The Mac writes its output state to the Volume characteristic, with response, at most 26 bytes:

    [ver=1][level][flags][name...]

- `level`: 0..100, or `ff` when the Mac has no output device.
- `flags`: bit0 muted, bit1 the volume can be set, bit2 mute can be set. Other bits are 0, and the
  cube ignores them.
- `name`: the output device name, printable ASCII (0x20..0x7E), 0..23 bytes, no terminator. The
  Mac folds it first: diacritics stripped, other non-ASCII dropped, cut to 23 bytes.

The cube drops the whole write when the value is shorter than 3 bytes, `ver` is not 1, `level` is
over 100 and not `ff`, or the name holds a byte outside 0x20..0x7E. A dropped write keeps the
previous state.

When the Mac writes:

- once, after it has subscribed to Control;
- then on every change to the volume, mute, name or default output device, coalesced over 30 ms.

The cube forgets the state when the link drops, and its Volume card then shows NO MAC. A cube
without the characteristic (fw_rev < 5) gets no writes, and the Mac logs that once per connection.

Latency: the idle connection parameters let the cube skip up to 6 connection events, so a write
can take about 300 ms to land. A drag on the cube goes the other way (Control `05`) and the fill
follows the finger locally, so this only delays how fast a change made on the Mac's keyboard
shows on the cube.

Golden encodings for both directions are the `vol_request` and `vol_state` lines in
`firmware/sim/fixtures/ble-frames.txt`.
