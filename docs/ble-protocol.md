# BLE protocol (version 1)

The cube is a BLE peripheral advertising as **Claude Cube**; the Mac app and the iPhone app are
centrals, bonded and connected at the same time (see "Pairing"). Everything here is pinned by
`firmware/src/ble_frame.h` and `firmware/sim/fixtures/ble-frames.txt` (read by both the C++ and the
Swift tests); the navigation frames by `firmware/src/nav_frame.h` and
`firmware/sim/fixtures/nav-frames.txt`.

## Service

| Name    | UUID                                   | Properties                              |
|---------|----------------------------------------|------------------------------------------|
| Service | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a01` | advertised                               |
| Payload | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a02` | write (encrypted + authenticated)        |
| Control | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a03` | notify                                   |
| Info    | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a04` | read (encrypted + authenticated)         |
| Settings| `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a05` | read + write (encrypted + authenticated) |
| Volume  | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a06` | write (encrypted + authenticated), fw_rev 5+ |
| Nav     | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a07` | write + write without response (encrypted + authenticated), fw_rev 8+ |

Info is 2 bytes: `[proto_ver, fw_rev]` (current firmware: `[1, 8]`). Reading it is what triggers pairing, so the Mac reads
Info first, checks `proto_ver == 1`, then subscribes to Control, then writes payloads.

A bonded Mac caches this table. When the cube boots a `fw_rev` other than the one it last
advertised (NVS namespace `ble`, key `gr`), it queues a Service Changed indication for the whole
handle range; NimBLE delivers it to each bonded Mac once that Mac reconnects and restores
encryption. The Mac then drops its cache (`didModifyServices`) and runs the sequence above again.
So bump `fw_rev` whenever the table changes.

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
| `04` | retired: it was the Pomodoro phase-end notice (fw_rev 3 to 7), and the Pomodoro is gone. Current firmware never sends it and the Mac parses it as unknown. Do not reuse it |
| `05 <level> <muted>` | set the Mac's output to `<level>` (0..100) and mute it when `<muted>` is `01` (added in fw_rev 5, protocol still 1). An absolute target, so a lost or repeated message cannot drift the volume. Fire and forget: no ack, and it is dropped if no Mac is subscribed. A Mac that does not know `05` ignores it |
| `06 <key>` | press a media key on the Mac: `00` play/pause, `01` next, `02` previous (added in fw_rev 6, protocol still 1). The Volume card's buttons send it. Fire and forget: no ack, and it is dropped if no Mac is subscribed. A Mac that does not know `06` ignores it |
| `07` | unlock the Mac's screen: a tap on the cube's unlock prompt (added in fw_rev 7, protocol still 1). The Mac types the stored login password only while its screen is locked, "Unlock Mac with cube" is on and it holds the Accessibility permission, and at most once per 5 s; otherwise it logs why and does nothing. Fire and forget: no ack, and it is dropped if no Mac is subscribed. A Mac that does not know `07` ignores it |

## Liveness

The Mac sends a heartbeat every 5 s even if the payload did not change. The cube treats BLE as
live while a complete, valid payload arrived in the last 15 s; if none arrives for 15 s it starts
WiFi (if configured) and polls the bridge. If the bridge is down the Mac stops sending, so the
cube sees honest staleness.

## Pairing

Display-only IO capability, MITM, Secure Connections, bonding. A device pairing shows a random
6-digit passkey on the cube; macOS or iOS asks for it. Only a passkey pairing counts: a link that
ends up encrypted but not authenticated (Just Works, from a central with no input) is dropped along
with any bond it stored. The policy is `firmware/src/ble_bonds.h` (pure, `ble_bonds_test`).

**Two bonds, two links.** The cube keeps one Mac and one iPhone bonded and connected at once. It
keeps advertising while fewer than two links are up (three during the pairing window, so a
replacement can pair while the old device is still connected), and drops a link that is not
secured within 60 s of connecting, so a central that connects and never pairs cannot hold a slot.

**Who may pair.** With no bond, the first device pairs freely. With a bond stored, a stored bond
re-encrypting always gets in, and a new pairing gets in only during the **pairing window**: the
120 s after a reboot from "Pair new device" in the setup portal (NVS namespace `ble`, key `pw`,
cleared at that boot). Outside the window, a device that completes pairing is disconnected and the
bond it just stored is deleted; the bonds already there are untouched. The window closes early once
one new bond beside the existing ones is accepted.

