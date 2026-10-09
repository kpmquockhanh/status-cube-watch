# Mac volume card

## Goal

A card on the cube that shows and changes the Mac's output volume: a full-width vertical slider in
the style of the Android "flex" volume control. Drag it, tap to jump, tap the speaker to mute. The
cube follows the Mac too, so a change made with the keyboard shows up on the cube.

## Decisions (agreed)

- A **deck card**, not a panel. Deck order: payload cards, **Volume**, Pomodoro (still last). The
  card exists while a Mac is bonded (`bleBonded()`); with no bond the deck is unchanged.
- Controls: **volume and mute** only. No media keys.
- Transport: **BLE only**, approach A: a new Control message (cube -> Mac) carries the request, and a
  new **Volume** characteristic (Mac -> cube) carries the Mac's state, including the **output device
  name**. The payload contract, the bridge and `preview.html` are untouched.
- Look: one rounded pill across the card, filling from the bottom (reference: Android flex volume
  UI, made full width).
- On this card a vertical drag is the volume, so **swipe down does not open the display panel
  here**. It still opens from every other card.
- No new menu item on the Mac.

## The card

```
  BLE   78%            4s .
 +------------------------+
 |  MACBOOK PRO SPEAKERS  |   output name, small caps
 |          56            |   level
 |########################|   fill edge = level (flat)
 |########################|
 |#########  spk  ########|   speaker glyph
 +------------------------+
           . . o .
```

Layout (240x280 panel; exact numbers are tuned against `make shot`):

- The top bar is the shared one (battery, link marker, age), and the page dots sit below as usual.
- The pill runs x 14..226 and y 42..252, with corners of about 40 px. Its track is dark (the ring
  track colour family).
- The fill is the same anti-aliased rounded rect, clipped to `y >= levelY`, so the corners stay
  smooth at any level and the top edge is flat. The fill colour is a soft lavender (a new accent
  constant).
- The output name is drawn in small caps near the top of the pill, and the level number large below
  it. Both are drawn twice through the fill clip: ink above the fill edge, dark where the fill
  covers them, so the text stays readable at any level.
- The speaker glyph is centred near the bottom of the pill with 0..3 waves by level (0, 1..33,
  34..66, 67..100). When muted it is slashed and drawn with no waves.
- No glow or texture: the panel is RGB565 and the frame budget is better spent elsewhere.

States:

| State | Fill | Number | Caption / name | Touch |
|-------|------|--------|----------------|-------|
| Live | lavender, at level | level | output name | drag, tap-jump, speaker = mute |
| Muted | crossfades to grey (`POMO_MUTED`), keeps its height | level | output name, glyph slashed, the word `MUTED` under the number | as live; any level change also unmutes |
| Fixed volume (`canSet` false) | full, dimmed grey | `--` | output name, `FIXED` under the number | speaker = mute if `canMute`, else nothing |
| No Mac (link down, or no Volume write yet) | empty track | `--` | `MAC VOLUME`, `NO MAC` under the number | nothing |

The state never relies on colour alone: `MUTED`, `FIXED` and `NO MAC` are spelled out.

Motion:

- A change from the Mac, or a tap-jump, eases the fill to the new level with the existing retarget
  timing (`RETARGET_MS`, 260 ms, ease-out).
- While a finger drags, the fill tracks the finger directly, with no easing.
- Mute and unmute crossfade the fill colour over `COLOR_MS` (400 ms).
- The first view of the card sweeps the fill in from empty, as rings do (`uiReplay`).

Interaction:

- Drag up or down anywhere on the pill: the level is the finger's height mapped linearly over the
  pill's inner height, clamped to 0..100.
- Tap on the pill, outside the speaker zone: the level jumps to that height.
- Tap in the speaker zone (about 60x50 px around the glyph): toggle mute.
- Swipe left or right: change card. Axis lock decides between the two: once the finger has moved
  10 px from where it landed, a mostly vertical move becomes a drag and a mostly horizontal one
  stays a swipe. A drag never turns into a swipe and vice versa.
- Swipe up and swipe down mean nothing on this card.
- Taps here are plain taps (multi-tap is off), delivered at once.
- Touch is ignored in the No Mac state. In the Fixed state only the speaker zone acts.

Other behaviour:

- Local-only, like the Pomodoro card: not in the payload contract, not counted against
  `MAX_CARDS`, not in `preview.html`.
- Volume writes from the Mac are not readings. They do not change `readingsKey()`, so they do not
  keep the screen awake or wake it.
- Auto-advance includes the card like any other. A touch on it restarts the auto-advance timer, as
  a swipe does.
