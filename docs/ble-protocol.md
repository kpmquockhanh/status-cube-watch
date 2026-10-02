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

## Liveness

The Mac sends a heartbeat every 5 s even if the payload did not change. The cube treats BLE as
live while a complete, valid payload arrived in the last 15 s; if none arrives for 15 s it starts
WiFi (if configured) and polls the bridge. If the bridge is down the Mac stops sending, so the
cube sees honest staleness.

## Pairing

Display-only IO capability, MITM, Secure Connections, bonding. A cube with no bond shows a random
6-digit passkey; macOS asks for it. One bond at a time: if a second device completes pairing while
a bond exists, the cube deletes the new bond and disconnects. To pair again, use "Forget paired
Mac" in the setup portal and also remove "Claude Cube" in the Mac's Bluetooth settings.
