# Mac Volume Card Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Volume card on the cube that shows the Mac's output volume (a full-width pill, filling from the bottom) and changes it by drag, tap-jump and a mute tap on the speaker, over BLE.

**Architecture:** Two new pure firmware headers hold the wire format (`volume_frame.h`) and the card's geometry and touch model (`volume_slider.h`). The gesture tracker gains a drag mode, the deck gains a second local card, and `ui.cpp` draws the card. BLE carries the request one way, as a new Control opcode `05`, and the Mac's state the other way, in a new Volume characteristic. On the Mac, a pure `Volume.swift` encodes the state, and a CoreAudio `SystemVolume` class reads and sets it.

**Tech Stack:** C++ (gnu++11 on the board, C++17 host tests), PlatformIO/Arduino, LovyanGFX, NimBLE 2.x; Swift 6 tools in Swift 5 mode, macOS 13, CoreBluetooth, CoreAudio, Swift Testing.

**Spec:** `docs/superpowers/specs/2026-10-08-mac-volume-design.md`

## Global Constraints

- Protocol version stays 1. `BLE_FW_REV` goes 4 -> 5.
- Volume characteristic UUID `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a06`, write (encrypted + authenticated), at most 26 bytes: `[ver=1][level][flags][name...]`.
- `level` is 0..100, or `FF` for no output device. `flags` are bit0 muted, bit1 volume settable, bit2 mute settable. `name` is printable ASCII (0x20..0x7E), 0..23 bytes, with no terminator.
- Control `05 <level> <muted>`: level 0..100, muted 0/1, an absolute target, fire and forget.
- The cube drops the whole Volume write when it is shorter than 3 bytes, when `ver != 1`, when `level` is over 100 and not `FF`, or when the name holds a byte outside 0x20..0x7E. A dropped write keeps the previous state.
- Deck order: payload cards, then **Volume** (only while `bleBonded()`), then Pomodoro, which stays last. With no bond the deck is unchanged.
- The Volume card is local-only: not in the payload contract, not counted against `MAX_CARDS`, not in `preview.html`, and not a reading (it never touches `readingsKey()` / `idle.data`).
- `VOLUME_HOLD_MS` = 600, `VOLUME_SEND_MS` = 50, `DRAG_LOCK_PX` = 10. The coalesce delay on the Mac is 30 ms.
- Pure headers build as gnu++11 on the board: a `constexpr` function is a single `return`, and there are no C++14/17 features. Aggregates (`MacVolume`, `VolumeView`, `VolRect`) have no default member initialisers; value-initialise them with `{}`.
- `main` has unrelated uncommitted edits by the user. Work on a branch, `git add` only the files a task names, and use `git add -p` for files that already had edits (`CLAUDE.md`, `README.md`, `mac-helper/CLAUDE.md`, `mac-helper/Sources/ClaudeCubeLink/main.swift`).
- Every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Run firmware tests from `firmware/sim/`, because they open `fixtures/ble-frames.txt` relative to it.

## Review Focus

1. **The Mac reports no output device (level `FF`).** The card should say `NO OUTPUT`, ignore touch, and show an empty fill, never a level of 255. The slider test `testNoOutput` is in Task 2, and the `@vol-noout` shot is in Task 5.
2. **The link drops while a finger is dragging.** The card should fall back to `NO MAC` at once, and the later lift should send nothing. The slider test `testLinkDropMidDrag` is in Task 2.
3. **The deck pulls the viewer off the card mid-drag** (a Pomodoro alert). The slider should be released, so the last level is still sent, and the drag must not turn into a swipe or open a panel on the new card. The gesture test `testDragEndAfterModeOff` is in Task 3, and the release wiring is in Task 7.
4. **A late Mac echo of an older level arrives just after the finger lifts.** The fill should not jump back. The echo is parked for 600 ms, then applied. The slider test `testHoldParksMacState` is in Task 2; the bench `volume` scenario echoes after 300 ms (Task 7).
5. **A long or non-ASCII device name** ("Café’s AirPods", emoji, 30+ characters). The Mac should fold it to ASCII and cut it to 23 bytes, the cube should reject anything else, and the card should cut it to fit with "...". The Swift test `foldsDeviceNamesForTheCube` is in Task 8, the cube rejection cases are in `testRejects` (Task 1), and the `@vol-long` shot is in Task 5.

---

## File Structure

**Firmware, new**
- `firmware/src/volume_frame.h`: wire format. `MacVolume`, `volumeParse`, `volumeRequestEncode`, `volumeSame`. Pure.
- `firmware/src/volume_slider.h`: pill geometry shared by drawing and hit-testing, `volumeLevelAt`, `volumeFillTop`, `VolState`, `VolumeView`, and the `VolumeSlider` touch model (hold window, send throttle). Pure.
- `firmware/sim/tests/volume_frame_test.cpp`, `firmware/sim/tests/volume_slider_test.cpp`.

**Firmware, changed**
- `firmware/src/ble_frame.h`: `BLE_FW_REV` 5, `BLE_CTRL_VOLUME`, `BLE_VOLUME_UUID`.
- `firmware/src/gesture.h/.cpp`: drag mode (`DragStart`/`Drag`/`DragEnd`, `lastY()`, axis lock).
- `firmware/src/deck_util.h`: `DeckSpot`, and `deckKeepIndex` for two local cards.
- `firmware/src/ui.h/.cpp`: `uiVolumeIndex`, the deck functions take `volume`, `drawVolumeCard`, `uiReplayVolume`, and `uiRender` takes `const VolumeView *`.
- `firmware/src/ble.h`, `firmware/src/net_ble.cpp`: the Volume characteristic, `bleTakeVolume`, `bleSendVolume`.
- `firmware/src/main.cpp`: wiring.
- `firmware/sim/ble_sim.cpp` (`CUBE_VOLUME`), `firmware/sim/bench.cpp` (stubs, `volume` scenario, render case), `firmware/sim/shot.cpp` (`@vol-*`), `firmware/sim/Makefile` (`TESTS`, `BENCH_RUNS`).
- `firmware/sim/fixtures/ble-frames.txt`: `vol_request` and `vol_state` lines.
- `firmware/sim/tests/gesture_test.cpp`, `firmware/sim/tests/deck_test.cpp`.

**Mac, new**
- `mac-helper/Sources/CubeLinkCore/Volume.swift`: `MacVolume`, `foldVolumeName`, `encodeVolume`. Pure.
- `mac-helper/Sources/ClaudeCubeLink/SystemVolume.swift`: CoreAudio read, listen, apply.
- `mac-helper/Tests/CubeLinkCoreTests/VolumeTests.swift`.

**Mac, changed**
- `mac-helper/Sources/CubeLinkCore/Protocol.swift`: `volumeUUID`, `.volumeRequest`.
- `mac-helper/Sources/CubeLinkCore/CubeLink.swift`: `onVolumeRequest`, `onReady`, the optional Volume characteristic, `writeVolume`.
- `mac-helper/Sources/ClaudeCubeLink/main.swift`: wiring.

**Docs:** `docs/ble-protocol.md` (Task 1), `docs/ble-acceptance.md`, `README.md`, `CLAUDE.md`, `firmware/CLAUDE.md`, `mac-helper/CLAUDE.md` (Task 10).

---

### Task 0: Branch

- [ ] **Step 1: Branch off main, keeping the user's uncommitted edits in the working tree**

```bash
cd /Users/kpmquockhanh/code/claude-status-cube
git checkout -b mac-volume
git status --short
```
Expected: the same modified and untracked files as on `main`. Nothing is staged, and none of them are touched by this step.

---

### Task 1: Wire format (`volume_frame.h`), fixture and protocol doc

**Files:**
- Create: `firmware/src/volume_frame.h`
- Create: `firmware/sim/tests/volume_frame_test.cpp`
- Modify: `firmware/src/ble_frame.h:12` (fw_rev), `:21` (after `BLE_POMO_ENDED_LEN`), `:35` (after `BLE_SETTINGS_UUID`)
- Modify: `firmware/sim/fixtures/ble-frames.txt` (append)
- Modify: `firmware/sim/Makefile:118` (`TESTS`)
- Modify: `docs/ble-protocol.md`

**Interfaces:**
- Produces:
  - `struct MacVolume { bool known; uint8_t level; bool muted; bool canSet; bool canMute; char name[VOLUME_NAME_MAX + 1]; }`
  - `bool volumeParse(const uint8_t *d, size_t n, MacVolume &out)`
  - `void volumeRequestEncode(uint8_t level, bool muted, uint8_t out[BLE_VOLUME_REQ_LEN])`
  - `bool volumeSame(const MacVolume &a, const MacVolume &b)`
  - constants `VOLUME_FRAME_VER`, `VOLUME_NO_DEVICE` (0xFF), `VOLUME_NAME_MAX` (23), `VOLUME_FRAME_MAX` (26), `VOL_FLAG_MUTED|CAN_SET|CAN_MUTE`, `BLE_VOLUME_REQ_LEN` (3), `BLE_CTRL_VOLUME` (0x05), `BLE_VOLUME_UUID`
  - the fixture keys `vol_request` and `vol_state`, which Task 8 reads from Swift.

- [ ] **Step 1: Write the failing test**

Create `firmware/sim/tests/volume_frame_test.cpp`:

```cpp
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "volume_frame.h"

namespace {

std::vector<uint8_t> hex(std::istringstream &in) {
  std::vector<uint8_t> out;
  std::string tok;
  while (in >> tok) out.push_back((uint8_t)std::stoul(tok, nullptr, 16));
  return out;
}

// The lines of the shared fixture that start with `kind `, without it.
std::vector<std::string> fixtureLines(const char *kind) {
  std::ifstream f("fixtures/ble-frames.txt");
  const std::string prefix = std::string(kind) + " ";
  std::vector<std::string> out;
  std::string line;
  while (std::getline(f, line))
    if (line.rfind(prefix, 0) == 0) out.push_back(line.substr(prefix.size()));
  return out;
}

// vol_request <level> <muted> <bytes>
void testRequestFixture() {
  int n = 0;
  for (const std::string &l : fixtureLines("vol_request")) {
    std::istringstream in(l);
    unsigned level = 0, muted = 0;
    in >> level >> muted;
    const std::vector<uint8_t> want = hex(in);
    uint8_t got[BLE_VOLUME_REQ_LEN];
    volumeRequestEncode((uint8_t)level, muted != 0, got);
    CHECK(want.size() == BLE_VOLUME_REQ_LEN && memcmp(got, want.data(), BLE_VOLUME_REQ_LEN) == 0);
    n++;
  }
  CHECK(n == 3);  // a missing fixture must fail, not pass vacuously
}

// vol_state <level|none> <flags hex> "<name>" <bytes>
void testStateFixture() {
  int n = 0;
  for (const std::string &l : fixtureLines("vol_state")) {
    const size_t q0 = l.find('"'), q1 = l.find('"', q0 + 1);
    std::istringstream head(l.substr(0, q0));
    std::string levelTok, flagsTok;
    head >> levelTok >> flagsTok;
    const std::string name = l.substr(q0 + 1, q1 - q0 - 1);
    std::istringstream rest(l.substr(q1 + 1));
    const std::vector<uint8_t> bytes = hex(rest);
    const unsigned level = levelTok == "none" ? VOLUME_NO_DEVICE : (unsigned)std::stoul(levelTok);
    const unsigned flags = (unsigned)std::stoul(flagsTok, nullptr, 16);

    MacVolume v{};
    CHECK(volumeParse(bytes.data(), bytes.size(), v));
    CHECK(v.known);
    CHECK(v.level == level);
    CHECK(v.muted == ((flags & VOL_FLAG_MUTED) != 0));
    CHECK(v.canSet == ((flags & VOL_FLAG_CAN_SET) != 0));
    CHECK(v.canMute == ((flags & VOL_FLAG_CAN_MUTE) != 0));
    CHECK(name == v.name);
    n++;
  }
  CHECK(n == 4);
}

// Every rejection leaves the previous state untouched.
void testRejects() {
  MacVolume before{};
  before.known = true;
  before.level = 77;
  strcpy(before.name, "KEEP");
  auto rejected = [&](const std::vector<uint8_t> &d) {
    MacVolume out = before;
    const bool ok = volumeParse(d.empty() ? nullptr : d.data(), d.size(), out);
    return !ok && volumeSame(out, before);
  };
  CHECK(rejected({}));                                     // nothing
  CHECK(rejected({1, 50}));                                // under 3 bytes
  std::vector<uint8_t> big(27, 'A');
  big[0] = 1;
  big[1] = 50;
  big[2] = 0;
  CHECK(rejected(big));                                    // over 26 bytes
  CHECK(rejected({2, 50, 0}));                             // unknown version
  CHECK(rejected({1, 101, 0}));                            // level over 100
  CHECK(rejected({1, 0xFE, 0}));                           // ...and not FF
  CHECK(rejected({1, 50, 0, 'A', 0x1F}));                  // control byte in the name
  CHECK(rejected({1, 50, 0, 'A', 0x7F}));                  // DEL
  CHECK(rejected({1, 50, 0, 'C', 'a', 'f', 0xC3, 0xA9}));  // UTF-8: the Mac folds names first
}

void testAccepts() {
  MacVolume v{};
  const uint8_t empty[] = {1, 0, VOL_FLAG_CAN_SET};
  CHECK(volumeParse(empty, sizeof(empty), v) && v.known && v.level == 0 && v.name[0] == '\0' && v.canSet);

  uint8_t full[VOLUME_FRAME_MAX];
  full[0] = 1;
  full[1] = 100;
  full[2] = 0xF8;  // only unknown flag bits: ignored
  for (size_t i = 3; i < sizeof(full); i++) full[i] = 'x';
  CHECK(volumeParse(full, sizeof(full), v));
  CHECK(strlen(v.name) == VOLUME_NAME_MAX && !v.muted && !v.canSet && !v.canMute);

  const uint8_t none[] = {1, VOLUME_NO_DEVICE, 0};
  CHECK(volumeParse(none, sizeof(none), v) && v.level == VOLUME_NO_DEVICE);

  // A shorter name after a longer one leaves no tail behind.
  const uint8_t a[] = {1, 5, 0, 'A', 'B', 'C'}, b[] = {1, 5, 0, 'Z'};
  CHECK(volumeParse(a, sizeof(a), v) && volumeParse(b, sizeof(b), v) && strcmp(v.name, "Z") == 0);
}

void testEncodeClamps() {
  uint8_t m[BLE_VOLUME_REQ_LEN];
  volumeRequestEncode(150, true, m);
  CHECK(m[0] == BLE_CTRL_VOLUME && m[1] == 100 && m[2] == 1);
}

}  // namespace

int main() {
  testRequestFixture();
  testStateFixture();
  testRejects();
  testAccepts();
  testEncodeClamps();
  return checksDone("volume_frame_test");
}
```

