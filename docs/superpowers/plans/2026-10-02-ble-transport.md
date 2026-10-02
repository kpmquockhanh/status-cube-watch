# BLE-first transport with WiFi fallback — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Mac pushes the finished payload to the cube over BLE (CoreBluetooth, passkey pairing, automatic reconnect); with no live BLE link the cube polls the bridge over WiFi as it does today.

**Architecture:** The cube becomes a NimBLE GATT server (Payload write, Control notify, Info read). A pure `FrameAssembler` reassembles chunked JSON into the existing `payloadFromJson()`. A pure `TransportPolicy` decides when WiFi is on. `ClaudeCubeLink.app` (Swift) supervises the unchanged Node bridge, reads `127.0.0.1:<port>/api/status` and writes it to the cube.

**Tech Stack:** C++17 / Arduino-ESP32 / NimBLE-Arduino (firmware), LovyanGFX SDL simulator, Swift 6 SwiftPM + CoreBluetooth + Swift Testing (Mac app), shell (installer), Node bridge (unchanged).

**Spec:** `docs/superpowers/specs/2026-10-02-ble-transport-design.md`

## Global Constraints

- Payload contract `{v, ts, src, est, cards}`, `cards.mjs` as the only formatter, `payloadFromJson()` as the only parser, and the bridge's HTTP endpoints (`/api/status`, `/`, `/health`) do not change. Do not add number/unit formatting to the firmware.
- Bridge stays **zero npm dependencies**, Node ≥20.
- BLE name "Claude Cube"; one custom service; characteristics Payload (write, encrypted+authenticated), Control (notify), Info (read, encrypted).
- Frame: 4-byte header `ver, seq, idx, total` + JSON bytes; ≤ 16 chunks; 2 KB reassembly buffer (`BLE_MAX_PAYLOAD = 2048`); chunks sized to the negotiated MTU (about 500 bytes).
- Pairing: display-only IO capability, MITM, Secure Connections, bonding; random 6-digit passkey shown on the cube; **one bonded Mac at a time**.
- Heartbeat every **5 s**; BLE is live while a complete payload arrived in the last **15 s**; WiFi starts when BLE has been stale **15 s** (or there is no bond); WiFi shuts down only after BLE has been live continuously **30 s**.
- Boot: no SSID no longer opens the portal; the portal auto-opens on a failed WiFi join **only when there is no bond**; the 3 s boot-hold still opens it and its hint reads "SETUP" (not "WIFI SETUP").
- `MAX_CARDS = 8`; fixed char buffers in `payload.h` unchanged.
- Firmware code shared with the simulator stays within the `sim/Arduino.h` shim; hardware-only files (`net_ble.cpp`, `portal.cpp`, `ota.cpp`, `settings.cpp`, `net.cpp`) get stand-ins in `firmware/sim/`.
- Mac app: LaunchAgent with `KeepAlive`, app bundle with `NSBluetoothAlwaysUsageDescription` and `LSUIElement`, ad-hoc signed, lock file for single instance, port default 8787 (`CUBE_PORT` / `--bridge`).
- Out of scope: several Macs, iOS or other hosts, Windows/Linux, touch/Pomodoro events back to the Mac.

## Additions beyond the spec (confirm with the owner)

These are gaps found while planning. They are small and implemented as written below; flag them in the final report.

1. **`WIFI_ALWAYS_ON`** (config.h, default 0). OTA needs WiFi, and the policy turns WiFi off while BLE is live. With `WIFI_ALWAYS_ON 1` the policy keeps WiFi up so OTA keeps working (more power). Policy input `keepWifi`.
2. **Chunk ordering.** ATT writes are ordered, so the assembler requires `idx` to arrive in order: a duplicate (`idx` already seen) is `Ignored`, a gap or a mid-payload join is `Bad`. The helper retries with a **new `seq`**.
3. **Info read triggers pairing.** The helper reads Info (encrypted) first, which triggers the macOS passkey dialog, then subscribes to Control; the cube sends "send now" on subscribe.
4. **ACK means "reassembled"**, not "parsed". Invalid JSON is dropped on the cube and the next heartbeat resends.
5. **Info = 2 bytes** `[proto_ver, fw_rev]`; `fw_rev` is a counter in `ble_frame.h`.

## Review Focus

Failure modes the spec implies but a task's happy path would miss, most likely first. Each has a test in the owning task.

1. **Payload larger than 2 KB / more than 16 chunks** — cube must not overflow its buffer (`ble_frame_test` overflow case, Task 2); helper must refuse to send and log (`FrameTests.rejectsOversize`, Task 8); the cube then goes stale and falls back to WiFi.
2. **Helper restarts or crashes mid-payload and its `seq` resets to 0** — the partial payload is never shown and the next payload is accepted (`testNewSeqDiscardsPartial`, `testSeqRestartAfterPartial`, `testClearForgetsLastDone`, Task 2).
3. **Bridge answers with HTML, an empty body, a JSON array or a 500** — the helper must not push it (`validatePayloadBody` tests, Task 8).
4. **`millis()` wraparound (49 days) and `bleLastGood == 0` sentinel** — policy must not flap or lock on (`testMillisWrap`, `testNeverLiveUsesStart`, Task 3).
5. **`node` not installed, or an external bridge (from `agent.sh`) already on the port** — supervisor must adopt or fail loudly, never fight for the port (`SupervisorTests`, Task 9).

---

## Task 0: Preflight and branch

**Files:** none (git only)

The working tree already has unrelated uncommitted changes (`README.md`, `bridge/cards.mjs`, `bridge/config.example.json`, `bridge/preview.html`, `bridge/server.mjs`, `firmware/src/main.cpp`, `firmware/src/ui.cpp`, `firmware/src/ui.h`, untracked `.gitignore`, `bridge/sources/gmail.mjs`). Tasks 4, 7 and 12 edit `main.cpp`, `ui.cpp`, `ui.h`, `README.md`. Those edits must not be committed together with the owner's unrelated hunks.

- [ ] **Step 1: Ask the owner to commit or stash their unrelated changes** (or agree to `git add -p` for every shared file). Do not proceed until answered.

- [ ] **Step 2: Create the branch**

```bash
cd /Users/kpmquockhanh/code/claude-status-cube
git switch -c feat/ble-transport
git status -sb | head -3
```
Expected: `## feat/ble-transport`.

- [ ] **Step 3: Baseline the existing tests and build**

```bash
cd firmware/sim && make test
cd .. && pio run 2>&1 | tail -3
```
Expected: every `*_test` line ends `0 failed`; `pio run` ends `[SUCCESS]`. If the baseline is red, stop and report; do not build on it.

---

## Task 1: Spike — Mac Bluetooth permission and a bare BLE round trip

**Question:** Can a LaunchAgent-started `.app` get the Bluetooth permission and complete passkey pairing, an encrypted write, a notify and a reconnect with an ESP32-S3 running NimBLE? Throwaway code; the deliverable is `docs/superpowers/spikes/2026-10-02-ble-spike.md`. Needs the real board and the Mac.

**Files:**
- Create (throwaway): `spike/firmware/platformio.ini`, `spike/firmware/src/main.cpp`
- Create (throwaway): `spike/mac/Package.swift`, `spike/mac/Sources/spike/main.swift`, `spike/mac/Info.plist`, `spike/mac/build.sh`
- Create: `docs/superpowers/spikes/2026-10-02-ble-spike.md`
- Modify: `.gitignore` (append `spike/firmware/.pio/`, `spike/mac/.build/`, `spike/mac/Spike.app/`)

**Interfaces:**
- Produces: answers to Q1–Q7 below, which Tasks 6, 10 and 11 depend on.

- [ ] **Step 1: Firmware stub** — `spike/firmware/platformio.ini`:

```ini
[env:spike]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
board_build.mcu = esp32s3
board_build.flash_mode = qio
board_build.arduino.memory_type = qio_qspi
board_upload.flash_size = 16MB
board_build.partitions = default_16MB.csv
build_flags = -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1
lib_deps = h2zero/NimBLE-Arduino@^2.1.0
```

`spike/firmware/src/main.cpp`:

```cpp
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_random.h>

static const char *SVC = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a01";
static const char *PAY = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a02";
static const char *CTL = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a03";
static const char *INF = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a04";
NimBLECharacteristic *ctl;

struct SrvCb : NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &i) override {
    Serial.printf("connect, bonds=%d, mtu=%u\n", NimBLEDevice::getNumBonds(), i.getMTU());
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int r) override {
    Serial.printf("disconnect %d\n", r);
    NimBLEDevice::startAdvertising();
  }
  uint32_t onPassKeyDisplay() override {
    const uint32_t k = esp_random() % 1000000;
    Serial.printf("PASSKEY %06u\n", (unsigned)k);
    return k;
  }
  void onConfirmPassKey(NimBLEConnInfo &i, uint32_t) override { NimBLEDevice::injectConfirmPasskey(i, true); }
  void onAuthenticationComplete(NimBLEConnInfo &i) override {
    Serial.printf("auth complete encrypted=%d bonded=%d idaddr=%s isBonded=%d\n", i.isEncrypted(), i.isBonded(),
                  i.getIdAddress().toString().c_str(), NimBLEDevice::isBonded(i.getIdAddress()));
  }
} srvCb;

struct PayCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    Serial.printf("write %u bytes\n", (unsigned)c->getValue().size());
    const uint8_t ack[2] = {0x02, c->getValue().data()[1]};
    ctl->setValue(ack, 2);
    ctl->notify();
  }
} payCb;

struct CtlCb : NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t sub) override {
    Serial.printf("subscribe %u\n", sub);
    if (sub & 1) { const uint8_t m = 0x01; ctl->setValue(&m, 1); ctl->notify(); }
  }
} ctlCb;

void setup() {
  Serial.begin(115200);
  delay(500);
  NimBLEDevice::init("Claude Cube");
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  auto *srv = NimBLEDevice::createServer();
  srv->setCallbacks(&srvCb);
  auto *svc = srv->createService(SVC);
  auto *pay = svc->createCharacteristic(PAY, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  pay->setCallbacks(&payCb);
  ctl = svc->createCharacteristic(CTL, NIMBLE_PROPERTY::NOTIFY);
  ctl->setCallbacks(&ctlCb);
  auto *inf = svc->createCharacteristic(INF, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
  const uint8_t iv[2] = {1, 1};
  inf->setValue(iv, 2);
  svc->start();
  auto *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SVC);
  adv->setName("Claude Cube");
  adv->enableScanResponse(true);
  adv->start();
  Serial.println("advertising");
}
void loop() { delay(1000); }
```

NimBLE-Arduino 2.x API names above are from memory. If `pio run` fails, read the installed headers (`spike/firmware/.pio/libdeps/spike/NimBLE-Arduino/src/NimBLEServer.h`, `NimBLECharacteristic.h`, `NimBLEDevice.h`, `NimBLEConnInfo.h`) and fix only the names; record every change in the findings doc (Task 6 reuses them).

- [ ] **Step 2: Build and flash the stub**

```bash
cd spike/firmware && pio run -t upload && pio device monitor
```
Expected: `advertising` on the serial monitor. Leave it running in another terminal.

- [ ] **Step 3: Mac spike tool** — `spike/mac/Package.swift`:

```swift
// swift-tools-version: 6.0
import PackageDescription
let package = Package(
    name: "spike",
    platforms: [.macOS(.v13)],
    targets: [.executableTarget(name: "spike")],
    swiftLanguageModes: [.v5]
)
```

`spike/mac/Sources/spike/main.swift`:

```swift
import CoreBluetooth
import Foundation

let svc = CBUUID(string: "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A01")
let payU = CBUUID(string: "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A02")
let ctlU = CBUUID(string: "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A03")
let infU = CBUUID(string: "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A04")

func log(_ s: String) { FileHandle.standardError.write(Data("\(Date()) \(s)\n".utf8)) }

final class Spike: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    var c: CBCentralManager!
    var p: CBPeripheral?
    var pay, ctl, inf: CBCharacteristic?
    override init() { super.init(); c = CBCentralManager(delegate: self, queue: .main) }
    func centralManagerDidUpdateState(_ m: CBCentralManager) {
        log("state \(m.state.rawValue) auth \(CBCentralManager.authorization.rawValue)")
        if m.state == .poweredOn {
            if let s = UserDefaults.standard.string(forKey: "id"), let u = UUID(uuidString: s),
               let known = m.retrievePeripherals(withIdentifiers: [u]).first {
                log("reconnect by identifier"); p = known; m.connect(known)
            } else { log("scanning"); m.scanForPeripherals(withServices: [svc]) }
        }
    }
    func centralManager(_ m: CBCentralManager, didDiscover x: CBPeripheral, advertisementData: [String: Any], rssi: NSNumber) {
        log("found \(x.name ?? "?")"); m.stopScan(); p = x; m.connect(x)
    }
    func centralManager(_ m: CBCentralManager, didConnect x: CBPeripheral) {
        log("connected; maxWrite=\(x.maximumWriteValueLength(for: .withResponse))")
        x.delegate = self; x.discoverServices([svc])
    }
    func centralManager(_ m: CBCentralManager, didFailToConnect x: CBPeripheral, error: Error?) { log("fail \(String(describing: error))") }
    func centralManager(_ m: CBCentralManager, didDisconnectPeripheral x: CBPeripheral, error: Error?) {
        log("disconnected \(String(describing: error))"); m.connect(x)
    }
    func peripheral(_ x: CBPeripheral, didDiscoverServices e: Error?) {
        x.services?.forEach { x.discoverCharacteristics([payU, ctlU, infU], for: $0) }
    }
    func peripheral(_ x: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for ch in s.characteristics ?? [] {
            if ch.uuid == payU { pay = ch } else if ch.uuid == ctlU { ctl = ch } else if ch.uuid == infU { inf = ch }
        }
        if let i = inf { log("reading Info (should trigger pairing)"); x.readValue(for: i) }
    }
    func peripheral(_ x: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        if ch.uuid == infU {
            log("Info = \(ch.value.map { Array($0) } ?? []) error=\(String(describing: error))")
            if error == nil {
                UserDefaults.standard.set(x.identifier.uuidString, forKey: "id")
                if let c = ctl { x.setNotifyValue(true, for: c) }
            }
        } else if ch.uuid == ctlU { log("control notify \(ch.value.map { Array($0) } ?? [])") }
    }
    func peripheral(_ x: CBPeripheral, didUpdateNotificationStateFor ch: CBCharacteristic, error: Error?) {
        log("notifying=\(ch.isNotifying) error=\(String(describing: error))")
        if ch.isNotifying, let w = pay {
            var f = Data([1, 9, 0, 1]); f.append(Data("{\"v\":1}".utf8))
            x.writeValue(f, for: w, type: .withResponse)
        }
    }
    func peripheral(_ x: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        log("write done error=\(String(describing: error))")
    }
}
let spike = Spike()
RunLoop.main.run()
```