- When the deck changes size, a viewer on the Volume card stays on it (`deck_util.h`), as a viewer
  on the Pomodoro card does.

## Protocol

Additive; protocol version stays 1. `fw_rev` goes 4 -> 5.

### Control (cube -> Mac)

| Bytes | Meaning |
|-------|---------|
| `05 <level> <muted>` | set the Mac's output to `<level>` (0..100) and mute `<muted>` (0/1). |

- The request is an absolute target, so a lost or repeated message cannot drift the volume.
- Fire and forget: no ack, and it is dropped when no Mac is subscribed.
- A Mac that does not know `05` ignores it.

### Volume characteristic (Mac -> cube)

UUID `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a06`, write (encrypted + authenticated), at most 26 bytes:

```
[ver=1][level][flags][name...]
```

- `level`: 0..100, or `FF` when there is no output device.
- `flags`: bit0 muted, bit1 volume settable, bit2 mute settable. Other bits are 0, and the cube
  ignores them.
- `name`: the output device name, printable ASCII (0x20..0x7E), 0..23 bytes, no terminator.

Validation:

- The cube drops the whole write when the value is shorter than 3 bytes, `ver != 1`, `level` is
  over 100 and not `FF`, or the name holds a byte outside 0x20..0x7E.
- A dropped write keeps the previous state.

When the Mac writes:

- once after it has subscribed to Control;
- then on every coalesced CoreAudio change (volume, mute, name, default output device).

Lifetime:

- The cube forgets the state when the link drops, and the card falls back to No Mac.
- A cube without the characteristic (fw_rev < 5) gets no writes, and the Mac logs it once.

Both encodings get golden lines in `firmware/sim/fixtures/ble-frames.txt`, read by `make test` and
`swift test`. Documented in `docs/ble-protocol.md`.

Latency note: the idle connection parameters let the cube skip up to 6 connection events, so a
Mac -> cube write can take about 300 ms to land. Drags go cube -> Mac and the fill follows the
finger locally, so this only delays how fast a *keyboard* change shows on the cube.

## Firmware

New pure units, each with a host test in `sim/tests/` (no Arduino, caller passes `now`, gnu++11
safe):

- **`volume_frame.h`**
  - `MacVolume { bool known; uint8_t level; bool muted, canSet, canMute; char name[24]; }`.
  - `bool volumeParse(const uint8_t *d, size_t n, MacVolume &out)` applies the validation above.
  - `void volumeRequestEncode(uint8_t level, bool muted, uint8_t out[3])`.
- **`volume_slider.h`**
  - The geometry: pill rect, inner travel, speaker zone. `ui.cpp` draws from the same constants,
    so drawing and hit-testing cannot drift (the `pomo_editor` pattern).
  - `uint8_t volumeLevelAt(int16_t y)`.
  - A `VolumeSlider` model:
    - holds the shown level and muted flag;
    - `grab(now)` / `drag(y, now)` / `release(now)` / `tap(x, y, now)`;
    - `fromMac(const MacVolume&, now)`, which is ignored while held and for `VOLUME_HOLD_MS`
      (600 ms) after release, so echoes of older levels cannot jerk the fill;
    - `bool takeSend(now, level&, muted&)`, which yields a request at most every `VOLUME_SEND_MS`
      (50 ms) while dragging, always the final value on release, and once per tap.

Changed units:

- **`gesture.h/.cpp`**: a `dragMode` flag next to `multiTap`, set by `main.cpp` only on the Volume
  card.
  - New gestures `DragStart`, `Drag`, `DragEnd`. `Drag` comes back while the finger moves, with
    `lastY()`.
  - Once travel reaches `DRAG_LOCK_PX` (10), a mostly vertical move locks to a drag and a mostly
    horizontal one is classified on lift as today.
  - Taps are unchanged. With `dragMode` off, behaviour is identical to today (the existing tests
    must still pass).
- **`ble.h`, `net_ble.cpp`**
  - The Volume characteristic; its callback copies the value out under the same pattern as
    Settings.
  - `bool bleTakeVolume(MacVolume &out)` returns true when the state changed, including the change
    to "unknown" when the link drops.
  - `void bleSendVolume(uint8_t level, bool muted)` notifies Control `05`.
  - `BLE_FW_REV` becomes 5.
  - Stand-ins in `sim/ble_sim.cpp` (driven by `CUBE_VOLUME=56|muted|fixed|none`, default `none`)
    and `sim/bench.cpp`.
- **`ui.h/.cpp`**
  - `uiVolumeIndex(p)`. `uiDeckSize` / `uiPomodoroIndex` account for the card when it is present,
    and `uiRender` takes whether it is present.
  - `drawVolumeCard(...)` draws the pill, the clipped fill, the two-tone text and the glyph.
  - A `GaugeAnim`-style animation holds the fill level and colour.
  - `uiReplay` covers the card.