In `firmware/sim/Makefile`, change the last `TESTS` line (line 118) from
```make
         build/ble_conn_test build/touch_gate_test build/melody_test
```
to
```make
         build/ble_conn_test build/touch_gate_test build/melody_test \
         build/volume_frame_test
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd firmware/sim && make build/volume_frame_test`
Expected: a compile error, `volume_frame.h: No such file or directory`.

- [ ] **Step 3: Add the constants to `ble_frame.h`**

In `firmware/src/ble_frame.h`, change line 12:
```cpp
constexpr uint8_t BLE_FW_REV = 5;  // bumped when behaviour the Mac can see changes
```
After line 21 (`constexpr size_t BLE_POMO_ENDED_LEN = 3;`), add:
```cpp
constexpr uint8_t BLE_CTRL_VOLUME = 0x05;  // cube -> Mac: [0x05, level, muted] set the Mac's output (fw_rev 5)
```
After line 35 (`BLE_SETTINGS_UUID`), add:
```cpp
constexpr char BLE_VOLUME_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a06";
```

- [ ] **Step 4: Write `volume_frame.h`**

Create `firmware/src/volume_frame.h`:

```cpp
#pragma once
// The Mac's output volume on the wire (docs/ble-protocol.md, "Volume"). Pure:
// no Arduino, host-tested in sim/tests/volume_frame_test.cpp.
//
//   Mac -> cube, Volume characteristic: [ver=1][level][flags][name...]
//   cube -> Mac, Control:               [0x05][level][muted]
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ble_frame.h"

constexpr uint8_t VOLUME_FRAME_VER = 1;
constexpr uint8_t VOLUME_NO_DEVICE = 0xFF;  // level when the Mac has no output device
constexpr size_t VOLUME_NAME_MAX = 23;
constexpr size_t VOLUME_FRAME_MAX = 3 + VOLUME_NAME_MAX;  // 26
constexpr uint8_t VOL_FLAG_MUTED = 1, VOL_FLAG_CAN_SET = 2, VOL_FLAG_CAN_MUTE = 4;
constexpr size_t BLE_VOLUME_REQ_LEN = 3;

// What the Mac last reported. known=false: nothing yet, or the link dropped.
struct MacVolume {
  bool known;
  uint8_t level;  // 0..100, or VOLUME_NO_DEVICE
  bool muted;
  bool canSet;    // the device's volume can be set (false: HDMI and the like)
  bool canMute;
  char name[VOLUME_NAME_MAX + 1];
};

// Parses a Volume write. On any fault returns false and leaves `out` as it was.
inline bool volumeParse(const uint8_t *d, size_t n, MacVolume &out) {
  if (!d || n < 3 || n > VOLUME_FRAME_MAX) return false;
  if (d[0] != VOLUME_FRAME_VER) return false;
  if (d[1] > 100 && d[1] != VOLUME_NO_DEVICE) return false;
  MacVolume v{};
  for (size_t i = 3; i < n; i++) {
    if (d[i] < 0x20 || d[i] > 0x7E) return false;  // the fonts are printable ASCII only
    v.name[i - 3] = (char)d[i];
  }
  v.known = true;
  v.level = d[1];
  v.muted = (d[2] & VOL_FLAG_MUTED) != 0;
  v.canSet = (d[2] & VOL_FLAG_CAN_SET) != 0;
  v.canMute = (d[2] & VOL_FLAG_CAN_MUTE) != 0;
  out = v;
  return true;
}

// The Control message asking the Mac to set its output. An absolute target, so
// a lost or repeated message cannot drift the volume.
inline void volumeRequestEncode(uint8_t level, bool muted, uint8_t out[BLE_VOLUME_REQ_LEN]) {
  out[0] = BLE_CTRL_VOLUME;
  out[1] = level > 100 ? 100 : level;
  out[2] = muted ? 1 : 0;
}

inline bool volumeSame(const MacVolume &a, const MacVolume &b) {
  return a.known == b.known && a.level == b.level && a.muted == b.muted && a.canSet == b.canSet &&
         a.canMute == b.canMute && strcmp(a.name, b.name) == 0;
}
```

- [ ] **Step 5: Append the fixture lines**

Append to `firmware/sim/fixtures/ble-frames.txt`:

```
# Control (cube -> Mac): set the Mac's volume. vol_request <level> <muted> <bytes>.
vol_request 56 0 05 38 00
vol_request 0 1 05 00 01
vol_request 100 0 05 64 00
# Volume characteristic (Mac -> cube): vol_state <level|none> <flags hex> "<name>" <bytes>. none = no output device (ff); flags bit0 muted, bit1 volume settable, bit2 mute settable.
vol_state 56 06 "MacBook Pro Speakers" 01 38 06 4d 61 63 42 6f 6f 6b 20 50 72 6f 20 53 70 65 61 6b 65 72 73
vol_state 30 07 "AirPods Pro" 01 1e 07 41 69 72 50 6f 64 73 20 50 72 6f
vol_state 100 04 "HDMI" 01 64 04 48 44 4d 49
vol_state none 00 "" 01 ff 00
```

Both existing fixture readers (`ble_frame_test.cpp` and `FrameTests.swift::loadFixture`) skip keys they do not know, so these lines do not affect them.

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cd firmware/sim && make test`
Expected: `volume_frame_test: N checks, 0 failed`, and every other test still at 0 failed (`ble_frame_test` included).

- [ ] **Step 7: Document the protocol**

In `docs/ble-protocol.md`, add a row after the Settings row in the Service table (line 15):
```
| Volume  | `6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a06` | write (encrypted + authenticated), fw_rev 5+ |
```
Add a row after the `04` row in the Control table (line 41):
```
| `05 <level> <muted>` | set the Mac's output to `<level>` (0..100) and mute it when `<muted>` is `01` (added in fw_rev 5, protocol still 1). An absolute target, so a lost or repeated message cannot drift the volume. Fire and forget: no ack, and it is dropped if no Mac is subscribed. A Mac that does not know `05` ignores it |
```
Append at the end of the file:
```markdown
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
```

- [ ] **Step 8: Commit**

```bash
git add firmware/src/volume_frame.h firmware/src/ble_frame.h firmware/sim/tests/volume_frame_test.cpp \
        firmware/sim/fixtures/ble-frames.txt firmware/sim/Makefile docs/ble-protocol.md \
        docs/superpowers/specs/2026-10-08-mac-volume-design.md docs/superpowers/plans/2026-10-08-mac-volume.md
git commit -m "feat(firmware): Volume wire format (Control 05, Volume characteristic, fw_rev 5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The slider model (`volume_slider.h`)

**Files:**
- Create: `firmware/src/volume_slider.h`
- Create: `firmware/sim/tests/volume_slider_test.cpp`
- Modify: `firmware/sim/Makefile` (`TESTS`)

**Interfaces:**
- Consumes: `MacVolume`, `VOLUME_NO_DEVICE` and `VOLUME_NAME_MAX` from Task 1.
- Produces:
  - geometry: `VOL_PILL_X/Y/W/H/R` (14, 42, 212, 210, 40), `VOL_TRAVEL_TOP/BOTTOM` (54, 240), `VOL_SPK_CX/CY` (120, 218), `struct VolRect`, `VOL_PILL_RECT`, `VOL_SPEAKER_ZONE`
  - `VOLUME_HOLD_MS`, `VOLUME_SEND_MS`
  - `constexpr uint8_t volumeLevelAt(int y)` and `int volumeFillTop(float level)`
  - `enum class VolState : uint8_t { NoMac, NoOutput, Live, Fixed }`
  - `struct VolumeView { VolState state; uint8_t level; bool muted; bool canMute; bool tracking; char name[24]; }`
  - `class VolumeSlider` with:
    - `state()`, `fromMac(const MacVolume&, uint32_t now)`, `tick(uint32_t now) -> bool`
    - `grab() -> bool`, `drag(int y) -> bool`, `release(uint32_t now) -> bool`
    - `tap(int x, int y, uint32_t now) -> bool`
    - `takeSend(uint32_t now, uint8_t &level, bool &muted) -> bool`
    - `view() -> VolumeView`, `held() -> bool`

- [ ] **Step 1: Write the failing test**

Create `firmware/sim/tests/volume_slider_test.cpp`:

```cpp
#include <cstring>

#include "check.h"
#include "volume_slider.h"

namespace {

MacVolume live(uint8_t level, bool muted = false) {
  MacVolume m{};
  m.known = true;
  m.level = level;
  m.muted = muted;
  m.canSet = m.canMute = true;
  strcpy(m.name, "MacBook Pro Speakers");
  return m;
}

void testGeometry() {
  CHECK(volumeLevelAt(147) == 50);
  CHECK(volumeLevelAt(55) == 99);
  CHECK(volumeLevelAt(239) == 1);
  CHECK(volumeLevelAt(VOL_TRAVEL_TOP) == 100);
  CHECK(volumeLevelAt(VOL_TRAVEL_BOTTOM) == 0);
  CHECK(volumeLevelAt(0) == 100);    // above the travel: clamped
  CHECK(volumeLevelAt(280) == 0);    // below it
  CHECK(volumeFillTop(100) == VOL_PILL_Y);
  CHECK(volumeFillTop(0) == VOL_PILL_Y + VOL_PILL_H);
  CHECK(volumeFillTop(50) == 147);
  CHECK(VOL_PILL_RECT.contains(VOL_SPK_CX, VOL_SPK_CY));
  CHECK(VOL_SPEAKER_ZONE.contains(VOL_SPK_CX, VOL_SPK_CY));
  CHECK(!VOL_SPEAKER_ZONE.contains(VOL_SPK_CX, 147));
}

void testNoMacByDefault() {
  VolumeSlider s;
  CHECK(s.state() == VolState::NoMac);
  CHECK(!s.grab());
  CHECK(!s.tap(120, 147, 0));
  CHECK(!s.tap(VOL_SPK_CX, VOL_SPK_CY, 0));
  const VolumeView v = s.view();
  CHECK(v.state == VolState::NoMac && v.level == 0 && !v.muted && v.name[0] == '\0');
  uint8_t l;
  bool m;
  CHECK(!s.takeSend(0, l, m));
}

void testLiveApplies() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  const VolumeView v = s.view();
  CHECK(v.state == VolState::Live && v.level == 40 && !v.muted && v.canMute && !v.tracking);
  CHECK(strcmp(v.name, "MacBook Pro Speakers") == 0);
}

// While dragging, at most one request every VOLUME_SEND_MS; the first goes at
// once, and the release always sends the final value.
void testDragThrottle() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l = 0;
  bool m = true;
  CHECK(s.grab());
  CHECK(s.held() && s.view().tracking);
  CHECK(s.drag(147));
  CHECK(s.takeSend(1000, l, m) && l == 50 && !m);
  CHECK(s.drag(100));
  CHECK(!s.takeSend(1020, l, m));            // 20 ms after the last: wait
  CHECK(s.takeSend(1050, l, m) && l == volumeLevelAt(100));
  CHECK(s.drag(80));
  CHECK(s.release(1070));
  CHECK(!s.held());
  CHECK(s.takeSend(1070, l, m) && l == volumeLevelAt(80));  // final value, unthrottled
  CHECK(!s.takeSend(1100, l, m));            // nothing left
  CHECK(!s.drag(60));                        // released: drags mean nothing
}

void testDragSameLevelIsNoChange() {
  VolumeSlider s;
  s.fromMac(live(50), 0);
  CHECK(s.grab());
  CHECK(!s.drag(147));  // already 50, not muted
  uint8_t l;
  bool m;
  CHECK(!s.takeSend(0, l, m));
}

// Review Focus 4: a Mac state that arrives while the finger is down, or within
// VOLUME_HOLD_MS of the lift, is parked and applied when the window ends.
void testHoldParksMacState() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  CHECK(s.grab());
  CHECK(s.drag(147));                    // 50
  s.fromMac(live(45), 1000);             // an echo of a level the drag already passed
  CHECK(s.view().level == 50);
  CHECK(s.release(1100));
  s.fromMac(live(48), 1200);             // still inside the window
  CHECK(s.view().level == 50);
  CHECK(!s.tick(1400));                  // 300 ms after the lift
  CHECK(s.view().level == 50);
  CHECK(s.tick(1700));                   // 600 ms: the parked state lands
  CHECK(s.view().level == 48);
  CHECK(!s.tick(1800));                  // once
  s.fromMac(live(30), 1900);             // outside the window: at once
  CHECK(s.view().level == 30);
}

// Review Focus 2: the link drops mid-drag. The card is NO MAC at once and the
// lift that follows sends nothing.
void testLinkDropMidDrag() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l;
  bool m;
  CHECK(s.grab());
  CHECK(s.drag(100));
  s.fromMac(MacVolume{}, 500);
  CHECK(s.state() == VolState::NoMac);
  CHECK(!s.held());
  CHECK(!s.release(600));
  CHECK(!s.takeSend(600, l, m));
  CHECK(s.view().level == 0);
}

void testFixed() {
  VolumeSlider s;
  MacVolume hdmi = live(100);
  hdmi.canSet = false;
  strcpy(hdmi.name, "HDMI");
  s.fromMac(hdmi, 0);
  CHECK(s.state() == VolState::Fixed);
  CHECK(!s.grab());
  CHECK(!s.tap(120, 147, 0));              // no tap-jump
  CHECK(s.view().level == 100);
  uint8_t l;
  bool m;
  CHECK(s.tap(VOL_SPK_CX, VOL_SPK_CY, 0));  // the speaker still mutes
  CHECK(s.view().muted);
  CHECK(s.takeSend(0, l, m) && l == 100 && m);

  VolumeSlider t;
  hdmi.canMute = false;
  t.fromMac(hdmi, 0);
  CHECK(!t.tap(VOL_SPK_CX, VOL_SPK_CY, 0));
}

// Review Focus 1: no output device. Nothing reacts, and the fill is empty, not 255.
void testNoOutput() {
  VolumeSlider s;
  MacVolume none{};
  none.known = true;
  none.level = VOLUME_NO_DEVICE;
  s.fromMac(none, 0);
  CHECK(s.state() == VolState::NoOutput);
  CHECK(!s.grab());
  CHECK(!s.tap(VOL_SPK_CX, VOL_SPK_CY, 0));
  CHECK(!s.tap(120, 100, 0));
  const VolumeView v = s.view();
  CHECK(v.level == 0 && !v.muted);
}

void testTapJump() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l;
  bool m;
  CHECK(s.tap(120, 147, 0));
  CHECK(s.view().level == 50);
  CHECK(s.takeSend(0, l, m) && l == 50 && !m);
  CHECK(!s.tap(120, 147, 10));  // the same height again: nothing to send
  CHECK(!s.takeSend(10, l, m));
  CHECK(!s.tap(5, 147, 20));    // left of the pill
  CHECK(!s.tap(120, 20, 20));   // above it (the top bar)
}

void testSpeakerToggle() {
  VolumeSlider s;
  s.fromMac(live(40), 0);
  uint8_t l;
  bool m;
  CHECK(s.tap(VOL_SPK_CX, VOL_SPK_CY, 0));
  CHECK(s.view().muted && s.view().level == 40);
  CHECK(s.takeSend(0, l, m) && l == 40 && m);
  CHECK(s.tap(VOL_SPK_CX + 20, VOL_SPK_CY - 20, 100));
  CHECK(!s.view().muted);
  CHECK(s.takeSend(100, l, m) && l == 40 && !m);
}

void testChangeUnmutes() {
  VolumeSlider s;
  s.fromMac(live(40, true), 0);
  uint8_t l;
  bool m;
  CHECK(s.grab());
  CHECK(s.drag(volumeFillTop(40)));  // even at the same height: unmuting is a change
  CHECK(!s.view().muted);
  CHECK(s.takeSend(0, l, m) && !m);
  CHECK(s.release(10));

  VolumeSlider t;
  t.fromMac(live(40, true), 0);
  CHECK(t.tap(120, 147, 0));
  CHECK(!t.view().muted && t.view().level == 50);
}

}  // namespace

int main() {
  testGeometry();
  testNoMacByDefault();
  testLiveApplies();
  testDragThrottle();
  testDragSameLevelIsNoChange();
  testHoldParksMacState();
  testLinkDropMidDrag();
  testFixed();
  testNoOutput();
  testTapJump();
  testSpeakerToggle();
  testChangeUnmutes();
  return checksDone("volume_slider_test");
}
```