`spike/mac/Info.plist`:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleIdentifier</key><string>com.claude-cube.spike</string>
  <key>CFBundleExecutable</key><string>spike</string>
  <key>CFBundleName</key><string>Spike</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSUIElement</key><true/>
  <key>NSBluetoothAlwaysUsageDescription</key><string>Spike: talk to the Claude cube.</string>
</dict></plist>
```

`spike/mac/build.sh`:

```bash
#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
swift build -c release
rm -rf Spike.app && mkdir -p Spike.app/Contents/MacOS
cp .build/release/spike Spike.app/Contents/MacOS/spike
cp Info.plist Spike.app/Contents/Info.plist
codesign --force --sign - --identifier com.claude-cube.spike Spike.app
echo "built $(pwd)/Spike.app"
```

- [ ] **Step 4: Build the Mac spike**

```bash
chmod +x spike/mac/build.sh && spike/mac/build.sh
```
Expected: `built .../Spike.app`.

- [ ] **Step 5: Run the experiments and record each result.** Run the app with `open -W -a spike/mac/Spike.app --stderr /tmp/spike.log` (or run the binary inside the bundle from a terminal) and answer:

| # | Question | How |
|---|----------|-----|
| Q1 | Does a `.app` opened by `open` show the Bluetooth permission prompt, and does it work after Allow? | First run; read the log for `state 5 auth 3` |
| Q2 | Does a **LaunchAgent**-started copy work *without* a manual `open` first? Does it prompt, fail silently or crash? | `launchctl bootstrap gui/$(id -u) <plist>` pointing at the bundle binary (plist as in Task 11); `launchctl bootout` after |
| Q3 | Does reading Info trigger the macOS passkey dialog, and does the cube print `PASSKEY nnnnnn`? Does typing it complete pairing (`auth complete ... encrypted=1 bonded=1`)? | log + serial |
| Q4 | After pairing, are the notify, the "send now" message and the encrypted write all OK (`write 11 bytes`, ACK `[2, 9]` seen)? | log + serial |
| Q5 | Does a re-run reconnect silently via `retrievePeripherals` with no dialog? Same after resetting the board? | re-run, then press RESET |
| Q6 | Does `isBonded(idAddress)` print 1 on the reconnect (so a stranger-bond check by address is possible)? | serial |
| Q7 | What is `maximumWriteValueLength(.withResponse)` and the cube's MTU? Does the cube see the advertised name and service UUID (scan by service works)? | log |

Also test: write a stale bond case — erase the board's flash (`pio run -t erase`), re-flash, run the app, and record the exact error the Mac reports (expect `CBError.peerRemovedPairingInformation`). This message is used in Task 10.

- [ ] **Step 6: Write the findings** into `docs/superpowers/spikes/2026-10-02-ble-spike.md`: the Q1–Q7 table with the observed answers, the exact NimBLE API name corrections, and a **Decision** line:
  - Q1–Q5 all pass → proceed unchanged.
  - Q2 fails (LaunchAgent cannot get Bluetooth) but Q1 passes → Task 11 changes: instead of a LaunchAgent, install the app as a **login item** (`osascript` / `SMAppService.mainApp.register()`) and keep the `.app` running with `LSUIElement`; Task 10 `main.swift` additionally runs `NSApplication.shared.run()`.
  - Q3 fails (no dialog, or passkey never shown) → stop and report to the owner; the pairing design needs rework.

- [ ] **Step 7: Commit the findings only** (the spike code is throwaway; keep it under `spike/` but do not ship it)

```bash
git add .gitignore docs/superpowers/spikes/2026-10-02-ble-spike.md spike
git commit -m "spike: BLE permission, pairing and round trip findings"
```

---

## Task 2: Protocol doc, frame assembler and shared fixture

**Files:**
- Create: `docs/ble-protocol.md`
- Create: `firmware/src/ble_frame.h`
- Create: `firmware/sim/fixtures/ble-frames.txt`
- Create: `firmware/sim/tests/ble_frame_test.cpp`
- Modify: `firmware/sim/Makefile` (add to `TESTS`)

**Interfaces:**
- Produces (`ble_frame.h`): constants `BLE_PROTO_VER=1`, `BLE_FW_REV=1`, `BLE_HDR=4`, `BLE_MAX_CHUNKS=16`, `BLE_MAX_PAYLOAD=2048`, `BLE_CTRL_SEND_NOW=0x01`, `BLE_CTRL_ACK=0x02`, UUID strings `BLE_SERVICE_UUID`, `BLE_PAYLOAD_UUID`, `BLE_CONTROL_UUID`, `BLE_INFO_UUID`; `enum class FrameResult : uint8_t { Partial, Complete, Ignored, Bad }`; `class FrameAssembler { FrameResult feed(const uint8_t*, size_t); const char* json() const; size_t size() const; uint8_t seq() const; void reset(); void clear(); }`.
- Task 6 (`net_ble.cpp`) and Task 8 (Swift `CubeProtocol`) consume these exact values.

- [ ] **Step 1: Write the fixture** `firmware/sim/fixtures/ble-frames.txt` (both the C++ and Swift tests read it; `chunk` is data bytes per frame, `frame` lines are the expected bytes in hex):

```
# Golden frames for the BLE payload protocol (docs/ble-protocol.md).
# `chunk` = JSON bytes per frame (the write size minus the 4-byte header).
case basic
seq 7
chunk 4
json {"v":1,"cards":[]}
frame 01 07 00 05 7b 22 76 22
frame 01 07 01 05 3a 31 2c 22
frame 01 07 02 05 63 61 72 64
frame 01 07 03 05 73 22 3a 5b
frame 01 07 04 05 5d 7d
case single
seq 255
chunk 16
json {"v":1}
frame 01 ff 00 01 7b 22 76 22 3a 31 7d
```

- [ ] **Step 2: Write the failing test** `firmware/sim/tests/ble_frame_test.cpp`:

```cpp
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ble_frame.h"
#include "check.h"

namespace {

using Bytes = std::vector<uint8_t>;

Bytes hex(const std::string &s) {
  Bytes out;
  std::istringstream in(s);
  std::string tok;
  while (in >> tok) out.push_back((uint8_t)std::stoul(tok, nullptr, 16));
  return out;
}

Bytes frame(uint8_t seq, uint8_t idx, uint8_t total, const std::string &data, uint8_t ver = BLE_PROTO_VER) {
  Bytes b{ver, seq, idx, total};
  b.insert(b.end(), data.begin(), data.end());
  return b;
}

FrameResult feed(FrameAssembler &a, const Bytes &b) { return a.feed(b.data(), b.size()); }

struct Case {
  std::string name, json;
  int seq = 0;
  std::vector<Bytes> frames;
};

std::vector<Case> loadFixture(const char *path) {
  std::vector<Case> cases;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t sp = line.find(' ');
    const std::string key = line.substr(0, sp);
    const std::string rest = sp == std::string::npos ? "" : line.substr(sp + 1);
    if (key == "case") {
      cases.push_back(Case{});
      cases.back().name = rest;
    } else if (cases.empty()) {
      continue;
    } else if (key == "seq") {
      cases.back().seq = std::stoi(rest);
    } else if (key == "json") {
      cases.back().json = rest;
    } else if (key == "frame") {
      cases.back().frames.push_back(hex(rest));
    }
  }
  return cases;
}

void testGoldenFixture() {
  const std::vector<Case> cases = loadFixture("fixtures/ble-frames.txt");
  CHECK(cases.size() >= 2);  // a missing fixture must fail, not pass vacuously
  for (const Case &c : cases) {
    FrameAssembler a;
    for (size_t i = 0; i < c.frames.size(); i++) {
      const FrameResult r = feed(a, c.frames[i]);
      CHECK(r == (i + 1 < c.frames.size() ? FrameResult::Partial : FrameResult::Complete));
    }
    CHECK(std::string(a.json()) == c.json);
    CHECK(a.size() == c.json.size());
    CHECK(a.seq() == c.seq);
  }
}

void testNewSeqDiscardsPartial() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 2, "aaaa")) == FrameResult::Partial);
  CHECK(feed(a, frame(2, 0, 1, "ok")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "ok");
  CHECK(a.seq() == 2);
}

void testSeqRestartAfterPartial() {  // the helper crashed mid-payload and restarted at seq 0
  FrameAssembler a;
  CHECK(feed(a, frame(5, 0, 2, "half")) == FrameResult::Partial);
  CHECK(feed(a, frame(0, 0, 1, "fresh")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "fresh");
}

void testSeqWrap() {
  FrameAssembler a;
  CHECK(feed(a, frame(255, 0, 1, "a")) == FrameResult::Complete);
  CHECK(feed(a, frame(0, 0, 1, "b")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "b");
}

void testDuplicateChunkIgnored() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 2, "ab")) == FrameResult::Partial);
  CHECK(feed(a, frame(1, 0, 2, "ab")) == FrameResult::Ignored);
  CHECK(feed(a, frame(1, 1, 2, "cd")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "abcd");
}

void testGapIsBadAndRecovers() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 3, "ab")) == FrameResult::Partial);
  CHECK(feed(a, frame(1, 2, 3, "ef")) == FrameResult::Bad);  // idx 1 was skipped
  CHECK(feed(a, frame(2, 0, 1, "good")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "good");
}

void testJoiningMidPayloadIsBad() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 1, 2, "cd")) == FrameResult::Bad);
}

void testBadHeaders() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 1, "x", 2)) == FrameResult::Bad);  // unknown version
  CHECK(feed(a, frame(1, 0, 0, "x")) == FrameResult::Bad);     // total 0
  CHECK(feed(a, frame(1, 0, 17, "x")) == FrameResult::Bad);    // more than 16 chunks
  CHECK(feed(a, frame(1, 3, 3, "x")) == FrameResult::Bad);     // idx >= total
  CHECK(feed(a, frame(1, 0, 1, "")) == FrameResult::Bad);      // header only, no data
  const uint8_t tiny[2] = {1, 2};
  CHECK(a.feed(tiny, 2) == FrameResult::Bad);                  // shorter than the header
  CHECK(a.feed(nullptr, 0) == FrameResult::Bad);
}

void testTotalChangeMidPayloadIsBad() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 3, "ab")) == FrameResult::Partial);
  CHECK(feed(a, frame(1, 1, 4, "cd")) == FrameResult::Bad);
}

