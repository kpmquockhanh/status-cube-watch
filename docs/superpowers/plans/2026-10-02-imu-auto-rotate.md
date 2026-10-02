# IMU auto-rotate (180°) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The cube watches its QMI8658C accelerometer and rotates the display 180° when it is turned upside down, remapping touch so swipes and taps still land where they are drawn.

**Architecture:** `lcd.setRotation(0|2)` rotates the existing full-screen sprite push in hardware, so `ui.cpp` and `preview.html` are untouched. A pure, host-tested `Orientation` classifier decides when to flip; a pure `touchToScreen` helper mirrors raw CST816 coordinates; a small `imu.cpp` driver (hardware-only, with a simulator stand-in) feeds it. `main.cpp` glues them together.

**Tech Stack:** C++17 Arduino/PlatformIO (ESP32-S3), LovyanGFX, `Wire`; host tests via `firmware/sim/Makefile` (plain compiler, no framework beyond `tests/check.h`). No new `lib_deps`.

**Spec:** `docs/superpowers/specs/2026-10-02-imu-auto-rotate-design.md`

## Global Constraints

- Always on: no setting, no NVS key, no portal/BLE field, no new row in the display panel.
- Portrait (rotation 0) and upside-down (rotation 2) only. No landscape, no flip-as-gesture.
- A flip needs `|up| > 0.6 g` **and** `|up| > |az|`, held for 1000 ms, where `up = IMU_UP_SIGN * ay` and `+1 g` means "screen top is up". Poll ~5 Hz (every 200 ms).
- IMU: QMI8658C at I2C `0x6B` on the shared `Wire` bus (already started by `Touch::begin()`); accelerometer only; gyro and INT pin unused.
- Local-only feature: not in the payload contract, not in `preview.html`.
- IMU motion is not activity: it never wakes the screen or resets the idle timer. Orientation is still tracked while asleep.
- Firmware code shared with the simulator must stay within `sim/Arduino.h` (millis/delay/Serial/min/max/constrain); hardware-only code gets a stand-in in `firmware/sim/`.
- Project rule: `.cpp` hardware layers have sim stand-ins; verification is host tests + `pio run` + the SDL window (there is no linter).

## Review Focus

Most likely first. Each line has a test (or explicit manual check) in the task that owns the code.

1. Cube lying flat, or tilted past 45° towards face-up, on a desk: must never flip. (Task 1: `testFlatNeverFlips`, `testTiltedFaceUpNeverFlips`, `testExactThresholdDoesNotFlip`)
2. A finger is on the screen when the cube is turned: no remap mid-gesture (no phantom swipe); the flip lands after the finger lifts. (Task 1: `testBusyDefersThenFlips`, `testBusyThenNoLongerWantedDoesNotFlip`; Task 2: remap is a pure per-sample function with no state)
3. IMU absent, erroring, or returning garbage (NaN): orientation stays put, touch and display keep working, one log line only. (Task 1: `testNanKeepsOrientation`; Task 3: `imuBegin()` false disables polling; Task 4 Step 6 runs the sim and the noble env build)
4. Cube powered up upside down: corrected about 1 s after boot, not never. (Task 1: `testFlipsAfterHold` from a cold start; Task 4 Step 7 `CUBE_ORIENT=2 make run`)
5. Cube flipped while the screen is asleep: the sleeping panel is not written to, and the first frame on wake is already the right way up. (Task 4: `applyRotation()` returns early while `!screenOn` and is called every pass; hardware checklist item in Task 5)

---

### Task 1: `Orientation` classifier

**Files:**
- Create: `firmware/src/orientation.h`
- Create: `firmware/sim/tests/orientation_test.cpp`
- Modify: `firmware/sim/Makefile` (add to `TESTS`)

**Interfaces:**
- Produces: `class Orientation` in `orientation.h`:
  - `uint8_t rotation() const` returns `0` or `2` (the value to pass to `lcd.setRotation`).
  - `bool update(uint32_t now, float up, float az, bool busy)` returns true when the rotation changed on this call. `up` is signed so `+1` = screen top is up; `az` is the axis out of the screen face.

- [ ] **Step 1: Branch and commit the spec and plan**