In `firmware/sim/Makefile`, extend the line Task 1 added:
```make
         build/volume_frame_test build/volume_slider_test
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd firmware/sim && make build/volume_slider_test`
Expected: a compile error, `volume_slider.h: No such file or directory`.

- [ ] **Step 3: Write `volume_slider.h`**

Create `firmware/src/volume_slider.h`:

```cpp
#pragma once
// The Volume card: the pill's geometry and its touch model. Pure: no Arduino,
// the caller passes `now`. ui.cpp draws from the same constants the hit-tests
// use, so what is drawn and what is pressed cannot drift apart. Host-tested in
// sim/tests/volume_slider_test.cpp.
#include <stdint.h>
#include <string.h>

#include "volume_frame.h"

// The pill: full width less a 14 px gutter, between the top bar and the dots.
constexpr int VOL_PILL_X = 14;
constexpr int VOL_PILL_Y = 42;
constexpr int VOL_PILL_W = 212;
constexpr int VOL_PILL_H = 210;
constexpr int VOL_PILL_R = 40;
// The finger's travel, inset from the pill so both ends are easy to reach: at
// or above TOP is 100, at or below BOTTOM is 0.
constexpr int VOL_TRAVEL_TOP = 54;
constexpr int VOL_TRAVEL_BOTTOM = 240;
// The speaker glyph's centre; a tap around it toggles mute.
constexpr int VOL_SPK_CX = 120;
constexpr int VOL_SPK_CY = 218;

struct VolRect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};
constexpr VolRect VOL_PILL_RECT{VOL_PILL_X, VOL_PILL_Y, VOL_PILL_W, VOL_PILL_H};
constexpr VolRect VOL_SPEAKER_ZONE{VOL_SPK_CX - 30, VOL_SPK_CY - 25, 60, 50};

// After a lift (or a tap), Mac states are parked this long: echoes of levels
// the finger already passed must not jerk the fill back.
constexpr uint32_t VOLUME_HOLD_MS = 600;
// Fewest ms between two requests while dragging.
constexpr uint32_t VOLUME_SEND_MS = 50;

// The level for a finger at height `y`, rounded to the nearest step.
constexpr uint8_t volumeLevelAt(int y) {
  return y <= VOL_TRAVEL_TOP      ? 100
         : y >= VOL_TRAVEL_BOTTOM ? 0
                                  : (uint8_t)(((VOL_TRAVEL_BOTTOM - y) * 100 + (VOL_TRAVEL_BOTTOM - VOL_TRAVEL_TOP) / 2) /
                                              (VOL_TRAVEL_BOTTOM - VOL_TRAVEL_TOP));
}

// The fill's top edge for a level (fractional while it eases): the whole pill
// at 100, nothing at 0.
inline int volumeFillTop(float level) { return (int)(VOL_PILL_Y + VOL_PILL_H * (100.0f - level) / 100.0f + 0.5f); }

enum class VolState : uint8_t {
  NoMac,     // link down, or no Volume write yet
  NoOutput,  // the Mac has no output device
  Live,
  Fixed,     // the device's volume cannot be set (HDMI and the like)
};

// What ui.cpp draws.
struct VolumeView {
  VolState state;
  uint8_t level;  // where the fill stands: the level when Live, 100 when Fixed, else 0
  bool muted;
  bool canMute;
  bool tracking;  // a finger holds the fill: draw it at `level`, no easing
  char name[VOLUME_NAME_MAX + 1];
};

class VolumeSlider {
 public:
  VolState state() const {
    if (!_mac.known) return VolState::NoMac;
    if (_mac.level == VOLUME_NO_DEVICE) return VolState::NoOutput;
    if (!_mac.canSet) return VolState::Fixed;
    return VolState::Live;
  }

  // The Mac's state arrived. known=false (the link dropped) forgets everything
  // at once, a drag in progress included. Otherwise, while a finger holds the
  // fill and for VOLUME_HOLD_MS after, the state is parked for tick().
  void fromMac(const MacVolume &v, uint32_t now) {
    if (!v.known) {
      *this = VolumeSlider();
      return;
    }
    if (_held || (_quiet && now - _releasedAt < VOLUME_HOLD_MS)) {
      _parked = v;
      _hasParked = true;
      return;
    }
    apply(v);
  }

  // Ends the hold window and applies a parked state. True when the view changed.
  bool tick(uint32_t now) {
    if (_quiet && now - _releasedAt >= VOLUME_HOLD_MS) _quiet = false;
    if (_held || _quiet || !_hasParked) return false;
    apply(_parked);
    return true;
  }

  // A drag began on the card. Only a live, settable output can be dragged.
  bool grab() {
    if (state() != VolState::Live) return false;
    _held = true;
    _quiet = false;
    return true;
  }

  // The finger is at height `y`. True when the level or mute changed.
  bool drag(int y) {
    if (!_held) return false;
    const uint8_t l = volumeLevelAt(y);
    if (l == _level && !_muted) return false;
    _level = l;
    _muted = false;  // any level change unmutes
    _pending = true;
    return true;
  }

  // The finger lifted. The last level goes out at once, and the hold window starts.
  bool release(uint32_t now) {
    if (!_held) return false;
    _held = false;
    _quiet = true;
    _releasedAt = now;
    if (_pending) _sendNow = true;
    return true;
  }

  // A tap: the speaker zone toggles mute; elsewhere on the pill the level jumps
  // to that height. True when something changed.
  bool tap(int x, int y, uint32_t now) {
    const VolState s = state();
    if (s == VolState::NoMac || s == VolState::NoOutput) return false;
    if (VOL_SPEAKER_ZONE.contains(x, y)) {
      if (!_mac.canMute) return false;
      _muted = !_muted;
      queue(now);
      return true;
    }
    if (s != VolState::Live || !VOL_PILL_RECT.contains(x, y)) return false;
    const uint8_t l = volumeLevelAt(y);
    if (l == _level && !_muted) return false;
    _level = l;
    _muted = false;
    queue(now);
    return true;
  }

  // A request for the Mac, when one is due: throttled to VOLUME_SEND_MS while
  // dragging, at once after a lift or a tap.
  bool takeSend(uint32_t now, uint8_t &level, bool &muted) {
    if (!_pending) return false;
    if (!_sendNow && _sentOnce && now - _lastSend < VOLUME_SEND_MS) return false;
    level = _level;
    muted = _muted;
    _pending = _sendNow = false;
    _lastSend = now;
    _sentOnce = true;
    return true;
  }

  VolumeView view() const {
    VolumeView v{};
    v.state = state();
    v.level = v.state == VolState::Live ? _level : v.state == VolState::Fixed ? 100 : 0;
    v.muted = (v.state == VolState::Live || v.state == VolState::Fixed) && _muted;
    v.canMute = _mac.canMute;
    v.tracking = _held;
    memcpy(v.name, _mac.name, sizeof(v.name));
    return v;
  }

  bool held() const { return _held; }

 private:
  void apply(const MacVolume &v) {
    _mac = v;
    _level = v.level == VOLUME_NO_DEVICE ? 0 : v.level;
    _muted = v.muted;
    _hasParked = false;
  }

  void queue(uint32_t now) {
    _pending = _sendNow = true;
    _quiet = true;
    _releasedAt = now;
  }

  MacVolume _mac{}, _parked{};
  bool _hasParked = false;
  uint8_t _level = 0;
  bool _muted = false, _held = false, _quiet = false;
  uint32_t _releasedAt = 0;
  bool _pending = false, _sendNow = false;
  uint32_t _lastSend = 0;
  bool _sentOnce = false;
};
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd firmware/sim && make test`
Expected: `volume_slider_test: N checks, 0 failed`, and every other test at 0 failed.

- [ ] **Step 5: Check the board build (gnu++11)**

Run: `cd firmware && pio run`
Expected: SUCCESS. Nothing includes the header yet, so this only proves the existing build is intact. Task 5 compiles the header for the board.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/volume_slider.h firmware/sim/tests/volume_slider_test.cpp firmware/sim/Makefile
git commit -m "feat(firmware): Volume card touch model (drag, tap-jump, mute, hold window, throttle)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Gesture drag mode

**Files:**
- Modify: `firmware/src/gesture.h`
- Modify: `firmware/src/gesture.cpp`
- Test: `firmware/sim/tests/gesture_test.cpp`

**Interfaces:**
- Produces:
  - `Gesture::DragStart`, `Gesture::Drag`, `Gesture::DragEnd`
  - `constexpr int DRAG_LOCK_PX = 10`
  - `Gesture GestureTracker::update(bool down, int16_t x, int16_t y, uint32_t now, bool multiTap, bool dragMode = false)`
  - `int16_t GestureTracker::lastY() const`
- Behaviour:
  - With `dragMode` on, once the finger is 10 px from where it landed, a mostly vertical move returns `DragStart`, then `Drag` on each sample whose y changed, then `DragEnd` on the lift. A mostly horizontal move is classified on the lift as before (and can never become a vertical swipe).
  - `DragEnd` comes for any drag that started, even if `dragMode` is off by the lift.
  - With `dragMode` off, behaviour is unchanged.

- [ ] **Step 1: Write the failing tests**

In `firmware/sim/tests/gesture_test.cpp`, give `Pad` a `drag` parameter, passed through:

```cpp
  Gesture raw(uint32_t now, bool down, int x, int y, bool multi = false, bool drag = false) {
    last = now;
    wasDown = down;
    return t.update(down, (int16_t)x, (int16_t)y, now, multi, drag);
  }
  Gesture at(uint32_t now, bool down, int x, int y, bool multi = false, bool drag = false) {
    if (down && wasDown) {
      while (now - last > 50u) raw(last + 50u, true, x, y, multi, drag);
    }
    return raw(now, down, x, y, multi, drag);
  }
```

Add these tests before `main()`, and call each from `main()` before `return checksDone(...)`:

```cpp
// Drag mode (the Volume card): a vertical move locks to a drag once it is
// DRAG_LOCK_PX from where it landed, and reports every move until the lift.
void testDragLock() {
  Pad p;
  CHECK(p.raw(0, true, 120, 200, false, true) == Gesture::None);
  CHECK(p.raw(20, true, 120, 195, false, true) == Gesture::None);  // 5 px: undecided
  CHECK(p.raw(40, true, 121, 188, false, true) == Gesture::DragStart);
  CHECK(p.t.lastY() == 188);
  CHECK(p.raw(60, true, 121, 150, false, true) == Gesture::Drag);
  CHECK(p.t.lastY() == 150);
  CHECK(p.raw(80, true, 121, 150, false, true) == Gesture::None);  // finger still
  CHECK(p.raw(100, true, 121, 170, false, true) == Gesture::Drag);  // back down
  CHECK(p.raw(120, false, 0, 0, false, true) == Gesture::DragEnd);
  CHECK(p.raw(140, false, 0, 0, false, true) == Gesture::None);
}

// A quick vertical flick in drag mode is a drag, never a swipe (it would open
// a panel on the Volume card).
void testDragFlickIsNotSwipe() {
  Pad p;
  p.raw(0, true, 120, 200, false, true);
  CHECK(p.raw(30, true, 120, 170, false, true) == Gesture::DragStart);
  CHECK(p.raw(60, true, 120, 100, false, true) == Gesture::Drag);
  CHECK(p.raw(90, false, 0, 0, false, true) == Gesture::DragEnd);
}

// Horizontal still changes card in drag mode.
void testDragModeHorizontalSwipe() {
  Pad p;
  CHECK(p.raw(0, true, 200, 100, false, true) == Gesture::None);
  CHECK(p.raw(50, true, 150, 102, false, true) == Gesture::None);  // locked horizontal
  CHECK(p.raw(100, true, 100, 100, false, true) == Gesture::None);
  CHECK(p.raw(200, false, 0, 0, false, true) == Gesture::SwipeNext);
}

// A move that locked horizontal and then wandered vertical is nothing: it can
// neither become a drag nor a vertical swipe.
void testDragModeHorizontalThenVertical() {
  Pad p;
  p.raw(0, true, 100, 150, false, true);
  CHECK(p.raw(30, true, 112, 150, false, true) == Gesture::None);  // locked horizontal
  CHECK(p.raw(60, true, 112, 100, false, true) == Gesture::None);  // no DragStart now
  CHECK(p.raw(90, false, 0, 0, false, true) == Gesture::None);     // and no SwipeUp
}

void testDragModeTap() {
  Pad p;
  p.raw(0, true, 100, 100, false, true);
  p.raw(40, true, 103, 102, false, true);
  CHECK(p.raw(80, false, 0, 0, false, true) == Gesture::Tap);
}

// Review Focus 3: the deck moves off the Volume card mid-drag (an alert), so
// main.cpp turns drag mode off. The drag runs to its end; the lift is DragEnd,
// not a swipe on whatever card is now showing.
void testDragEndAfterModeOff() {
  Pad p;
  p.raw(0, true, 120, 200, false, true);
  CHECK(p.raw(20, true, 120, 185, false, true) == Gesture::DragStart);
  CHECK(p.raw(40, true, 120, 150, false, false) == Gesture::Drag);
  CHECK(p.raw(60, true, 120, 110, false, false) == Gesture::Drag);
  CHECK(p.raw(80, false, 0, 0, false, false) == Gesture::DragEnd);
}

// Drag mode off: nothing new. A slow vertical move is still nothing.
void testDragModeOffUnchanged() {
  Pad p;
  CHECK(p.raw(0, true, 120, 200) == Gesture::None);
  CHECK(p.raw(400, true, 120, 190) == Gesture::None);
  CHECK(p.raw(800, true, 120, 180) == Gesture::None);
  CHECK(p.raw(801, false, 0, 0) == Gesture::None);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd firmware/sim && make build/gesture_test`
Expected: compile errors: no `DragStart` in `Gesture`, no `lastY` member, and too many arguments to `update`.

- [ ] **Step 3: Change `gesture.h`**

Add to the enum after `TripleTap,`:
```cpp
  DragStart,   // drag mode: the finger locked to a vertical drag (lastY() is where it is)
  Drag,        // drag mode: the finger moved while dragging (lastY())
  DragEnd,     // the dragging finger lifted (also after drag mode was turned off mid-drag)
```
After `MULTI_TAP_GAP_MS`, add:
```cpp
// In drag mode, how far the finger must move from where it landed before the
// move is called vertical (a drag) or horizontal (a swipe, decided on lift).
constexpr int DRAG_LOCK_PX = 10;
```
Replace the `update` declaration and its comment's last two lines:
```cpp
  // `multiTap` says whether taps group into DoubleTap/TripleTap (only the
  // Pomodoro card uses them). When false every tap is a plain Tap, at once.
  // `dragMode` (only the Volume card) turns a vertical move into
  // DragStart / Drag / DragEnd instead of a swipe. The two are never both on.
  Gesture update(bool down, int16_t x, int16_t y, uint32_t now, bool multiTap, bool dragMode = false);
```
After `startY()`, add:
```cpp
  // The latest sample's height: where a drag is.
  int16_t lastY() const { return _ly; }
```
In the private members, after `_tapUp`, add:
```cpp
  enum : uint8_t { AXIS_NONE, AXIS_V, AXIS_H };
  uint8_t _axis = AXIS_NONE;  // drag mode: which way this touch locked
```

- [ ] **Step 4: Change `gesture.cpp`**

Change the definition's signature to:
```cpp
Gesture GestureTracker::update(bool down, int16_t x, int16_t y, uint32_t now, bool multiTap, bool dragMode) {
```
Replace the whole `if (down) { ... }` block with:
```cpp
  if (down) {
    if (!_down) {
      _down = true;
      _sx = _lx = x;
      _sy = _ly = y;
      _t0 = now;
      _axis = AXIS_NONE;
      return settled;
    }
    const int16_t prevY = _ly;
    _lx = x;
    _ly = y;
    if (_axis == AXIS_NONE && dragMode) {
      const int adx = abs(_lx - _sx), ady = abs(_ly - _sy);
      if (adx >= DRAG_LOCK_PX || ady >= DRAG_LOCK_PX) {
        _axis = ady > adx ? AXIS_V : AXIS_H;
        if (_axis == AXIS_V) {
          _taps = 0;
          return Gesture::DragStart;  // settled is None here: multiTap is off in drag mode
        }
      }
    } else if (_axis == AXIS_V && _ly != prevY) {
      return Gesture::Drag;
    }
    return settled;
  }
```
After `_down = false;`, add:
```cpp
  if (_axis == AXIS_V) {
    _axis = AXIS_NONE;
    _taps = 0;
    return Gesture::DragEnd;
  }
```
Change the vertical swipe condition to:
```cpp
  if (_axis != AXIS_H && abs(dy) >= SWIPE_MIN_PX && abs(dy) > abs(dx) && dt <= SWIPE_MAX_MS) {
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd firmware/sim && make test`
Expected: `gesture_test: N checks, 0 failed`, with every pre-existing check still passing, and all other tests at 0 failed.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/gesture.h firmware/src/gesture.cpp firmware/sim/tests/gesture_test.cpp
git commit -m "feat(firmware): gesture drag mode with a 10 px axis lock

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Deck index with two local cards

**Files:**
- Modify: `firmware/src/deck_util.h`
- Test: `firmware/sim/tests/deck_test.cpp`

**Interfaces:**
- Produces:
  - `enum class DeckSpot : uint8_t { Payload, Volume, Pomodoro }`
  - `uint8_t deckKeepIndex(DeckSpot was, uint8_t index, uint8_t newDeckSize, bool hasVolume)`: the deck is payload cards, then Volume (when `hasVolume`), then Pomodoro, last.

- [ ] **Step 1: Write the failing test**

Replace the body of `main()` in `firmware/sim/tests/deck_test.cpp`:

```cpp
int main() {
  // Final review I2: the Pomodoro card is the last card; if you are on it, a
  // payload that grows or shrinks must leave you on it.
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 0, 1, false) == 0);   // bridge was down: deck of one
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 0, 3, false) == 2);   // bridge came back with 2 cards
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 2, 4, false) == 3);   // payload grew
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 3, 2, false) == 1);   // payload shrank
  // On a payload card: stay put while it exists, else wrap to the first.
  CHECK(deckKeepIndex(DeckSpot::Payload, 1, 4, false) == 1);
  CHECK(deckKeepIndex(DeckSpot::Payload, 2, 3, false) == 0);  // card 2 is now the Pomodoro slot
  CHECK(deckKeepIndex(DeckSpot::Payload, 5, 3, false) == 0);
  CHECK(deckKeepIndex(DeckSpot::Payload, 0, 1, false) == 0);

  // With the Volume card (a Mac is bonded): payload cards, Volume, Pomodoro.
  CHECK(deckKeepIndex(DeckSpot::Pomodoro, 1, 4, true) == 3);
  CHECK(deckKeepIndex(DeckSpot::Volume, 0, 4, true) == 2);    // payload arrived: still on Volume
  CHECK(deckKeepIndex(DeckSpot::Volume, 3, 2, true) == 0);    // payload gone: Volume is card 0
  CHECK(deckKeepIndex(DeckSpot::Volume, 2, 3, false) == 2);   // the bond went: to the Pomodoro
  CHECK(deckKeepIndex(DeckSpot::Payload, 1, 4, true) == 1);
  CHECK(deckKeepIndex(DeckSpot::Payload, 2, 4, true) == 0);   // index 2 is the Volume slot now
  CHECK(deckKeepIndex(DeckSpot::Payload, 0, 2, true) == 0);   // no payload cards left
  return checksDone("deck_test");
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd firmware/sim && make build/deck_test`
Expected: a compile error, `'DeckSpot' has not been declared`.

- [ ] **Step 3: Change `deck_util.h`**

Replace the comment above `deckKeepIndex` and the function itself with:

```cpp
// Which kind of card the viewer was on before the deck changed size.
enum class DeckSpot : uint8_t { Payload, Volume, Pomodoro };

// The deck is the payload cards, then the Volume card (while a Mac is bonded,
// `hasVolume`), then the Pomodoro card, always last. A viewer on either local
// card stays on it when the payload arrives, grows or shrinks; one on the
// Volume card when it goes away lands on the Pomodoro. A viewer on a payload
// card keeps it while it still exists and otherwise goes to the first.
inline uint8_t deckKeepIndex(DeckSpot was, uint8_t index, uint8_t newDeckSize, bool hasVolume) {
  if (was == DeckSpot::Pomodoro) return newDeckSize - 1;
  if (was == DeckSpot::Volume) return hasVolume ? newDeckSize - 2 : newDeckSize - 1;
  const int payloadCards = newDeckSize - 1 - (hasVolume ? 1 : 0);
  return index < payloadCards ? index : 0;
}
```

`main.cpp` still calls the old three-argument form until Task 7, so `make` (the SDL sim) and `pio run` will not build between Task 4 and Task 7. That is expected. Within those tasks, verify with `make test` only.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd firmware/sim && make test`
Expected: `deck_test: 15 checks, 0 failed`, and all other tests at 0 failed.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/deck_util.h firmware/sim/tests/deck_test.cpp
git commit -m "feat(firmware): deckKeepIndex keeps the viewer on either local card

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Draw the Volume card (`ui.h/.cpp`, `@vol-*` shots)

**Files:**
- Modify: `firmware/src/ui.h:1-22`
- Modify: `firmware/src/ui.cpp`: insert after `drawPomodoroCard` (ends at line ~695, before `// --- the settings sheets`), after `uiReplayPomodoro` (line 962), and in `uiDeckSize`/`uiPomodoroIndex`/`uiRender` (lines 975-1006)
- Modify: `firmware/sim/shot.cpp` (header comment line 9, new `volumeShot`, `renderSpecial`)

**Interfaces:**
- Consumes: `VolumeView`, `VolState`, `VOL_*` geometry and `volumeFillTop` from Task 2.
- Produces:
  - `uint8_t uiDeckSize(const Payload &p, bool volume = false)`
  - `uint8_t uiVolumeIndex(const Payload &p)`
  - `uint8_t uiPomodoroIndex(const Payload &p, bool volume = false)`
  - `void uiReplayVolume()`
  - `void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs, const PomoView &pomo, const BatteryView &bat, UiLink link = UiLink::None, const VolumeView *vol = nullptr)`: `vol == nullptr` means there is no Volume card in the deck.
  - shots `@vol-56|muted|full|zero|fixed|noout|long|none`.

