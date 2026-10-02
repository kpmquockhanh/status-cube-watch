# Pomodoro Phase-End Notification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** When a Pomodoro phase ends on the cube, `ClaudeCubeLink.app` shows a macOS notification.

**Architecture:** The cube sends a new 3-byte Control notify `04 <ended> <next>` from the existing `pomo.takeAlert()` block. The Mac parses it into `ControlMessage.pomodoroEnded`, `CubeLink` hands it to a callback, and a `Notifier` posts it through `UNUserNotificationCenter`, gated by a mute checkbox in the menu bar dropdown. Wording and the mute preference are pure and unit-tested in `CubeLinkCore`.

**Tech Stack:** C++17 (firmware, NimBLE, host tests under `firmware/sim`), Swift 6 tools / language mode 5 (SwiftPM, Swift Testing, AppKit, UserNotifications).

**Spec:** `docs/superpowers/specs/2026-10-02-pomodoro-notification-design.md`

## Global Constraints

- Protocol version stays `1`; the new message is additive. `BLE_FW_REV` goes `2` -> `3`.
- Control frame is exactly `04 <ended> <next>`; phase codes `00` focus, `01` short break, `02` long break (equal to `PomoPhase`: `PHASE_FOCUS`, `PHASE_SHORT`, `PHASE_LONG`).
- Notify on every phase end. Fire-and-forget: no ack, no retry, no queue.
- BLE only; WiFi-only setups are out of scope.
- The mute flag is a Mac `UserDefaults` value, default on, shown as a checkable item in the dropdown. It is not a cube setting.
- The cube's own on-screen alert is unchanged.
- Firmware shared with the sim must stay within the `firmware/sim/Arduino.h` shim; `ble_frame.h` stays pure (no Arduino, no NimBLE).
- Mac helper keeps zero third-party dependencies.
- `firmware/src/main.cpp` and `CLAUDE.md` already carry uncommitted edits unrelated to this work. Stage only your own hunks there (`git add -p`); never `git add -A` or `git commit -a`.

## Review Focus

- A `04` frame that is short (`04`, `04 00`) or carries a phase code above `02` must be ignored, never crash or notify (Task 3 tests).
- The cube notifying when no Mac is connected or subscribed must neither crash nor block the main loop (Task 2: `notifyControl` already guards a null characteristic; sim and off stubs; hand check in Task 5).
- Running the Mac binary unbundled (`swift run`) has no bundle identifier, and `UNUserNotificationCenter.current()` traps; `Notifier` must no-op instead (Task 4).
- Notification permission denied or not yet answered: the menu item is disabled with a hint and nothing crashes (Task 4).
- Two phase ends back to back (or a replayed one) must replace the earlier banner, not stack: one fixed request identifier (Task 4).
- Muted means nothing is posted, and the default for a fresh install is on (Task 3 prefs tests).

## File Structure

| File | Responsibility |
|------|----------------|
| `firmware/src/ble_frame.h` | modify: `BLE_CTRL_POMO_ENDED`, `bleEncodePomoEnded`, `BLE_FW_REV` 3 |
| `firmware/sim/fixtures/ble-frames.txt` | modify: `pomo_ended` golden lines |
| `firmware/sim/tests/ble_frame_test.cpp` | modify: fixture + enum-code test |
| `firmware/sim/tests/pomodoro_test.cpp` | modify: test that the post-alert view names the ended/next phase |
| `firmware/src/ble.h` | modify: declare `bleNotifyPomodoro` |
| `firmware/src/net_ble.cpp`, `net_ble_off.cpp`, `firmware/sim/ble_sim.cpp` | modify: real impl and two stubs |
| `firmware/src/main.cpp` | modify: call it from the `takeAlert()` block |
| `mac-helper/Sources/CubeLinkCore/Protocol.swift` | modify: `PomodoroPhase`, `ControlMessage.pomodoroEnded` |
| `mac-helper/Sources/CubeLinkCore/PomodoroNotice.swift` | create: wording + `PomodoroNoticePrefs` |
| `mac-helper/Sources/CubeLinkCore/CubeLink.swift` | modify: `onPomodoroEnded` callback |
| `mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift`, `PomodoroNoticeTests.swift` | modify / create |
| `mac-helper/Sources/ClaudeCubeLink/Notifier.swift` | create: `UNUserNotificationCenter` wrapper |
| `mac-helper/Sources/ClaudeCubeLink/StatusMenu.swift`, `main.swift` | modify: menu item, wiring |
| `docs/ble-protocol.md`, `docs/ble-acceptance.md`, `CLAUDE.md` | modify |