void testOverflowIsBadAndRecovers() {
  FrameAssembler a;
  const std::string chunk(200, 'x');
  for (uint8_t i = 0; i < 10; i++) CHECK(feed(a, frame(1, i, 16, chunk)) == FrameResult::Partial);  // 2000 bytes
  CHECK(feed(a, frame(1, 10, 16, chunk)) == FrameResult::Bad);                                       // 2200 > 2048
  CHECK(feed(a, frame(2, 0, 1, "ok")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "ok");
}

void testExactlyMaxPayload() {
  FrameAssembler a;
  const std::string chunk(128, 'y');  // 16 x 128 = 2048
  for (uint8_t i = 0; i < 15; i++) CHECK(feed(a, frame(1, i, 16, chunk)) == FrameResult::Partial);
  CHECK(feed(a, frame(1, 15, 16, chunk)) == FrameResult::Complete);
  CHECK(a.size() == BLE_MAX_PAYLOAD);
  CHECK(a.json()[BLE_MAX_PAYLOAD] == '\0');
}

void testStaleReplayIgnoredUntilCleared() {
  FrameAssembler a;
  CHECK(feed(a, frame(3, 0, 1, "one")) == FrameResult::Complete);
  CHECK(feed(a, frame(3, 0, 1, "one")) == FrameResult::Ignored);  // a late retransmit of a finished payload
  a.clear();                                                      // a new connection starts clean
  CHECK(feed(a, frame(3, 0, 1, "two")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "two");
}

void testClearForgetsLastDone() {  // reconnect after a helper restart that reuses a seq
  FrameAssembler a;
  CHECK(feed(a, frame(9, 0, 2, "ab")) == FrameResult::Partial);
  a.clear();
  CHECK(feed(a, frame(9, 1, 2, "cd")) == FrameResult::Bad);  // the half payload is gone, not resumed
  CHECK(feed(a, frame(9, 0, 1, "ok")) == FrameResult::Complete);
}

}  // namespace

int main() {
  testGoldenFixture();
  testNewSeqDiscardsPartial();
  testSeqRestartAfterPartial();
  testSeqWrap();
  testDuplicateChunkIgnored();
  testGapIsBadAndRecovers();
  testJoiningMidPayloadIsBad();
  testBadHeaders();
  testTotalChangeMidPayloadIsBad();
  testOverflowIsBadAndRecovers();
  testExactlyMaxPayload();
  testStaleReplayIgnoredUntilCleared();
  testClearForgetsLastDone();
  return checksDone("ble_frame_test");
}
```

- [ ] **Step 3: Register the test and run it to see it fail**

In `firmware/sim/Makefile`, change the `TESTS :=` line to also list `build/ble_frame_test` and `build/transport_policy_test` (the latter is built in Task 3; add only `build/ble_frame_test` now):

```make
TESTS := build/portal_util_test build/pomodoro_test build/gesture_test build/deck_test \
         build/pomo_settings_test build/pomo_editor_test build/battery_util_test \
         build/ble_frame_test
```

Run: `cd firmware/sim && make build/ble_frame_test`
Expected: FAIL to compile with `'ble_frame.h' file not found`.

- [ ] **Step 4: Write the implementation** `firmware/src/ble_frame.h`:

```cpp
#pragma once
// BLE payload framing, shared by net_ble.cpp (firmware) and the host tests.
// Pure: no Arduino, no NimBLE. The wire format is in docs/ble-protocol.md and
// is pinned by firmware/sim/fixtures/ble-frames.txt, which the Mac app's tests
// read too.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr uint8_t BLE_PROTO_VER = 1;
constexpr uint8_t BLE_FW_REV = 1;  // bumped when behaviour the Mac can see changes
constexpr size_t BLE_HDR = 4;      // ver, seq, idx, total
constexpr uint8_t BLE_MAX_CHUNKS = 16;
constexpr size_t BLE_MAX_PAYLOAD = 2048;

constexpr uint8_t BLE_CTRL_SEND_NOW = 0x01;  // cube -> Mac: send the payload now
constexpr uint8_t BLE_CTRL_ACK = 0x02;       // cube -> Mac: [0x02, seq] payload reassembled

constexpr char BLE_SERVICE_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a01";
constexpr char BLE_PAYLOAD_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a02";
constexpr char BLE_CONTROL_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a03";
constexpr char BLE_INFO_UUID[] = "6e6d3c10-5d1a-4c1e-9f0b-7c4a2b8e1a04";

enum class FrameResult : uint8_t {
  Partial,   // stored; more chunks expected
  Complete,  // json() now holds a whole payload
  Ignored,   // a duplicate chunk or a retransmit of a finished payload
  Bad,       // malformed, out of order or too large; the partial payload is dropped
};

// Reassembles one payload at a time. ATT writes arrive in order, so chunks
// must too: a repeated idx is Ignored, a gap or a mid-payload join is Bad. A
// new seq discards a half-finished payload. json() is only meaningful right
// after feed() returned Complete.
class FrameAssembler {
 public:
  FrameResult feed(const uint8_t *d, size_t len) {
    if (!d || len <= BLE_HDR) return bad();
    const uint8_t ver = d[0], seq = d[1], idx = d[2], total = d[3];
    if (ver != BLE_PROTO_VER || total == 0 || total > BLE_MAX_CHUNKS || idx >= total) return bad();
    if (seq == lastDone_) return FrameResult::Ignored;

    if (!active_ || seq != seq_) {
      if (idx != 0) return bad();  // joined mid-payload
      active_ = true;
      seq_ = seq;
      total_ = total;
      next_ = 0;
      len_ = 0;
    } else {
      if (total != total_) return bad();
      if (idx < next_) return FrameResult::Ignored;
      if (idx > next_) return bad();
    }

    const size_t n = len - BLE_HDR;
    if (len_ + n > BLE_MAX_PAYLOAD) return bad();
    memcpy(buf_ + len_, d + BLE_HDR, n);
    len_ += n;
    next_++;
    if (next_ == total_) {
      buf_[len_] = '\0';
      active_ = false;
      lastDone_ = seq_;
      return FrameResult::Complete;
    }
    return FrameResult::Partial;
  }

  const char *json() const { return buf_; }
  size_t size() const { return len_; }
  uint8_t seq() const { return seq_; }

  // Drops a half-finished payload; keeps the "already finished" seq so a late
  // retransmit is still ignored.
  void reset() {
    active_ = false;
    len_ = 0;
    next_ = 0;
    buf_[0] = '\0';
  }
  // reset() plus forgetting the last finished seq. Call on every new connection,
  // so a restarted helper that reuses a seq is not ignored.
  void clear() {
    reset();
    lastDone_ = -1;
  }

 private:
  FrameResult bad() {
    reset();
    return FrameResult::Bad;
  }

  char buf_[BLE_MAX_PAYLOAD + 1] = {0};
  size_t len_ = 0;
  bool active_ = false;
  uint8_t seq_ = 0, total_ = 0, next_ = 0;
  int lastDone_ = -1;
};
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd firmware/sim && make build/ble_frame_test && ./build/ble_frame_test`
Expected: `ble_frame_test: N checks, 0 failed`.

- [ ] **Step 6: Write the protocol doc** `docs/ble-protocol.md`:

````markdown
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
````

- [ ] **Step 7: Commit**

```bash
git add docs/ble-protocol.md firmware/src/ble_frame.h firmware/sim/fixtures/ble-frames.txt firmware/sim/tests/ble_frame_test.cpp firmware/sim/Makefile
git commit -m "feat(ble): payload frame assembler, protocol doc and golden fixture"
```

---

## Task 3: Transport policy

**Files:**
- Create: `firmware/src/transport_policy.h`
- Create: `firmware/sim/tests/transport_policy_test.cpp`
- Modify: `firmware/sim/Makefile` (add `build/transport_policy_test` to `TESTS`)

**Interfaces:**
- Produces: constants `BLE_LIVE_MS = 15000`, `BLE_HOLD_MS = 30000`; `struct TransportDecision { bool bleLive; bool wifiOn; }`; `class TransportPolicy { void begin(uint32_t startMs); TransportDecision update(uint32_t now, uint32_t bleLastGood, bool bonded, bool wifiConfigured, bool keepWifi); }`; `inline bool portalOnJoinFail(bool bonded)`; `inline bool pairWaitAtBoot(bool bonded, bool haveWifi)`.
- Task 7 consumes all of these.

- [ ] **Step 1: Write the failing test** `firmware/sim/tests/transport_policy_test.cpp`:

```cpp
#include "check.h"
#include "transport_policy.h"

namespace {

// bonded, wifi configured, no keepWifi: the common case.
TransportDecision upd(TransportPolicy &p, uint32_t now, uint32_t bleGood) {
  return p.update(now, bleGood, true, true, false);
}

void testNoWifiConfiguredNeverTurnsWifiOn() {
  TransportPolicy p;
  p.begin(0);
  for (uint32_t t = 0; t < 120000; t += 5000) {
    const TransportDecision d = p.update(t, 0, true, false, false);
    CHECK(!d.wifiOn);
    CHECK(!d.bleLive);
  }
}

void testNoBondMeansWifiAtOnce() {
  TransportPolicy p;
  p.begin(1000);
  CHECK(p.update(1000, 0, false, true, false).wifiOn);
}

void testNeverLiveUsesStart() {  // bonded, BLE has never delivered: 15 s grace from boot, then WiFi
  TransportPolicy p;
  p.begin(1000);
  CHECK(!upd(p, 1000, 0).wifiOn);
  CHECK(!upd(p, 15999, 0).wifiOn);
  CHECK(upd(p, 16000, 0).wifiOn);
}

void testLiveKeepsWifiOff() {
  TransportPolicy p;
  p.begin(0);
  for (uint32_t t = 5000; t <= 60000; t += 5000) {
    const TransportDecision d = upd(p, t, t);  // a payload every 5 s
    CHECK(d.bleLive);
    CHECK(!d.wifiOn);
  }
}

void testStaleStartsWifiAfter15s() {
  TransportPolicy p;
  p.begin(0);
  CHECK(upd(p, 2000, 2000).bleLive);
  CHECK(upd(p, 16999, 2000).bleLive);   // 14999 ms old
  CHECK(!upd(p, 16999, 2000).wifiOn);
  const TransportDecision d = upd(p, 17000, 2000);  // 15000 ms old
  CHECK(!d.bleLive);
  CHECK(d.wifiOn);
}

void testWifiDropsOnlyAfterBleLive30s() {
  TransportPolicy p;
  p.begin(0);
  CHECK(upd(p, 20000, 0).wifiOn);  // BLE never came: WiFi on
  for (uint32_t t = 20000; t < 50000; t += 5000) CHECK(upd(p, t, t).wifiOn);  // live since 20000, not yet 30 s
  CHECK(upd(p, 45000, 45000).wifiOn);
  CHECK(!upd(p, 50000, 50000).wifiOn);  // 30 s of continuous BLE
}

void testFlappingBleDoesNotDropWifi() {
  TransportPolicy p;
  p.begin(0);
  CHECK(upd(p, 20000, 0).wifiOn);
  CHECK(upd(p, 20000, 20000).wifiOn);   // live again
  CHECK(upd(p, 35000, 20000).wifiOn);   // stale again (15 s): liveSince must reset
  CHECK(upd(p, 36000, 36000).wifiOn);   // live
  CHECK(upd(p, 60000, 60000).wifiOn);   // only 24 s since this live run began
  CHECK(!upd(p, 66000, 66000).wifiOn);  // 30 s
}

void testKeepWifiAlwaysOn() {
  TransportPolicy p;
  p.begin(0);
  for (uint32_t t = 0; t <= 60000; t += 5000) CHECK(p.update(t, t ? t : 0, true, true, true).wifiOn);
}

void testMillisWrap() {  // millis() rolls over after 49 days
  TransportPolicy p;
  p.begin(0xFFFFF000u);
  CHECK(upd(p, 0xFFFFFF00u, 0xFFFFFF00u).bleLive);
  CHECK(upd(p, 0x00000100u, 0xFFFFFF00u).bleLive);          // 512 ms old across the wrap
  CHECK(!upd(p, 0x00000100u, 0xFFFFFF00u).wifiOn);
  CHECK(upd(p, 0x00004000u, 0xFFFFFF00u).wifiOn);           // ~16.6 s old across the wrap
}

void testBootDecisions() {
  CHECK(portalOnJoinFail(false));   // no bond: the portal is the only way to fix WiFi
  CHECK(!portalOnJoinFail(true));   // bonded: a failed join is not an emergency
  CHECK(pairWaitAtBoot(false, false));
  CHECK(!pairWaitAtBoot(false, true));
  CHECK(!pairWaitAtBoot(true, false));
}

}  // namespace

int main() {
  testNoWifiConfiguredNeverTurnsWifiOn();
  testNoBondMeansWifiAtOnce();
  testNeverLiveUsesStart();
  testLiveKeepsWifiOff();
  testStaleStartsWifiAfter15s();
  testWifiDropsOnlyAfterBleLive30s();
  testFlappingBleDoesNotDropWifi();
  testKeepWifiAlwaysOn();
  testMillisWrap();
  testBootDecisions();
  return checksDone("transport_policy_test");
}
```

- [ ] **Step 2: Register and run to see it fail**

Add `build/transport_policy_test` to `TESTS` in `firmware/sim/Makefile`. Run `cd firmware/sim && make build/transport_policy_test`. Expected: FAIL, `'transport_policy.h' file not found`.

- [ ] **Step 3: Write the implementation** `firmware/src/transport_policy.h`:

```cpp
#pragma once
// Which transport feeds the cube. Pure (no Arduino) so it is host-tested.
// BLE is preferred: while a complete payload arrived in the last BLE_LIVE_MS
// the WiFi radio stays off. When BLE goes stale, WiFi comes up and polls the
// bridge; it drops again only after BLE has been live for BLE_HOLD_MS in a
// row, so a flaky link does not flap the radio.

#include <stdint.h>

constexpr uint32_t BLE_LIVE_MS = 15000;
constexpr uint32_t BLE_HOLD_MS = 30000;

struct TransportDecision {
  bool bleLive;  // a payload arrived over BLE within BLE_LIVE_MS
  bool wifiOn;   // WiFi should be up and polling
};

class TransportPolicy {
 public:
  // `startMs` is when the cube booted: the grace period for BLE to deliver its
  // first payload is measured from it.
  void begin(uint32_t startMs) { start_ = startMs; wifiOn_ = false; wasLive_ = false; liveSince_ = 0; }

  // bleLastGood: millis() of the last valid BLE payload, 0 if none yet.
  // bonded: a Mac is bonded. wifiConfigured: there is an SSID to join.
  // keepWifi: keep WiFi up even while BLE is live (OTA needs it).
  TransportDecision update(uint32_t now, uint32_t bleLastGood, bool bonded, bool wifiConfigured, bool keepWifi) {
    const bool live = bleLastGood != 0 && (uint32_t)(now - bleLastGood) < BLE_LIVE_MS;
    if (live && !wasLive_) liveSince_ = now;
    wasLive_ = live;

    if (!wifiConfigured) {
      wifiOn_ = false;
    } else if (keepWifi || !bonded) {
      wifiOn_ = true;  // nobody to wait for, or WiFi explicitly wanted
    } else if (live) {
      if (wifiOn_ && (uint32_t)(now - liveSince_) >= BLE_HOLD_MS) wifiOn_ = false;
    } else {
      const uint32_t ref = bleLastGood ? bleLastGood : start_;
      if ((uint32_t)(now - ref) >= BLE_LIVE_MS) wifiOn_ = true;
    }
    return {live, wifiOn_};
  }

 private:
  uint32_t start_ = 0;
  uint32_t liveSince_ = 0;
  bool wifiOn_ = false;
  bool wasLive_ = false;
};

// A failed WiFi join opens the setup portal by itself only when there is no
// bond: without a Mac the cube has no other way to get data. With a bond the
// portal is still reachable by holding the screen at boot.
inline bool portalOnJoinFail(bool bonded) { return !bonded; }

// First boot of a cube that has neither a Mac nor a network: show the "pair
// with Mac" screen instead of the empty deck.
inline bool pairWaitAtBoot(bool bonded, bool haveWifi) { return !bonded && !haveWifi; }
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd firmware/sim && make test`
Expected: all lines `0 failed`, including `ble_frame_test` and `transport_policy_test`.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/transport_policy.h firmware/sim/tests/transport_policy_test.cpp firmware/sim/Makefile
git commit -m "feat(ble): transport policy (BLE first, WiFi fallback) with host tests"
```

---

## Task 4: Pairing screen and screenshots

**Files:**
- Modify: `firmware/src/ui.h` (declare `uiBlePair`)
- Modify: `firmware/src/ui.cpp` (add `uiBlePair` after `uiPortal`)
- Modify: `firmware/sim/shot.cpp` (`@ble-pair`, `@ble-wait`)

`ui.h` and `ui.cpp` have unrelated uncommitted changes unless Task 0 cleared them; if not, commit with `git add -p`.

**Interfaces:**
- Produces: `void uiBlePair(Display &lcd, uint32_t passkey);` — passkey `0` draws the "waiting" variant, non-zero draws the 6-digit code. Task 7 consumes it.

- [ ] **Step 1: Declare it** in `firmware/src/ui.h`, after the `uiPortal` declaration:

```cpp
// Pairing screen. With a passkey (non-zero) it shows the 6-digit code to type
// on the Mac; with 0 it shows the "waiting for a Mac" variant.
void uiBlePair(Display &lcd, uint32_t passkey);
```

- [ ] **Step 2: Implement it** in `firmware/src/ui.cpp`, directly after `uiPortal()`:

```cpp
void uiBlePair(Display &lcd, uint32_t passkey) {
  LovyanGFX *g = target(lcd);
  g->fillScreen(BG);
  drawCaps(g, "Pair with Mac", LCD_WIDTH / 2, 40, g_palette[ACC_ACCENT], middle_center);

  g->setTextDatum(middle_center);
  if (passkey) {
    char code[8];
    snprintf(code, sizeof(code), "%06u", (unsigned)passkey);
    g->setFont(&V_B24.font);
    g->setTextColor(INK, BG);
    g->drawString(code, LCD_WIDTH / 2, 118);
    drawCaps(g, "Type this on your Mac", LCD_WIDTH / 2, 156, DIM, middle_center);
  } else {
    g->setFont(&V_B18.font);
    g->setTextColor(INK, BG);
    g->drawString("Waiting for a Mac", LCD_WIDTH / 2, 112);
    drawCaps(g, "Open Claude Cube Link", LCD_WIDTH / 2, 150, DIM, middle_center);
  }

  g->setFont(&V_S12.font);
  g->setTextColor(DIM, BG);
  g->drawString("Stuck? Forget Claude Cube", LCD_WIDTH / 2, 226);
  g->drawString("in Mac Bluetooth settings", LCD_WIDTH / 2, 244);

  if (g_sprite) g_canvas.pushSprite(&lcd, 0, 0);
}
```

- [ ] **Step 3: Add the screenshot targets** in `firmware/sim/shot.cpp`, in `renderSpecial()`, after the `"ota"` branch:

```cpp
  } else if (!strcmp(name, "ble-pair")) {
    uiBlePair(lcd, 482913);
  } else if (!strcmp(name, "ble-wait")) {
    uiBlePair(lcd, 0);
```
Also add `@ble-pair` and `@ble-wait` to the comment at the top of `shot.cpp` listing the specials.

- [ ] **Step 4: Build and look at the screenshots**

```bash
cd firmware/sim && make build/cube-shot && ./build/cube-shot build/shot @ble-pair && ./build/cube-shot build/shot @ble-wait
```
Expected: prints `build/shot-ble-pair.png` and `build/shot-ble-wait.png`. Open both (Read tool on the PNGs). Check: the 6-digit code is centred and not clipped at 240 px, no text overlaps, the two hint lines fit inside the width. If a line is clipped, shorten the string, not the font.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/ui.h firmware/src/ui.cpp firmware/sim/shot.cpp
git commit -m "feat(ui): BLE pairing screen with passkey and waiting variant"
```

---

## Task 5: BLE interface, WiFi start/stop, OTA end, simulator stand-ins

**Files:**
- Create: `firmware/src/ble.h`
- Create: `firmware/sim/ble_sim.cpp`
- Modify: `firmware/src/net.h`, `firmware/src/net.cpp` (add `netStart`, `netStop`)
- Modify: `firmware/src/ota.h`, `firmware/src/ota.cpp` (add `otaEnd`)
- Modify: `firmware/sim/net_sim.cpp`, `firmware/sim/ota_sim.cpp`
- Modify: `firmware/sim/Makefile` (add `ble_sim.cpp` to `SRCS`)

**Interfaces:**
- Produces (`ble.h`):

```cpp
enum class BleState : uint8_t { Off, Advertising, Pairing, Connected };
void bleBegin();
BleState bleState();
uint32_t blePasskey();      // 6-digit code while Pairing, else 0
bool bleBonded();
bool bleTake(Payload &out); // true when a new valid payload arrived; out untouched otherwise
uint32_t bleLastGood();     // millis() of the last valid payload, 0 = never
void bleForgetBonds();
```
- Produces: `void netStart();` (non-blocking WiFi.begin), `void netStop();` (WiFi off), `void otaEnd();`.
- Task 6 implements `ble.h` on hardware; Task 7 consumes everything.

- [ ] **Step 1: Write `firmware/src/ble.h`:**

```cpp
#pragma once
#include <stdint.h>

#include "payload.h"

// The BLE side of the cube: a NimBLE GATT server the Mac app writes payloads
// to (see docs/ble-protocol.md). net_ble.cpp is the hardware implementation;
// sim/ble_sim.cpp is the desktop stand-in.

enum class BleState : uint8_t {
  Off,
  Advertising,  // waiting for a Mac
  Pairing,      // a Mac is pairing: blePasskey() is the code to type
  Connected,
};

void bleBegin();  // once, at boot, before anything asks bleBonded()
BleState bleState();
uint32_t blePasskey();  // the 6-digit code while Pairing, else 0
bool bleBonded();       // a Mac is bonded
// True when a complete, valid payload arrived since the last call; it is
// copied to `out`. `out` is untouched otherwise, so the display keeps the last
// good data.
bool bleTake(Payload &out);
uint32_t bleLastGood();  // millis() of the last valid payload, 0 = never
void bleForgetBonds();   // forget the bonded Mac (the caller reboots)
```

- [ ] **Step 2: Non-blocking WiFi.** In `firmware/src/net.h` add:

```cpp
// Starts joining the stored network and returns at once; netOnline() turns
// true when it is up. netBegin() is this plus a wait of up to ~20 s.
void netStart();
// Turns the radio off (BLE is carrying the data).
void netStop();
```

In `firmware/src/net.cpp`, replace the body of `netBegin()`'s setup with a call to `netStart()`:

```cpp
void netStart() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);  // the radio idles between 5s polls
  const Settings &s = settings();
  // A null passphrase is how the WiFi library spells "open network".
  WiFi.begin(s.ssid, s.pass[0] ? s.pass : nullptr);
  Serial.printf("[net] connecting to %s\n", s.ssid);
}