The drawing is verified by looking at shots, not by unit tests (the repo's pattern for `ui.cpp`).

- [ ] **Step 1: Write the shot harness first (the "failing test")**

In `firmware/sim/shot.cpp`, change header comment line 9 to:
```cpp
//                                   # @portal, @ota, @ble-pair, @ble-wait, @pomo-ready|focus|paused|break|done|long|edit,
//                                   # @dev-edit, @vol-56|muted|full|zero|fixed|noout|long|none
```
Add before `renderSpecial`:
```cpp
// The Volume card in a named state, fed through the real VolumeSlider, with an
// empty payload (the deck is Volume, Pomodoro), drawn until the fill settles.
bool volumeShot(Display &lcd, const char *state) {
  MacVolume m{};
  m.known = true;
  m.canSet = m.canMute = true;
  snprintf(m.name, sizeof(m.name), "MacBook Pro Speakers");
  if (!strcmp(state, "56")) {
    m.level = 56;
  } else if (!strcmp(state, "muted")) {
    m.level = 56;
    m.muted = true;
  } else if (!strcmp(state, "full")) {
    m.level = 100;
  } else if (!strcmp(state, "zero")) {
    m.level = 0;
  } else if (!strcmp(state, "fixed")) {
    m.level = 100;
    m.canSet = false;
    snprintf(m.name, sizeof(m.name), "HDMI");
  } else if (!strcmp(state, "noout")) {
    m.level = VOLUME_NO_DEVICE;
    m.canSet = m.canMute = false;
    m.name[0] = '\0';
  } else if (!strcmp(state, "long")) {
    m.level = 30;
    snprintf(m.name, sizeof(m.name), "LG UltraFine Display Au");  // 23 bytes: the most the Mac sends
  } else if (!strcmp(state, "none")) {
    m = MacVolume{};
  } else {
    return false;
  }
  VolumeSlider s;
  s.fromMac(m, 0);
  const VolumeView vv = s.view();
  const Payload none{};
  const PomoView pv = Pomodoro(PomoConfig{25 * 60000u, 5 * 60000u, 15 * 60000u, 4}).view();
  for (int frame = 0; frame < 400; frame++) {
    uiRender(lcd, none, uiVolumeIndex(none), true, 4000, pv, batteryViewFromEnv(), linkFromEnv(), &vv);
    if (!uiAnimating()) break;
    delay(8);
  }
  return true;
}
```
In `renderSpecial`, add a branch before `} else if (!strncmp(name, "pomo-", 5)) {`:
```cpp
  } else if (!strncmp(name, "vol-", 4)) {
    if (!volumeShot(lcd, name + 4)) return false;
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cd firmware/sim && make build/cube-shot`
Expected: compile errors: `uiVolumeIndex` was not declared, `VolumeSlider` was not declared, and too many arguments to `uiRender`.

- [ ] **Step 3: Change `ui.h`**

Add `#include "volume_slider.h"` after `#include "pomodoro.h"`. Replace lines 10-13 (the deck comment and the two declarations) with:
```cpp
// The deck is the bridge's cards, then the local cards: the Volume card while
// a Mac is bonded (`volume`), then the Pomodoro timer, always last. The
// Pomodoro exists even with no payload (bridge down, no Mac): then it is the
// only card.
uint8_t uiDeckSize(const Payload &p, bool volume = false);       // always >= 1
uint8_t uiVolumeIndex(const Payload &p);                         // valid only when the card is in the deck
uint8_t uiPomodoroIndex(const Payload &p, bool volume = false);  // the last index
```
Replace the `uiRender` declaration (lines 21-22) with:
```cpp
// `vol`: the Volume card's state, or nullptr when the deck has no Volume card.
void uiRender(Display &lcd, const Payload &p, uint8_t index, bool online, uint32_t ageMs,
              const PomoView &pomo, const BatteryView &bat, UiLink link = UiLink::None,
              const VolumeView *vol = nullptr);
```
After the `uiReplay` declaration, add:
```cpp
// Makes the next uiRender of the Volume card sweep its fill in from empty.
void uiReplayVolume();
```

- [ ] **Step 4: Draw the card in `ui.cpp`**

Insert right after the closing `}` of `drawPomodoroCard` (before `// --- the settings sheets`):

```cpp
// --- the Volume card ---------------------------------------------------------
// One rounded pill filling from the bottom; volume_slider.h owns the geometry,
// shared with the hit-testing. The text and the speaker are drawn twice through
// a clip at the fill's edge: light over the track, dark where the fill covers
// them, so they read at any level.
constexpr uint32_t VOL_FILL = 0xCDB8FF;     // lavender: live
constexpr uint32_t VOL_FIXED = 0x4A5366;    // the output sets its own level (HDMI)
constexpr uint32_t VOL_ON_FILL = 0x231A3D;  // text and glyph where the fill covers them
constexpr int VOL_TEXT_MAX_W = 184;         // the name, clear of the pill's corners
constexpr int VOL_NAME_Y = VOL_PILL_Y + 26;
constexpr int VOL_NUM_Y = VOL_PILL_Y + 70;
constexpr int VOL_WORD_Y = VOL_PILL_Y + 104;

GaugeAnim g_volAnim;

// Clips to the screen above (pass 0) or below (pass 1) the fill edge. False
// when that part is empty, so the pass can be skipped.
bool clipSide(LovyanGFX *g, int edge, int pass) {
  if (pass == 0) {
    if (edge <= 0) return false;
    g->setClipRect(0, 0, LCD_WIDTH, edge);
  } else {
    if (edge >= LCD_HEIGHT) return false;
    g->setClipRect(0, edge, LCD_WIDTH, LCD_HEIGHT - edge);
  }
  return true;
}

// The caption: the output's name in capitals ("MAC VOLUME" when there is
// none), cut with "..." to fit between the pill's corners.
void volumeLabel(LovyanGFX *g, const char *name, char *out, size_t cap) {
  size_t n = 0;
  for (const char *c = name; *c && n + 1 < cap; c++) out[n++] = (*c >= 'a' && *c <= 'z') ? (char)(*c - 32) : *c;
  out[n] = '\0';
  if (!n) snprintf(out, cap, "MAC VOLUME");
  g->setFont(&V_S12.font);
  if (g->textWidth(out) <= VOL_TEXT_MAX_W) return;
  char cut[40] = "...";
  while (n > 0) {
    out[--n] = '\0';
    while (n > 0 && out[n - 1] == ' ') out[--n] = '\0';
    snprintf(cut, sizeof(cut), "%s...", out);
    if (g->textWidth(cut) <= VOL_TEXT_MAX_W) break;
  }
  snprintf(out, cap, "%s", cut);
}

// The speaker near the bottom of the pill: 0..3 waves by level, or a cross
// when muted (the spec's "slashed": a cross reads better at this size).
void drawSpeaker(LovyanGFX *g, uint8_t level, bool muted, uint16_t col) {
  const int x = VOL_SPK_CX - 14, y = VOL_SPK_CY;
  g->fillRect(x, y - 4, 6, 9, col);
  g->fillTriangle(x + 5, y - 4, x + 13, y - 11, x + 13, y + 11, col);
  g->fillTriangle(x + 5, y - 4, x + 13, y + 11, x + 5, y + 4, col);
  if (muted) {
    g->drawWideLine(x + 18, y - 7, x + 30, y + 7, 1.5f, col);
    g->drawWideLine(x + 18, y + 7, x + 30, y - 7, 1.5f, col);
    return;
  }
  const int waves = level == 0 ? 0 : level <= 33 ? 1 : level <= 66 ? 2 : 3;
  for (int w = 0; w < waves; w++) {
    const int r = 6 + 5 * w;
    g->fillArc(x + 13, y, r, r + 2, -40.0f, 40.0f, col);
  }
}

void drawVolumeCard(LovyanGFX *g, const VolumeView &v, bool online, uint32_t ageMs, const BatteryView &bat) {
  const uint32_t now = millis();
  const bool live = v.state == VolState::Live, fixed = v.state == VolState::Fixed;
  const bool idle = v.state == VolState::NoMac || v.state == VolState::NoOutput;
  const uint32_t rgb = fixed ? VOL_FIXED : v.muted ? POMO_MUTED : VOL_FILL;
  GaugeAnim &a = g_volAnim;
  if (v.tracking) {
    // Under the finger: no easing, the fill is where the finger is.
    if (!a.seen) {
      a.seen = true;
      a.colFrom = a.colTo = rgb;
      a.colStart = now - COLOR_MS;
    }
    a.from = a.to = a.shown = v.level;
    a.start = now;
    a.dur = 0;
    g_animating = true;
  }
  uint32_t shownRgb = rgb;
  const float shown = stepGauge(a, v.level, rgb, now, shownRgb);
  const int edge = volumeFillTop(shown);
  const int bottom = VOL_PILL_Y + VOL_PILL_H;

  g->fillSmoothRoundRect(VOL_PILL_X, VOL_PILL_Y, VOL_PILL_W, VOL_PILL_H, VOL_PILL_R, to565(RING_TRACK));
  if (edge < bottom) {
    // The same rounded shape, clipped to below the edge: smooth corners at any
    // level and a flat top.
    g->setClipRect(VOL_PILL_X, edge, VOL_PILL_W, bottom - edge);
    g->fillSmoothRoundRect(VOL_PILL_X, VOL_PILL_Y, VOL_PILL_W, VOL_PILL_H, VOL_PILL_R, to565(shownRgb));
    g->clearClipRect();
  }

  char label[32];
  volumeLabel(g, v.name, label, sizeof(label));
  char num[8];
  if (live) snprintf(num, sizeof(num), "%u", (unsigned)v.level);
  else snprintf(num, sizeof(num), "--");
  const char *word = v.state == VolState::NoMac      ? "NO MAC"
                     : v.state == VolState::NoOutput ? "NO OUTPUT"
                     : fixed                         ? "FIXED"
                     : v.muted                       ? "MUTED"
                                                     : "";
  const uint16_t onFill = fixed ? INK : to565(VOL_ON_FILL);
  g->setTextDatum(middle_center);
  for (int pass = 0; pass < 2; pass++) {
    if (!clipSide(g, edge, pass)) continue;
    const bool over = pass == 1;
    g->setFont(&V_S12.font);
    g->setTextColor(over ? onFill : DIM);
    g->drawString(label, LCD_WIDTH / 2, VOL_NAME_Y);
    g->setFont(&V_B44.font);
    g->setTextColor(over ? onFill : INK);
    g->drawString(num, LCD_WIDTH / 2, VOL_NUM_Y);
    if (*word) {
      g->setFont(&V_S12.font);
      g->setTextColor(over ? onFill : DIM);
      g->drawString(word, LCD_WIDTH / 2, VOL_WORD_Y);
    }
    drawSpeaker(g, v.level, v.muted, over ? onFill : idle ? FAINT : INK);
  }
  g->clearClipRect();
  drawTopBar(g, online, ageMs, bat);
}
```

After `void uiReplayPomodoro() { g_pomoAnim.seen = false; }`, add:
```cpp
void uiReplayVolume() { g_volAnim.seen = false; }
```

Replace `uiDeckSize` and `uiPomodoroIndex` with:
```cpp
uint8_t uiDeckSize(const Payload &p, bool volume) { return (p.valid ? p.nCards : 0) + (volume ? 2 : 1); }

uint8_t uiVolumeIndex(const Payload &p) { return p.valid ? p.nCards : 0; }

uint8_t uiPomodoroIndex(const Payload &p, bool volume) { return uiVolumeIndex(p) + (volume ? 1 : 0); }
```

In `uiRender`, change the signature to end with `const BatteryView &bat, UiLink link, const VolumeView *vol) {`, and change the deck lines and the first branch:
```cpp
  const bool hasVol = vol != nullptr;
  const uint8_t deck = uiDeckSize(p, hasVol);
  const uint8_t i = index % deck;
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);

  if (i == uiPomodoroIndex(p, hasVol)) {
    drawPomodoroCard(g, pomo, flash * ALERT_RING_MIX, online, ageMs, bat);
  } else if (hasVol && i == uiVolumeIndex(p)) {
    drawVolumeCard(g, *vol, online, ageMs, bat);
  } else {
```
(The rest of `uiRender` is unchanged. `drawDots(g, deck, i)` already counts the new card.)

- [ ] **Step 5: Build the shot tool and look at every state**

Run:
```bash
cd firmware/sim && make build/cube-shot && \
for s in 56 muted full zero fixed noout long none; do ./build/cube-shot build/shot @vol-$s || exit 1; done
```
Expected: `build/shot-vol-56.png` … `build/shot-vol-none.png` exist. Open each one with the Read tool and check:
- `vol-56`: the lavender fill reaches a little above the middle, and the name and the number `56` read cleanly across the fill edge. The speaker has 2 waves, and there are 2 dots with the first one active.
- `vol-muted`: the fill is grey at the same height, `MUTED` sits under the number, and the speaker shows a cross with no waves.
- `vol-full`: the fill covers the whole pill with smooth corners, and all text is dark on lavender.
- `vol-zero`: the track is empty, the number is `0`, and the speaker has no waves.
- `vol-fixed`: a full dim-grey fill, `--`, `FIXED`, and the name `HDMI`, all text light.
- `vol-noout`: an empty track, `MAC VOLUME`, `--`, `NO OUTPUT`, and a faint speaker.
- `vol-long`: the name is cut with `...` inside the pill, not touching the corners.
- `vol-none`: an empty track, `MAC VOLUME`, `--`, `NO MAC`.

Tune the `VOL_*_Y` offsets, `VOL_TEXT_MAX_W` or the glyph sizes if anything collides, then re-run. The geometry constants in `volume_slider.h` stay as they are, because the slider tests pin them.

- [ ] **Step 6: Run the tests**

Run: `cd firmware/sim && make test`
Expected: all at 0 failed. (`make` and `pio run` still fail on `main.cpp`'s old `deckKeepIndex` call until Task 7.)

- [ ] **Step 7: Commit**

```bash
git add firmware/src/ui.h firmware/src/ui.cpp firmware/sim/shot.cpp
git commit -m "feat(firmware): draw the Volume card (pill fill, two-tone text, speaker) and @vol shots

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: BLE transport (`ble.h`, `net_ble.cpp`, sim and bench stand-ins)

**Files:**
- Modify: `firmware/src/ble.h` (include, and two declarations after `bleNotifyPomodoro`, line 44)
- Modify: `firmware/src/net_ble.cpp` (namespace globals near line 32, `onDisconnect` at line 73, a callback before line 165, the instance at line 168, `bleBegin` after line 197, functions after `bleNotifyPomodoro` at line 282)
- Modify: `firmware/sim/ble_sim.cpp`
- Modify: `firmware/sim/bench.cpp` (globals near line 234, stubs after `bleNotifyPomodoro` at line 332)

**Interfaces:**
- Consumes: `MacVolume`, `volumeParse`, `volumeSame`, `volumeRequestEncode`, `VOLUME_FRAME_MAX`, `BLE_VOLUME_REQ_LEN` and `BLE_VOLUME_UUID` from Task 1.
- Produces:
  - `bool bleTakeVolume(MacVolume &out)`: main loop only. True when the state changed, including the change to `known=false` when the link drops.
  - `void bleSendVolume(uint8_t level, bool muted)`
  - the sim env `CUBE_VOLUME=<0..100>|muted|fixed|none`
  - bench globals `g_volRequests` and `g_volEcho`, which Task 7 uses.

- [ ] **Step 1: Declare in `ble.h`**

Add `#include "volume_frame.h"` after `#include "payload.h"`. After the `bleNotifyPomodoro` declaration, add:
```cpp
// The Mac's output volume (Volume characteristic, fw_rev 5). True when it
// changed since the last call, including the change to unknown (known=false)
// when the link drops. Main loop only.
bool bleTakeVolume(MacVolume &out);
// Asks the Mac to set its output: Control `05 <level> <muted>`. Fire and forget.
void bleSendVolume(uint8_t level, bool muted);
```

- [ ] **Step 2: Implement in `net_ble.cpp`**

In the anonymous namespace, after `portMUX_TYPE g_mux = ...;` (line 32), add:
```cpp
// The latest Volume write, copied out of the NimBLE task; bleTakeVolume() parses it.
uint8_t g_volPending[VOLUME_FRAME_MAX];
size_t g_volLen = 0;
volatile bool g_volReady = false;
volatile bool g_volDropped = false;  // the link dropped: forget the Mac's state
MacVolume g_vol{};                   // main loop only
```
In `ServerCb::onDisconnect`, after `g_secured = false;`, add:
```cpp
    portENTER_CRITICAL(&g_mux);
    g_volReady = false;
    g_volDropped = true;
    portEXIT_CRITICAL(&g_mux);
```
Before `struct ControlCb`, add:
```cpp
struct VolumeCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    const NimBLEAttValue v = chr->getValue();
    if (v.size() == 0 || v.size() > VOLUME_FRAME_MAX) return;
    portENTER_CRITICAL(&g_mux);
    memcpy(g_volPending, v.data(), v.size());
    g_volLen = v.size();
    g_volReady = true;
    portEXIT_CRITICAL(&g_mux);
  }
};
```
After `SettingsCb g_settingsCb;`, add `VolumeCb g_volumeCb;`.

In `bleBegin`, after `publishSettings();` (line 197), add:
```cpp
  NimBLECharacteristic *volume = svc->createCharacteristic(
      BLE_VOLUME_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  volume->setCallbacks(&g_volumeCb);
```
After `bleNotifyPomodoro`, add:
```cpp
bool bleTakeVolume(MacVolume &out) {
  uint8_t buf[VOLUME_FRAME_MAX];
  size_t len = 0;
  bool ready, dropped;
  portENTER_CRITICAL(&g_mux);
  ready = g_volReady;
  dropped = g_volDropped;
  if (ready) {
    len = g_volLen;
    memcpy(buf, g_volPending, len);
  }
  g_volReady = g_volDropped = false;
  portEXIT_CRITICAL(&g_mux);

  bool changed = false;
  if (dropped && g_vol.known) {
    g_vol = MacVolume{};
    changed = true;
  }
  if (ready) {
    MacVolume v = g_vol;
    if (!volumeParse(buf, len, v)) Serial.println("[ble] volume write rejected");
    else if (!volumeSame(v, g_vol)) {
      g_vol = v;
      changed = true;
    }
  }
  out = g_vol;
  return changed;
}

void bleSendVolume(uint8_t level, bool muted) {
  uint8_t m[BLE_VOLUME_REQ_LEN];
  volumeRequestEncode(level, muted, m);
  notifyControl(m, sizeof(m));
}
```
A drop followed by a new write in the same pass (a quick reconnect) is handled in order: the drop clears the state, then the write sets it.

- [ ] **Step 3: The simulator stand-in, `sim/ble_sim.cpp`**

Add these lines to the header comment after the `pair` line:
```cpp
// CUBE_VOLUME (only while bonded) is what the Mac reports for the Volume card,
// once: a level 0..100, `muted` (56, muted), `fixed` (an HDMI output), or
// `none` / unset (no Volume write: the card shows NO MAC).
```
Add `#include <cstdio>` with the other includes. At the end of the file, add:
```cpp
bool bleTakeVolume(MacVolume &out) {
  static bool sent = false;
  if (sent || !bleBonded()) return false;
  const char *e = getenv("CUBE_VOLUME");
  if (!e || !strcmp(e, "none")) return false;
  MacVolume v{};
  v.known = true;
  v.canSet = v.canMute = true;
  snprintf(v.name, sizeof(v.name), "MacBook Pro Speakers");
  if (!strcmp(e, "muted")) {
    v.level = 56;
    v.muted = true;
  } else if (!strcmp(e, "fixed")) {
    v.level = 100;
    v.canSet = false;
    snprintf(v.name, sizeof(v.name), "HDMI");
  } else {
    v.level = (uint8_t)constrain(atoi(e), 0, 100);
  }
  sent = true;
  out = v;
  return true;
}

void bleSendVolume(uint8_t level, bool muted) {
  Serial.printf("[ble] (sim) volume %u%s\n", (unsigned)level, muted ? " muted" : "");
}
```

- [ ] **Step 4: The bench stand-in, `sim/bench.cpp`**

After `uint32_t g_bleLastPush = 0, g_bleLastGood = 0;` (line 234), add:
```cpp
// The Mac's side of the Volume card: its state once the link is live, then an
// echo of each request 300 ms later (about how slow the real link is).
uint32_t g_volEchoAt = 0;
bool g_volSent = false;
uint32_t g_volRequests = 0;
MacVolume g_volEcho{};
```
After `void bleNotifyPomodoro(uint8_t, uint8_t) {}`, add:
```cpp
bool bleTakeVolume(MacVolume &out) {
  if (!S.bleLive || !g_running) return false;
  if (!g_volSent) {
    g_volSent = true;
    g_volEcho = MacVolume{};
    g_volEcho.known = true;
    g_volEcho.level = 40;
    g_volEcho.canSet = g_volEcho.canMute = true;
    snprintf(g_volEcho.name, sizeof(g_volEcho.name), "MacBook Pro Speakers");
    out = g_volEcho;
    return true;
  }
  if (g_volEchoAt && millis() - g_volEchoAt >= 300) {
    g_volEchoAt = 0;
    out = g_volEcho;
    return true;
  }
  return false;
}

void bleSendVolume(uint8_t level, bool muted) {
  g_volRequests++;
  g_volEcho.level = level;
  g_volEcho.muted = muted;
  if (!g_volEchoAt) g_volEchoAt = millis() ? millis() : 1;
}
```

- [ ] **Step 5: Run the tests**

Run: `cd firmware/sim && make test`
Expected: all at 0 failed. The SDL sim, the bench and `pio run` link only after Task 7, which wires `main.cpp`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/ble.h firmware/src/net_ble.cpp firmware/sim/ble_sim.cpp firmware/sim/bench.cpp
git commit -m "feat(firmware): Volume characteristic and Control 05 over BLE, with sim and bench stand-ins

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Wire it into `main.cpp`, plus the bench scenario

**Files:**
- Modify: `firmware/src/main.cpp`: include (line 33), globals after line 71, `step()` (lines 143-152), `pollTouch()` (lines 277-312), end of `setup()` (line 392), `loop()` (lines 407, 443-466, 469, 476, 531-532)
- Modify: `firmware/sim/bench.cpp`: usage comment (line 19), `makeScenario` (before `} else if (name == "sleep")`), the failure checks (after the `mustSleep` check), `runRender` cases
- Modify: `firmware/sim/Makefile:93` (`BENCH_RUNS`)

**Interfaces:**
- Consumes:
  - `VolumeSlider` (Task 2)
  - `Gesture::DragStart/Drag/DragEnd`, `gestures.update(..., dragMode)`, `lastY()` (Task 3)
  - `DeckSpot`, `deckKeepIndex(4 args)` (Task 4)
  - `uiDeckSize/uiVolumeIndex/uiPomodoroIndex/uiReplayVolume/uiRender(..., vol)` (Task 5)
  - `bleTakeVolume`, `bleSendVolume`, bench `g_volRequests` (Task 6)
- Produces: the finished firmware feature, and the bench scenario `volume`.

- [ ] **Step 1: Add the bench scenario and its check (the failing test)**

In `firmware/sim/bench.cpp`, change usage line 19 to:
```cpp
//   ./build/cube-bench <desk|linked|pomodoro|browse|editor|sleep|volume|render> [slowdown]
```
In `makeScenario`, before `} else if (name == "sleep") {`, add:
```cpp
  } else if (name == "volume") {
    s.what = "Volume card: a drag up the pill, then a tap on the speaker (the Mac echoes each)";
    s.seconds = 6;
    s.strokes.push_back(swipe(1000, 50, 150, 190, 150));       // swipe right: Pomodoro -> Volume
    s.strokes.push_back(Stroke{2000, 600, 120, 230, 120, 70});  // drag from low to high
    s.strokes.push_back(tapAt(3500, VOL_SPK_CX, VOL_SPK_CY));  // the speaker: mute
```
After the `if (S.mustSleep && g_backlight != 0) { ... }` block, add:
```cpp
  if (!strcmp(S.name, "volume") && g_volRequests < 2) {
    fprintf(stderr, "%s: %u volume requests reached the Mac, expected the drag's and the mute's\n", S.name,
            (unsigned)g_volRequests);
    return 1;
  }
```
In `runRender`, after `const BatteryView bv = batteryView();`, add:
```cpp
  VolumeSlider volume;
  MacVolume mv{};
  mv.known = true;
  mv.level = 56;
  mv.canSet = mv.canMute = true;
  snprintf(mv.name, sizeof(mv.name), "MacBook Pro Speakers");
  volume.fromMac(mv, 0);
  const VolumeView vv = volume.view();
```
Change the `"pomodoro card"` case's index from `1` to `uiPomodoroIndex(p)`, and add a case after it:
```cpp
      {"volume card", [&] { uiRender(lcd, p, uiVolumeIndex(p), true, 2000, pomo.view(), bv, UiLink::Ble, &vv); }},
```
In `firmware/sim/Makefile` line 93:
```make
BENCH_RUNS ?= desk linked pomodoro browse editor sleep volume
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cd firmware/sim && make bench`
Expected: a build failure in `main.cpp` (`deckKeepIndex` called with 3 arguments).

- [ ] **Step 3: Globals and helpers in `main.cpp`**

Add `#include "volume_slider.h"` after `#include "ui.h"`. After `bool dirty = true;` (line 71), add:
```cpp
// The Volume card: in the deck while a Mac is bonded, between the payload cards
// and the Pomodoro. `deckVolume` is what the deck was last built with, so a
// bond coming or going can keep the viewer on their card.
VolumeSlider volSlider;
bool deckVolume = false;
bool volumeShown() { return bleBonded(); }
uint8_t deckSize() { return uiDeckSize(payload, deckVolume); }
uint8_t pomoIndex() { return uiPomodoroIndex(payload, deckVolume); }
bool onVolumeCard() { return deckVolume && cardIndex == uiVolumeIndex(payload); }
DeckSpot deckSpot() {
  return cardIndex == pomoIndex() ? DeckSpot::Pomodoro : onVolumeCard() ? DeckSpot::Volume : DeckSpot::Payload;
}
```

- [ ] **Step 4: `step()`**

Replace its body after `lastRotate = millis();` with:
```cpp
  const uint8_t n = deckSize();
  if (n <= 1) return;
  cardIndex = (cardIndex + n + delta) % n;
  if (cardIndex == pomoIndex()) uiReplayPomodoro();
  else if (onVolumeCard()) uiReplayVolume();
  else uiReplay(cardIndex);
  dirty = true;
```

- [ ] **Step 5: `pollTouch()`**

Replace from `const bool onPomodoro = ...` through the line before `switch (gesture) {` with:
```cpp
  const bool onPomodoro = cardIndex == pomoIndex();
  // On the Volume card a vertical move is the volume, not a swipe (gesture.h).
  const bool onVolume = onVolumeCard() && !editing;
  const Gesture gesture = gestures.update(down, x, y, now, onPomodoro && !editing, onVolume);
  // The deck left the card under a dragging finger (an alert pulled it to the
  // Pomodoro): let go, so the last level is still sent.
  if (volSlider.held() && !onVolume) {
    volSlider.release(now);
    dirty = true;
  }
  // While the panel slides in its buttons are not where the hit-test puts
  // them, so nothing is pressed until it has landed.
  if (editing && uiEditorSliding()) return;
  if (editing) {
    if (editingDevice) deviceEditorGesture(gesture);
    else editorGesture(gesture);
    return;
  }
  if (onVolume) {
    bool acted = false;
    switch (gesture) {
      case Gesture::DragStart:
        if (volSlider.grab()) {
          volSlider.drag(gestures.lastY());
          acted = true;
        }
        break;
      case Gesture::Drag:
        acted = volSlider.drag(gestures.lastY());
        break;
      case Gesture::DragEnd:
        acted = volSlider.release(now);
        break;
      case Gesture::Tap:
        acted = volSlider.tap(gestures.startX(), gestures.startY(), now);
        break;
      default:
        break;
    }
    if (acted) {
      lastRotate = now;  // touching the card holds off auto-advance, as a swipe does
      dirty = true;
    }
  }
```
In the main `switch`, change the `SwipeDown` case to:
```cpp
    case Gesture::SwipeDown:  // open the display settings panel, from any card but Volume
      if (!onVolume && devEditorMayOpen(editing)) openDeviceEditor();
      break;
```

- [ ] **Step 6: End of `setup()`**

After `lastRotate = millis();` (line 392), add:
```cpp
  // A bonded cube's deck has the Volume card before the Pomodoro; boot still
  // lands on the Pomodoro, as it did when it was the only card.
  deckVolume = volumeShown();
  cardIndex = pomoIndex();
```

- [ ] **Step 7: `loop()`**

1. In the alert block (line 407): `cardIndex = pomoIndex();`
2. Replace `const bool wasOnPomodoro = cardIndex == uiPomodoroIndex(payload);` (line 443) with:
```cpp
  // The card follows the bond. Settled before `was`, so a payload arriving in
  // the same pass keeps the viewer where this left them.
  if (volumeShown() != deckVolume) {
    const DeckSpot spot = deckSpot();
    deckVolume = !deckVolume;
    cardIndex = deckKeepIndex(spot, cardIndex, deckSize(), deckVolume);
    if (onVolumeCard()) uiReplayVolume();
    dirty = true;
  }
  const DeckSpot was = deckSpot();
```
3. After `handleBleSettings();`, add:
```cpp
  // The Mac's volume. Not a reading: it never keeps the screen awake.
  MacVolume macVol{};
  if (bleTakeVolume(macVol)) {
    volSlider.fromMac(macVol, now);
    dirty = true;
  }
  if (volSlider.tick(now)) dirty = true;
  uint8_t volLevel = 0;
  bool volMuted = false;
  if (volSlider.takeSend(now, volLevel, volMuted)) bleSendVolume(volLevel, volMuted);
```
4. Line 464: `cardIndex = deckKeepIndex(was, cardIndex, deckSize(), deckVolume);`
5. Auto-advance (line 469): `if (rotateMs() > 0 && !editing && !volSlider.held() && now - lastRotate >= rotateMs()) {`
6. Pomodoro redraw (line 476): `if (cardIndex == pomoIndex() && pv.displaySec != lastPomoSec) {`
7. The deck draw (lines 531-532, the `else uiRender(...)` after the editor branch):
```cpp
    else {
      const VolumeView vv = volSlider.view();
      uiRender(lcd, payload, cardIndex, td.bleLive || netOnline(), age, pv, bv,
               td.bleLive ? UiLink::Ble : (wifiUp && netOnline() ? UiLink::Wifi : UiLink::None),
               deckVolume ? &vv : nullptr);
    }
```

The CPU clock needs no change. `drawVolumeCard` sets `g_animating` while a finger holds the fill, so `uiAnimating()` keeps 240 MHz and the `FRAME_MS` pacing for the whole drag.

Check that no old call is left:
```bash
grep -n "uiPomodoroIndex(payload)\|uiDeckSize(payload)\|wasOnPomodoro" firmware/src/main.cpp
```
Expected: no output.

- [ ] **Step 8: Run everything**

```bash
cd firmware/sim && make test && make bench && make && \
CUBE_BLE=stale CUBE_VOLUME=56 ./build/cube-shot build/shot @vol-56
cd .. && pio run
```
Expected:
- `make test`: all at 0 failed.
- `make bench` prints a `volume` row, exits 0, and every other scenario still passes its checks (`linked` and `sleep` still sleep, and no stale-panel or stale-Settings failure). The render table gains a `volume card` row.
- `make` builds the SDL sim.
- `pio run`: SUCCESS. This is the gnu++11 compile of `volume_frame.h` and `volume_slider.h`.

- [ ] **Step 9: Try it in the simulator**

Run: `cd firmware/sim && CUBE_BLE=live CUBE_VOLUME=40 make run` (needs the bridge running for the payload; without it the deck is Volume, Pomodoro).

Swipe right from the Pomodoro card to reach the Volume card, then check each of these:
- A vertical mouse drag moves the fill under the cursor and prints `[ble] (sim) volume N` lines.
- A click in the pill jumps the level.
- A click on the speaker prints `muted`.
- A horizontal drag changes card.
- A swipe down on the Volume card does not open the display panel, and on the usage card it still does.

- [ ] **Step 10: Commit**

```bash
git add firmware/src/main.cpp firmware/sim/bench.cpp firmware/sim/Makefile
git commit -m "feat(firmware): Volume card in the deck while a Mac is bonded

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Mac wire format (`Volume.swift`, Control `05`)

**Files:**
- Create: `mac-helper/Sources/CubeLinkCore/Volume.swift`
- Create: `mac-helper/Tests/CubeLinkCoreTests/VolumeTests.swift`
- Modify: `mac-helper/Sources/CubeLinkCore/Protocol.swift` (after line 15, enum at line 59, parse after line 72)
- Modify: `mac-helper/Sources/CubeLinkCore/CubeLink.swift` (line 13, switch at line 271)

**Interfaces:**
- Consumes: the `vol_request` and `vol_state` fixture lines (Task 1).
- Produces:
  - `public struct MacVolume: Equatable { level: UInt8?; muted, canSet, canMute: Bool; name: String }`, with `init` and `static let noDevice`
  - `public func foldVolumeName(_:) -> String` and `public func encodeVolume(_:) -> Data`
  - `CubeProtocol.volumeUUID`
  - `ControlMessage.volumeRequest(level: UInt8, muted: Bool)`
  - `CubeLink.onVolumeRequest: ((UInt8, Bool) -> Void)?`

- [ ] **Step 1: Write the failing tests**

Create `mac-helper/Tests/CubeLinkCoreTests/VolumeTests.swift`:

```swift
import Foundation
import Testing
@testable import CubeLinkCore

/// The fixture lines of one kind, without the key. A missing fixture fails the count checks below.
private func volumeFixture(_ kind: String) throws -> [Substring] {
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<4 { root.deleteLastPathComponent() }
    let text = try String(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/ble-frames.txt"), encoding: .utf8)
    return text.split(separator: "\n").filter { $0.hasPrefix(kind + " ") }.map { $0.dropFirst(kind.count + 1) }
}

@Test func volumeRequestMatchesTheFirmwareFixture() throws {
    let lines = try volumeFixture("vol_request")
    for line in lines {
        let f = line.split(separator: " ")
        let bytes = f.dropFirst(2).map { UInt8($0, radix: 16)! }
        #expect(ControlMessage.parse(Data(bytes)) == .volumeRequest(level: UInt8(f[0])!, muted: f[1] == "1"))
    }
    #expect(lines.count == 3)
}

@Test func volumeStateMatchesTheFirmwareFixture() throws {
    let lines = try volumeFixture("vol_state")
    for line in lines {
        let q0 = line.firstIndex(of: "\"")!
        let q1 = line[line.index(after: q0)...].firstIndex(of: "\"")!
        let head = line[..<q0].split(separator: " ")
        let name = String(line[line.index(after: q0)..<q1])
        let bytes = line[line.index(after: q1)...].split(separator: " ").map { UInt8($0, radix: 16)! }
        let flags = UInt8(head[1], radix: 16)!
        let v = MacVolume(level: head[0] == "none" ? nil : UInt8(head[0])!,
                          muted: flags & 1 != 0, canSet: flags & 2 != 0, canMute: flags & 4 != 0, name: name)
        #expect(encodeVolume(v) == Data(bytes))
    }
    #expect(lines.count == 4)
}

@Test func parsesVolumeRequests() {
    #expect(ControlMessage.parse(Data([0x05, 100, 1])) == .volumeRequest(level: 100, muted: true))
    #expect(ControlMessage.parse(Data([0x05, 0, 0])) == .volumeRequest(level: 0, muted: false))
    #expect(ControlMessage.parse(Data([0x05])) == nil)           // no level
    #expect(ControlMessage.parse(Data([0x05, 0x10])) == nil)     // no muted byte
    #expect(ControlMessage.parse(Data([0x05, 101, 0])) == nil)   // level over 100
    #expect(ControlMessage.parse(Data([0x05, 50, 2])) == nil)    // muted is 0 or 1
}

// Review Focus 5: the cube's fonts are printable ASCII and it rejects anything
// else, so the name must be folded here.
@Test func foldsDeviceNamesForTheCube() {
    #expect(foldVolumeName("Café’s Speakers") == "Cafe's Speakers")
    #expect(foldVolumeName("🎧 Studio") == "Studio")
    #expect(foldVolumeName("A Very Long Output Device Name") == "A Very Long Output Devi")
    #expect(foldVolumeName("") == "")
    let d = encodeVolume(MacVolume(level: 150, muted: false, canSet: true, canMute: true,
                                   name: "Ünïcödé — Output Device Long"))
    #expect(d[1] == 100)                                        // level clamped
    #expect(d.count <= 26)
    #expect(d.dropFirst(3).allSatisfy { $0 >= 0x20 && $0 <= 0x7E })
    #expect(encodeVolume(.noDevice) == Data([1, 0xFF, 0]))
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cd mac-helper && swift test --filter VolumeTests`
Expected: compile errors: `cannot find 'MacVolume' in scope`, and no member `volumeRequest`.

- [ ] **Step 3: Write `Volume.swift`**

Create `mac-helper/Sources/CubeLinkCore/Volume.swift`:

```swift
import Foundation

/// The Mac's default output as the cube's Volume card shows it. `level` nil = no output device.
public struct MacVolume: Equatable {
    public var level: UInt8?
    public var muted: Bool
    public var canSet: Bool
    public var canMute: Bool
    public var name: String

    public init(level: UInt8?, muted: Bool, canSet: Bool, canMute: Bool, name: String) {
        self.level = level
        self.muted = muted
        self.canSet = canSet
        self.canMute = canMute
        self.name = name
    }

    public static let noDevice = MacVolume(level: nil, muted: false, canSet: false, canMute: false, name: "")
}

/// The Volume characteristic's layout: `[ver=1][level][flags][name...]` (docs/ble-protocol.md).
enum VolumeFrame {
    static let version: UInt8 = 1
    static let noDevice: UInt8 = 0xFF
    static let maxName = 23
}

/// The device name as the cube can draw it and will accept: printable ASCII,
/// diacritics stripped, anything else dropped, at most 23 bytes.
public func foldVolumeName(_ s: String) -> String {
    let folded = s.replacingOccurrences(of: "\u{2019}", with: "'")
        .folding(options: .diacriticInsensitive, locale: nil)
    var ascii = String.UnicodeScalarView()
    ascii.append(contentsOf: folded.unicodeScalars.filter { $0.value >= 0x20 && $0.value <= 0x7E })
    let trimmed = String(ascii).trimmingCharacters(in: .whitespaces)
    return String(trimmed.prefix(VolumeFrame.maxName)).trimmingCharacters(in: .whitespaces)
}

/// The value written to the cube's Volume characteristic.
public func encodeVolume(_ v: MacVolume) -> Data {
    var flags: UInt8 = 0
    if v.muted { flags |= 1 }
    if v.canSet { flags |= 2 }
    if v.canMute { flags |= 4 }
    let level = v.level.map { min($0, 100) } ?? VolumeFrame.noDevice
    return Data([VolumeFrame.version, level, flags]) + Data(foldVolumeName(v.name).utf8)
}
```

- [ ] **Step 4: `Protocol.swift`**

After `settingsUUID` (line 15), add:
```swift
    public static let volumeUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A06"
```
In `ControlMessage`, after `case pomodoroEnded(...)`, add:
```swift
    /// Set the Mac's output: an absolute level 0...100 and mute (fw_rev 5).
    case volumeRequest(level: UInt8, muted: Bool)
```
In `parse`, before `default: return nil`, add:
```swift
        case 0x05:
            guard b.count >= 3, b[1] <= 100, b[2] <= 1 else { return nil }
            return .volumeRequest(level: b[1], muted: b[2] == 1)
```

- [ ] **Step 5: `CubeLink.swift`, in the same task so the exhaustive `switch msg` still compiles**

After `public var onPomodoroEnded: ...` (line 13), add:
```swift
    /// The cube asks for the Mac's output to be set: (level 0...100, muted).
    public var onVolumeRequest: ((UInt8, Bool) -> Void)?
```
In the Control `switch msg` (line 271), after the `.pomodoroEnded` arm, add:
```swift
            case .volumeRequest(let level, let muted):
                onVolumeRequest?(level, muted)
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `cd mac-helper && swift test`
Expected: all tests pass, including the four new ones and the existing `FrameTests` fixture tests.

- [ ] **Step 7: Commit**

```bash
git add mac-helper/Sources/CubeLinkCore/Volume.swift mac-helper/Sources/CubeLinkCore/Protocol.swift \
        mac-helper/Sources/CubeLinkCore/CubeLink.swift mac-helper/Tests/CubeLinkCoreTests/VolumeTests.swift
git commit -m "feat(mac-helper): Volume encoding, name folding and Control 05 parsing

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: CoreAudio and the link (`SystemVolume`, `CubeLink`, `main.swift`)

**Files:**
- Create: `mac-helper/Sources/ClaudeCubeLink/SystemVolume.swift`
- Modify: `mac-helper/Sources/CubeLinkCore/CubeLink.swift` (properties near lines 13, 32 and 44; `discoverCharacteristics` at line 213; the discovery loop at line 226; `didUpdateNotificationStateFor` at line 298; `setReady` at line 332)
- Modify: `mac-helper/Sources/ClaudeCubeLink/main.swift` (after `notifier.start()`, line 176). This file already has user edits: stage it with `git add -p`.

**Interfaces:**
- Consumes: `MacVolume`, `encodeVolume`, `CubeProtocol.volumeUUID` and `CubeLink.onVolumeRequest` (Task 8).
- Produces:
  - `CubeLink.onReady: (() -> Void)?` and `CubeLink.writeVolume(_ d: Data) -> Bool`
  - `final class SystemVolume { var onChange; func start(); func current() -> MacVolume; func apply(level: UInt8, muted: Bool) }`

There is no unit test here: this is the CoreAudio and CoreBluetooth glue, which the package leaves untested by design (`mac-helper/CLAUDE.md`). Verification is `swift build`, `swift test`, and a manual run.

- [ ] **Step 1: `CubeLink.swift`**

After `onVolumeRequest`, add:
```swift
    /// The link became ready (Control subscribed). The Volume state is written from here.
    public var onReady: (() -> Void)?
```
Next to `private var settingsChar: CBCharacteristic?`, add:
```swift
    private var volumeChar: CBCharacteristic?
    private var volumeMissingLogged = false
```
Next to `private let settingsID = ...`, add:
```swift
    private let volumeID = CBUUID(string: CubeProtocol.volumeUUID)
```
After `refreshSettings()`'s function body, add:
```swift
    /// Writes the Mac's output state to the cube's Volume characteristic. False when not ready,
    /// or when the cube's firmware predates the characteristic (logged once per connection).
    @discardableResult
    public func writeVolume(_ d: Data) -> Bool {
        guard isReady, let p = peripheral else { return false }
        guard let ch = volumeChar else {
            if !volumeMissingLogged {
                volumeMissingLogged = true
                log("cube has no Volume characteristic (firmware before rev 5): volume card off")
            }
            return false
        }
        Trace.log("ble", "volume write: \([UInt8](d))")
        p.writeValue(d, for: ch, type: .withResponse)
        return true
    }
```
Change line 213 to:
```swift
        p.discoverCharacteristics([payloadID, controlID, infoID, settingsID, volumeID], for: svc)
```
In the discovery loop, after the `settingsID` line, add:
```swift
            else if ch.uuid == volumeID { volumeChar = ch }  // optional: firmware before rev 5 has none
```
In `didUpdateNotificationStateFor`, after `refreshSettings()`, add:
```swift
            onReady?()
```
In `setReady`, inside `if !ready {`, after `settingsChar = nil`, add:
```swift
            volumeChar = nil
            volumeMissingLogged = false
```

- [ ] **Step 2: Write `SystemVolume.swift`**

Create `mac-helper/Sources/ClaudeCubeLink/SystemVolume.swift`:

```swift
import AudioToolbox
import CoreAudio
import CubeLinkCore
import Foundation

/// The Mac's default output device: its volume, mute and name, and changes to any of them or
/// to which device is the default. Needs no permission prompt. Main queue only.
final class SystemVolume {
    /// Called on the main queue 30 ms after the last of a burst of changes.
    var onChange: ((MacVolume) -> Void)?

    private static let unknown = AudioObjectID(kAudioObjectUnknown)
    private static let deviceProps: [AudioObjectPropertyAddress] = [
        address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume),
        address(kAudioDevicePropertyMute),
        address(kAudioObjectPropertyName, scope: kAudioObjectPropertyScopeGlobal),
    ]

    private var device = SystemVolume.unknown
    private var pending: DispatchWorkItem?
    // Stored as block types, so the same block object is passed to Add and Remove: Core Audio
    // matches listeners by block identity, and a Swift closure passed inline would be a new
    // block each time and never be removed.
    private lazy var changed: AudioObjectPropertyListenerBlock = { [weak self] _, _ in self?.schedule() }
    private lazy var outputChanged: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
        self?.follow()
        self?.schedule()
    }

    func start() {
        var a = Self.address(kAudioHardwarePropertyDefaultOutputDevice, scope: kAudioObjectPropertyScopeGlobal)
        let s = AudioObjectAddPropertyListenerBlock(AudioObjectID(kAudioObjectSystemObject), &a, .main, outputChanged)
        if s != noErr { Trace.log("volume", "default-output listener failed: \(s)") }
        follow()
    }

    /// The default output as the cube shows it.
    func current() -> MacVolume {
        guard device != Self.unknown else { return .noDevice }
        let volAddr = Self.address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume)
        let muteAddr = Self.address(kAudioDevicePropertyMute)
        let scalar: Float32? = read(volAddr)
        let mute: UInt32? = read(muteAddr)
        return MacVolume(
            level: UInt8((min(max(scalar ?? 1, 0), 1) * 100).rounded()),
            muted: (mute ?? 0) != 0,
            canSet: scalar != nil && settable(volAddr),
            canMute: mute != nil && settable(muteAddr),
            name: name())
    }

    /// Sets mute, then the volume (level / 100), each only where the device allows it.
    func apply(level: UInt8, muted: Bool) {
        guard device != Self.unknown else { return }
        var muteAddr = Self.address(kAudioDevicePropertyMute)
        if settable(muteAddr) {
            var m: UInt32 = muted ? 1 : 0
            let s = AudioObjectSetPropertyData(device, &muteAddr, 0, nil, UInt32(MemoryLayout<UInt32>.size), &m)
            if s != noErr { Trace.log("volume", "set mute failed: \(s)") }
        }
        var volAddr = Self.address(kAudioHardwareServiceDeviceProperty_VirtualMainVolume)
        if settable(volAddr) {
            var v = Float32(min(level, 100)) / 100
            let s = AudioObjectSetPropertyData(device, &volAddr, 0, nil, UInt32(MemoryLayout<Float32>.size), &v)
            if s != noErr { Trace.log("volume", "set volume failed: \(s)") }
        }
    }

    // MARK: helpers

    /// Moves the device listeners to the current default output.
    private func follow() {
        let next = Self.defaultOutput()
        guard next != device else { return }
        if device != Self.unknown {
            for prop in Self.deviceProps {
                var a = prop
                if AudioObjectHasProperty(device, &a) {
                    _ = AudioObjectRemovePropertyListenerBlock(device, &a, .main, changed)
                }
            }
        }
        device = next
        guard device != Self.unknown else { return }
        for prop in Self.deviceProps {
            var a = prop
            if AudioObjectHasProperty(device, &a) {
                _ = AudioObjectAddPropertyListenerBlock(device, &a, .main, changed)
            }
        }
    }

    private func schedule() {
        pending?.cancel()
        let w = DispatchWorkItem { [weak self] in
            guard let self else { return }
            self.onChange?(self.current())
        }
        pending = w
        DispatchQueue.main.asyncAfter(deadline: .now() + .milliseconds(30), execute: w)
    }

    private static func address(_ sel: AudioObjectPropertySelector,
                                scope: AudioObjectPropertyScope = kAudioDevicePropertyScopeOutput) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: sel, mScope: scope, mElement: kAudioObjectPropertyElementMain)
    }

    private static func defaultOutput() -> AudioObjectID {
        var a = address(kAudioHardwarePropertyDefaultOutputDevice, scope: kAudioObjectPropertyScopeGlobal)
        var id = unknown
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        let s = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &a, 0, nil, &size, &id)
        return s == noErr ? id : unknown
    }

    private func settable(_ addr: AudioObjectPropertyAddress) -> Bool {
        var a = addr
        guard AudioObjectHasProperty(device, &a) else { return false }
        var ok = DarwinBoolean(false)
        return AudioObjectIsPropertySettable(device, &a, &ok) == noErr && ok.boolValue
    }

    private func read<T>(_ addr: AudioObjectPropertyAddress) -> T? {
        var a = addr
        guard AudioObjectHasProperty(device, &a) else { return nil }
        var size = UInt32(MemoryLayout<T>.size)
        let p = UnsafeMutablePointer<T>.allocate(capacity: 1)
        defer { p.deallocate() }
        guard AudioObjectGetPropertyData(device, &a, 0, nil, &size, p) == noErr else { return nil }
        return p.pointee
    }

    private func name() -> String {
        var a = Self.address(kAudioObjectPropertyName, scope: kAudioObjectPropertyScopeGlobal)
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        var cf: Unmanaged<CFString>?
        guard AudioObjectGetPropertyData(device, &a, 0, nil, &size, &cf) == noErr, let n = cf else { return "" }
        return n.takeRetainedValue() as String
    }
}
```

- [ ] **Step 3: Wire it in `main.swift`**

After `notifier.start()` (line 176), add:
```swift
let systemVolume = SystemVolume()
systemVolume.onChange = { v in
    Trace.log("volume", "mac \(v.level.map(String.init) ?? "none")\(v.muted ? " muted" : "") \(v.name)")
    _ = link.writeVolume(encodeVolume(v))
}
link.onReady = { _ = link.writeVolume(encodeVolume(systemVolume.current())) }
link.onVolumeRequest = { level, muted in
    Trace.log("volume", "cube asks \(level)\(muted ? " muted" : "")")
    systemVolume.apply(level: level, muted: muted)
}
systemVolume.start()
```

- [ ] **Step 4: Build and test**

Run: `cd mac-helper && swift build && swift test`
Expected: both succeed with no new warnings in `SystemVolume.swift`. If the compiler warns about forming a raw pointer to `cf` in `name()`, change the read to `withUnsafeMutablePointer(to: &cf) { AudioObjectGetPropertyData(device, &a, 0, nil, &size, $0) }`.

- [ ] **Step 5: Smoke-test against the real audio system**

Run: `cd mac-helper && ./install.sh uninstall; CUBE_TRACE=1 swift run ClaudeCubeLink 2>&1 | grep --line-buffered "volume"`
Then press the keyboard volume keys, mute, and switch the output (System Settings > Sound).
Expected: `[trace:volume] mac N ...` lines, with the right level, ` muted`, and the device name. If no cube is connected, `writeVolume` returns false silently, so only the trace lines show. Stop the run with Ctrl-C, then `./install.sh install` to restore the installed copy.

- [ ] **Step 6: Commit**

```bash
git add mac-helper/Sources/ClaudeCubeLink/SystemVolume.swift mac-helper/Sources/CubeLinkCore/CubeLink.swift
git add -p mac-helper/Sources/ClaudeCubeLink/main.swift   # stage only the SystemVolume hunk
git commit -m "feat(mac-helper): mirror the Mac's output volume to the cube and apply its requests

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Docs and the hardware checklist