---

### Task 1: Control frame encoding, fixture and protocol doc

**Files:**
- Modify: `firmware/src/ble_frame.h`
- Modify: `firmware/sim/fixtures/ble-frames.txt`
- Modify: `firmware/sim/tests/ble_frame_test.cpp`
- Modify: `docs/ble-protocol.md`

**Interfaces:**
- Consumes: nothing.
- Produces (`ble_frame.h`): `constexpr uint8_t BLE_CTRL_POMO_ENDED = 0x04;`, `constexpr size_t BLE_POMO_ENDED_LEN = 3;`, `inline size_t bleEncodePomoEnded(uint8_t ended, uint8_t next, uint8_t out[BLE_POMO_ENDED_LEN])` returning `BLE_POMO_ENDED_LEN`. Fixture lines `pomo_ended <ended> <next> <hex bytes>` (decimal phase codes), which Task 3 reads from Swift.

- [ ] **Step 1: Write the failing test**

Add to `firmware/sim/tests/ble_frame_test.cpp` (with `#include "pomodoro.h"` next to the other includes, and the two functions inside the anonymous namespace before `}  // namespace`):

```cpp
// The Control byte for a phase is the PomoPhase value itself, so main.cpp can pass the enum.
static_assert(PHASE_FOCUS == 0 && PHASE_SHORT == 1 && PHASE_LONG == 2, "wire phase codes");

void testPomoEndedFixture() {
  std::ifstream f("fixtures/ble-frames.txt");
  std::string line;
  int n = 0;
  while (std::getline(f, line)) {
    if (line.rfind("pomo_ended ", 0) != 0) continue;
    std::istringstream in(line.substr(11));
    unsigned ended = 0, next = 0;
    in >> ended >> next;
    std::string rest;
    std::getline(in, rest);
    const Bytes want = hex(rest);
    uint8_t out[BLE_POMO_ENDED_LEN];
    const size_t len = bleEncodePomoEnded((uint8_t)ended, (uint8_t)next, out);
    CHECK(len == BLE_POMO_ENDED_LEN);
    CHECK(Bytes(out, out + len) == want);
    n++;
  }
  CHECK(n == 4);  // a missing fixture must fail, not pass vacuously
}
```

and in `main()` add `testPomoEndedFixture();` before the `return`.

Append to `firmware/sim/fixtures/ble-frames.txt` (end of file; the existing loaders ignore unknown keys):

```
# Control (cube -> Mac): a Pomodoro phase ended. pomo_ended <ended> <next> <bytes>; 0 focus, 1 short, 2 long.
pomo_ended 0 1 04 00 01
pomo_ended 0 2 04 00 02
pomo_ended 1 0 04 01 00
pomo_ended 2 0 04 02 00
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd firmware/sim && make build/ble_frame_test && ./build/ble_frame_test`
Expected: compile error, `bleEncodePomoEnded` / `BLE_POMO_ENDED_LEN` not declared.

- [ ] **Step 3: Write minimal implementation**

In `firmware/src/ble_frame.h`, change `BLE_FW_REV` to `3`, and after the `BLE_CTRL_SETTINGS` line add:

```cpp
constexpr uint8_t BLE_CTRL_POMO_ENDED = 0x04;  // cube -> Mac: [0x04, ended, next] a Pomodoro phase ended (0 focus, 1 short, 2 long)
constexpr size_t BLE_POMO_ENDED_LEN = 3;

// Writes the Control frame for a finished Pomodoro phase into `out`; returns its length.
inline size_t bleEncodePomoEnded(uint8_t ended, uint8_t next, uint8_t out[BLE_POMO_ENDED_LEN]) {
  out[0] = BLE_CTRL_POMO_ENDED;
  out[1] = ended;
  out[2] = next;
  return BLE_POMO_ENDED_LEN;
}
```

In `docs/ble-protocol.md`, add a row to the Control table:

```
| `04 <ended> <next>` | a Pomodoro phase just ended (added in fw_rev 3, protocol still 1). `<ended>` is the phase that finished and `<next>` the one a long-press would start: `00` focus, `01` short break, `02` long break. Fire and forget: no ack, and it is dropped if no Mac is subscribed |
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd firmware/sim && make test`
Expected: every test binary prints `0 failed`, including `ble_frame_test`.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/ble_frame.h firmware/sim/fixtures/ble-frames.txt firmware/sim/tests/ble_frame_test.cpp docs/ble-protocol.md
git commit -m "feat(ble): Control frame 04 for a finished Pomodoro phase

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Cube sends the event

**Files:**
- Modify: `firmware/src/ble.h`
- Modify: `firmware/src/net_ble.cpp` (before the final `#endif  // CUBE_NO_BLE`)
- Modify: `firmware/src/net_ble_off.cpp`
- Modify: `firmware/sim/ble_sim.cpp`
- Modify: `firmware/src/main.cpp` (the `if (pomo.takeAlert())` block in `loop()`)
- Test: `firmware/sim/tests/pomodoro_test.cpp`

**Interfaces:**
- Consumes: `bleEncodePomoEnded`, `BLE_POMO_ENDED_LEN` (Task 1); `PomoView::phase` / `next` from `pomodoro.h`.
- Produces: `void bleNotifyPomodoro(uint8_t ended, uint8_t next);` in `ble.h`.

- [ ] **Step 1: Write the failing test**

The call site relies on the view right after `takeAlert()` naming the ended phase and what comes next. Pin it. In `firmware/sim/tests/pomodoro_test.cpp` add inside the anonymous namespace:

```cpp
// main.cpp reads view() right after takeAlert() to tell the Mac which phase ended.
void testAlertNamesTheEndedPhase() {
  Rig r;
  r.press();
  r.advance(25 * MIN);
  CHECK(r.p.takeAlert());
  PomoView v = r.p.view();
  CHECK(v.state == POMO_DONE);
  CHECK(v.phase == PHASE_FOCUS);
  CHECK(v.next == PHASE_SHORT);

  r.press();
  r.advance(5 * MIN);
  CHECK(r.p.takeAlert());
  v = r.p.view();
  CHECK(v.phase == PHASE_SHORT);
  CHECK(v.next == PHASE_FOCUS);

  // The fourth focus session is followed by the long break.
  for (int i = 0; i < 3; i++) {
    r.press();
    r.advance(25 * MIN);
    CHECK(r.p.takeAlert());
    if (i < 2) {
      r.press();
      r.advance(5 * MIN);
      CHECK(r.p.takeAlert());
    }
  }
  v = r.p.view();
  CHECK(v.phase == PHASE_FOCUS);
  CHECK(v.next == PHASE_LONG);
}
```

Add `testAlertNamesTheEndedPhase();` to `main()` before the `return`.

- [ ] **Step 2: Run test to verify it fails or exposes a wrong assumption**

Run: `cd firmware/sim && make build/pomodoro_test && ./build/pomodoro_test`
Expected: PASS already (this pins existing behaviour, not new code). If a `CHECK` fails, the state machine does not behave as `pomodoro.h` documents; stop and report it rather than editing `pomodoro.cpp` or the test to fit.

- [ ] **Step 3: Write the implementation**