```bash
cd /Users/kpmquockhanh/code/claude-status-cube
git switch -c feat/imu-auto-rotate
git add docs/superpowers/specs/2026-10-02-imu-auto-rotate-design.md docs/superpowers/plans/2026-10-02-imu-auto-rotate.md
git commit -m "docs: IMU auto-rotate spec and implementation plan

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 2: Write the failing test**

Create `firmware/sim/tests/orientation_test.cpp`:

```cpp
#include <cmath>

#include "check.h"
#include "orientation.h"

namespace {

// up = -1: upside down. up = +1: upright. az = 0: standing on an edge.
bool feed(Orientation &o, uint32_t t, float up, float az = 0.0f, bool busy = false) {
  return o.update(t, up, az, busy);
}

void testStartsUpright() {
  Orientation o;
  CHECK(o.rotation() == 0);
}

void testFlipsAfterHold() {  // also the cold start: boots upside down, corrected after 1 s
  Orientation o;
  CHECK(!feed(o, 0, -1.0f));
  CHECK(!feed(o, 999, -1.0f));
  CHECK(feed(o, 1000, -1.0f));
  CHECK(o.rotation() == 2);
  CHECK(!feed(o, 1200, -1.0f));  // already flipped: no repeat
}

void testInterruptedHoldRestarts() {
  Orientation o;
  feed(o, 0, -1.0f);
  feed(o, 500, 0.0f);            // back to the dead zone: candidate cleared
  feed(o, 600, -1.0f);           // new candidate starts at 600
  CHECK(!feed(o, 1500, -1.0f));  // only 900 ms held
  CHECK(feed(o, 1600, -1.0f));
}

void testFlatNeverFlips() {
  Orientation o;
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o, t, -0.3f, 1.0f));
  CHECK(o.rotation() == 0);
  Orientation o2;  // face down is flat too
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o2, t, -0.3f, -1.0f));
  CHECK(o2.rotation() == 0);
}

void testTiltedFaceUpNeverFlips() {  // |up| is over 0.6 but the face still points more at the ceiling
  Orientation o;
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o, t, -0.7f, 0.9f));
  CHECK(o.rotation() == 0);
}

void testDeadZoneNeverFlips() {
  Orientation o;
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o, t, -0.5f, 0.1f));
  CHECK(o.rotation() == 0);
}

void testExactThresholdDoesNotFlip() {
  Orientation o;
  for (uint32_t t = 0; t < 10000; t += 200) CHECK(!feed(o, t, -0.6f, 0.0f));
  CHECK(o.rotation() == 0);
}

void testFlipsBackWithHysteresis() {
  Orientation o;
  feed(o, 0, -1.0f);
  CHECK(feed(o, 1000, -1.0f));
  CHECK(o.rotation() == 2);
  for (uint32_t t = 2000; t < 6000; t += 200) CHECK(!feed(o, t, 0.5f));  // not upright enough
  CHECK(o.rotation() == 2);
  CHECK(!feed(o, 7000, 1.0f));
  CHECK(feed(o, 8000, 1.0f));
  CHECK(o.rotation() == 0);
}

void testBusyDefersThenFlips() {
  Orientation o;
  feed(o, 0, -1.0f);
  CHECK(!feed(o, 2000, -1.0f, 0.0f, true));  // held long enough, but a finger is down
  CHECK(o.rotation() == 0);
  CHECK(feed(o, 2015, -1.0f, 0.0f, false));  // finger up, still wanted: flips at once
  CHECK(o.rotation() == 2);
}

void testBusyThenNoLongerWantedDoesNotFlip() {
  Orientation o;
  feed(o, 0, -1.0f);
  feed(o, 2000, -1.0f, 0.0f, true);
  CHECK(!feed(o, 2015, 1.0f, 0.0f, false));  // turned back upright meanwhile
  CHECK(o.rotation() == 0);
}

void testNanKeepsOrientation() {
  Orientation o;
  const float nan = std::nanf("");
  for (uint32_t t = 0; t < 5000; t += 200) CHECK(!feed(o, t, nan, nan));
  CHECK(o.rotation() == 0);
  Orientation f;  // flipped state survives garbage too
  feed(f, 0, -1.0f);
  feed(f, 1000, -1.0f);
  for (uint32_t t = 2000; t < 5000; t += 200) CHECK(!feed(f, t, nan, nan));
  CHECK(f.rotation() == 2);
}

void testMillisWrap() {
  Orientation o;
  const uint32_t t0 = 0xFFFFFF00u;
  feed(o, t0, -1.0f);
  CHECK(!feed(o, t0 + 999u, -1.0f));
  CHECK(feed(o, t0 + 1000u, -1.0f));  // t0 + 1000 wrapped past zero
}