**Files:**
- Modify: `README.md` (insert before `### Pomodoro card`, line 270). It has user edits: use `git add -p`.
- Modify: `CLAUDE.md` (the deck bullet at line 67, the Transports paragraph, the simulator commands). It has user edits: use `git add -p`.
- Modify: `firmware/CLAUDE.md:34`
- Modify: `mac-helper/CLAUDE.md:27`, `:33`, `:59`. It has user edits: use `git add -p`.
- Modify: `docs/ble-acceptance.md` (append)

- [ ] **Step 1: `README.md`**

Insert before `### Pomodoro card`:
```markdown
### Volume card

While a Mac is paired, the deck has a Volume card just before the Pomodoro: one tall pill that fills from the bottom with the Mac's output volume, with the output's name above the number. **Drag** up or down anywhere on the pill to set the volume, the fill following your finger; **tap** the pill to jump to that height; **tap the speaker** to mute or unmute (any volume change also unmutes). Left and right swipes still change card; swipe down does not open the display panel on this card. Changes made on the Mac (keyboard, menu bar, switching to AirPods) show on the cube within about a third of a second. An output whose volume the Mac cannot set (HDMI, some USB devices) shows `FIXED`, and only mute works there; with the Mac's app not connected the card shows `NO MAC`. It needs the Mac app and firmware from the same release: an older app or cube leaves the card on `NO MAC`.
```