- **`deck_util.h`**: `deckKeepIndex` keeps a viewer on either local card.
- **`main.cpp`**
  - calls `bleTakeVolume` every pass and feeds the slider;
  - on the Volume card passes `dragMode`, routes `DragStart` / `Drag` / `DragEnd` / `Tap` to the
    slider and skips `SwipeDown`;
  - calls `bleSendVolume` from `takeSend`;
  - sets `dirty` on any slider change;
  - treats a drag as animating, so the CPU stays at 240 MHz and frames are paced to `FRAME_MS`.
- **`sim/shot.cpp`**: `@vol-56`, `@vol-muted`, `@vol-fixed`, `@vol-none`, `@vol-full`,
  `@vol-zero`.
- **`sim/bench.cpp`**: a `volume` scenario (one drag across the pill, then a tap on the speaker).

## Mac helper

- **`CubeLinkCore/Volume.swift`** (pure, tested)
  - `MacVolume`.
  - `encodeVolume(_ v: MacVolume) -> Data`. The name is folded to ASCII: diacritics stripped,
    other non-ASCII dropped, cut to 23 bytes.
  - `ControlMessage.volumeRequest(level:muted:)` in `Protocol.swift`. A frame under 3 bytes, a
    level over 100 or a muted byte over 1 parses to nil.
  - `CubeProtocol.volumeUUID`.
- **`ClaudeCubeLink/SystemVolume.swift`** (CoreAudio; no permission prompt needed)
  - Reads the default output device, `kAudioHardwareServiceDeviceProperty_VirtualMainVolume`,
    `kAudioDevicePropertyMute`, `kAudioObjectPropertyName`, and whether each is settable. With no
    output device, `level` is `FF`.
  - Listens with `AudioObjectAddPropertyListenerBlock` to the system default-output change and to
    volume, mute and name on the current device. The device listeners move when the default
    output changes.
  - Coalesces callbacks into one `onChange(MacVolume)` about 30 ms after the last.
  - `apply(level:muted:)` sets mute, then the volume scalar (level / 100). A request with
    `muted = 0` unmutes. A part the device cannot set is skipped.
- **`CubeLink`**
  - Discovers the Volume characteristic optionally (like Settings).
  - `writeVolume(Data)` writes it with response.
  - The Control handler calls `onVolumeRequest(level, muted)`.
  - Once subscribed, the link asks for the current state so it is written at once.
- **Wiring (`main.swift` / `StatusMenu.swift`)**: `SystemVolume.onChange` -> `CubeLink.writeVolume`
  (only while ready), and `CubeLink.onVolumeRequest` -> `SystemVolume.apply`. Changes are logged
  through `Trace`.

## Testing

- `make test`:
  - `volume_frame_test`: parse and encode, every rejection case, the fixture lines.
  - `volume_slider_test`: `levelAt` at the travel ends and past them, tap-jump, the speaker zone,
    the hold window ignoring and then accepting Mac state, the send throttle and the final send,
    unmute-on-change.
  - `gesture_test`: axis lock both ways, the drag event sequence, taps in drag mode, `dragMode`
    off unchanged.
  - `deck_util` cases for two local cards.
- `swift test`: fixture parity for both encodings, `05` parsing including malformed frames, name
  folding and truncation.
- `make shot` / `cube-shot @vol-*`: each state looked at before the UI is called done.
- `make bench`: the `volume` scenario stays within frame pacing, and the screen still sleeps at
  the end of `linked` / `sleep`.
- `pio run`: the board build (gnu++11) after the pure headers are added.
- Hardware: a "Volume card" section in `docs/ble-acceptance.md` covering drag, tap-jump, mute,
  keyboard echo, switching output to AirPods, an HDMI or fixed output, link drop, and an older Mac
  app.

## Out of scope

- Media keys (play/pause, next).
- Per-app volume.
- Input (microphone) volume.
- A Mac-side toggle for the feature.
- WiFi delivery.
- Haptic or sound feedback on the cube.
- Hold-to-repeat.
- A volume HUD on the Mac.

## Docs to update on implementation

- `CLAUDE.md`: the deck description and the Transports paragraph.
- `firmware/CLAUDE.md`: the pure-header list.
- `docs/ble-protocol.md`: Control `05` and the Volume characteristic.
- `docs/ble-acceptance.md`: the new section.
- `mac-helper/CLAUDE.md`.
- `README.md`: where it describes the cards and the Mac app.