void testStampNewerThanNow() {  // a timestamp taken later in the same pass is not "~49 days ago"
  Orientation o;
  feed(o, 100, -1.0f);
  CHECK(!feed(o, 98, -1.0f));
  CHECK(o.rotation() == 0);
}

}  // namespace

int main() {
  testStartsUpright();
  testFlipsAfterHold();
  testInterruptedHoldRestarts();
  testFlatNeverFlips();
  testTiltedFaceUpNeverFlips();
  testDeadZoneNeverFlips();
  testExactThresholdDoesNotFlip();
  testFlipsBackWithHysteresis();
  testBusyDefersThenFlips();
  testBusyThenNoLongerWantedDoesNotFlip();
  testNanKeepsOrientation();
  testMillisWrap();
  testStampNewerThanNow();
  return checksDone("orientation_test");
}
```

Edit `firmware/sim/Makefile`: append the new test to `TESTS`:

```make
         build/settings_json_test build/dev_editor_test build/orientation_test
```

(The existing last line is `build/settings_json_test build/dev_editor_test`; add `build/orientation_test` after it. The generic `build/%_test` rule already compiles `tests/orientation_test.cpp` with `-I../src`.)

- [ ] **Step 3: Run it to see it fail**

Run: `cd firmware/sim && make build/orientation_test`
Expected: compile error `orientation.h: No such file or directory`.

- [ ] **Step 4: Write the implementation**

Create `firmware/src/orientation.h`:

```cpp
#pragma once
// Which way up the cube is. Pure (no Arduino) so it is host-tested.
//
// `up` is the accelerometer axis along the screen's vertical, already signed so
// that +1 g means "the top of the screen points at the ceiling"; `az` is the axis
// out of the screen face. The display flips between rotation 0 (upright) and 2
// (upside down) only when the cube is clearly on its edge the other way round:
// |up| over FLIP_G and larger than |az| (so lying flat, or tilted towards
// face-up/face-down, never flips), held for HOLD_MS. Needing up < -FLIP_G to go
// to 2 and up > +FLIP_G to come back is the hysteresis. `busy` (a finger is
// down) postpones a due flip until it lifts. NaN readings never match, so they
// leave the orientation alone.

#include <stdint.h>

class Orientation {
 public:
  static constexpr float FLIP_G = 0.6f;
  static constexpr uint32_t HOLD_MS = 1000;

  // The value to give lcd.setRotation(): 0 or 2.
  uint8_t rotation() const { return rot_; }

  // True when the rotation changed on this call.
  bool update(uint32_t now, float up, float az, bool busy) {
    const float a = up < 0 ? -up : up;
    const float z = az < 0 ? -az : az;
    uint8_t want = rot_;
    if (a > FLIP_G && a > z) want = up > 0 ? 0 : 2;
    if (want == rot_) {
      pending_ = false;
      return false;
    }
    if (!pending_) {
      pending_ = true;
      since_ = now;
    }
    // Signed difference: survives millis() wrap, and a `now` a touch older than
    // since_ counts as zero elapsed rather than ~49 days.
    if (busy || (int32_t)(now - since_) < (int32_t)HOLD_MS) return false;
    rot_ = want;
    pending_ = false;
    return true;
  }

 private:
  uint8_t rot_ = 0;
  bool pending_ = false;
  uint32_t since_ = 0;
};
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd firmware/sim && make build/orientation_test && ./build/orientation_test`
Expected: `orientation_test: <N> checks, 0 failed`

- [ ] **Step 6: Run the whole host suite**

Run: `cd firmware/sim && make test`
Expected: every test line ends `0 failed`, including `orientation_test`.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/orientation.h firmware/sim/tests/orientation_test.cpp firmware/sim/Makefile
git commit -m "feat(firmware): Orientation classifier for IMU auto-rotate

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Touch remap helper

**Files:**
- Create: `firmware/src/touch_map.h`
- Create: `firmware/sim/tests/touch_map_test.cpp`
- Modify: `firmware/sim/Makefile` (add to `TESTS`)

**Interfaces:**
- Consumes: `LCD_WIDTH` (240) and `LCD_HEIGHT` (280) from `board_pins.h`.
- Produces: `inline void touchToScreen(uint8_t rotation, int16_t &x, int16_t &y)` in `touch_map.h`. Rotation `0` leaves the point alone; rotation `2` mirrors both axes and clamps into `0..LCD_WIDTH-1` / `0..LCD_HEIGHT-1`. It is its own inverse for in-range points (the simulator relies on that).

- [ ] **Step 1: Write the failing test**

Create `firmware/sim/tests/touch_map_test.cpp`:

```cpp
#include "check.h"
#include "touch_map.h"