- [ ] **Step 2: Root `CLAUDE.md`**

Replace the deck bullet's opening, from "The deck is the payload cards **plus one local Pomodoro card, always last** (`uiDeckSize` / `uiPomodoroIndex` in `ui.cpp`). It is not in the contract, not counted against `MAX_CARDS`, and exists even with no payload.", with:
```
- The deck is the payload cards **plus the local cards: the Volume card while a Mac is bonded, then the Pomodoro card, always last** (`uiDeckSize` / `uiVolumeIndex` / `uiPomodoroIndex` in `ui.cpp`; `deckKeepIndex` in `deck_util.h` keeps a viewer on either local card as the deck changes). Neither is in the contract or counted against `MAX_CARDS`; the Pomodoro exists even with no payload.
```
After the Display panel paragraph, add:
```markdown
**Volume card.** Shows and sets the Mac's output volume over BLE only. The Mac writes `[1][level|FF][flags][name]` to the Volume characteristic (`volume_frame.h`, pure, host-tested) once it is subscribed and on every CoreAudio change (`SystemVolume.swift`, coalesced 30 ms); the cube asks with Control `05 <level> <muted>`. The card is a full-width pill (`drawVolumeCard`; geometry and the touch model are `volume_slider.h`, pure, host-tested): on it `GestureTracker::update` runs in drag mode (`dragMode`, 10 px axis lock: vertical = `DragStart`/`Drag`/`DragEnd`, horizontal still swipes), so swipe down does not open the display panel there. While a finger holds the fill and for 600 ms after, states from the Mac are parked (`VOLUME_HOLD_MS`), and requests go at most every 50 ms while dragging, the final value always. States: live, muted, `FIXED` (volume not settable: only mute), `NO OUTPUT`, `NO MAC` (link down: the state is forgotten on disconnect). Volume writes are not readings, so they never keep the screen awake. Local-only, not in `preview.html`. Preview: `./build/cube-shot build/shot @vol-56` (also `muted|full|zero|fixed|noout|long|none`); in `make run`, `CUBE_BLE=live CUBE_VOLUME=40|muted|fixed`.
```
In the Transports paragraph, after the sentence ending "BLE only, fire and forget.", add:
```
Control `05 <level> <muted>` (cube -> Mac) and the Volume characteristic (Mac -> cube, fw_rev 5) carry the Volume card.
```
In the simulator command block, after the `CUBE_ORIENT=2 make run` line, add:
```sh
CUBE_BLE=live CUBE_VOLUME=40 make run      # the Volume card with a Mac at 40% (also muted|fixed|none); requests print as [ble] (sim) volume
```
and change the `./build/cube-shot build/shot @portal` line's list to end with `@pomo-ready|focus|paused|break|done|long, @vol-56|muted|full|zero|fixed|noout|long|none`.