void netStop() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[net] wifi off");
}

bool netBegin() {
  netStart();
  for (int i = 0; i < 80 && WiFi.status() != WL_CONNECTED; i++) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[net] %s  rssi %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }
  fail("wifi timeout");
  Serial.println("[net] could not join");
  return false;
}
```
(Delete the old `netBegin()` body it replaces; leave `netOnline`, `netLastError`, `netFetch` as they are.)

- [ ] **Step 3: OTA end.** `firmware/src/ota.h` add `void otaEnd();` with the comment `// Stops the OTA listener (WiFi is about to go away); otaBegin may be called again later.` In `ota.cpp` add:

```cpp
void otaEnd() { ArduinoOTA.end(); }
```
In `ota_sim.cpp` add `void otaEnd() {}`.

- [ ] **Step 4: Simulator net stand-ins.** In `firmware/sim/net_sim.cpp` add after `netBegin()`:

```cpp
void netStart() { Serial.println("[net] simulator wifi on"); }
void netStop() { Serial.println("[net] simulator wifi off"); }
```

- [ ] **Step 5: Simulator BLE stand-in** `firmware/sim/ble_sim.cpp`. `CUBE_BLE` picks the state: unset/`none` = no Mac (the simulator behaves exactly as before), `live` = bonded and "BLE" delivers a payload every 5 s (fetched from the bridge over HTTP, since a laptop has no radio to this code), `stale` = bonded but nothing arrives, `pair` = a Mac is mid-pairing and the passkey screen shows.

```cpp
// Desktop stand-in for net_ble.cpp. A laptop has no BLE peripheral here, so
// CUBE_BLE selects what the cube believes:
//   (unset)|none  no Mac: the simulator behaves exactly as it did before BLE
//   live          bonded; a payload "arrives over BLE" every 5 s (it is fetched
//                 from the bridge over HTTP, the parsing is the real one)
//   stale         bonded; nothing ever arrives, so WiFi takes over after 15 s
//   pair          a Mac is pairing: the passkey screen shows

#include <Arduino.h>

#include <cstdlib>
#include <cstring>

#include "../src/ble.h"
#include "../src/net.h"

namespace {

enum class Mode { None, Live, Stale, Pair };

Mode mode() {
  static const Mode m = [] {
    const char *e = getenv("CUBE_BLE");
    if (!e) return Mode::None;
    if (!strcmp(e, "live")) return Mode::Live;
    if (!strcmp(e, "stale")) return Mode::Stale;
    if (!strcmp(e, "pair")) return Mode::Pair;
    return Mode::None;
  }();
  return m;
}

uint32_t g_lastGood = 0;
uint32_t g_lastFetch = 0;

}  // namespace

void bleBegin() { Serial.printf("[ble] simulator mode %d (CUBE_BLE=live|stale|pair|none)\n", (int)mode()); }

BleState bleState() {
  switch (mode()) {
    case Mode::Live: return BleState::Connected;
    case Mode::Pair: return BleState::Pairing;
    default: return BleState::Advertising;
  }
}

uint32_t blePasskey() { return mode() == Mode::Pair ? 482913u : 0u; }

bool bleBonded() { return mode() == Mode::Live || mode() == Mode::Stale; }

bool bleTake(Payload &out) {
  if (mode() != Mode::Live) return false;
  const uint32_t now = millis();
  if (g_lastFetch && now - g_lastFetch < 5000) return false;
  g_lastFetch = now ? now : 1;
  if (!netFetch(out)) return false;
  g_lastGood = g_lastFetch;
  return true;
}

uint32_t bleLastGood() { return g_lastGood; }

void bleForgetBonds() { Serial.println("[ble] (sim) forget bonds"); }
```

- [ ] **Step 6: Link it into the simulator.** In `firmware/sim/Makefile` change the `SRCS :=` line's second row to include `ble_sim.cpp`:

```make
        net_sim.cpp touch_sim.cpp settings_sim.cpp portal_sim.cpp ota_sim.cpp battery_sim.cpp ble_sim.cpp sdl_main.cpp
```

- [ ] **Step 7: Verify it all still builds**

```bash
cd firmware/sim && make 2>&1 | tail -3 && make test
cd .. && pio run 2>&1 | tail -3
```
Expected: `built build/cube-sim`; tests `0 failed`; the `pio run` link **fails** on `bleBegin` etc. only if something references `ble.h` already. Nothing does yet, so it succeeds. (`net_ble.cpp` arrives in Task 6.)

- [ ] **Step 8: Commit**

```bash
git add firmware/src/ble.h firmware/src/net.h firmware/src/net.cpp firmware/src/ota.h firmware/src/ota.cpp firmware/sim/ble_sim.cpp firmware/sim/net_sim.cpp firmware/sim/ota_sim.cpp firmware/sim/Makefile
git commit -m "feat(ble): ble.h interface, non-blocking WiFi start/stop, simulator stand-ins"
```

---

## Task 6: NimBLE GATT server on the cube

**Files:**
- Create: `firmware/src/net_ble.cpp`
- Modify: `firmware/platformio.ini` (add `h2zero/NimBLE-Arduino@^2.1.0` to `lib_deps`)

Hardware-only; verified by a successful `pio run` here and on the board in Task 12. NimBLE-Arduino 2.x API names below must match the findings in `docs/superpowers/spikes/2026-10-02-ble-spike.md`; apply the spike's name corrections, not the structure.

**Interfaces:**
- Consumes: `ble.h` (Task 5), `ble_frame.h` (Task 2), `payloadFromJson` (`payload.h`).
- Produces: the implementation of every function in `ble.h`.

- [ ] **Step 1: Add the dependency.** In `firmware/platformio.ini` `lib_deps`:

```ini
lib_deps =
  lovyan03/LovyanGFX@^1.2.0
  bblanchon/ArduinoJson@^7.2.1
  h2zero/NimBLE-Arduino@^2.1.0
```

- [ ] **Step 2: Write `firmware/src/net_ble.cpp`:**

```cpp
#include <Arduino.h>
#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include <esp_random.h>

#include "ble.h"
#include "ble_frame.h"

// GATT server for the Mac app. NimBLE calls these callbacks from its own
// task, so they only copy bytes and set flags; parsing happens in bleTake(),
// on the main loop. See docs/ble-protocol.md.

namespace {

FrameAssembler g_asm;
NimBLECharacteristic *g_control = nullptr;

volatile BleState g_state = BleState::Off;
volatile uint32_t g_passkey = 0;
volatile uint32_t g_lastGood = 0;
volatile bool g_sendNow = false;
int g_bondsAtConnect = 0;

portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
char g_pending[BLE_MAX_PAYLOAD + 1];
size_t g_pendingLen = 0;
volatile bool g_pendingReady = false;

void notifyControl(const uint8_t *data, size_t len) {
  if (!g_control) return;
  g_control->setValue(data, len);
  g_control->notify();
}

struct ServerCb : NimBLEServerCallbacks {
  void onConnect(NimBLEServer *, NimBLEConnInfo &) override {
    g_bondsAtConnect = NimBLEDevice::getNumBonds();
    g_asm.clear();
    g_state = BleState::Connected;
    Serial.printf("[ble] connect (bonds %d)\n", g_bondsAtConnect);
  }

  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    Serial.printf("[ble] disconnect %d\n", reason);
    g_passkey = 0;
    g_state = BleState::Advertising;
    NimBLEDevice::startAdvertising();
  }

  // Display-only IO capability: the stack asks us for the code to show.
  uint32_t onPassKeyDisplay() override {
    g_passkey = esp_random() % 1000000;
    g_state = BleState::Pairing;
    Serial.printf("[ble] pairing passkey %06u\n", (unsigned)g_passkey);
    return g_passkey;
  }

  void onConfirmPassKey(NimBLEConnInfo &info, uint32_t) override { NimBLEDevice::injectConfirmPasskey(info, true); }

  void onAuthenticationComplete(NimBLEConnInfo &info) override {
    g_passkey = 0;
    NimBLEServer *srv = NimBLEDevice::getServer();
    if (!info.isEncrypted()) {  // wrong passkey or refused
      Serial.println("[ble] pairing failed");
      g_state = BleState::Connected;
      srv->disconnect(info.getConnHandle());
      return;
    }
    // One bonded Mac at a time: if a bond already existed and a second device
    // just got through, drop the new one.
    if (g_bondsAtConnect > 0 && NimBLEDevice::getNumBonds() > g_bondsAtConnect) {
      Serial.println("[ble] second Mac refused");
      NimBLEDevice::deleteBond(info.getIdAddress());
      srv->disconnect(info.getConnHandle());
      return;
    }
    g_state = BleState::Connected;
    Serial.println("[ble] encrypted");
  }
};

struct PayloadCb : NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *chr, NimBLEConnInfo &) override {
    const NimBLEAttValue v = chr->getValue();
    const FrameResult r = g_asm.feed(v.data(), v.size());
    if (r == FrameResult::Complete) {
      portENTER_CRITICAL(&g_mux);
      memcpy(g_pending, g_asm.json(), g_asm.size() + 1);
      g_pendingLen = g_asm.size();
      g_pendingReady = true;
      portEXIT_CRITICAL(&g_mux);
      const uint8_t ack[2] = {BLE_CTRL_ACK, g_asm.seq()};
      notifyControl(ack, sizeof(ack));
    } else if (r == FrameResult::Bad) {
      Serial.println("[ble] bad frame dropped");
    }
  }
};

struct ControlCb : NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t subValue) override {
    if (subValue & 1) g_sendNow = true;  // sent from bleTake(), on the main loop
  }
};

ServerCb g_serverCb;
PayloadCb g_payloadCb;
ControlCb g_controlCb;

}  // namespace

void bleBegin() {
  NimBLEDevice::init("Claude Cube");
  NimBLEDevice::setSecurityAuth(true, true, true);  // bonding, MITM, secure connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

  NimBLEServer *server = NimBLEDevice::createServer();
  server->setCallbacks(&g_serverCb);
  NimBLEService *svc = server->createService(BLE_SERVICE_UUID);

  NimBLECharacteristic *payload = svc->createCharacteristic(
      BLE_PAYLOAD_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  payload->setCallbacks(&g_payloadCb);

  g_control = svc->createCharacteristic(BLE_CONTROL_UUID, NIMBLE_PROPERTY::NOTIFY);
  g_control->setCallbacks(&g_controlCb);

  NimBLECharacteristic *info = svc->createCharacteristic(
      BLE_INFO_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);
  const uint8_t infoVal[2] = {BLE_PROTO_VER, BLE_FW_REV};
  info->setValue(infoVal, sizeof(infoVal));

  svc->start();
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(BLE_SERVICE_UUID);  // in the advertisement: the Mac scans by it
  adv->setName("Claude Cube");            // too long for the same packet: goes in the scan response
  adv->enableScanResponse(true);
  adv->start();
  g_state = BleState::Advertising;
  Serial.printf("[ble] advertising, bonds %d\n", NimBLEDevice::getNumBonds());
}

BleState bleState() { return g_state; }

uint32_t blePasskey() { return g_passkey; }

bool bleBonded() { return NimBLEDevice::getNumBonds() > 0; }

uint32_t bleLastGood() { return g_lastGood; }

void bleForgetBonds() {
  NimBLEDevice::deleteAllBonds();
  Serial.println("[ble] bonds deleted");
}

bool bleTake(Payload &out) {
  if (g_sendNow) {
    g_sendNow = false;
    const uint8_t m = BLE_CTRL_SEND_NOW;
    notifyControl(&m, 1);
  }
  if (!g_pendingReady) return false;

  static char local[BLE_MAX_PAYLOAD + 1];
  portENTER_CRITICAL(&g_mux);
  memcpy(local, g_pending, g_pendingLen + 1);
  g_pendingReady = false;
  portEXIT_CRITICAL(&g_mux);

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, local);
  if (err) {
    Serial.printf("[ble] json: %s\n", err.c_str());
    return false;
  }
  Payload p{};
  char why[64] = "";
  if (!payloadFromJson(doc, p, why, sizeof(why))) {
    Serial.printf("[ble] payload rejected: %s\n", why);
    return false;
  }
  out = p;
  const uint32_t now = millis();
  g_lastGood = now ? now : 1;  // 0 means "never"
  return true;
}
```