namespace {

void testUprightIsIdentity() {
  int16_t x = 12, y = 34;
  touchToScreen(0, x, y);
  CHECK(x == 12);
  CHECK(y == 34);
}

void testFlippedMirrorsBothAxes() {
  int16_t x = 0, y = 0;
  touchToScreen(2, x, y);
  CHECK(x == LCD_WIDTH - 1);
  CHECK(y == LCD_HEIGHT - 1);

  x = LCD_WIDTH - 1;
  y = LCD_HEIGHT - 1;
  touchToScreen(2, x, y);
  CHECK(x == 0);
  CHECK(y == 0);

  x = 100;
  y = 40;
  touchToScreen(2, x, y);
  CHECK(x == LCD_WIDTH - 1 - 100);
  CHECK(y == LCD_HEIGHT - 1 - 40);
}

void testFlippedIsItsOwnInverse() {
  for (int16_t sx = 0; sx < LCD_WIDTH; sx += 17) {
    for (int16_t sy = 0; sy < LCD_HEIGHT; sy += 19) {
      int16_t x = sx, y = sy;
      touchToScreen(2, x, y);
      touchToScreen(2, x, y);
      CHECK(x == sx);
      CHECK(y == sy);
    }
  }
}

void testOutOfRangeIsClamped() {  // the controller can report a hair past the glass
  int16_t x = 300, y = -5;
  touchToScreen(2, x, y);
  CHECK(x == 0);
  CHECK(y == LCD_HEIGHT - 1);
}

}  // namespace

int main() {
  testUprightIsIdentity();
  testFlippedMirrorsBothAxes();
  testFlippedIsItsOwnInverse();
  testOutOfRangeIsClamped();
  return checksDone("touch_map_test");
}
```

Edit `firmware/sim/Makefile`: add `build/touch_map_test` to `TESTS` (after `build/orientation_test`).

- [ ] **Step 2: Run it to see it fail**

Run: `cd firmware/sim && make build/touch_map_test`
Expected: compile error `touch_map.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

Create `firmware/src/touch_map.h`:

```cpp
#pragma once
// Raw CST816 coordinates are in the panel's native orientation. When the display
// is rotated 180° (lcd.setRotation(2)) a finger on what is drawn at the top-left
// reports bottom-right, so the point is mirrored before gesture/hit-testing code
// sees it. Pure, so it is host-tested.

#include <stdint.h>
#include "board_pins.h"

inline void touchToScreen(uint8_t rotation, int16_t &x, int16_t &y) {
  if (rotation != 2) return;
  int16_t nx = (int16_t)(LCD_WIDTH - 1 - x);
  int16_t ny = (int16_t)(LCD_HEIGHT - 1 - y);
  if (nx < 0) nx = 0;
  if (nx > LCD_WIDTH - 1) nx = LCD_WIDTH - 1;
  if (ny < 0) ny = 0;
  if (ny > LCD_HEIGHT - 1) ny = LCD_HEIGHT - 1;
  x = nx;
  y = ny;
}
```

- [ ] **Step 4: Run the tests**

Run: `cd firmware/sim && make test`
Expected: all tests `0 failed`, including `touch_map_test`.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/touch_map.h firmware/sim/tests/touch_map_test.cpp firmware/sim/Makefile
git commit -m "feat(firmware): touch remap helper for the rotated display

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: IMU driver and simulator stand-in

**Files:**
- Create: `firmware/src/imu.h`
- Create: `firmware/src/imu.cpp`
- Create: `firmware/sim/imu_sim.cpp`
- Modify: `firmware/src/board_pins.h` (add IMU constants)
- Modify: `firmware/sim/Makefile` (add `imu_sim.cpp` to `SRCS`)

**Interfaces:**
- Produces (`imu.h`):
  - `bool imuBegin();` true if the QMI8658 answered and the accelerometer is running. Must be called after `touch.begin()` (which starts `Wire`).
  - `bool imuReadAccel(float &ax, float &ay, float &az);` acceleration in g; false on any I2C error.