- [ ] **Step 3: `firmware/CLAUDE.md`**

In line 34's header-only list, add `volume_frame.h`, `volume_slider.h` after `deck_util.h`.

- [ ] **Step 4: `mac-helper/CLAUDE.md`**

- Line 27 becomes: `  - \`PushPolicy\`, \`Backoff\`, \`encodeFrames\`, \`ControlMessage.parse\`, \`encodeVolume\` and \`foldVolumeName\``
- In line 33, after "and \`Notifier\` posts banners", add: `; \`SystemVolume\` reads and sets the default output through CoreAudio and reports changes (coalesced 30 ms), and \`main.swift\` writes them to the cube with \`CubeLink.writeVolume\` (also once on \`onReady\`) and applies \`onVolumeRequest\``
- In line 59, change "and Control opcodes \`01\` to \`04\`" to "Control opcodes \`01\` to \`05\`, and the optional Volume characteristic (fw_rev 5; a cube without it is logged once per connection)".

- [ ] **Step 5: `docs/ble-acceptance.md`**

Append:
```markdown
## Volume card

- [ ] `AudioObjectGetPropertyData` reads `kAudioHardwareServiceDeviceProperty_VirtualMainVolume` on the built-in speakers: `CUBE_TRACE=1` shows `[trace:volume] mac N` matching the menu bar slider.
- [ ] Paired cube: the Volume card sits before the Pomodoro and shows the output name and level within a second of connecting.
- [ ] Drag up and down: the fill follows the finger with no lag, the Mac's volume follows within a few hundred ms, and the fill does not jump back after the lift.
- [ ] Tap-jump: a tap on the pill sets that level; a tap on the speaker mutes, a second unmutes; a drag on a muted output unmutes it.
- [ ] Keyboard volume keys and mute on the Mac: the card follows within about 300 ms.
- [ ] Switch the output to AirPods and back: the name and level change to that device's.
- [ ] An HDMI or other fixed output: `FIXED`, full grey fill, drags do nothing, the speaker mutes if the device allows it.
- [ ] Turn the Mac's Bluetooth off mid-drag: the card shows `NO MAC` at once and nothing is sent after; back on, the state returns.
- [ ] Horizontal swipes on the card still change card; a swipe down does not open the display panel there.
- [ ] An older Mac app (before this change) with this firmware: the card stays `NO MAC`, nothing else changes.
- [ ] This Mac app with older firmware (fw_rev 4): the log says "cube has no Volume characteristic" once per connection, and nothing else changes.
```

- [ ] **Step 6: Final verification across the branch**

```bash
cd firmware/sim && make test && make bench && make shot
cd .. && pio run
cd ../mac-helper && swift test && swift build
```
Expected: every test at 0 failed, the bench runs every scenario including `volume` with no failure line, `make shot` writes the payload cards (with the bridge running), `pio run` SUCCESS, and `swift test` all pass.

- [ ] **Step 7: Commit**

```bash
git add firmware/CLAUDE.md docs/ble-acceptance.md
git add -p README.md CLAUDE.md mac-helper/CLAUDE.md   # stage only the Volume card hunks
git commit -m "docs: Volume card, its protocol and the hardware checklist

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