`firmware/src/ble.h`, after `bleSettingsReply`:

```cpp
// Tells the Mac a Pomodoro phase ended: Control `04 <ended> <next>` with the
// PomoPhase values (0 focus, 1 short, 2 long). Fire and forget; dropped when no
// Mac is subscribed.
void bleNotifyPomodoro(uint8_t ended, uint8_t next);
```

`firmware/src/net_ble.cpp`, just before `#endif  // CUBE_NO_BLE`:

```cpp
void bleNotifyPomodoro(uint8_t ended, uint8_t next) {
  uint8_t m[BLE_POMO_ENDED_LEN];
  const size_t len = bleEncodePomoEnded(ended, next, m);
  notifyControl(m, len);
}
```

`firmware/src/net_ble_off.cpp`, after `bleSettingsReply`:

```cpp
void bleNotifyPomodoro(uint8_t, uint8_t) {}
```

`firmware/sim/ble_sim.cpp`, at the end:

```cpp
void bleNotifyPomodoro(uint8_t ended, uint8_t next) {
  Serial.printf("[ble] (sim) pomodoro ended %u, next %u\n", (unsigned)ended, (unsigned)next);
}
```

`firmware/src/main.cpp`, first lines inside `if (pomo.takeAlert()) {`:

```cpp
    const PomoView ended = pomo.view();  // DONE: phase = the one that just ended
    bleNotifyPomodoro(ended.phase, ended.next);
```

- [ ] **Step 4: Verify everything builds and passes**

Run:
```bash
cd firmware/sim && make test
cd .. && pio run
cd sim && make
```
Expected: `make test` all `0 failed`; `pio run` builds every env, including the `-noble` one that uses `net_ble_off.cpp`; the simulator binary builds. Then `cd firmware/sim && POMO_FAST=60 CUBE_BLE=live make run`, long-press the Pomodoro card to start, and confirm the terminal prints `[ble] (sim) pomodoro ended 0, next 1` when the focus phase ends (then `make clean`).

- [ ] **Step 5: Commit**

Stage your hunks of `main.cpp` only:

```bash
git add firmware/src/ble.h firmware/src/net_ble.cpp firmware/src/net_ble_off.cpp firmware/sim/ble_sim.cpp firmware/sim/tests/pomodoro_test.cpp
git add -p firmware/src/main.cpp   # only the bleNotifyPomodoro hunk
git commit -m "feat(firmware): notify the Mac when a Pomodoro phase ends

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Mac parses the event, wording and mute preference

**Files:**
- Modify: `mac-helper/Sources/CubeLinkCore/Protocol.swift`
- Create: `mac-helper/Sources/CubeLinkCore/PomodoroNotice.swift`
- Modify: `mac-helper/Sources/CubeLinkCore/CubeLink.swift`
- Modify: `mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift`
- Create: `mac-helper/Tests/CubeLinkCoreTests/PomodoroNoticeTests.swift`

**Interfaces:**
- Consumes: fixture lines `pomo_ended <ended> <next> <bytes>` (Task 1).
- Produces:
  - `public enum PomodoroPhase: UInt8, Equatable { case focus = 0, shortBreak = 1, longBreak = 2 }`
  - `ControlMessage.pomodoroEnded(ended: PomodoroPhase, next: PomodoroPhase)`
  - `public struct PomodoroNotice: Equatable { public let title: String; public let body: String; public static func make(ended: PomodoroPhase, next: PomodoroPhase) -> PomodoroNotice }`
  - `public struct PomodoroNoticePrefs { public init(defaults: UserDefaults = .standard); public var enabled: Bool { get nonmutating set } }`
  - `CubeLink.onPomodoroEnded: ((PomodoroPhase, PomodoroPhase) -> Void)?`

- [ ] **Step 1: Write the failing tests**

Append to `mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift`:

```swift
@Test func parsesPomodoroEnded() {
    #expect(ControlMessage.parse(Data([0x04, 0, 1])) == .pomodoroEnded(ended: .focus, next: .shortBreak))
    #expect(ControlMessage.parse(Data([0x04, 2, 0])) == .pomodoroEnded(ended: .longBreak, next: .focus))
    #expect(ControlMessage.parse(Data([0x04])) == nil)        // no phases
    #expect(ControlMessage.parse(Data([0x04, 0])) == nil)     // no next
    #expect(ControlMessage.parse(Data([0x04, 3, 0])) == nil)  // unknown ended phase
    #expect(ControlMessage.parse(Data([0x04, 0, 9])) == nil)  // unknown next phase
}