- Produces (`board_pins.h`): `QMI8658_ADDR` (`0x6B`), `IMU_UP_SIGN` (`1` or `-1`: the sign of the IMU Y reading when the screen top points up; **unverified**, see the hardware checklist in Task 5).

- [ ] **Step 1: Add the board constants**

In `firmware/src/board_pins.h`, directly after the CST816 block (`#define CST816_ADDR  0x15`), add:

```cpp

// --- QMI8658C 6-axis IMU, I2C (same bus as the touch controller) --------
#define QMI8658_ADDR 0x6B
// Sign of the IMU's Y-axis reading when the top of the screen points at the
// ceiling (+1 g = upright). Flip to -1 if the screen rotates the wrong way when
// the cube is stood on its edge; see docs/imu-acceptance.md.
#define IMU_UP_SIGN  1
```

- [ ] **Step 2: Write the header**

Create `firmware/src/imu.h`:

```cpp
#pragma once
#include <stdint.h>

// Minimal QMI8658C accelerometer reader -- enough to tell which way up the cube
// is (see orientation.h). The gyro and the interrupt pin are not used.

// Call after touch.begin(), which starts the shared I2C bus. False if the sensor
// does not answer; the cube then simply never auto-rotates.
bool imuBegin();

// Acceleration in g along the sensor's x, y and z axes. False on an I2C error.
bool imuReadAccel(float &ax, float &ay, float &az);
```

- [ ] **Step 3: Write the hardware implementation**

Create `firmware/src/imu.cpp`:

```cpp
#include "imu.h"
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

namespace {
constexpr uint8_t REG_WHO_AM_I = 0x00;  // reads 0x05
constexpr uint8_t REG_CTRL1 = 0x02;     // bit 6: register address auto-increment
constexpr uint8_t REG_CTRL2 = 0x03;     // accelerometer: full scale [6:4], output rate [3:0]
constexpr uint8_t REG_CTRL7 = 0x08;     // bit 0: accelerometer enable, bit 1: gyro enable
constexpr uint8_t REG_ACC_X_L = 0x35;   // x, y, z as little-endian int16
constexpr uint8_t REG_RESET = 0x60;
constexpr uint8_t WHO_AM_I_VALUE = 0x05;
constexpr uint8_t RESET_CMD = 0xB0;
constexpr uint8_t CTRL1_ADDR_AUTO_INC = 0x40;
constexpr uint8_t CTRL2_4G_62HZ = 0x17;  // +-4 g, 62.5 Hz: plenty for a 5 Hz orientation poll
constexpr uint8_t CTRL7_ACC_ONLY = 0x01;
constexpr float LSB_PER_G = 8192.0f;     // +-4 g full scale

bool g_ready = false;

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((int)QMI8658_ADDR, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}
}  // namespace

bool imuBegin() {
  g_ready = false;
  uint8_t id = 0;
  if (!readRegs(REG_WHO_AM_I, &id, 1)) {
    Serial.println("[imu] not responding on I2C -- auto-rotate off");
    return false;
  }
  Serial.printf("[imu] QMI8658 who-am-i 0x%02X\n", id);
  if (id != WHO_AM_I_VALUE) {
    Serial.println("[imu] unexpected chip id (wanted 0x05) -- auto-rotate off");
    return false;
  }
  writeReg(REG_RESET, RESET_CMD);
  delay(15);  // the reset takes a few ms to finish
  g_ready = writeReg(REG_CTRL1, CTRL1_ADDR_AUTO_INC) && writeReg(REG_CTRL2, CTRL2_4G_62HZ) &&
            writeReg(REG_CTRL7, CTRL7_ACC_ONLY);
  if (!g_ready) Serial.println("[imu] configuration failed -- auto-rotate off");
  return g_ready;
}

bool imuReadAccel(float &ax, float &ay, float &az) {
  if (!g_ready) return false;
  uint8_t b[6];
  if (!readRegs(REG_ACC_X_L, b, sizeof(b))) return false;
  ax = (int16_t)(b[0] | (b[1] << 8)) / LSB_PER_G;
  ay = (int16_t)(b[2] | (b[3] << 8)) / LSB_PER_G;
  az = (int16_t)(b[4] | (b[5] << 8)) / LSB_PER_G;
  return true;
}
```