- [ ] **Step 3: Build**

Run: `cd firmware && pio run 2>&1 | tail -15`
Expected: `[SUCCESS]`. If it fails with NimBLE API errors, open `.pio/libdeps/waveshare-s3-lcd169/NimBLE-Arduino/src/` and fix **names and signatures only** (same corrections the spike recorded). If it fails on flash size, report the numbers; do not drop features.

- [ ] **Step 4: Check the size and heap budget.** Note the flash and RAM percentages from the `pio run` summary in the commit message. `net_ble.cpp` is not yet called from `main.cpp`, so the linker may drop it; that is expected until Task 7.

- [ ] **Step 5: Commit**

```bash
git add firmware/platformio.ini firmware/src/net_ble.cpp
git commit -m "feat(ble): NimBLE GATT server with passkey pairing and payload reassembly"
```

---

## Task 7: Wire the transports into `main.cpp`, portal and boot flow

**Files:**
- Modify: `firmware/src/main.cpp`
- Modify: `firmware/src/portal.cpp` ("Forget paired Mac")
- Modify: `firmware/src/config.h.example`

`main.cpp` may carry unrelated uncommitted changes (see Task 0). Edits below are anchored on exact existing text; apply with the Edit tool.

**Interfaces:**
- Consumes: `ble.h`, `transport_policy.h`, `uiBlePair`, `netStart/netStop/otaEnd`.

- [ ] **Step 1: Config knob.** Append to `firmware/src/config.h.example` after the `POLL_INTERVAL_MS`/`BACKLIGHT` block:

```c
// Keep WiFi up even while the Mac is delivering over Bluetooth. Needed for OTA
// updates (OTA is a WiFi feature); costs power. 0 = WiFi only when BLE is stale.
#define WIFI_ALWAYS_ON       0
```

- [ ] **Step 2: Includes and defaults in `main.cpp`.** Replace

```cpp
#include "battery.h"
#include "config.h"
```
with
```cpp
#include "battery.h"
#include "ble.h"
#include "config.h"
```
and replace `#include "touch.h"\n#include "ui.h"` with
```cpp
#include "touch.h"
#include "transport_policy.h"
#include "ui.h"
```
After the `POMO_TIME_DIV` block (before `namespace {`) add:

```cpp
// Keep WiFi up while BLE is live (OTA needs it). A config.h from before this
// existed still builds.
#ifndef WIFI_ALWAYS_ON
#define WIFI_ALWAYS_ON 0
#endif
```

- [ ] **Step 3: Globals.** Inside the anonymous namespace, after `bool dirty = true;` add:

```cpp
TransportPolicy policy;
bool wifiUp = false;           // netStart() called and not yet netStop()
bool otaUp = false;            // otaBegin() called and not yet otaEnd()
bool pairWaitDismissed = false;  // a touch dismisses the first-boot "waiting for a Mac" screen

// First boot with neither a Mac nor a network and nothing received yet.
bool waitingToPair() { return pairWaitAtBoot(bleBonded(), settingsHaveWifi()) && !payload.valid; }
```

- [ ] **Step 4: Boot-hold hints.** Replace `uiMessage(lcd, "STARTING", "HOLD SCREEN FOR WIFI SETUP");` with `uiMessage(lcd, "STARTING", "HOLD SCREEN FOR SETUP");` and `uiMessage(lcd, "KEEP HOLDING", "FOR WIFI SETUP");` with `uiMessage(lcd, "KEEP HOLDING", "FOR SETUP");`.

- [ ] **Step 5: Touch dismisses the waiting screen.** In `pollTouch()`, right after `const bool down = touch.read(x, y);` add:

```cpp
  if (down && !pairWaitDismissed && waitingToPair()) {
    pairWaitDismissed = true;
    dirty = true;
  }
```

- [ ] **Step 6: New boot sequence.** In `setup()`, replace the block from the comment `// The setup portal is asked for by holding the screen...` through `dirty = true;` that ends `setup()` (the portal call, `CONNECTING`, `netBegin`, `otaBegin`, the first `netFetch` with its `NO BRIDGE` message, and `lastPoll = lastRotate = millis();`) with:

```cpp
  // BLE first: the stack must be up before bleBonded() means anything. The
  // setup portal is asked for by holding the screen; WiFi is joined at boot
  // only when there is no Mac to wait for (the old behaviour, and the old
  // fallback to the portal). With a bond, WiFi comes up later and only if BLE
  // goes quiet (see transport_policy.h).
  bleBegin();
  if (setupHoldRequested()) portalRun(lcd, false);

  if (!bleBonded()) {
    if (settingsHaveWifi()) {
      uiMessage(lcd, "CONNECTING", settings().ssid);
      const bool joined = netBegin();
      wifiUp = true;
      if (!joined && portalOnJoinFail(false)) portalRun(lcd, true);
      if (joined) {
        otaBegin(lcd);
        otaUp = true;
      }
    }
  } else if (!settingsHaveWifi()) {
    uiMessage(lcd, "NO MAC", "WAITING FOR LINK");
  }

  policy.begin(millis());
  // First poll on the first loop pass rather than a full interval from now.
  lastPoll = millis() - POLL_INTERVAL_MS;
  lastRotate = millis();
  dirty = true;
}
```
(The closing `}` of `setup()` stays.)

- [ ] **Step 7: Loop — OTA guard.** Replace the first line of `loop()` `otaHandle();` with `if (otaUp) otaHandle();`.

- [ ] **Step 8: Loop — transport block.** Replace the whole `if (now - lastPoll >= POLL_INTERVAL_MS) { ... }` block (the one calling `netFetch(payload)`) with:

```cpp
  // Which transport feeds the cube (transport_policy.h): BLE while the Mac is
  // delivering, WiFi when it is not.
  const TransportDecision td =
      policy.update(now, bleLastGood(), bleBonded(), settingsHaveWifi(), WIFI_ALWAYS_ON != 0);
  if (td.wifiOn && !wifiUp) {
    netStart();
    wifiUp = true;
  } else if (!td.wifiOn && wifiUp) {
    if (otaUp) {
      otaEnd();
      otaUp = false;
    }
    netStop();
    wifiUp = false;
  }
  if (wifiUp && !otaUp && netOnline()) {
    otaBegin(lcd);
    otaUp = true;
  }

  bool gotData = false;
  const bool wasOnPomodoro = cardIndex == uiPomodoroIndex(payload);
  if (bleTake(payload)) gotData = true;
  if (wifiUp && now - lastPoll >= POLL_INTERVAL_MS) {
    lastPoll = now;
    if (netFetch(payload)) {
      gotData = true;
    } else {
      Serial.printf("[net] fetch failed: %s\n", netLastError());
    }
    dirty = true;
  }
  if (gotData) {
    lastGood = now;
    // The deck may have changed size: keep the Pomodoro card under the user.
    cardIndex = deckKeepIndex(wasOnPomodoro, cardIndex, uiDeckSize(payload));
    dirty = true;
  }
```

- [ ] **Step 9: Loop — draw.** Replace

```cpp
    if (editing && !uiEditorSliding()) uiPomodoroEditor(lcd, editSettings);
    else uiRender(lcd, payload, cardIndex, netOnline(), age, pv, bv);
```
with
```cpp
    if (bleState() == BleState::Pairing) uiBlePair(lcd, blePasskey());  // the code must be seen
    else if (!pairWaitDismissed && waitingToPair()) uiBlePair(lcd, 0);
    else if (editing && !uiEditorSliding()) uiPomodoroEditor(lcd, editSettings);
    else uiRender(lcd, payload, cardIndex, td.bleLive || netOnline(), age, pv, bv);
```
Because the pairing screen is a fresh full-screen frame each draw, also make it redraw when the code appears: in the same `if (dirty || uiAnimating() || now - lastDraw >= 1000)` block this already happens at least once a second.

- [ ] **Step 10: Portal — "Forget paired Mac".** In `firmware/src/portal.cpp` add `#include "ble.h"` after `#include "portal_util.h"`. In `formPage()`, replace the final statement

```cpp
       "<button>Save and reboot</button></form></body></html>";
  return h;
```
with
```cpp
       "<button>Save and reboot</button></form>";
  if (bleBonded())
    h += "<form method=post action=/forget><button style='background:#2a3242;color:#e7ecf5'>"
         "Forget paired Mac</button></form>"
         "<div class=hint>Also remove &ldquo;Claude Cube&rdquo; in the Mac's Bluetooth settings.</div>";
  h += "</body></html>";
  return h;
```
Add the handler before the `handleNotFound` comment:

```cpp
void handleForget() {
  bleForgetBonds();
  server.send(200, "text/html",
              "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
              "<body style='font:16px system-ui;background:#0b0d12;color:#e7ecf5;padding:24px'>"
              "Forgotten. The cube is rebooting and will wait to pair again.</body>");
  delay(1000);
  ESP.restart();
}
```
and register it in `portalRun()` next to the other routes: `server.on("/forget", HTTP_POST, handleForget);`.

- [ ] **Step 11: Build firmware and simulator**

```bash
cd firmware && pio run 2>&1 | tail -4
cd sim && make 2>&1 | tail -3 && make test
```
Expected: `[SUCCESS]`, `built build/cube-sim`, all tests `0 failed`.

- [ ] **Step 12: Exercise the states in the simulator.** Needs the bridge on :8787 (`cd bridge && node server.mjs`).

```bash
cd firmware/sim
make run                      # no Mac: behaves as before, data over HTTP
CUBE_BLE=live  ./build/cube-sim   # log: "[net] simulator wifi off" is NOT printed at boot; data keeps updating; no wifi on
CUBE_BLE=stale ./build/cube-sim   # after 15 s the log prints "[net] simulator wifi on" and data appears
CUBE_BLE=pair  ./build/cube-sim   # the passkey screen 482913 shows
```
Expected for each, per the comments. For `live`, confirm in the log that WiFi is never started and cards update every 5 s. For `stale`, no data for ~15 s, then it fills in. Take `make shot` once for the normal path to confirm no regression.

- [ ] **Step 13: Commit**

```bash
git add firmware/src/main.cpp firmware/src/portal.cpp firmware/src/config.h.example
git commit -m "feat(ble): BLE-first transport selection, boot flow and forget-Mac portal action"
```

---

## Task 8: Mac package — protocol core (framing, backoff, push policy, body validation)

**Files:**
- Create: `mac-helper/Package.swift`
- Create: `mac-helper/Sources/CubeLinkCore/Protocol.swift`
- Create: `mac-helper/Sources/CubeLinkCore/PushPolicy.swift`
- Create: `mac-helper/Sources/CubeLinkCore/Validate.swift`
- Create: `mac-helper/Sources/ClaudeCubeLink/main.swift` (placeholder that compiles; replaced in Task 10)
- Create: `mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift`
- Create: `mac-helper/Tests/CubeLinkCoreTests/PolicyTests.swift`
- Modify: `.gitignore` (append `mac-helper/.build/`, `mac-helper/build/`)

**Interfaces:**
- Produces (module `CubeLinkCore`): `enum CubeProtocol { version, headerSize, maxChunks, maxPayload, serviceUUID, payloadUUID, controlUUID, infoUUID }`; `enum FrameError: Error, Equatable { case empty, tooLarge(Int), mtuTooSmall(Int), tooManyChunks(Int) }`; `func encodeFrames(payload: Data, seq: UInt8, maxWrite: Int) throws -> [Data]`; `enum ControlMessage: Equatable { case sendNow, ack(seq: UInt8) }` with `static func parse(_:) -> ControlMessage?`; `struct Backoff { mutating func next() -> TimeInterval; mutating func reset() }`; `struct PushPolicy { init(heartbeat: TimeInterval = 5); mutating func shouldSend(body: Data?, now: Date, force: Bool) -> Bool; mutating func didSend(body: Data, at: Date) }`; `func validatePayloadBody(_ body: Data) -> String?` (nil = ok).

- [ ] **Step 1: Package manifest** `mac-helper/Package.swift`:

```swift
// swift-tools-version: 6.0
import PackageDescription

let package = Package(
    name: "ClaudeCubeLink",
    platforms: [.macOS(.v13)],
    targets: [
        .target(name: "CubeLinkCore"),
        .executableTarget(name: "ClaudeCubeLink", dependencies: ["CubeLinkCore"]),
        .testTarget(name: "CubeLinkCoreTests", dependencies: ["CubeLinkCore"]),
    ],
    swiftLanguageModes: [.v5]
)
```

`mac-helper/Sources/ClaudeCubeLink/main.swift` (placeholder):

```swift
import CubeLinkCore
print("ClaudeCubeLink placeholder")
```

- [ ] **Step 2: Write the failing tests.** `mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift`:

```swift
import Foundation
import Testing
@testable import CubeLinkCore

struct FixtureCase {
    var name = ""
    var seq = 0
    var chunk = 0
    var json = ""
    var frames: [[UInt8]] = []
}

func loadFixture() throws -> [FixtureCase] {
    // .../mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift -> repo root is 4 levels up
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<4 { root.deleteLastPathComponent() }
    let text = try String(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/ble-frames.txt"), encoding: .utf8)
    var cases: [FixtureCase] = []
    for line in text.split(separator: "\n", omittingEmptySubsequences: true) where !line.hasPrefix("#") {
        let parts = line.split(separator: " ", maxSplits: 1, omittingEmptySubsequences: false)
        let key = String(parts[0])
        let rest = parts.count > 1 ? String(parts[1]) : ""
        switch key {
        case "case": cases.append(FixtureCase(name: rest))
        case "seq": cases[cases.count - 1].seq = Int(rest)!
        case "chunk": cases[cases.count - 1].chunk = Int(rest)!
        case "json": cases[cases.count - 1].json = rest
        case "frame": cases[cases.count - 1].frames.append(rest.split(separator: " ").map { UInt8($0, radix: 16)! })
        default: break
        }
    }
    return cases
}

@Test func encodesTheGoldenFrames() throws {
    let cases = try loadFixture()
    #expect(cases.count >= 2)  // a missing fixture must fail, not pass vacuously
    for c in cases {
        let frames = try encodeFrames(payload: Data(c.json.utf8), seq: UInt8(c.seq), maxWrite: c.chunk + CubeProtocol.headerSize)
        #expect(frames.map { [UInt8]($0) } == c.frames, "case \(c.name)")
    }
}

@Test func rejectsOversize() {
    let big = Data(repeating: 0x78, count: CubeProtocol.maxPayload + 1)
    #expect(throws: FrameError.tooLarge(CubeProtocol.maxPayload + 1)) {
        try encodeFrames(payload: big, seq: 1, maxWrite: 500)
    }
}

@Test func acceptsExactlyMaxPayloadInSixteenChunks() throws {
    let data = Data(repeating: 0x79, count: CubeProtocol.maxPayload)
    let frames = try encodeFrames(payload: data, seq: 1, maxWrite: 128 + CubeProtocol.headerSize)
    #expect(frames.count == 16)
    #expect(frames.last!.count == 128 + CubeProtocol.headerSize)
}

@Test func rejectsMoreThanSixteenChunks() {
    let data = Data(repeating: 0x7a, count: 1000)
    // 1000 bytes at 10 bytes per write = 100 chunks
    #expect(throws: FrameError.tooManyChunks(100)) {
        try encodeFrames(payload: data, seq: 1, maxWrite: 10 + CubeProtocol.headerSize)
    }
}

@Test func rejectsEmptyAndTinyMTU() {
    #expect(throws: FrameError.empty) { try encodeFrames(payload: Data(), seq: 1, maxWrite: 100) }
    #expect(throws: FrameError.mtuTooSmall(4)) { try encodeFrames(payload: Data([1]), seq: 1, maxWrite: 4) }
}

@Test func parsesControlMessages() {
    #expect(ControlMessage.parse(Data([0x01])) == .sendNow)
    #expect(ControlMessage.parse(Data([0x02, 9])) == .ack(seq: 9))
    #expect(ControlMessage.parse(Data([0x02])) == nil)   // ack without a seq
    #expect(ControlMessage.parse(Data([0x7f])) == nil)
    #expect(ControlMessage.parse(Data()) == nil)
}

@Test func validatesBridgeBodies() {
    #expect(validatePayloadBody(Data("{\"v\":1,\"cards\":[]}".utf8)) == nil)
    #expect(validatePayloadBody(Data()) != nil)                                   // empty
    #expect(validatePayloadBody(Data("<html>502 Bad Gateway</html>".utf8)) != nil)  // HTML from a proxy
    #expect(validatePayloadBody(Data("[1,2,3]".utf8)) != nil)                     // JSON, but not an object
    #expect(validatePayloadBody(Data("{\"v\":".utf8)) != nil)                     // truncated
    var huge = "{\"x\":\""
    huge += String(repeating: "a", count: 2100)
    huge += "\"}"
    #expect(validatePayloadBody(Data(huge.utf8)) != nil)                          // over 2 KB
}
```

`mac-helper/Tests/CubeLinkCoreTests/PolicyTests.swift`:

```swift
import Foundation
import Testing
@testable import CubeLinkCore

@Test func pushesFirstBodyThenOnlyOnChangeOrHeartbeat() {
    var p = PushPolicy(heartbeat: 5)
    let t0 = Date(timeIntervalSince1970: 1000)
    let a = Data("a".utf8), b = Data("b".utf8)

    #expect(p.shouldSend(body: a, now: t0, force: false))
    p.didSend(body: a, at: t0)
    #expect(!p.shouldSend(body: a, now: t0.addingTimeInterval(4.9), force: false))   // unchanged, no heartbeat yet
    #expect(p.shouldSend(body: a, now: t0.addingTimeInterval(5), force: false))      // heartbeat
    #expect(p.shouldSend(body: b, now: t0.addingTimeInterval(1), force: false))      // changed
}

@Test func forceSendsUnchangedBody() {
    var p = PushPolicy(heartbeat: 5)
    let t0 = Date(timeIntervalSince1970: 1000)
    let a = Data("a".utf8)
    p.didSend(body: a, at: t0)
    #expect(p.shouldSend(body: a, now: t0.addingTimeInterval(1), force: true))   // the cube asked "send now"
}

@Test func neverSendsWhenBridgeIsDown() {
    var p = PushPolicy(heartbeat: 5)
    let t0 = Date(timeIntervalSince1970: 1000)
    #expect(!p.shouldSend(body: nil, now: t0, force: true))                       // not even on "send now"
    p.didSend(body: Data("a".utf8), at: t0)
    #expect(!p.shouldSend(body: nil, now: t0.addingTimeInterval(60), force: false))  // so the cube sees honest staleness
}

@Test func backoffDoublesToThirtyAndResets() {
    var b = Backoff()
    #expect(b.next() == 1)
    #expect(b.next() == 2)
    #expect(b.next() == 4)
    for _ in 0..<10 { _ = b.next() }
    #expect(b.next() == 30)
    b.reset()
    #expect(b.next() == 1)
}
```

- [ ] **Step 3: Run to see it fail**

Run: `cd mac-helper && swift test 2>&1 | tail -15`
Expected: compile errors `cannot find 'encodeFrames' in scope` etc. If `swift test` cannot find the Testing module, note the toolchain problem in the report and switch the tests to XCTest only if Swift Testing is unavailable.

- [ ] **Step 4: Implement.** `mac-helper/Sources/CubeLinkCore/Protocol.swift`:

```swift
import Foundation

/// Wire constants. Mirrors firmware/src/ble_frame.h and docs/ble-protocol.md;
/// firmware/sim/fixtures/ble-frames.txt pins both sides.
public enum CubeProtocol {
    public static let version: UInt8 = 1
    public static let headerSize = 4
    public static let maxChunks = 16
    public static let maxPayload = 2048

    public static let serviceUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A01"
    public static let payloadUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A02"
    public static let controlUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A03"
    public static let infoUUID = "6E6D3C10-5D1A-4C1E-9F0B-7C4A2B8E1A04"
}

public enum FrameError: Error, Equatable {
    case empty
    case tooLarge(Int)
    case mtuTooSmall(Int)
    case tooManyChunks(Int)
}

/// Splits a payload into frames `[ver, seq, idx, total] + bytes`, each at most
/// `maxWrite` bytes (the peripheral's maximumWriteValueLength).
public func encodeFrames(payload: Data, seq: UInt8, maxWrite: Int) throws -> [Data] {
    guard !payload.isEmpty else { throw FrameError.empty }
    guard payload.count <= CubeProtocol.maxPayload else { throw FrameError.tooLarge(payload.count) }
    let chunk = maxWrite - CubeProtocol.headerSize
    guard chunk > 0 else { throw FrameError.mtuTooSmall(maxWrite) }
    let total = (payload.count + chunk - 1) / chunk
    guard total <= CubeProtocol.maxChunks else { throw FrameError.tooManyChunks(total) }

    var frames: [Data] = []
    for idx in 0..<total {
        let lo = payload.startIndex + idx * chunk
        let hi = min(lo + chunk, payload.endIndex)
        var f = Data([CubeProtocol.version, seq, UInt8(idx), UInt8(total)])
        f.append(payload[lo..<hi])
        frames.append(f)
    }
    return frames
}

/// What the cube sends on the Control characteristic.
public enum ControlMessage: Equatable {
    case sendNow
    case ack(seq: UInt8)

    public static func parse(_ d: Data) -> ControlMessage? {
        let b = [UInt8](d)
        guard let first = b.first else { return nil }
        switch first {
        case 0x01: return .sendNow
        case 0x02: return b.count >= 2 ? .ack(seq: b[1]) : nil
        default: return nil
        }
    }
}

/// Reconnect delays: 1 s, doubling, capped at 30 s.
public struct Backoff {
    private var current: TimeInterval = 1
    public init() {}
    public mutating func next() -> TimeInterval {
        let v = current
        current = min(current * 2, 30)
        return v
    }
    public mutating func reset() { current = 1 }
}
```

`mac-helper/Sources/CubeLinkCore/PushPolicy.swift`:

```swift
import Foundation

/// When to write to the cube: on change, on the 5 s heartbeat, or at once when
/// the cube asks. Never when the bridge gave nothing, so a dead bridge shows
/// up on the cube as stale data instead of being papered over.
public struct PushPolicy {
    public let heartbeat: TimeInterval
    private var lastSent: Date?
    private var lastBody: Data?

    public init(heartbeat: TimeInterval = 5) { self.heartbeat = heartbeat }

    public mutating func shouldSend(body: Data?, now: Date, force: Bool) -> Bool {
        guard let body else { return false }
        if force { return true }
        guard let lastSent, let lastBody else { return true }
        return body != lastBody || now.timeIntervalSince(lastSent) >= heartbeat
    }

    public mutating func didSend(body: Data, at: Date) {
        lastBody = body
        lastSent = at
    }
}
```

`mac-helper/Sources/CubeLinkCore/Validate.swift`:

```swift
import Foundation

/// nil when `body` is something the cube can use; otherwise the reason it is not.
/// Guards against pushing a proxy's HTML error page or an oversized payload.
public func validatePayloadBody(_ body: Data) -> String? {
    if body.isEmpty { return "empty response" }
    if body.count > CubeProtocol.maxPayload {
        return "payload is \(body.count) bytes, over the \(CubeProtocol.maxPayload) byte limit"
    }
    guard let obj = try? JSONSerialization.jsonObject(with: body), obj is [String: Any] else {
        return "response is not a JSON object"
    }
    return nil
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd mac-helper && swift test 2>&1 | tail -8`
Expected: all tests pass, including `encodesTheGoldenFrames` reading the same fixture as the C++ test.

- [ ] **Step 6: Commit**

```bash
git add .gitignore mac-helper/Package.swift mac-helper/Sources mac-helper/Tests
git commit -m "feat(mac): protocol core - frame encoder, control parser, push policy, body validation"
```

---

## Task 9: Bridge client and supervisor

**Files:**
- Create: `mac-helper/Sources/CubeLinkCore/BridgeClient.swift`
- Create: `mac-helper/Sources/CubeLinkCore/BridgeSupervisor.swift`
- Create: `mac-helper/Tests/CubeLinkCoreTests/SupervisorTests.swift`

**Interfaces:**
- Consumes: `Backoff`, `validatePayloadBody`.
- Produces: `final class BridgeClient { init(port: Int); func fetch(_ done: @escaping (Data?, String?) -> Void); func healthy(_ done: @escaping (Bool) -> Void) }`; `enum SupervisorAction: Equatable { case adopt, spawn(node: String), fail(String) }`; `final class BridgeSupervisor { static func decide(externalBridgeUp: Bool, node: String?) -> SupervisorAction; static func findNode(env: [String: String], exists: (String) -> Bool) -> String?; init(bridgeDir: URL, port: Int, node: String?, log: @escaping (String) -> Void); func start(); func stop() }`.

- [ ] **Step 1: Write the failing test** `mac-helper/Tests/CubeLinkCoreTests/SupervisorTests.swift`:

```swift
import Testing
@testable import CubeLinkCore

@Test func adoptsAnExternalBridgeInsteadOfFightingForThePort() {
    #expect(BridgeSupervisor.decide(externalBridgeUp: true, node: "/opt/homebrew/bin/node") == .adopt)
    #expect(BridgeSupervisor.decide(externalBridgeUp: true, node: nil) == .adopt)  // no node needed if it is already running
}

@Test func spawnsWhenNothingIsListening() {
    #expect(BridgeSupervisor.decide(externalBridgeUp: false, node: "/opt/homebrew/bin/node") == .spawn(node: "/opt/homebrew/bin/node"))
}

@Test func failsLoudlyWithoutNode() {
    guard case .fail(let why) = BridgeSupervisor.decide(externalBridgeUp: false, node: nil) else {
        Issue.record("expected .fail")
        return
    }
    #expect(why.contains("CUBE_NODE"))
}

@Test func findsNodeFromEnvironmentThenKnownPaths() {
    let present: Set<String> = ["/usr/local/bin/node", "/custom/node"]
    let exists: (String) -> Bool = { present.contains($0) }
    #expect(BridgeSupervisor.findNode(env: ["CUBE_NODE": "/custom/node"], exists: exists) == "/custom/node")
    #expect(BridgeSupervisor.findNode(env: [:], exists: exists) == "/usr/local/bin/node")
    #expect(BridgeSupervisor.findNode(env: ["CUBE_NODE": "/missing/node"], exists: exists) == "/usr/local/bin/node")  // a bad override falls through
    #expect(BridgeSupervisor.findNode(env: [:], exists: { _ in false }) == nil)
}
```

- [ ] **Step 2: Run to see it fail**

Run: `cd mac-helper && swift test 2>&1 | tail -8`
Expected: `cannot find 'BridgeSupervisor' in scope`.

- [ ] **Step 3: Implement the client** `mac-helper/Sources/CubeLinkCore/BridgeClient.swift`:

```swift
import Foundation

/// Reads the bridge on localhost. The body is passed through byte for byte:
/// the helper never re-encodes the payload, so it cannot drift from the contract.
public final class BridgeClient {
    private let base: URL
    private let session: URLSession

    public init(port: Int) {
        base = URL(string: "http://127.0.0.1:\(port)")!
        let cfg = URLSessionConfiguration.ephemeral
        cfg.timeoutIntervalForRequest = 3
        cfg.waitsForConnectivity = false
        session = URLSession(configuration: cfg)
    }

    /// `done(body, nil)` on success, `done(nil, reason)` otherwise. May be called on any queue.
    public func fetch(_ done: @escaping (Data?, String?) -> Void) {
        session.dataTask(with: base.appendingPathComponent("api/status")) { data, resp, err in
            if let err { return done(nil, err.localizedDescription) }
            guard let http = resp as? HTTPURLResponse else { return done(nil, "no HTTP response") }
            guard http.statusCode == 200 else { return done(nil, "HTTP \(http.statusCode)") }
            let body = data ?? Data()
            if let why = validatePayloadBody(body) { return done(nil, why) }
            done(body, nil)
        }.resume()
    }

    /// Whether something answers /health on the port.
    public func healthy(_ done: @escaping (Bool) -> Void) {
        session.dataTask(with: base.appendingPathComponent("health")) { _, resp, _ in
            done((resp as? HTTPURLResponse)?.statusCode == 200)
        }.resume()
    }
}
```