**Kinds.** A bond's kind is learned from the first write on its secured link: Nav means an iPhone;
Payload, Settings or Volume mean a Mac. It is stored in NVS namespace `ble` under the identity
address as 12 lowercase hex digits (most significant byte first), value `m` or `p` (one byte). A
bond left by firmware before fw_rev 8 (which bonded only a Mac) is taken as the Mac. Once a kind is
learned, an older bond of the same kind is deleted and its link dropped: pairing a new iPhone
replaces the old one, and likewise for a Mac. A device accepted as a new bond whose link drops before its
first write would stay of unknown kind for good, so its bond is deleted when that link goes.

**Full bond store.** NimBLE keeps at most 3 bonds. When a write does not fit, NimBLE's default
would delete the oldest bond, often the Mac. The cube instead deletes a bond of unknown kind (one
with no link up first, then the oldest) and otherwise refuses the write, so the new pairing does
not bond; a known Mac or iPhone bond is never removed to make room. The Volume card and the WiFi transport policy follow
the Mac's bond only; the "Waiting for a Mac" screen at first boot follows any bond.

**Links.** Once secured, the iPhone's link asks for 15-30 ms, latency 0, timeout 4 s
(`BLE_NAV_PARAMS`), and the Mac's for 30-45 ms, latency 6, timeout 4 s (`BLE_IDLE_PARAMS`), both in
`firmware/src/ble_conn.h` and checked against Apple's rules. Control notifications go to every
subscribed central. Liveness (`bleLastGood`) counts only Payload writes.

**Forgetting.** The setup portal has "Forget Mac", "Forget iPhone" and "Forget unknown devices" (each
shown only while such a bond exists; each deletes only bonds of that kind; the cube reboots). Also remove "Claude Cube" in that device's Bluetooth settings
before pairing it again.

## Nav (added in fw_rev 8, protocol still 1)

The iPhone streams ready-to-draw map frames to the Nav characteristic (about one every 200 ms,
written without response). There is no ack: the newest frame wins. The cube reassembles and decodes
each frame on the NimBLE task and hands the latest valid one to the main loop; a frame that fails
to decode is dropped and the last good one stays.

Chunks: each write is `[ver=1][seq][idx][total]` + body bytes, same rules as Payload (`docs/ble-protocol.md`) except `total ≤ 32` and the reassembled frame `≤ 8192` bytes. A new `seq` replaces a half-finished frame (latest wins).

Reassembled frame, little-endian:

| Field | Type | Notes |
|---|---|---|
| version | u8 | `1` |
| state | u8 | `NavState`: 0 Ended, 1 Navigating, 2 Rerouting, 3 OffRouteNoSignal, 4 Arrived |
| turn | u8 | `NavTurn`: 0 Straight, 1 SlightLeft, 2 Left, 3 SharpLeft, 4 SlightRight, 5 Right, 6 SharpRight, 7 UTurn, 8 KeepLeft, 9 KeepRight, 10 Roundabout, 11 Arrive, 12 Rejoin |
| exit | u8 | roundabout exit number, 0 = none |
| dir | u8 | Rejoin arrow, 0..255 = 0..360° clockwise from screen-up (only meaningful for Rejoin) |
| flags | u8 | bit0 = GPS weak (rider arrow drawn grey) |
| dist | u8 len + UTF-8 | ≤ 15 bytes, e.g. `150 m` |
| street | u8 len + UTF-8 | ≤ 63 bytes |
| eta | u8 len + UTF-8 | ≤ 23 bytes, e.g. `12 min` |
| nRoute | u16 | ≤ 256, then nRoute × (i16 x, i16 y) screen px |
| nStreets | u8 | ≤ 96, then each: u8 cls (0–3, as the server's tiles), u8 nPts (≥ 2), nPts × (i16 x, i16 y); total street points ≤ 640 |

Coordinates are screen pixels of the 240×280 frame, already clipped by the phone to x ∈ [−64, 304], y ∈ [−64, 344]. Any length over its limit, a truncated body, or trailing bytes rejects the frame (the last good frame stays).

Golden frames (pinned by `firmware/sim/fixtures/nav-frames.txt`, one per line, `nav <name> <hex>`; `nav_frame_test` decodes them):

```
# turn: Navigating, Right, "150 m", "Lê Lợi", "12 min", route (120,200)(120,90)(200,80), one cls-2 street (0,150)(240,160)
nav turn 01 01 05 00 00 00 05 31 35 30 20 6d 09 4c c3 aa 20 4c e1 bb a3 69 06 31 32 20 6d 69 6e 03 00 78 00 c8 00 78 00 5a 00 c8 00 50 00 01 02 02 00 00 96 00 f0 00 a0 00
# arrived: Arrived, Arrive, "", "Bến Thành", "", no route, no streets
nav arrived 01 04 0b 00 00 00 00 0c 42 e1 ba bf 6e 20 54 68 c3 a0 6e 68 00 00 00 00
# offroute: OffRouteNoSignal, Rejoin, dir 64 (90° right), GPS weak, "80 m", route (-64,304)(300,-64)
nav offroute 01 03 0c 00 40 01 04 38 30 20 6d 00 00 02 00 c0 ff 30 01 2c 01 c0 ff 00
```

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
| `ssid` `bridge` | WiFi network, bridge URL | portal rules |
| `pass` `otapass` | WiFi / OTA password (write only) | portal rules |

**Read** returns every key above except `pass` / `otapass`, plus `v` and booleans `wifiPass` /
`otaPass` (whether one is set). **Write** is a partial object: only the keys present change, and one
bad value rejects the whole write. Keys the cube does not know are ignored, so the retired Pomodoro
keys `pf` `ps` `pl` `pn` that an older Mac may still send change nothing. A new `ssid` without a `pass` means an open network, as in the
portal. The cube answers with Control `03 <result>`; it saves on its main loop, and reboots (after
the reply) when a network key changed, since those are only read at boot. Only the groups the write
actually changes are stored (display, then network last). If a later group fails to
store after an earlier one did, the result is `00` (Ok) rather than an error, so after a non-reboot
result the Mac reads Settings again to see what was stored. A rejected write is never what a read
returns: the cube republishes the stored values. Settings saved on the cube itself (the display
panel) are republished as the panel closes, so the next read returns them;
nothing is notified.

## Volume (added in fw_rev 5, protocol still 1)

The Mac writes its output state to the Volume characteristic, with response, at most 26 bytes:

    [ver=1][level][flags][name...]

- `level`: 0..100, or `ff` when the Mac has no output device.
- `flags`: bit0 muted, bit1 the volume can be set, bit2 mute can be set, bit3 the Now Playing state
  is known, bit4 something is playing (bits 3-4 from fw_rev 6: the play/pause button shows play or
  pause; with bit3 clear it shows both), bit5 the Mac's screen is locked and it will unlock for
  Control `07` (fw_rev 7, see "Unlock" below). Other bits are 0, and the cube ignores them, so older
  firmware ignores the bits it does not know. The Mac reads the state through an `osascript` child, because macOS
  answers MediaRemote's now-playing queries only for Apple-signed processes, and rewrites the
  frame whenever it changes.
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

## Unlock (added in fw_rev 7, protocol still 1)

The Mac sets Volume flag bit5 while its screen is locked and the user turned on "Unlock Mac with
cube" (the menu item stores the login password in the Mac's Keychain after checking it with
OpenDirectory). It rewrites the Volume frame when either changes. The cube covers its deck with a
"Tap to unlock" screen while the bit is set; a tap sends Control `07` and the screen says UNLOCKING
until the bit clears, or for 6 s, then asks again. A swipe shows the deck; the prompt returns after
30 s without a touch, or on the next lock.

The password never crosses the link: `07` carries nothing, and the Mac decides on its own state
(locked, feature on, Accessibility granted, 5 s since the last attempt) before it wakes the display
and types the password and Return as keyboard events. It checks the stored password again first, so
one changed since it was stored is never typed. The link is the bonded, passkey-paired one, so only
the paired cube can ask; anyone holding that cube can, which is the trade the user opts into. It
cannot help at the FileVault login after a restart, since the app is not running yet.

The golden encoding is the `unlock` line in `firmware/sim/fixtures/ble-frames.txt`.