- [ ] **Step 4: Write the simulator stand-in**

Create `firmware/sim/imu_sim.cpp`:

```cpp
// Desktop stand-in for imu.cpp: there is no accelerometer on a laptop, so the
// cube's pose comes from CUBE_ORIENT. 0 (default) = upright, 2 = upside down.
// `make run` then flips about a second after start, exactly as a cube powered
// up upside down would.
#include <Arduino.h>
#include "../src/board_pins.h"
#include "../src/imu.h"

namespace {
bool flipped() {
  const char *e = getenv("CUBE_ORIENT");
  return e && atoi(e) == 2;
}
}  // namespace

bool imuBegin() {
  Serial.printf("[imu] simulated, CUBE_ORIENT=%d\n", flipped() ? 2 : 0);
  return true;
}

bool imuReadAccel(float &ax, float &ay, float &az) {
  ax = 0.0f;
  ay = (flipped() ? -1.0f : 1.0f) * (float)IMU_UP_SIGN;  // main.cpp multiplies by IMU_UP_SIGN again
  az = 0.0f;
  return true;
}
```

- [ ] **Step 5: Add the stand-in to the simulator build**

In `firmware/sim/Makefile`, change the `SRCS` stand-in line to include `imu_sim.cpp`:

```make
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp ota_sim.cpp battery_sim.cpp ble_sim.cpp imu_sim.cpp sdl_main.cpp
```

- [ ] **Step 6: Compile the hardware driver**

Run: `cd firmware && pio run`
Expected: `SUCCESS`. (`imu.cpp` is compiled but not yet called; this proves it builds. `main.cpp` does not link-fail because nothing references it yet.)

- [ ] **Step 7: Commit**

```bash
git add firmware/src/imu.h firmware/src/imu.cpp firmware/src/board_pins.h firmware/sim/imu_sim.cpp firmware/sim/Makefile
git commit -m "feat(firmware): QMI8658 accelerometer driver and sim stand-in

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Wire it into `main.cpp` and the simulator touch layer

**Files:**
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/sim/touch_sim.cpp`

**Interfaces:**
- Consumes: `Orientation` (Task 1), `touchToScreen` (Task 2), `imuBegin` / `imuReadAccel` / `IMU_UP_SIGN` (Task 3).
- Produces: nothing for later tasks. Behaviour: `appliedRot` is the rotation currently set on `lcd`; touch is remapped by it.

- [ ] **Step 1: Includes**

In `firmware/src/main.cpp`, add three includes in alphabetical position among the existing ones:

```cpp
#include "imu.h"
```
after `#include "idle_sleep.h"`,

```cpp
#include "orientation.h"
```
after `#include "net.h"`, and

```cpp
#include "touch_map.h"
```
after `#include "touch.h"`.

- [ ] **Step 2: State and helpers**

In the anonymous namespace, directly after the `bool pairWaitDismissed = false;` declaration and its comment, add:

```cpp

// Auto-rotate (orientation.h). `orient` is what the IMU says; `appliedRot` is what
// is on the panel: they differ only while the screen sleeps, so the panel is
// never written to while it is off. Touch is remapped by `appliedRot`.
Orientation orient;
uint8_t appliedRot = 0;
bool imuUp = false;
bool touchDown = false;  // a finger was on the glass at the last poll: do not flip under it
uint32_t lastImu = 0;
constexpr uint32_t IMU_POLL_MS = 200;
```

Then, directly before `uint32_t pollMs()` (after the closing brace of `pollTouch()`), add:

```cpp
void pollOrientation(uint32_t now) {
  if (!imuUp || now - lastImu < IMU_POLL_MS) return;
  lastImu = now;
  float ax, ay, az;
  if (!imuReadAccel(ax, ay, az)) return;  // a missed read keeps the current orientation
  orient.update(now, (float)IMU_UP_SIGN * ay, az, touchDown);
}

// Puts the classifier's answer on the panel. Not while the screen is asleep; the
// wake path calls this on the same loop pass, before the first frame is drawn.
void applyRotation() {
  if (!screenOn || appliedRot == orient.rotation()) return;
  appliedRot = orient.rotation();
  lcd.setRotation(appliedRot);
  dirty = true;
  Serial.printf("[imu] rotation -> %d\n", (int)appliedRot);
}
```

- [ ] **Step 3: Remap touch**

In `pollTouch()`, directly after `const bool down = touch.read(x, y);`, add:

```cpp
  touchDown = down;
  if (down) touchToScreen(appliedRot, x, y);
```

- [ ] **Step 4: Start the IMU**

In `setup()`, directly after `touch.begin();`, add:

```cpp
  imuUp = imuBegin();  // after touch.begin(), which starts the shared I2C bus
```

- [ ] **Step 5: Poll and apply in the loop**

In `loop()`, directly after `pollTouch();`, add:

```cpp
  pollOrientation(now);
```

and directly after the screen-sleep block's `if (!screenOn) { delay(50); return; }` (i.e. immediately before the `// Redraw on change, ...` comment), add:

```cpp
  applyRotation();
```

- [ ] **Step 6: Simulator touch stand-in**

In `firmware/sim/touch_sim.cpp`, add the include after `#include "../src/touch.h"`:

```cpp
#include "../src/touch_map.h"
```

and replace the end of `Touch::read`:

```cpp
  x = (int16_t)tp.x;
  y = (int16_t)tp.y;
  return true;
```

with:

```cpp
  x = (int16_t)tp.x;
  y = (int16_t)tp.y;
  // main.cpp expects raw panel coordinates and mirrors them itself when the display
  // is rotated. The mouse is over the already-rotated window, so undo that first;
  // the mirror is its own inverse.
  touchToScreen(g_simDisplay->getRotation(), x, y);
  return true;
```

- [ ] **Step 7: Build everything**

Run:

```bash
cd firmware && pio run && pio run -e waveshare-s3-lcd169-noble
cd sim && make test && make
```

Expected: both `pio` builds `SUCCESS`; `make test` all `0 failed`; `make` prints `built build/cube-sim`.

- [ ] **Step 8: Manual check in the simulator (needs a human at the SDL window)**

Run, with the bridge running (`node bridge/server.mjs`) or without it (the cards just show offline):

```bash
cd firmware/sim && CUBE_ORIENT=2 make run
```