- [ ] **Step 4: Implement the supervisor** `mac-helper/Sources/CubeLinkCore/BridgeSupervisor.swift`:

```swift
import Foundation

public enum SupervisorAction: Equatable {
    case adopt                // something already serves the port: use it, do not spawn
    case spawn(node: String)
    case fail(String)
}

/// Runs `node server.mjs` as a child of the (Bluetooth-permitted) app and keeps
/// it alive. If a bridge from agent.sh is already running it is adopted instead.
public final class BridgeSupervisor {
    public static func decide(externalBridgeUp: Bool, node: String?) -> SupervisorAction {
        if externalBridgeUp { return .adopt }
        guard let node else { return .fail("node not found: install Node 20+ or set CUBE_NODE to its path") }
        return .spawn(node: node)
    }

    public static func findNode(env: [String: String], exists: (String) -> Bool) -> String? {
        if let p = env["CUBE_NODE"], exists(p) { return p }
        for p in ["/opt/homebrew/bin/node", "/usr/local/bin/node", "/usr/bin/node"] where exists(p) { return p }
        return nil
    }

    private let bridgeDir: URL
    private let port: Int
    private let node: String?
    private let log: (String) -> Void
    private let client: BridgeClient
    private var process: Process?
    private var stopping = false
    private var backoff = Backoff()

    public init(bridgeDir: URL, port: Int, node: String?, log: @escaping (String) -> Void) {
        self.bridgeDir = bridgeDir
        self.port = port
        self.node = node
        self.log = log
        self.client = BridgeClient(port: port)
    }

    /// Call on the main queue.
    public func start() {
        stopping = false
        client.healthy { [weak self] up in
            DispatchQueue.main.async { self?.act(externalUp: up) }
        }
    }

    public func stop() {
        stopping = true
        process?.terminate()
    }

    private func act(externalUp: Bool) {
        guard !stopping else { return }
        switch Self.decide(externalBridgeUp: externalUp, node: node) {
        case .adopt:
            if process == nil {
                log("bridge already running on :\(port); using it")
                recheck(after: 30)  // if it goes away, take over
            }
        case .fail(let why):
            log(why)
            recheck(after: 30)
        case .spawn(let node):
            if process == nil { spawn(node) }
        }
    }

    private func recheck(after seconds: TimeInterval) {
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds) { [weak self] in self?.start() }
    }

    private func spawn(_ node: String) {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: node)
        p.arguments = ["server.mjs"]
        p.currentDirectoryURL = bridgeDir
        var env = ProcessInfo.processInfo.environment
        env["CUBE_PORT"] = String(port)
        p.environment = env
        p.terminationHandler = { [weak self] proc in
            DispatchQueue.main.async { self?.exited(status: proc.terminationStatus) }
        }
        do {
            try p.run()
            process = p
            log("started bridge (pid \(p.processIdentifier)) in \(bridgeDir.path)")
        } catch {
            log("could not start bridge: \(error.localizedDescription)")
            recheck(after: backoff.next())
        }
    }

    private func exited(status: Int32) {
        process = nil
        guard !stopping else { return }
        let delay = backoff.next()
        log("bridge exited (status \(status)); restarting in \(Int(delay)) s")
        recheck(after: delay)
    }
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cd mac-helper && swift test 2>&1 | tail -8`
Expected: all tests pass.

- [ ] **Step 6: Commit**

```bash
git add mac-helper/Sources/CubeLinkCore/BridgeClient.swift mac-helper/Sources/CubeLinkCore/BridgeSupervisor.swift mac-helper/Tests/CubeLinkCoreTests/SupervisorTests.swift
git commit -m "feat(mac): bridge client and node supervisor that adopts an external bridge"
```

---

## Task 10: CoreBluetooth link and app entry point

**Files:**
- Create: `mac-helper/Sources/CubeLinkCore/CubeLink.swift`
- Modify: `mac-helper/Sources/ClaudeCubeLink/main.swift` (replace the placeholder)

`CubeLink` talks to real Bluetooth, so it has no unit test; its pure parts (framing, backoff, push policy) are tested in Task 8. Verification is the hardware checklist in Task 12 and a build here. If the spike's Decision line says the app must be a login item (Q2 failed), also run `NSApplication.shared.run()` in `main.swift` instead of `RunLoop.main.run()`.

**Interfaces:**
- Consumes: `CubeProtocol`, `encodeFrames`, `ControlMessage`, `Backoff`, `PushPolicy`, `BridgeClient`, `BridgeSupervisor`.
- Produces: `final class CubeLink { init(log: @escaping (String) -> Void); var onSendNow: (() -> Void)?; var isReady: Bool { get }; func send(_ payload: Data) }`.

- [ ] **Step 1: Implement `CubeLink`** `mac-helper/Sources/CubeLinkCore/CubeLink.swift`:

```swift
import CoreBluetooth
import Foundation

/// The Bluetooth side: find the cube, pair, then write payloads to it.
///
/// Flow: scan by service UUID (or reconnect by the stored identifier) ->
/// connect -> discover -> READ Info (encrypted, so this is what makes macOS ask
/// for the passkey) -> check the protocol version -> subscribe to Control ->
/// ready. The cube sends "send now" on subscribe and an ACK per payload.
public final class CubeLink: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    public var onSendNow: (() -> Void)?
    public private(set) var isReady = false

    private let log: (String) -> Void
    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var payloadChar: CBCharacteristic?
    private var controlChar: CBCharacteristic?
    private var infoChar: CBCharacteristic?
    private var backoff = Backoff()
    private var seq: UInt8 = 0
    private var awaitingAck: UInt8?
    private var ackTimeout: DispatchWorkItem?
    private var retried = false
    private var lastPayload: Data?

    private let serviceID = CBUUID(string: CubeProtocol.serviceUUID)
    private let payloadID = CBUUID(string: CubeProtocol.payloadUUID)
    private let controlID = CBUUID(string: CubeProtocol.controlUUID)
    private let infoID = CBUUID(string: CubeProtocol.infoUUID)
    private let idKey = "cubeIdentifier"

    public init(log: @escaping (String) -> Void) {
        self.log = log
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: sending

    /// Writes `payload` to the cube. A no-op until the link is ready.
    public func send(_ payload: Data) {
        guard isReady, let p = peripheral, let payloadChar else { return }
        lastPayload = payload
        retried = false
        write(payload, to: p, char: payloadChar)
    }

    private func write(_ payload: Data, to p: CBPeripheral, char: CBCharacteristic) {
        seq &+= 1  // a retry uses a new seq, so the cube never mistakes it for a replay
        do {
            let frames = try encodeFrames(payload: payload, seq: seq,
                                          maxWrite: p.maximumWriteValueLength(for: .withResponse))
            for f in frames { p.writeValue(f, for: char, type: .withResponse) }
            awaitingAck = seq
            ackTimeout?.cancel()
            let item = DispatchWorkItem { [weak self] in self?.ackTimedOut() }
            ackTimeout = item
            DispatchQueue.main.asyncAfter(deadline: .now() + 2, execute: item)
        } catch {
            log("not sending: \(error)")
        }
    }

    private func ackTimedOut() {
        guard awaitingAck != nil, let p = peripheral, let c = payloadChar, let payload = lastPayload else { return }
        awaitingAck = nil
        if !retried {
            retried = true
            log("no ACK, retrying once")
            write(payload, to: p, char: c)
        } else {
            log("no ACK after retry; the next heartbeat will try again")
        }
    }

    // MARK: connecting

    public func centralManagerDidUpdateState(_ c: CBCentralManager) {
        switch c.state {
        case .poweredOn:
            log("Bluetooth on")
            connect()
        case .unauthorized:
            log("Bluetooth permission denied: allow Claude Cube Link in System Settings > Privacy & Security > Bluetooth")
            setReady(false)
        default:
            log("Bluetooth unavailable (state \(c.state.rawValue))")
            setReady(false)
        }
    }

    private func connect() {
        guard central.state == .poweredOn else { return }
        if let s = UserDefaults.standard.string(forKey: idKey), let id = UUID(uuidString: s),
           let known = central.retrievePeripherals(withIdentifiers: [id]).first {
            log("reconnecting to the paired cube")
            peripheral = known
            central.connect(known)  // stays pending until the cube is in range
        } else {
            log("scanning for a cube (put it in pairing mode: no bond on the cube)")
            central.scanForPeripherals(withServices: [serviceID])
        }
    }

    public func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral,
                               advertisementData: [String: Any], rssi: NSNumber) {
        log("found \(p.name ?? "a cube")")
        c.stopScan()
        peripheral = p
        c.connect(p)
    }

    public func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) {
        log("connected")
        p.delegate = self
        p.discoverServices([serviceID])
    }

    public func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) {
        explain(error)
        reconnectLater()
    }

    public func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
        log("disconnected")
        explain(error)
        setReady(false)
        reconnectLater()
    }

    private func reconnectLater() {
        let delay = backoff.next()
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in self?.connect() }
    }

    // MARK: discovery

    public func peripheral(_ p: CBPeripheral, didDiscoverServices error: Error?) {
        guard let svc = p.services?.first(where: { $0.uuid == serviceID }) else {
            log("cube service not found")
            return
        }
        p.discoverCharacteristics([payloadID, controlID, infoID], for: svc)
    }

    public func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
        for ch in s.characteristics ?? [] {
            if ch.uuid == payloadID { payloadChar = ch }
            else if ch.uuid == controlID { controlChar = ch }
            else if ch.uuid == infoID { infoChar = ch }
        }
        guard let info = infoChar, controlChar != nil, payloadChar != nil else {
            log("cube is missing a characteristic")
            return
        }
        log("reading Info; if this is a new cube, macOS now asks for the code shown on its screen")
        p.readValue(for: info)
    }

    // MARK: values

    public func peripheral(_ p: CBPeripheral, didUpdateValueFor ch: CBCharacteristic, error: Error?) {
        if ch.uuid == infoID {
            if let error {
                explain(error)
                return
            }
            let b = [UInt8](ch.value ?? Data())
            guard b.first == CubeProtocol.version else {
                log("cube speaks protocol \(b.first.map(String.init) ?? "?"), this app speaks \(CubeProtocol.version); update one of them")
                return
            }
            UserDefaults.standard.set(p.identifier.uuidString, forKey: idKey)
            if let c = controlChar { p.setNotifyValue(true, for: c) }
        } else if ch.uuid == controlID, let value = ch.value, let msg = ControlMessage.parse(value) {
            switch msg {
            case .sendNow:
                onSendNow?()
            case .ack(let s):
                if awaitingAck == s {
                    awaitingAck = nil
                    ackTimeout?.cancel()
                }
            }
        }
    }

    public func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor ch: CBCharacteristic, error: Error?) {
        if let error {
            explain(error)
            return
        }
        if ch.uuid == controlID, ch.isNotifying {
            log("ready")
            backoff.reset()
            setReady(true)
        }
    }

    public func peripheral(_ p: CBPeripheral, didWriteValueFor ch: CBCharacteristic, error: Error?) {
        if let error { explain(error) }
    }

    // MARK: helpers

    private func setReady(_ ready: Bool) {
        isReady = ready
        if !ready {
            awaitingAck = nil
            ackTimeout?.cancel()
        }
    }

    /// Turns the Bluetooth errors a user can act on into a sentence.
    private func explain(_ error: Error?) {
        guard let error else { return }
        if let e = error as? CBError, e.code == .peerRemovedPairingInformation {
            log("the cube forgot this Mac (its flash was erased or the bond was forgotten). "
                + "Remove \"Claude Cube\" in System Settings > Bluetooth, then pair again.")
        } else if let e = error as? CBATTError,
                  e.code == .insufficientEncryption || e.code == .insufficientAuthentication {
            log("waiting for pairing: enter the code shown on the cube")
        } else {
            log("bluetooth error: \(error.localizedDescription)")
        }
    }
}
```

- [ ] **Step 2: Replace `main.swift`:**

```swift
import CubeLinkCore
import Foundation

setvbuf(stdout, nil, _IOLBF, 0)

func log(_ s: String) {
    let f = ISO8601DateFormatter()
    FileHandle.standardError.write(Data("\(f.string(from: Date())) \(s)\n".utf8))
}

// One instance only: a second launch (e.g. `open` after the LaunchAgent started one) exits quietly.
func acquireSingleInstanceLock() -> Bool {
    let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("ClaudeCubeLink")
    try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    let fd = open(dir.appendingPathComponent("lock").path, O_CREAT | O_RDWR, 0o644)
    return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0  // the fd stays open for the life of the process
}

guard acquireSingleInstanceLock() else {
    log("already running; exiting")
    exit(0)
}

let env = ProcessInfo.processInfo.environment
var port = Int(env["CUBE_PORT"] ?? "") ?? 8787
let argv = CommandLine.arguments
if let i = argv.firstIndex(of: "--port"), i + 1 < argv.count, let p = Int(argv[i + 1]) { port = p }

let bridgeDir = URL(fileURLWithPath: env["CUBE_BRIDGE_DIR"] ?? FileManager.default.currentDirectoryPath + "/bridge")
let node = BridgeSupervisor.findNode(env: env, exists: { FileManager.default.isExecutableFile(atPath: $0) })

let supervisor = BridgeSupervisor(bridgeDir: bridgeDir, port: port, node: node, log: log)
let client = BridgeClient(port: port)
let link = CubeLink(log: log)
var policy = PushPolicy(heartbeat: 5)
var lastBridgeError = ""

func tick(force: Bool) {
    client.fetch { body, why in
        DispatchQueue.main.async {
            if body == nil, let why, why != lastBridgeError {
                lastBridgeError = why
                log("bridge: \(why)")
            }
            if body != nil { lastBridgeError = "" }
            guard link.isReady else { return }
            let now = Date()
            if let body, policy.shouldSend(body: body, now: now, force: force) {
                link.send(body)
                policy.didSend(body: body, at: now)
            }
        }
    }
}

link.onSendNow = { tick(force: true) }
supervisor.start()
Timer.scheduledTimer(withTimeInterval: 5, repeats: true) { _ in tick(force: false) }
log("Claude Cube Link started (bridge :\(port), dir \(bridgeDir.path))")

signal(SIGTERM) { _ in
    supervisor.stop()
    exit(0)
}
RunLoop.main.run()
```

- [ ] **Step 3: Build and run the unit tests**