@Test func pomodoroEndedMatchesTheFirmwareFixture() throws {
    var root = URL(fileURLWithPath: #filePath)
    for _ in 0..<4 { root.deleteLastPathComponent() }
    let text = try String(contentsOf: root.appendingPathComponent("firmware/sim/fixtures/ble-frames.txt"), encoding: .utf8)
    var n = 0
    for line in text.split(separator: "\n") where line.hasPrefix("pomo_ended ") {
        let f = line.split(separator: " ").dropFirst()
        let ended = PomodoroPhase(rawValue: UInt8(f[f.startIndex])!)!
        let next = PomodoroPhase(rawValue: UInt8(f[f.startIndex + 1])!)!
        let bytes = f.dropFirst(2).map { UInt8($0, radix: 16)! }
        #expect(ControlMessage.parse(Data(bytes)) == .pomodoroEnded(ended: ended, next: next))
        n += 1
    }
    #expect(n == 4)  // a missing fixture must fail, not pass vacuously
}
```

Create `mac-helper/Tests/CubeLinkCoreTests/PomodoroNoticeTests.swift`:

```swift
import Foundation
import Testing
@testable import CubeLinkCore

@Test func noticeWording() {
    #expect(PomodoroNotice.make(ended: .focus, next: .shortBreak) == PomodoroNotice(title: "Focus done", body: "Take a short break"))
    #expect(PomodoroNotice.make(ended: .focus, next: .longBreak) == PomodoroNotice(title: "Focus done", body: "Take a long break"))
    #expect(PomodoroNotice.make(ended: .shortBreak, next: .focus) == PomodoroNotice(title: "Break over", body: "Time to focus"))
    #expect(PomodoroNotice.make(ended: .longBreak, next: .focus) == PomodoroNotice(title: "Break over", body: "Time to focus"))
}

@Test func unexpectedCombinationFallsBack() {
    let n = PomodoroNotice.make(ended: .shortBreak, next: .longBreak)
    #expect(n == PomodoroNotice(title: "Pomodoro", body: "Phase finished"))
}

@Test func muteDefaultsOnAndPersists() {
    let defaults = UserDefaults(suiteName: "pomodoro-notice-test-\(UUID().uuidString)")!
    let prefs = PomodoroNoticePrefs(defaults: defaults)
    #expect(prefs.enabled)  // a fresh install notifies
    prefs.enabled = false
    #expect(!PomodoroNoticePrefs(defaults: defaults).enabled)
    prefs.enabled = true
    #expect(PomodoroNoticePrefs(defaults: defaults).enabled)
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cd mac-helper && swift test`
Expected: build error, `PomodoroPhase`, `PomodoroNotice`, `PomodoroNoticePrefs` and `.pomodoroEnded` not found.

- [ ] **Step 3: Write minimal implementation**

`Protocol.swift`: add above `ControlMessage`:

```swift
/// A Pomodoro phase as the cube numbers it (firmware `PomoPhase`).
public enum PomodoroPhase: UInt8, Equatable {
    case focus = 0
    case shortBreak = 1
    case longBreak = 2
}
```

Add `case pomodoroEnded(ended: PomodoroPhase, next: PomodoroPhase)` to `ControlMessage`, and in `parse` before `default`:

```swift
        case 0x04:
            guard b.count >= 3, let e = PomodoroPhase(rawValue: b[1]), let n = PomodoroPhase(rawValue: b[2]) else { return nil }
            return .pomodoroEnded(ended: e, next: n)
```

Create `PomodoroNotice.swift`:

```swift
import Foundation

/// What the Mac says when the cube reports a finished Pomodoro phase.
public struct PomodoroNotice: Equatable {
    public let title: String
    public let body: String

    public init(title: String, body: String) {
        self.title = title
        self.body = body
    }

    public static func make(ended: PomodoroPhase, next: PomodoroPhase) -> PomodoroNotice {
        switch (ended, next) {
        case (.focus, .shortBreak): return PomodoroNotice(title: "Focus done", body: "Take a short break")
        case (.focus, .longBreak): return PomodoroNotice(title: "Focus done", body: "Take a long break")
        case (.shortBreak, .focus), (.longBreak, .focus): return PomodoroNotice(title: "Break over", body: "Time to focus")
        default: return PomodoroNotice(title: "Pomodoro", body: "Phase finished")
        }
    }
}

/// The menu's "Pomodoro notifications" checkbox. On unless the user turned it off.
public struct PomodoroNoticePrefs {
    private let defaults: UserDefaults
    private let key = "pomodoroNotifications"

    public init(defaults: UserDefaults = .standard) { self.defaults = defaults }

    public var enabled: Bool {
        get { defaults.object(forKey: key) as? Bool ?? true }
        nonmutating set { defaults.set(newValue, forKey: key) }
    }
}
```

`CubeLink.swift`: next to `onSendNow` add

```swift
    /// The cube reports a finished Pomodoro phase: (ended, next).
    public var onPomodoroEnded: ((PomodoroPhase, PomodoroPhase) -> Void)?
```

and in the Control `switch msg` add

```swift
            case .pomodoroEnded(let ended, let next):
                onPomodoroEnded?(ended, next)
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd mac-helper && swift test`
Expected: all tests pass, including the three new groups.

- [ ] **Step 5: Commit**

```bash
git add mac-helper/Sources/CubeLinkCore mac-helper/Tests
git commit -m "feat(mac-helper): parse the Pomodoro-ended Control event

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Notifier, menu checkbox and wiring

**Files:**
- Create: `mac-helper/Sources/ClaudeCubeLink/Notifier.swift`
- Modify: `mac-helper/Sources/ClaudeCubeLink/StatusMenu.swift`
- Modify: `mac-helper/Sources/ClaudeCubeLink/main.swift`

**Interfaces:**
- Consumes: `PomodoroNotice.make`, `PomodoroNoticePrefs`, `CubeLink.onPomodoroEnded` (Task 3); `Trace.log(_:_:)`.
- Produces: `Notifier` with `start()`, `refreshAuthorization()`, `post(_:)`, `onAuthorizationChange`; `StatusMenu.prefs`, `.notificationsAllowed`, `.onMenuWillOpen`.

No automated test: this layer is AppKit and a system service. Verification is by hand in Step 4.

- [ ] **Step 1: Create the Notifier**

`mac-helper/Sources/ClaudeCubeLink/Notifier.swift`:

```swift
import CubeLinkCore
import Foundation
import UserNotifications

/// Posts the Pomodoro banner. Needs the app bundle: `UNUserNotificationCenter.current()`
/// traps in an unbundled `swift run` binary, so every call is a no-op without one.
final class Notifier: NSObject, UNUserNotificationCenterDelegate {
    /// True when the user allows notifications; called on the main queue.
    var onAuthorizationChange: (Bool) -> Void = { _ in }

    private var center: UNUserNotificationCenter? {
        Bundle.main.bundleIdentifier == nil ? nil : UNUserNotificationCenter.current()
    }

    func start() {
        guard let center else {
            Trace.log("notify", "no app bundle: notifications disabled")
            return
        }
        center.delegate = self
        center.requestAuthorization(options: [.alert, .sound]) { [weak self] granted, error in
            Trace.log("notify", "authorization granted \(granted), error \(error?.localizedDescription ?? "none")")
            self?.refreshAuthorization()
        }
    }

    func refreshAuthorization() {
        guard let center else { return }
        center.getNotificationSettings { [weak self] s in
            let ok = s.authorizationStatus == .authorized || s.authorizationStatus == .provisional
            DispatchQueue.main.async { self?.onAuthorizationChange(ok) }
        }
    }

    func post(_ notice: PomodoroNotice) {
        guard let center else { return }
        let c = UNMutableNotificationContent()
        c.title = notice.title
        c.body = notice.body
        c.sound = .default
        // One fixed identifier: a newer phase end replaces the banner instead of stacking.
        center.add(UNNotificationRequest(identifier: "pomodoro-phase-end", content: c, trigger: nil)) { error in
            if let error { Trace.log("notify", "post failed: \(error.localizedDescription)") }
        }
    }

    // An accessory (menu bar) app counts as foreground; without this the banner is swallowed.
    func userNotificationCenter(_ center: UNUserNotificationCenter, willPresent notification: UNNotification,
                                withCompletionHandler done: @escaping (UNNotificationPresentationOptions) -> Void) {
        done([.banner, .sound])
    }
}
```

- [ ] **Step 2: Add the menu checkbox**

In `StatusMenu.swift`:

- Make the class `final class StatusMenu: NSObject, NSMenuDelegate`.
- Add properties next to `onShowSettings`:

```swift
    var prefs = PomodoroNoticePrefs()
    var onMenuWillOpen: () -> Void = {}
    /// False when macOS notifications are off for the app; the checkbox is then disabled with a hint.
    var notificationsAllowed = true {
        didSet { styleNoticesItem() }
    }
    private var noticesItem: NSMenuItem?
```

- In `init()`, after `item.menu = menu`, add `menu.delegate = self`.
- In `apply`, after the `Cube settings…` line add:

```swift
        let notices = action("Pomodoro notifications", #selector(toggleNotices), key: "")
        noticesItem = notices
        styleNoticesItem()
        menu.addItem(notices)
```

- Add methods:

```swift
    private func styleNoticesItem() {
        guard let i = noticesItem else { return }
        i.state = prefs.enabled ? .on : .off
        i.title = notificationsAllowed ? "Pomodoro notifications" : "Pomodoro notifications (off in System Settings)"
    }

    @objc func validateMenuItem(_ item: NSMenuItem) -> Bool {
        item !== noticesItem || notificationsAllowed
    }

    @objc private func toggleNotices() {
        prefs.enabled.toggle()
        styleNoticesItem()
    }

    func menuWillOpen(_ menu: NSMenu) { onMenuWillOpen() }
```

- [ ] **Step 3: Wire it in `main.swift`**

After `link.onSendNow = ...` add:

```swift
let notifier = Notifier()
notifier.onAuthorizationChange = { statusMenu.notificationsAllowed = $0 }
statusMenu.onMenuWillOpen = { notifier.refreshAuthorization() }
link.onPomodoroEnded = { ended, next in
    guard statusMenu.prefs.enabled else {
        Trace.log("main", "pomodoro notice muted")
        return
    }
    notifier.post(PomodoroNotice.make(ended: ended, next: next))
}
notifier.start()
```

- [ ] **Step 4: Build and verify by hand**

Run: `cd mac-helper && swift test && ./build-app.sh`
Expected: tests pass, `build/ClaudeCubeLink.app` is produced.

Then, to exercise delivery without a cube, temporarily add under `notifier.start()` in `main.swift`:

```swift
DispatchQueue.main.asyncAfter(deadline: .now() + 3) { link.onPomodoroEnded?(.focus, .shortBreak) }
```

Rebuild with `./build-app.sh`, quit any running copy, and `open build/ClaudeCubeLink.app`. Check, in order:
1. macOS asks for notification permission on first launch; allow it.
2. About 3 s later a "Focus done / Take a short break" banner appears.
3. Untick "Pomodoro notifications" in the dropdown, relaunch with the temporary line still present: no banner, and `~/Library/Logs/claude-cube-link.log` shows `pomodoro notice muted` with Verbose trace on.
4. Re-tick it. In System Settings > Notifications turn Claude Cube Link off, open the dropdown: the item reads "(off in System Settings)" and is greyed out.
5. `swift run` (unbundled) starts without crashing; the log notes "no app bundle".

Then remove the temporary `asyncAfter` line and rebuild.

- [ ] **Step 5: Commit**

```bash
git add mac-helper/Sources/ClaudeCubeLink
git commit -m "feat(mac-helper): Pomodoro-ended banner with a mute checkbox in the menu

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Docs and hardware acceptance

**Files:**
- Modify: `CLAUDE.md` (Transports paragraph and the `mac-helper/` bullet near the top)
- Modify: `docs/ble-acceptance.md`

**Interfaces:**
- Consumes: everything above. Produces: nothing code-facing.

- [ ] **Step 1: Update `CLAUDE.md`**

In the `mac-helper/` bullet, extend the menu description with: "a 'Pomodoro notifications' checkbox (macOS banner when the cube reports a finished phase; `Notifier.swift`, wording in `PomodoroNotice`)". In the Transports paragraph, after the Settings sentence, add: "The cube also notifies Control `04 <ended> <next>` when a Pomodoro phase ends (`bleNotifyPomodoro`, called from the `takeAlert()` block in `main.cpp`); the Mac turns it into a banner, BLE only, fire and forget." Stage with `git add -p CLAUDE.md` if it still has unrelated uncommitted edits.

- [ ] **Step 2: Add acceptance items**

Append a section to `docs/ble-acceptance.md`:

```markdown
## Pomodoro notification

- [ ] With the cube connected over BLE, let a focus phase end (set a 1 minute focus in the Pomodoro editor): a "Focus done" banner appears on the Mac as the cube alerts.
- [ ] The break ending gives "Break over / Time to focus"; the fourth focus gives "Take a long break".
- [ ] Unticking "Pomodoro notifications" in the menu silences it; the cube alert still happens.
- [ ] Mac asleep or out of range when the phase ends: nothing is queued and nothing crashes on either side.
```

- [ ] **Step 3: Final check and commit**

Run: `cd firmware/sim && make test && cd ../../mac-helper && swift test`
Expected: all pass.

```bash
git add docs/ble-acceptance.md
git add -p CLAUDE.md
git commit -m "docs: Pomodoro notifications in CLAUDE.md and the BLE acceptance list

Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-Review

- **Spec coverage:** protocol row and fixture (Task 1); `bleNotifyPomodoro` plus stubs, `main.cpp` call, `fw_rev` bump (Tasks 1-2); `ControlMessage.pomodoroEnded`, `PomodoroNotice` table and fallback, `onPomodoroEnded` (Task 3); `Notifier`, authorization at launch, dropdown checkbox in `UserDefaults` default on, denied-permission hint (Task 4); tests, `docs/ble-acceptance.md`, `CLAUDE.md` (Tasks 1-5). The spec's "pure helper next to `ble_frame.h`" is `bleEncodePomoEnded` in that header. The spec's `README.md` line was conditional on it listing menu items; it does not, so it is not touched.
- **Deviation from the spec:** the golden frames are four `pomo_ended` lines appended to `ble-frames.txt` rather than a new `case`, because both existing loaders require every `case` to be a reassembly case; unknown keys are ignored by both.
- **Type consistency:** `PomodoroPhase` / `.pomodoroEnded(ended:next:)` / `PomodoroNotice.make(ended:next:)` / `onPomodoroEnded` match across Tasks 3-4. `bleNotifyPomodoro(uint8_t, uint8_t)` and `bleEncodePomoEnded` match across Tasks 1-2.