Expected:
1. The terminal prints `[imu] simulated, CUBE_ORIENT=2`, then about 1 s after start `[imu] rotation -> 2`, and the window content turns upside down.
2. The picture is upside down, so drag the mouse **left-to-right** across the window (towards the picture's left): the card advances. Right-to-left goes back. Click: it advances. Drag bottom-to-top on the window: the display panel slides in from the top **of the picture**.
3. `make run` with no `CUBE_ORIENT` stays upright and never prints `[imu] rotation`.

Keep the `touchToScreen(...)` call in `firmware/sim/touch_sim.cpp`: `getTouch()` already mirrors the point for the current rotation, so the call undoes that and `main.cpp` then re-mirrors it. (Plan corrected after the final review; the original remedy here was inverted.)

- [ ] **Step 9: Commit**

```bash
git add firmware/src/main.cpp firmware/sim/touch_sim.cpp
git commit -m "feat(firmware): auto-rotate the display from the IMU

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Docs and the hardware checklist

**Files:**
- Modify: `CLAUDE.md`
- Create: `docs/imu-acceptance.md`

**Interfaces:**
- Consumes/Produces: none (documentation only).

- [ ] **Step 1: CLAUDE.md, simulator command**

In `CLAUDE.md`, directly after the line starting `CUBE_BLE=live|stale|pair|none make run`, add:

```
CUBE_ORIENT=2 make run                     # sim cube is upside down: flips ~1 s after start (default 0); the window is the only place the flip shows, `make shot` reads back the logical frame and is unaffected
```

- [ ] **Step 2: CLAUDE.md, architecture paragraph**

In `CLAUDE.md`, directly after the **Screen sleep.** paragraph (the one ending `so only the state machine is testable off-device.`), add:

```

**Auto-rotate.** The cube flips its display 180° when stood on its other end, always on (no setting). `imu.cpp` reads the QMI8658C accelerometer (I2C `0x6B`, shared bus) and `main.cpp` polls it at 5 Hz into `Orientation` (`orientation.h`, pure, host-tested: `|up| > 0.6 g`, `|up| > |az|`, held 1 s, deferred while a finger is down; flat or tilted never flips). The result goes to `lcd.setRotation(0|2)`, so `ui.cpp` and `preview.html` are untouched; raw CST816 coordinates are mirrored by `touchToScreen` (`touch_map.h`) before gesture code sees them. The panel is not written while the screen sleeps; the rotation is applied on wake before the first frame. Local-only: not in the payload contract. `IMU_UP_SIGN` in `board_pins.h` is the sign of IMU Y when the screen top is up and is unverified on hardware; `docs/imu-acceptance.md` is the open checklist. The simulator stand-in is `imu_sim.cpp` (`CUBE_ORIENT`); `touch_sim.cpp` undoes the mirror so `main.cpp` sees raw coordinates as on the board.
```

- [ ] **Step 3: Hardware checklist**

Create `docs/imu-acceptance.md`:

```markdown
# IMU auto-rotate: hardware acceptance checklist

The classifier and the touch remap are host-tested, and the flow is exercised in
the simulator. Nothing here has run on a real cube yet. Work through this after
flashing (`pio run -t upload && pio device monitor`).

## Boot log

- [ ] `[imu] QMI8658 who-am-i 0x05` appears right after `[touch] CST816 chip id ...`.
      If it says "not responding", check `QMI8658_ADDR` in `board_pins.h` (`0x6A`
      is the other possible address) and the I2C pins. Touch must still work.

## Direction

- [ ] Stand the cube on its edge so the picture is the right way up. It must stay
      as is. (If it flips to upside down after about a second and back, `IMU_UP_SIGN`
      in `board_pins.h` is wrong: change `1` to `-1` and reflash.)
- [ ] Turn it over 180° on its edge. After about 1 s the picture rotates and
      `[imu] rotation -> 2` is logged. Turn it back: `rotation -> 0`.
- [ ] Lay it flat face up, then face down, and tilt it about 30° either way: the
      picture never flips.

## Display

- [ ] After the flip there is no band or shift at either screen edge. The 20 px
      `LCD_OFFSET_Y` is symmetric in the 40 spare rows of the 320-row frame, so it
      should be unchanged; if a band appears, adjust the offset in `display.h`.
- [ ] Colours are unchanged (no inversion after `setRotation`).

## Touch

- [ ] Upside down: a tap on a visible control (e.g. the DONE bar of the display
      panel, swipe down to open it) lands where it is drawn.
- [ ] Upside down: swipe left advances the card, swipe right goes back, swipe down
      opens the display panel from the top **of the picture**, swipe up closes it.
- [ ] Hold a finger on the screen and turn the cube: the picture does not flip until
      the finger lifts, and no swipe fires.
- [ ] No touch drops or I2C errors in the log while the IMU is polled (watch for a
      minute).

## Sleep and boot

- [ ] Let the screen sleep (`SCREEN_SLEEP_MS`), flip the cube, then touch: it wakes
      already the right way up, and that wake touch does not change the card.
- [ ] Moving the cube alone never wakes a sleeping screen.
- [ ] Power the cube up upside down: after the boot screens the picture rotates
      within about a second.
```

- [ ] **Step 4: Commit**

```bash
git add CLAUDE.md docs/imu-acceptance.md
git commit -m "docs: auto-rotate in CLAUDE.md and a hardware acceptance checklist

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-Review

- **Spec coverage:** classifier and thresholds (Task 1); remap incl. clamping (Task 2); IMU driver at `0x6B`, accel only, `IMU_UP_SIGN` constant (Task 3); 5 Hz polling, `setRotation`, forced redraw, deferral while a finger is down, sleep handling, boot correction, error handling (Task 4); sim stand-ins and `CUBE_ORIENT` (Tasks 3-4); CLAUDE.md and hardware checklist (Task 5). The spec's `make shot` support was dropped and the spec corrected (readRect is post-rotation).
- **Placeholders:** none; every code step has the code.
- **Type consistency:** `Orientation::update(uint32_t, float, float, bool)`, `rotation()`, `touchToScreen(uint8_t, int16_t&, int16_t&)`, `imuBegin()`, `imuReadAccel(float&, float&, float&)`, `IMU_UP_SIGN`, `QMI8658_ADDR` are spelled identically everywhere. `IMU_UP_SIGN` is applied once in `main.cpp`; `imu_sim.cpp` pre-multiplies by it so the product is the intended `±1`.
- **Known unknowns (stated, not hidden):** QMI8658 register values come from the datasheet/vendor demo and are unconfirmed on this board (boot log `who-am-i` and the checklist catch it); `IMU_UP_SIGN`; whether the SDL panel rotates mouse coordinates itself (Task 4 Step 8 has the exact remedy).