```bash
cd mac-helper && swift build 2>&1 | tail -8 && swift test 2>&1 | tail -4
```
Expected: `Build complete!` and all tests pass. Fix any Swift 6 toolchain diagnostics in place (language mode is 5; warnings are fine, errors are not). `signal(SIGTERM)` with a closure capturing `supervisor` may need a global handler; if the compiler rejects the capture, use `DispatchSource.makeSignalSource(signal: SIGTERM, queue: .main)` with `signal(SIGTERM, SIG_IGN)` first.

- [ ] **Step 4: Commit**

```bash
git add mac-helper/Sources
git commit -m "feat(mac): CoreBluetooth link and app entry point"
```

---

## Task 11: App bundle and installer

**Files:**
- Create: `mac-helper/Info.plist`
- Create: `mac-helper/build-app.sh`
- Create: `mac-helper/install.sh`

Per the spike's Decision line: if Q2 (LaunchAgent can use Bluetooth) failed, replace the LaunchAgent in `install.sh` with a login item (`osascript -e 'tell application "System Events" to make login item at end with properties {path:"<app>", hidden:true}'`) and keep everything else.

**Interfaces:**
- Consumes: the `ClaudeCubeLink` executable from Task 10.
- Produces: `./install.sh install|status|logs|restart|uninstall`, mirroring `bridge/agent.sh`.

- [ ] **Step 1: `mac-helper/Info.plist`:**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleIdentifier</key><string>com.claude-cube.link</string>
  <key>CFBundleExecutable</key><string>ClaudeCubeLink</string>
  <key>CFBundleName</key><string>Claude Cube Link</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSUIElement</key><true/>
  <key>NSBluetoothAlwaysUsageDescription</key>
  <string>Claude Cube Link sends your Claude usage to the Claude status cube over Bluetooth.</string>
</dict>
</plist>
```

- [ ] **Step 2: `mac-helper/build-app.sh`:**

```bash
#!/bin/bash
# Builds build/ClaudeCubeLink.app: the SwiftPM executable in a bundle with the
# Info.plist that carries the Bluetooth usage description, ad-hoc signed.
set -euo pipefail
cd "$(dirname "$0")"

swift build -c release
APP=build/ClaudeCubeLink.app
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS"
cp .build/release/ClaudeCubeLink "$APP/Contents/MacOS/ClaudeCubeLink"
cp Info.plist "$APP/Contents/Info.plist"
codesign --force --sign - --identifier com.claude-cube.link "$APP"
echo "built $(pwd)/$APP"
```

- [ ] **Step 3: `mac-helper/install.sh`:**

```bash
#!/bin/bash
# Claude Cube Link as a macOS LaunchAgent, like bridge/agent.sh.
#   ./install.sh install | status | logs | restart | uninstall
# The app supervises the Node bridge, so this replaces `bridge/agent.sh install`
# for the Bluetooth setup (an already-running bridge is adopted, not duplicated).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
LABEL="com.claude-cube.link"
APP_DST="$HOME/Applications/ClaudeCubeLink.app"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"
LOG="$HOME/Library/Logs/claude-cube-link.log"
DOMAIN="gui/$(id -u)"

case "${1:-}" in
  install)
    "$HERE/build-app.sh"
    mkdir -p "$HOME/Applications" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
    rm -rf "$APP_DST"
    cp -R "$HERE/build/ClaudeCubeLink.app" "$APP_DST"

    NODE_PATH_FOUND="$(command -v node || true)"
    PORT="${CUBE_PORT:-8787}"
    cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>Label</key><string>$LABEL</string>
  <key>ProgramArguments</key><array><string>$APP_DST/Contents/MacOS/ClaudeCubeLink</string></array>
  <key>EnvironmentVariables</key><dict>
    <key>CUBE_BRIDGE_DIR</key><string>$REPO/bridge</string>
    <key>CUBE_PORT</key><string>$PORT</string>
    <key>CUBE_NODE</key><string>$NODE_PATH_FOUND</string>
  </dict>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict></plist>
EOF

    # macOS shows the Bluetooth permission prompt the first time the app runs
    # from a normal launch, so start it once by hand, then hand over to launchd.
    echo "Starting Claude Cube Link once so macOS can ask for Bluetooth permission."
    echo "Click Allow, then come back here."
    open -a "$APP_DST"
    read -r -p "Press Enter after you have allowed Bluetooth... " _
    pkill -f "$APP_DST/Contents/MacOS/ClaudeCubeLink" || true
    sleep 1
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    launchctl bootstrap "$DOMAIN" "$PLIST"
    echo "Installed. Logs: $0 logs"
    ;;
  status)
    launchctl print "$DOMAIN/$LABEL" 2>/dev/null | grep -E "state|pid|last exit" || echo "not loaded"
    ;;
  logs)
    tail -n 50 -f "$LOG"
    ;;
  restart)
    launchctl kickstart -k "$DOMAIN/$LABEL"
    ;;
  uninstall)
    launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
    rm -f "$PLIST"
    rm -rf "$APP_DST"
    echo "Uninstalled. (Bluetooth permission and the cube's pairing are kept.)"
    ;;
  *)
    echo "usage: $0 install|status|logs|restart|uninstall" >&2
    exit 2
    ;;
esac
```

- [ ] **Step 4: Build the bundle and check it**

```bash
chmod +x mac-helper/build-app.sh mac-helper/install.sh
mac-helper/build-app.sh
codesign -dv mac-helper/build/ClaudeCubeLink.app 2>&1 | head -5
plutil -lint mac-helper/Info.plist
/usr/libexec/PlistBuddy -c "Print :NSBluetoothAlwaysUsageDescription" mac-helper/build/ClaudeCubeLink.app/Contents/Info.plist
```
Expected: `built .../ClaudeCubeLink.app`, `Identifier=com.claude-cube.link`, `Info.plist: OK`, and the usage string printed.

- [ ] **Step 5: Dry-run the installer's non-destructive parts.** Run `mac-helper/install.sh status` (expect `not loaded`) and `mac-helper/install.sh bogus` (expect the usage line, exit 2). The real `install` is exercised on hardware in Task 12.

- [ ] **Step 6: Commit**

```bash
git add mac-helper/Info.plist mac-helper/build-app.sh mac-helper/install.sh
git commit -m "feat(mac): app bundle build and LaunchAgent installer"
```

---

## Task 12: Docs and the hardware acceptance pass

**Files:**
- Modify: `README.md` (new section; has unrelated uncommitted changes unless Task 0 cleared them — use `git add -p`)
- Modify: `CLAUDE.md`
- Modify: `docs/ESP32-S3-Touch-LCD-1.69.md` only if the spike found a board-specific BLE fact worth recording

- [ ] **Step 1: README.** After the `### 2. Firmware` section add:

````markdown
### 3. Pairing over Bluetooth (optional, recommended)

The Mac can push the data to the cube over Bluetooth, so the cube needs no WiFi at all. With WiFi
also configured, it is the automatic fallback when the Mac is out of range or asleep.

```sh
cd mac-helper
./install.sh install      # builds the app, asks for Bluetooth permission, installs the LaunchAgent
./install.sh status|logs|restart|uninstall
```

1. Flash the cube. With no WiFi configured and no Mac paired it shows **Waiting for a Mac**.
2. `./install.sh install`, click **Allow** on the macOS Bluetooth prompt.
3. The cube shows a 6-digit code; macOS asks for it. Type it. That is the whole pairing.
4. From then on it reconnects by itself. The app also runs the Node bridge for you; if you already
   run `bridge/agent.sh`, the app adopts that bridge instead.

The cube takes data from Bluetooth while it arrives (a payload every 5 s) and turns WiFi off. If
nothing arrives for 15 s and WiFi is configured it polls the bridge instead, and goes back to
Bluetooth after it has been steady for 30 s. `WIFI_ALWAYS_ON 1` in `config.h` keeps WiFi up
(needed for OTA while Bluetooth is working).

**Pairing again.** Hold the screen at boot, join the cube's `claude-cube-XXXX` network, press
**Forget paired Mac**, then also remove "Claude Cube" in System Settings > Bluetooth. If the cube's
flash was erased the Mac keeps the old bond and the log says so (`./install.sh logs`).

The protocol is in `docs/ble-protocol.md`.
````

Also: in the README's "Testing without the board" section add `CUBE_BLE=live|stale|pair|none` and `./build/cube-shot build/shot @ble-pair|@ble-wait` next to the other simulator switches, and in "Troubleshooting" add a bullet: "Cube never pairs: check `./install.sh logs`; the app needs the Bluetooth permission (System Settings > Privacy & Security > Bluetooth)."

- [ ] **Step 2: CLAUDE.md.** In "What this is" add a third bullet: `mac-helper/` — Swift app (`ClaudeCubeLink.app`) that supervises the bridge and pushes `/api/status` to the cube over BLE. In "Commands" add `cd mac-helper && swift test`, `./install.sh install|status|logs|restart|uninstall`, and `CUBE_BLE=live|stale|pair|none` for the simulator. In "Architecture" add a **Transports** paragraph:

```markdown
**Transports.** The cube takes the same payload over BLE (preferred) or WiFi polling. BLE: `net_ble.cpp` is a NimBLE GATT server (Payload write / Control notify / Info read, passkey pairing, one bond); `ble_frame.h` (`FrameAssembler`) reassembles chunked JSON and `bleTake()` feeds it to the unchanged `payloadFromJson()`. `transport_policy.h` (pure, host-tested) decides when WiFi runs: off while a BLE payload arrived in the last 15 s, on when it goes stale (or there is no bond), off again after 30 s of steady BLE; `WIFI_ALWAYS_ON` keeps it up for OTA. Boot no longer forces the setup portal when there is no SSID; the portal auto-opens on a failed join only with no bond; "Forget paired Mac" lives in the portal. The wire format is `docs/ble-protocol.md`, pinned by `firmware/sim/fixtures/ble-frames.txt`, which both `make test` and `swift test` read. The Mac side is `mac-helper/` (`BridgeSupervisor` runs `node bridge/server.mjs`, `CubeLink` is CoreBluetooth, `PushPolicy` sends on change / every 5 s / on "send now" and never when the bridge is down). The payload contract and `cards.mjs` are unchanged.
```

- [ ] **Step 3: Run everything automated once more**

```bash
cd firmware/sim && make clean && make test && make 2>&1 | tail -2
cd ../.. && (cd firmware && pio run 2>&1 | tail -3) && (cd mac-helper && swift test 2>&1 | tail -3)
```
Expected: all C++ tests `0 failed`, sim builds, `[SUCCESS]`, Swift tests pass.

- [ ] **Step 4: Hardware acceptance pass** (board + Mac; record each result in a new section of `docs/superpowers/spikes/2026-10-02-ble-spike.md` titled "Acceptance"). Tick each only when observed.

- [ ] First pairing: erase the cube's bond (portal "Forget paired Mac"), `./install.sh install`, code on the cube matches the Mac prompt, cards appear.
- [ ] Wrong passkey: type a wrong code; the cube stays unpaired and shows a fresh code on the next attempt.
- [ ] Reconnect after cube reboot, and after Mac sleep/wake, with no dialog.
- [ ] Out of range or Mac Bluetooth off: after ~15 s with WiFi configured the cube fills from WiFi; back in range it returns to BLE and WiFi drops after ~30 s.
- [ ] No WiFi configured and Mac away: last cards stay, freshness counter ticks, no portal appears.
- [ ] Bridge down (`pkill -f server.mjs` while the app supervises it): the app restarts it; with the bridge held down the cube goes stale and falls back.
- [ ] External bridge: start `bridge/agent.sh install` first, then the app: the log says it adopted it.
- [ ] A second Mac or phone (nRF Connect) cannot pair while a bond exists: cube log shows "second Mac refused".
- [ ] OTA with `WIFI_ALWAYS_ON 1` still works; with 0 and BLE live, confirm OTA is unreachable and write that in the README note.
- [ ] Free heap with BLE and WiFi both up: log `ESP.getFreeHeap()` once from `loop()` temporarily; note the number (remove the line afterwards).
- [ ] Battery/idle draw, BLE-only versus WiFi-only: note the USB meter readings if available.
- [ ] Stale bond: erase flash (`pio run -t erase`), re-flash, run: the app's log shows the "remove Claude Cube in Bluetooth settings" message; after doing so, pairing works.

- [ ] **Step 5: Commit**

```bash
git add README.md CLAUDE.md docs/superpowers/spikes/2026-10-02-ble-spike.md
git commit -m "docs: Bluetooth pairing guide, transports architecture note and acceptance results"
```

---

## Self-review

**Spec coverage**

| Spec requirement | Task |
|---|---|
| GATT layout (Payload/Control/Info), framing, 2 KB / 16 chunks, atomic swap | 2, 6 |
| Passkey pairing, bonding, one Mac, "forget" | 6 (stranger-bond drop), 7 (portal), 4 (screen) |
| Heartbeat 5 s, live 15 s | 8 (`PushPolicy`), 3 |
| BLE-first policy, 15 s / 30 s, no-WiFi mode, `main.cpp` wiring | 3, 7 |
| Boot: no SSID no portal; portal on failed join only with no bond; "SETUP" hint | 3, 7 |
| `NO MAC` message | 7 (boot message in the bonded, no-WiFi case) |
| Simulator `CUBE_BLE`, `@ble-pair` | 4, 5 |
| NimBLE dependency | 6 |
| Mac app: supervisor, client, link, run loop, lock file, install.sh, bundle | 9, 10, 11 |
| Bridge-down → stop heartbeats | 8, 10 |
| Stale-bond detection and message | 10 (and README in 12) |
| Shared fixture + Swift/C++ tests | 2, 8 |
| Manual hardware checklist | 12 |
| Rollout order incl. spike first | 1, then 2–12 |
| Docs (README, CLAUDE.md, protocol doc) | 2, 12 |

**Placeholder scan:** none. The only deliberately open point is NimBLE-Arduino 2.x API naming, which is resolved by the spike and by `pio run` against the installed headers (Tasks 1 and 6).

**Type consistency:** `FrameAssembler` (feed/json/size/seq/reset/clear) is used the same way in Tasks 2 and 6. `TransportPolicy::update(now, bleLastGood, bonded, wifiConfigured, keepWifi)` is identical in Tasks 3 and 7. `ble.h` functions match between Tasks 5, 6 and 7. Swift names (`encodeFrames`, `ControlMessage`, `PushPolicy`, `Backoff`, `validatePayloadBody`, `BridgeSupervisor.decide/findNode`) match between Tasks 8, 9 and 10. UUIDs are identical in `ble_frame.h`, `Protocol.swift`, the spike and the protocol doc.

**Gaps against the spec, flagged above:** `WIFI_ALWAYS_ON` (addition), in-order chunk semantics (clarification), Info-read-triggers-pairing (clarification). Two behaviors changed by the spec itself and implemented as written: the portal no longer auto-opens on a failed join when bonded, and boot no longer forces the portal with no SSID.
