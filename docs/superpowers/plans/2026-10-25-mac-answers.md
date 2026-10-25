# The Mac Answers Cube Prompts — Implementation Plan (4 of 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `ClaudeCubeLink.app` turns a tap on the cube (BLE Control `09`) into one `POST /api/answer` to the bridge on loopback, refuses it while the Mac is locked, tells the cube when the screen is locked (Volume bit 6), polls the bridge faster while an agent waits, and lets the user switch agent tracking from Bridge settings, which is now on by default.

**Architecture:** Every decision goes into `CubeLinkCore` as a pure, tested type: `ControlMessage.answer` (parse), `AnswerPolicy` (locked? already in flight?), `BridgeClient.answerRequest` (the exact HTTP request) and `answerOutcome` (what a status code means), `PollPolicy` (1 s while a tile waits, else 5 s), `BridgeSettings.agents`. `main.swift` only wires them to `CubeLink`, `ScreenLock` and the tick timer. The bridge's `agents` default flips to on. One firmware one-liner parked by plan 3 rides along.

**Tech Stack:** Swift 6 tools / Swift 5 mode, macOS 13, Swift Testing; Go (bridge config default); C++ (one firmware line).

**Spec:** `docs/superpowers/specs/2026-10-24-go-bridge-agents-design.md` (goal 7 Mac side, the lock rule, goal 9's "forwards the new Control answer message"). Plans 1–3 are merged on `route-choice`; the cube already sends Control `09` and reads bit 6 (fw_rev 10, `docs/ble-protocol.md`, fixture `answer` lines in `firmware/sim/fixtures/ble-frames.txt`); the bridge already serves `POST /api/answer` (`bridge/CLAUDE.md`).

## Global Constraints

- Control `09` wire format (fixed by plan 3, `docs/ble-protocol.md`): `09 <la> <agent> <lp> <prompt> <key>`; `la`, `lp` 1..15; ids printable ASCII 0x20–0x7E; `key` one byte `'1'`..`'9'`; the message is exactly `4 + la + lp` bytes. Anything else parses as unknown (nil) and is ignored.
- `POST http://127.0.0.1:<port>/api/answer`, body `{"agent":"…","prompt":"…","key":"…"}`, headers `Content-Type: application/json` and `X-Cube-Client: 1`, no `Origin`; `Host` is what URLSession sends for `127.0.0.1:<port>` (the bridge's loopback guard accepts exactly that). Timeout 5 s. Sent once; never retried, whatever the outcome.
- Bridge answers: 204 answered; 409 not answerable now (moved on, already claimed, not in tmux); 404 agent tracking off or guard refused; 400 bad request; 405; 500 send failed. The app logs one line per answer with the outcome; it never tells the cube anything (the cube learns from the next payload).
- **Not while the Mac is locked:** an answer that arrives while `ScreenLock.isLocked` is dropped and logged, with no HTTP request. Volume flag bit 6 (`0x40`) = the screen is locked, sent whenever it is, independent of "Unlock Mac with cube…" (bit 5 stays `enabled && locked`).
- At most one answer POST in flight; a `09` that arrives meanwhile is dropped and logged.
- Polling: fetch `/api/status` every 1 s while the last payload has an Agents card tile with `"k":"wait"`, else every 5 s. `PushPolicy`'s 5 s heartbeat is unchanged (a changed body is pushed at once).
- `BridgeSettings.agents` (default `true`) → `CUBE_AGENTS` `1`/`0`; the bridge's own `agents` default becomes `true`.
- No `swift test`, `go test`, `make test` regressions. Never run the install scripts, touch `~/.claude/settings.json`, bind 8787 or flash the board while implementing.

## Review Focus

1. **A tap reaches the Mac while it is locked** (the user walked away; someone taps the cube) — expect no HTTP request and one log line. *(Task 2 `lockedAnswerIsDropped`; Task 4 wiring calls `AnswerPolicy` before `BridgeClient`.)*
2. **The same `09` twice** (a BLE retransmit, two quick taps the cube let through) while the first POST is still out — expect one POST. *(Task 2 `secondAnswerWhileInFlightIsDropped`.)*
3. **The bridge is down or slow** — expect one attempt, a 5 s timeout, a logged reason, and no retry. *(Task 2 `answerRequestShape` pins the timeout; Task 4 has no retry path.)*
4. **A malformed `09`** (wrong length byte, trailing byte, key `'0'`, a byte 0x7F, empty id) — expect `parse` to return nil. *(Task 1 `rejectsMalformedAnswers`.)*
5. **Agent tracking switched off in Bridge settings** — expect `CUBE_AGENTS=0` reaching the child, no Agents card, and a tap (from a stale cube screen) logged as "agent tracking is off" from the 404. *(Task 3 `agentsEnv`; Task 2 `outcomeWording`.)*

---

### Task 1: Parse Control `09`; send bit 6

**Files:** Modify `mac-helper/Sources/CubeLinkCore/Protocol.swift`, `Volume.swift`, `mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift` (or a new `AnswerTests.swift`), `VolumeTests.swift`, `firmware/sim/fixtures/ble-frames.txt`, `docs/ble-protocol.md`.

**Interfaces:**
- Produces:
  ```swift
  // ControlMessage gains:
  case answer(agent: String, prompt: String, key: Character)   // Control 09 (fw_rev 10)
  // MacVolume gains (init parameter defaulted false):
  public var screenLocked: Bool   // bit 6 (0x40)
  ```

- [ ] **Step 1: Fixture.** Add `vol_state` lines with bit 6: `vol_state 56 46 "Speakers" 01 38 46 53 70 65 61 6b 65 72 73` and `vol_state 56 66 "Speakers" 01 38 66 53 70 65 61 6b 65 72 73`; extend the `vol_state` comment line with bit 6. Check firmware's `volume_frame_test` still passes (`cd firmware/sim && make build/volume_frame_test && ./build/volume_frame_test`).
- [ ] **Step 2: Failing tests:** `parsesFixtureAnswers` — every `answer` fixture line's bytes parse to `.answer` with the line's agent, prompt and key; `rejectsMalformedAnswers` — nil for: `09` alone; `la` 0; `la` 16; length byte larger than the rest; one trailing byte; key `'0'`, `'a'`; an id byte `0x7F` or `0x19`; `lp` 0; `encodesScreenLocked` — the new fixture lines encode byte-for-byte (`screenLocked` true, `locked` false/true); an existing line still encodes unchanged.
- [ ] **Step 3: Run** `swift test --package-path mac-helper` — FAIL.
- [ ] **Step 4: Implement** the parse case and the flag.
- [ ] **Step 5: Run** `swift test` and `make test` (firmware/sim) — PASS. Update `docs/ble-protocol.md`: `09` and bit 6 are now acted on by the Mac app (drop "ignores it" wording for current versions; keep that an older app ignores them).
- [ ] **Step 6: Commit** `feat(mac-helper): parse Control 09; send the screen-locked bit`.

### Task 2: `AnswerPolicy`, the answer request and its outcome

**Files:** Create `mac-helper/Sources/CubeLinkCore/Answer.swift`, `mac-helper/Tests/CubeLinkCoreTests/AnswerTests.swift`; Modify `BridgeClient.swift`.

**Interfaces:**
- Produces:
  ```swift
  public struct AnswerPolicy {
      public enum Decision: Equatable { case post, ignore(String) }
      public init()
      // ignore("the Mac is locked") when locked; ignore("an answer is still being sent") while one is in flight;
      // otherwise .post and marks one in flight.
      public mutating func decide(locked: Bool) -> Decision
      public mutating func finished()                      // the POST completed (any outcome)
  }
  public enum AnswerOutcome: Equatable { case answered, notNow, trackingOff, rejected(Int), failed(String) }
  // 204 answered; 409 notNow; 404 trackingOff; other status rejected(code); nil status failed(reason)
  public func answerOutcome(status: Int?, error: String?) -> AnswerOutcome
  public func describe(_ o: AnswerOutcome) -> String   // log text, e.g. "answered", "not answerable now (409)", "agent tracking is off (404)"
  extension BridgeClient {
      public static func answerRequest(port: Int, agent: String, prompt: String, key: Character) -> URLRequest
      public func answer(agent: String, prompt: String, key: Character, _ done: @escaping (Int?, String?) -> Void)  // may call on any queue
  }
  ```
- [ ] **Step 1: Failing tests:** `lockedAnswerIsDropped` (`decide(locked: true)` → `.ignore("the Mac is locked")`, nothing marked in flight); `secondAnswerWhileInFlightIsDropped` (post, then post again before `finished` → ignore; after `finished` → post); `answerRequestShape` (URL `http://127.0.0.1:8799/api/answer`, method POST, headers `Content-Type: application/json` and `X-Cube-Client: 1`, no `Origin`, `timeoutInterval` 5, body decodes to exactly `{"agent","prompt","key"}` with the given values); `outcomeWording` (204/409/404/500/nil map as specified, and `describe(.trackingOff)` contains "agent tracking is off").
- [ ] **Step 2–4:** RED, implement (`answer` uses the client's session with the request from `answerRequest`; no retry), GREEN.
- [ ] **Step 5: Commit** `feat(mac-helper): AnswerPolicy and the bridge answer request`.

### Task 3: Agent tracking switch and polling cadence

**Files:** Modify `mac-helper/Sources/CubeLinkCore/BridgeSettings.swift`, `Tests/CubeLinkCoreTests/BridgeSettingsTests.swift`, `Sources/ClaudeCubeLink/BridgeSettingsWindow.swift`, `Sources/CubeLinkCore/MenuModel.swift` (or a new `Poll.swift`), `bridge/internal/config/config.go`, `bridge/internal/config/agents_test.go`, `bridge/config.example.json`.

**Interfaces:**
- Produces:
  ```swift
  // BridgeSettings: public var agents = true   (Codable key "agents", missing -> true; imported from config.json "agents")
  // environment(): CUBE_AGENTS "1"/"0"; "CUBE_AGENTS" in managedKeys
  public enum PollPolicy {
      public static let waiting: TimeInterval = 1, idle: TimeInterval = 5
      public static func agentWaiting(payload: Data?) -> Bool   // a card with an "a" array holding a tile whose "k" == "wait"
      public static func interval(payload: Data?) -> TimeInterval
  }
  ```
  Bridge: `agents` defaults to `true` when neither `CUBE_AGENTS` nor config `agents` is set (`boolean` gains a default, or the field is set after it; env/config `0`/`false` still turn it off).
- [ ] **Step 1: Failing tests:** `agentsEnv` (default settings → `CUBE_AGENTS` = `"1"`; `agents = false` → `"0"`; an inherited `CUBE_AGENTS` is replaced); `agentsDecodesMissingAsTrue` (old JSON without the key); `agentsImported` (`{"agents": false}` → false); `pollFastWhileWaiting` (fixture `firmware/sim/fixtures/agents-full.json` → 1 s; `agents-lan.json` → 1 s; `usage.json` → 5 s; nil/garbage → 5 s). Go: `TestAgentsDefaultOn` (no env, no config → true; `CUBE_AGENTS=0` → false; config `"agents": false` → false).
- [ ] **Step 2–4:** RED, implement (window: a checkbox "Track Claude Code agents (Agents card)" in the grid after the extra-cards one, read/written like `extraCards`), GREEN: `swift test`, `cd bridge && go vet ./... && go test ./...`.
- [ ] **Step 5: Commit** `feat: agent tracking on by default, switchable in Bridge settings; poll fast while an agent waits`.

### Task 4: Wire it in the app

**Files:** Modify `mac-helper/Sources/CubeLinkCore/CubeLink.swift` (dispatch `.answer` to `public var onAnswer: ((String, String, Character) -> Void)?`), `Sources/ClaudeCubeLink/main.swift`.

**Behaviour:**
- `volumeFrame` sets `screenLocked = screenLock.isLocked` (bit 5 unchanged).
- `link.onAnswer`: `answerPolicy.decide(locked: screenLock.isLocked)`; on `.ignore(why)` log `answer for <agent> ignored: <why>`; on `.post`, `client.answer(...)` and on completion (hop to main) `answerPolicy.finished()`, log `answer <key> for <agent>/<prompt>: <describe(outcome)>`, and `tick(force: true)` so the cube sees the new state at once. Never retry.
- Polling: replace the fixed 5 s timer with a 1 s timer (`.common` mode) whose tick fetches only when `now - lastFetch >= PollPolicy.interval(payload: lastBody)`; `PushPolicy` unchanged.
- Agent ids, prompt ids and keys may be logged; prompt text never reaches the app.
- [ ] **Step 1:** Implement; `swift build` and `swift test` — PASS.
- [ ] **Step 2: Check the request against the bridge:** compare `answerRequest` with what `bridge/internal/server/agents_test.go` sends to get a 204 (path, headers, Host, body keys); note the comparison in the report. BLE needs hardware (acceptance).
- [ ] **Step 3: Commit** `feat(mac-helper): answer the cube's prompts through the bridge; poll faster while an agent waits`.

### Task 5: Firmware settle fix (parked from plan 3) and docs

**Files:** Modify `firmware/src/agent_view.h`, `firmware/sim/tests/agent_view_test.cpp`; root `CLAUDE.md`, `mac-helper/CLAUDE.md`, `bridge/CLAUDE.md`, `README.md`, `docs/agents-acceptance.md`.

- [ ] **Step 1: Failing test** `noAnswerSettlesOnReappear`: tap p1 at t, `+ANSWER_WAIT_MS` (NoAnswer), `resettle(now)`, then at `+500 ms` `buttonsOff` is `Settling` and `tap` is false; at `+1000 ms` `NoAnswer` and `tap` true.
- [ ] **Step 2:** In `buttonsOff`, return `Settling` instead of `NoAnswer` while `now - shownAt_ < PROMPT_SETTLE_MS`. `make test`, `pio run` — PASS.
- [ ] **Step 3: Docs:** root `CLAUDE.md` (Agents: on by default, answering path end to end, the Mac's lock refusal and bit 6, 1 s polling while waiting); `mac-helper/CLAUDE.md` (Control `01`–`09`, `AnswerPolicy`, `PollPolicy`, `BridgeSettings.agents`, the tick now 1 s with a 5 s idle fetch); `bridge/CLAUDE.md` (`agents` default on); README (answering from the cube now works: Bluetooth, the Mac app, tmux, Mac unlocked); `docs/agents-acceptance.md` (add: a real answer typed into a tmux pane from the cube; a tap while locked does nothing; the Bridge-settings switch).
- [ ] **Step 4: Run** everything: `make test`, `make bench`, `pio run`, `go vet ./... && go test ./...`, `swift test`.
- [ ] **Step 5: Commit** `fix(firmware): settle a timed-out prompt again when it reappears; docs: answering from the cube`.
