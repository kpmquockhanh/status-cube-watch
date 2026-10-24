# Agents on the Cube — Implementation Plan (3 of 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The cube draws the bridge's Agents card (a scrolling 2×2 grid of tiles with pixel critters), opens a waiting agent's prompt with one button per option, sends a tapped answer to the Mac as Control `09`, and alerts (wake, jump to the prompt, `Attention` beep) once per new prompt.

**Architecture:** All decisions live in new pure headers with host tests: `agents_layout.h` (grid and prompt geometry, vertical scroll), `agent_view.h` (which prompt screen shows, which buttons are live, the settle and sent states), `agent_alert.h` (when to wake, jump and beep), `critter.h/.cpp` (agents-deck's critter as cell rectangles), `answer_frame.h` (Control `09`). `payload.cpp` parses `a`/`q`/`qc` into one `AgentsData` per payload. `ui.cpp` only draws; `main.cpp` wires gestures, the alert and the screen priority. The Mac side of answering (Control `09` → `POST /api/answer`, the lock policy, the Bridge-settings switch for `CUBE_AGENTS`) is plan 4; until then a tap is sent and the cube reports "no answer from Mac" after 8 s.

**Tech Stack:** C++ (board `gnu++11`, host tests C++17), LovyanGFX, ArduinoJson 7, NimBLE-Arduino 2.x, the SDL simulator; one Go test in the bridge.

**Spec:** `docs/superpowers/specs/2026-10-24-go-bridge-agents-design.md` (goals 5, 6, 7 cube side, 7a, 8). The payload contract is what plan 2 shipped: root `CLAUDE.md` "Payload contract", `bridge/internal/cards/agents.go`. Plan 2's leftovers for this plan are in the user's memory note `go-bridge-plan2-notes.md` and are folded in below.

## Global Constraints

- Firmware rules (`firmware/CLAUDE.md`): pure logic header-only or a small `.cpp`, no Arduino calls, `now` passed in, unsigned elapsed time; one `sim/tests/<name>_test.cpp` each, added to `TESTS`; a new `.cpp` used by `main.cpp`/`ui.cpp` goes in `SRCS`, `BENCH_SRCS` and (if `ui.cpp`/`shot.cpp` need it) `SHOT_OBJ`; pure headers must compile as `gnu++11` (`constexpr` functions are one `return`) — run `pio run` after touching one.
- No formatting in the firmware: every string drawn comes from the payload or is fixed UI copy listed in this plan. The firmware maps `p` (pose) to a drawing and `k` to colours; it never builds status words, counts or percentages.
- Field sizes are the bridge's (`agents.go` const block) plus one byte: tile `i` 16, `n` 17, `l` 17, `p` 9; prompt `i` 16, `p` 16, `t` 41, `d` 121, `c` 41, options ≤ 6 with `k` 8, `l` 41. `MAX_CARDS` stays 8.
- `Payload` grows by ~1.2 KB: no `Payload` may live on a stack any more (loop stack 8 KB, fetch task 8 KB). `payloadFromJson` fills `out` in place; every caller parses into a global or `static` scratch.
- Prompt text is drawn only from a payload that arrived over BLE. A payload from WiFi (the LAN variant) never has `q`; the cube shows `needs you` / `open on Bluetooth` for it (spec 7a).
- An answer is sent only when all hold: the payload came over BLE, the prompt's kind is not `input`, `x` is empty, the Mac is not known to be locked, the prompt id has been on screen for `PROMPT_SETTLE_MS` = 1000 ms, and no answer for that prompt id is outstanding. One tap sends one Control `09`; nothing is resent automatically.
- Control `09` (new, fw_rev 10): `09 <la> <agent> <lp> <prompt> <key>`: `la`, `lp` = byte lengths 1..15, ids as printable ASCII, `key` one ASCII byte `'1'`..`'9'`. At most 34 bytes. Volume flag **bit 6** (new, fw_rev 10): the Mac's screen is locked (whether or not it unlocks for Control `07`). `BLE_FW_REV` 9 → 10.
- Tests never need hardware; `make test`, `make bench`, `pio run`, `swift test` (unchanged Swift code must keep reading the fixture) and `go test ./...` stay green.

## Review Focus

1. **The prompt changes under a finger** — a new prompt (or the alert's jump) arrives just as the user taps: expect no Control `09` for a prompt id that has been on screen < 1 s. *(Task 4 `settleBlocksEarlyTap`, `newIdResettles`; Task 8 checks the jump goes through `AgentView`.)*
2. **Busy agents and screen sleep** — four agents whose tool, pose, line and context change every few seconds: expect the screen still to sleep. *(Task 1 `readingsIgnoreAgentChurn`; Task 8 bench `agents` with `mustSleep`.)*
3. **WiFi-only cube with a waiting agent** — expect an orange tile, a tap shows `needs you` / `open on Bluetooth`, no prompt text, no buttons; the alert still wakes and jumps (to that screen). *(Task 4 `wifiShowsOpenOnBluetooth`; Task 5 `waitingTileWithoutPromptAlerts`.)*
4. **Two agents waiting** — expect only the longest-waiting (`q.i`) to open its prompt; tapping the other orange tile shows `needs you` / `answer <name> first`; after the first is answered the second becomes `q` and alerts once. *(Task 4 `otherWaitingTileIsQueued`; Task 5 `nextPromptAlertsOnce`.)*
5. **The alert while something else owns the screen** — navigation, the open display panel, a finger down, a pairing screen, the unlock prompt: expect no jump and no beep until it clears, then one alert if the prompt is still waiting, none if it went away. *(Task 5 `blockedAlertWaits`, `blockedAlertDropsWhenAnswered`.)*

---

## File map

| File | Responsibility |
|---|---|
| `firmware/src/payload.h/.cpp` | `AgentsData` (tiles, prompt, flags), parse `a`/`q`/`qc`/`cut` in place |
| `firmware/src/readings_key.h` | Agents part of the sleep key |
| `firmware/src/critter.h/.cpp` | pose → cell rectangles (port of agents-deck `critter.c`, Claude block only) |
| `firmware/src/agents_layout.h` | grid geometry, `ScrollY`, `PromptLayout` |
| `firmware/src/agent_view.h` | `AgentView`: grid/prompt screen state, button policy, settle, sent |
| `firmware/src/agent_alert.h` | `AgentAlert`: new-prompt detection, blockers, one alert per prompt |
| `firmware/src/idle_sleep.h` | `wake(now)` |
| `firmware/src/answer_frame.h`, `volume_frame.h`, `ble_frame.h` | Control `09` encoder, Volume bit 6, fw_rev 10 |
| `firmware/src/ble.h`, `net_ble.cpp`, `sim/ble_sim.cpp`, `sim/bench.cpp` | `bleSendAnswer` |
| `firmware/src/ui.h/.cpp` | Agents card, prompt screen, `uiPromptMetrics` |
| `firmware/src/main.cpp` | gestures, alert, screen priority, transport of the payload |
| `firmware/sim/*` | fixtures, `@prompt-*` shots, `CUBE_PAYLOAD*`, bench scenarios |
| `bridge/internal/cards/cards_test.go` | `MAX_CARDS` pin |
| `bridge/preview.html` | grid + prompt mirror |
| docs | `docs/ble-protocol.md`, root and `firmware/CLAUDE.md`, README, `docs/agents-acceptance.md` |

---

### Task 1: Parse the Agents card; the sleep key; the card-count pin

**Files:**
- Modify: `firmware/src/payload.h`, `payload.cpp`, `readings_key.h`, `net_ble.cpp` (`bleTake` scratch), `net.cpp`, `sim/net_sim.cpp`, `sim/bench.cpp`, `sim/shot.cpp` (callers of `payloadFromJson`), `sim/Makefile`
- Create: `firmware/sim/tests/payload_test.cpp`, `firmware/sim/fixtures/agents-full.json`, `agents-lan.json`
- Modify: `firmware/sim/tests/readings_key_test.cpp`, `bridge/internal/cards/cards_test.go`

**Interfaces:**
- Produces (in `payload.h`):
  ```cpp
  constexpr uint8_t AGENT_TILES = 8, PROMPT_OPTS = 6;
  enum class AgentKind : uint8_t { Run, Wait, Idle, Err, Exit, Start };   // "run|wait|idle|err|exit|start", unknown -> Idle
  enum class PromptKind : uint8_t { Permission, Question, Input };          // unknown -> Input (no buttons)
  struct AgentTile { char id[16]; char name[17]; char line[17]; char pose[9]; AgentKind kind; uint8_t ctx; bool unseen; };
  struct PromptOpt { char key; char label[41]; };                          // key '1'..'9'
  struct AgentPrompt { char agent[16]; char id[16]; PromptKind kind; char title[41]; char detail[121];
                       char context[41]; uint8_t nOpts; PromptOpt opts[PROMPT_OPTS]; bool notInTmux; bool cut; };
  struct AgentsData { bool present; uint8_t card; uint8_t nTiles; AgentTile tiles[AGENT_TILES];
                      bool hasPrompt; bool promptDropped; AgentPrompt prompt; };
  // Card gains: bool agents;   Payload gains: AgentsData agents;
  const AgentTile *agentsTile(const AgentsData &a, const char *id);  // nullptr when absent
  ```
  `payloadFromJson` keeps its signature but now clears and fills `out` in place (no local `Payload`); on failure `out.valid` is false.

- [ ] **Step 1: Fixtures.** `agents-full.json`: the usage card (copy `fixtures/usage.json`'s card) then an Agents card with 5 tiles — `infra-network` wait, `checkout-api` run pose `write` line `Edit +3` ctx 41 `u:1`, `claude-status-cube` run pose `bash`, `docs-site` idle pose `hungry` line `idle 12m`, `old-thing` exit — and `q` for `infra-network`: kind `permission`, title `Run this command?`, detail `terraform apply -target=module.staging_net`, context `~/code/infra-network`, options `1 Yes`, `2 Yes, and don't ask again for terraform apply`, `3 No`. `agents-lan.json`: the same without `q`. Values as the bridge would emit them (printable ASCII, within the limits).

- [ ] **Step 2: Failing tests** in `payload_test.cpp` (needs ArduinoJson: add `build/payload_test: ../src/payload.cpp` and include `$(AJSON)` as `settings_json_test` does):
  - `parsesTilesAndPrompt`: `agents-full.json` → `agents.present`, `agents.card == 1`, `cards[1].agents`, `nTiles == 5`, tile 0 `Wait`, tile 1 pose `"write"`, ctx 41, unseen; `hasPrompt`, `prompt.kind == Permission`, `nOpts == 3`, `opts[1].key == '2'`, `!notInTmux`, `!cut`.
  - `lanHasNoPrompt`: `agents-lan.json` → `present && !hasPrompt && !promptDropped`.
  - `flags`: `"cut":1` → `prompt.cut`; card `"qc":1` with no `q` → `promptDropped`; `"x":"tmux"` → `notInTmux`.
  - `badKeysDropped`: options with keys `"10"`, `""`, `"a"`, `"0"` are skipped (only `'1'`..`'9'` kept); more than 6 options keeps 6; more than 8 tiles keeps 8.
  - `unknownKinds`: tile `k:"zzz"` → `Idle`; prompt `k:"zzz"` → `Input`.
  - `longStringsTruncate`: a 200-char `d` ends NUL-terminated within 121 bytes.
  - `onlyFirstAgentsCard`: a second card with `a` parses as a plain card (`agents` false) and does not replace `AgentsData`.
  - `inPlaceFailure`: `{"cards":[]}` into a filled `Payload` → returns false, `out.valid` false.
  - `noAgents`: a payload without `a` → `!agents.present`.
- [ ] **Step 3: Failing tests** in `readings_key_test.cpp`: `readingsIgnoreAgentChurn` — changing any tile's `line`, `pose`, `ctx` or `unseen` keeps the key; `readingsSeeAgentChange` — a tile's `kind`, a tile added/removed, a different `prompt.id`, `promptDropped` each change the key.
- [ ] **Step 4: Go test** `TestBuildCardCountFitsFirmware` in `cards_test.go`: `Build` with `Error` set, `ExtraCards` on (events that produce every spend card), `ShowAgents` with 8 agents, `Full` true → `len(p.Cards) <= 8` (the firmware's `MAX_CARDS`), Agents card present.
- [ ] **Step 5: Run** `cd firmware/sim && make build/payload_test build/readings_key_test && ./build/payload_test && ./build/readings_key_test` — FAIL (no `AgentsData`). `cd bridge && go test ./internal/cards -run CardCount` — PASS or FAIL; if it fails, the bridge's deck can exceed the firmware: stop and report.
- [ ] **Step 6: Implement** the structs and parsing in `payload.h/.cpp`; `agentsTile`; the Agents part of `readingsKey` (mix `present`, `nTiles`, each tile's `id` and `kind`, `hasPrompt ? prompt.id : ""`, `promptDropped`). Make every caller parse into a global/`static` (`bleTake`: `static Payload scratch` then `out = scratch` on success; check `net.cpp`/`net_sim.cpp` keep handing over the last good payload on failure).
- [ ] **Step 7: Run** `make test` and `pio run` (from `firmware/`) — PASS; `cd bridge && go test ./...` — PASS.
- [ ] **Step 8: Commit** `feat(firmware): parse the Agents card; agents in the sleep key`.

### Task 2: The critter

**Files:**
- Create: `firmware/src/critter.h`, `critter.cpp`, `firmware/sim/tests/critter_test.cpp`; Modify: `sim/Makefile` (`SRCS`, `BENCH_SRCS`, `SHOT_OBJ`, test rule)

**Interfaces:**
- Produces:
  ```cpp
  enum class Pose : uint8_t { Idle, Start, Run, Think, Read, Write, Bash, Web, Plan, Delegate, Compact, Wait, Err, Exit, Hungry };
  Pose poseFromName(const char *p);                 // the bridge's names; unknown/empty -> Idle
  enum class CritterInk : uint8_t { Body, Cut, Flag, Accent };
  struct CritterRect { float x, y, w, h; CritterInk ink; };   // sprite cells: x 0..18, y -4..9
  // Shapes for `pose` at time `ms`; at most `cap` written; returns the count.
  size_t critterShapes(Pose pose, uint32_t ms, CritterRect *out, size_t cap);
  constexpr size_t CRITTER_MAX_RECTS = 64;
  ```
- Source: `~/code/agents-deck/firmware/devices/ws169-panel/src/critter.c` @ 699bba8 (`critter_draw` and its helpers). First line of `critter.cpp`: `// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, firmware/devices/ws169-panel/src/critter.c.` Keep: the Claude block body, legs, arms, faces, per-pose props, the bob/hop/step timing. Cut: onigiri, hats, the thought bubble, `POSE_CELEBRATE`, LVGL. Stroked eyes (`x`, `^`, `><`) and the font-drawn `z` become short runs of 1×1-cell (or 0.5-cell) rectangles in `Cut` ink.

- [ ] **Step 1: Failing tests** (`critter_test.cpp`): `everyPoseDraws` — each `Pose` gives ≥ 6 rects, none outside x [-1, 19], y [-5, 10], all `w,h > 0`; `poseNames` — `"write"` → `Write`, `"hungry"` → `Hungry`, `"bogus"`/`""`/`nullptr` → `Idle`; `waitIsStill` — `Wait` at 0 and 777 ms gives identical shapes (a waiting tile does not jitter); `runMoves` — `Run` at 0 and half its step period differs; `capRespected` — `cap` 3 returns 3.
- [ ] **Step 2: Run** `make build/critter_test && ./build/critter_test` — FAIL.
- [ ] **Step 3: Implement** the port.
- [ ] **Step 4: Run** `make test`, `pio run` — PASS.
- [ ] **Step 5: Commit** `feat(firmware): pixel critter from agents-deck, as cell rectangles`.

### Task 3: Grid and prompt geometry, vertical scroll

**Files:** Create `firmware/src/agents_layout.h`, `firmware/sim/tests/agents_layout_test.cpp`; Modify `sim/Makefile` (`TESTS`).

**Interfaces:**
- Produces:
  ```cpp
  struct LayoutRect { int16_t x, y, w, h; };
  // Grid: the card's s1 caption at y 44; tiles in a view from AGENTS_VIEW_Y = 58, height AGENTS_VIEW_H = 196,
  // two columns x 14 and 124 (w 102), tile h 84, row gap 8.
  constexpr int16_t AGENTS_VIEW_Y = 58, AGENTS_VIEW_H = 196, TILE_W = 102, TILE_H = 84, TILE_GAP = 8;
  LayoutRect agentsTileRect(uint8_t i, int16_t scroll);        // screen coords
  int16_t agentsContentH(uint8_t nTiles);                       // rows*84 + (rows-1)*8, 0 for none
  int agentsTileAt(int16_t x, int16_t y, uint8_t nTiles, int16_t scroll);   // -1 = none or outside the view
  struct ScrollY {                                              // a vertical drag, as volume_slider.h does for its fill
    void setRange(int16_t contentH, int16_t viewH);            // max = contentH > viewH ? contentH - viewH : 0; clamps
    void grab(int16_t y); bool drag(int16_t y); void release();// drag: offset = grabbed - (y - y0), clamped; true if it moved
    int16_t offset() const; void reset();
  };
  // Prompt screen: a column that scrolls under a fixed header (tile name, y 44).
  constexpr int16_t PROMPT_VIEW_Y = 58, PROMPT_VIEW_H = 202, PROMPT_BTN_H = 34, PROMPT_BTN_GAP = 6;
  struct PromptMetrics { uint8_t titleLines, detailLines; bool context, reason; };  // detailLines 0..3
  struct PromptLayout {
    void build(const PromptMetrics &m, uint8_t nButtons);       // title (20 px/line), detail box (14 px/line + 12),
                                                                 // context line 16, buttons, reason line 18, BACK button
    int16_t contentH() const; int16_t buttonY(uint8_t i) const; int16_t backY() const;
    int buttonAt(int16_t y, int16_t scroll) const;              // index, -1 none, PROMPT_HIT_BACK = -2
  };
  ```
- [ ] **Step 1: Failing tests:** tile rects for indices 0..3 at scroll 0 (`{14,58,102,84}`, `{124,58,..}`, `{14,150,..}`, `{124,150,..}`); `agentsContentH(5) == 268`, `(0) == 0`, `(2) == 84`; `agentsTileAt` hits tile 3 at (170, 200), misses the gap (119, 100), misses above the view (60, 50) even when a scrolled tile is drawn there, follows `scroll` 60; `ScrollY` clamps to 0..72 for content 268 / view 196, `drag` reports false at the stop; `PromptLayout` for 2 title lines, 3 detail lines, context, 3 buttons, no reason: buttons stacked 40 px apart, BACK last, `buttonAt` inside button 1 → 1, in a gap → -1, on BACK → -2, honours `scroll`.
- [ ] **Step 2–4:** RED, implement, `make test` + `pio run` GREEN.
- [ ] **Step 5: Commit** `feat(firmware): grid and prompt layout with vertical scroll`.

### Task 4: `AgentView` — which screen, which buttons

**Files:** Create `firmware/src/agent_view.h`, `firmware/sim/tests/agent_view_test.cpp`; Modify `sim/Makefile`.

**Interfaces:**
- Consumes: `AgentsData`, `agentsTile` (Task 1).
- Produces:
  ```cpp
  constexpr uint32_t PROMPT_SETTLE_MS = 1000, ANSWER_WAIT_MS = 8000;
  enum class PromptScreen : uint8_t { None, Prompt, Queued, OverWifi, Dropped, NoText };
  // Why the buttons are off; drawn as one caps line (fixed copy below).
  enum class ButtonsOff : uint8_t { No, Input, NotTmux, Locked, Settling, Sent, NoAnswer };
  class AgentView {
   public:
    void open(const char *agentId, uint32_t now);   // a tap on a waiting tile, or the alert
    void close();                                    // swipe right / BACK
    bool isOpen() const; const char *agent() const;
    // Every new payload (and when the link or lock state changes). Closes the view when the
    // agent's tile is gone or no longer Wait; a changed prompt id restarts the settle time
    // and clears Sent. Returns true when what shows changed.
    bool update(const AgentsData &a, bool viaBle, bool macLocked, uint32_t now);
    PromptScreen screen() const;
    ButtonsOff buttonsOff(uint32_t now) const;
    // A tap on button i: true when Control 09 should go out (then the view is Sent, i lit).
    bool tap(uint8_t i, uint32_t now);
    int8_t sentButton() const;                       // -1 none
    bool tick(uint32_t now);                         // settle / answer wait ran out: redraw
  };
  ```
  Screen choice for the open agent's tile: payload not via BLE → `OverWifi`; `hasPrompt` and `prompt.agent` is this agent → `Prompt`; `hasPrompt` for another agent → `Queued`; `promptDropped` → `Dropped`; else `NoText`. Buttons (only for `Prompt`), first match wins: `kind == Input` → `Input`; `notInTmux` → `NotTmux`; `macLocked` → `Locked`; Sent and < `ANSWER_WAIT_MS` since the tap → `Sent`; Sent and past it → `NoAnswer` (taps allowed again); shown < `PROMPT_SETTLE_MS` → `Settling`; else `No`. `tap` returns true only when `buttonsOff` is `No` or `NoAnswer` and `i < nOpts`.
  Fixed copy (drawn by Task 7): `OverWifi` "needs you" / "open on Bluetooth"; `Queued` "needs you" / "answer <q's tile name> first"; `Dropped` "too long for the cube" / "answer at the terminal"; `NoText` "needs you" / "answer at the terminal"; reasons `Input` "answer at the terminal", `NotTmux` "not in tmux", `Locked` "unlock Mac first", `Sent` "sending...", `NoAnswer` "no answer from Mac".
- [ ] **Step 1: Failing tests:** `wifiShowsOpenOnBluetooth`; `promptForQAgent`; `otherWaitingTileIsQueued`; `droppedAndNoText`; `settleBlocksEarlyTap` (tap at +999 ms false, at +1000 true); `newIdResettles` (same agent, new `prompt.id` while open → `Settling` again, Sent cleared); `oneTapOneSend` (second tap while Sent false); `noAnswerAllowsRetry` (+8000 ms → `NoAnswer`, tap true); `inputHasNoButtons`; `notTmuxAndLocked` order; `closesWhenAnswered` (tile becomes `Run` → `isOpen()` false, `update` true); `closesWhenTileGone`; `tapOutOfRange` false; `tickReportsChanges`.
- [ ] **Step 2–4:** RED, implement, `make test` + `pio run` GREEN.
- [ ] **Step 5: Commit** `feat(firmware): AgentView decides the prompt screen and its buttons`.

### Task 5: `AgentAlert`; `IdleSleep::wake`

**Files:** Create `firmware/src/agent_alert.h`, `firmware/sim/tests/agent_alert_test.cpp`; Modify `idle_sleep.h`, `sim/tests/idle_sleep_test.cpp`, `sim/Makefile`.

**Interfaces:**
- Produces:
  ```cpp
  // idle_sleep.h
  bool wake(uint32_t now);   // the screen should come on now (an alert): true if it was asleep; restarts the timer
  // agent_alert.h
  class AgentAlert {
   public:
    // Every new payload. A prompt id not alerted before (remembered: the last 8 ids), or a tile
    // id that was not Wait in the previous payload and is now, makes an alert pending for
    // q's agent (when hasPrompt) else that tile. Pending is dropped when no tile is Wait any more.
    void update(const AgentsData &a);
    // When nothing blocks: the pending alert, once. `agent` is the id to open.
    bool take(bool blocked, char *agent, size_t cap);
  };
  ```
- [ ] **Step 1: Failing tests:** `firstPromptAlerts`; `samePromptResentDoesNot` (the Mac's 5 s resend: same payload 3 times → one alert); `nextPromptAlertsOnce` (q moves to another agent with a new id → one more); `waitingTileWithoutPromptAlerts` (LAN payload: a tile turns Wait → alert for that tile; stays Wait → no more); `blockedAlertWaits` (blocked → nothing; unblocked later → the alert); `blockedAlertDropsWhenAnswered` (blocked, then no Wait tile, then unblocked → nothing); `promptIdMemory` (9 distinct ids then the first again → alerts again; 8 → not); `idle_sleep_test`: `wakeFromSleep` (asleep → `wake` true, `update` returns on), `wakeWhileOn` false and restarts the timer.
- [ ] **Step 2–4:** RED, implement, `make test` + `pio run` GREEN.
- [ ] **Step 5: Commit** `feat(firmware): AgentAlert, once per prompt; IdleSleep::wake`.

### Task 6: Control `09`, Volume bit 6, fw_rev 10

**Files:**
- Create: `firmware/src/answer_frame.h`, `firmware/sim/tests/answer_frame_test.cpp`
- Modify: `ble_frame.h` (`BLE_CTRL_ANSWER = 0x09`, `BLE_FW_REV = 10`), `volume_frame.h` (`VOL_FLAG_SCREEN_LOCKED` bit 6, `MacVolume.screenLocked`), `sim/tests/volume_frame_test.cpp`, `ble.h`, `net_ble.cpp`, `sim/ble_sim.cpp`, `sim/bench.cpp`, `sim/fixtures/ble-frames.txt`, `docs/ble-protocol.md`, `sim/Makefile`

**Interfaces:**
- Produces:
  ```cpp
  constexpr size_t BLE_ANSWER_MAX = 34;
  // 0 when an id is empty, longer than 15 or not printable ASCII, or key is not '1'..'9'.
  size_t answerEncode(const char *agent, const char *prompt, char key, uint8_t *out, size_t cap);
  // ble.h: Control 09 to the Mac's link only. False (and a [ble] log line) when no Mac link is
  // subscribed to Control or the message is longer than that link's MTU - 3.
  bool bleSendAnswer(const char *agent, const char *prompt, char key);
  ```
- Fixture lines (prefix `answer`, which the Swift tests filter out until plan 4 reads them): `answer l1a2b3 p1kq3x0-7 2 09 06 6c 31 61 32 62 33 09 70 31 6b 71 33 78 30 2d 37 32` and one with 15-char ids. No `vol_state` line with bit 6 yet: the Mac encoder learns it in plan 4.
- [ ] **Step 1: Failing tests:** `answer_frame_test` reads every `answer` fixture line and compares bytes; rejects `""`, a 16-char id, a `\x7f` byte, key `'0'`, `'a'`; `cap` too small → 0. `volume_frame_test` `screenLockedBit`: flags `0x46` → `screenLocked && !macLocked`; older flags leave it false.
- [ ] **Step 2: Run** — FAIL.
- [ ] **Step 3: Implement** the encoder, the flag, `bleSendAnswer` on the board (notify only the Mac bond's connection; check the NimBLE-Arduino 2.x API in `.pio/libdeps` for per-connection notify and `getPeerMTU`), the sim stand-in (prints `[ble] (sim) answer <agent> <prompt> <key>` and returns true when `CUBE_BLE=live`), the bench stand-in (counts sends). Document `09` and bit 6 in `docs/ble-protocol.md` (table rows, fw_rev 10, Info `[1, 10]`), noting the Mac acts on them from plan 4.
- [ ] **Step 4: Run** `make test`, `pio run`, `swift test --package-path mac-helper` — PASS.
- [ ] **Step 5: Commit** `feat(firmware): Control 09 answer and the screen-locked flag (fw_rev 10)`.

### Task 7: Draw the Agents card and the prompt screen

**Files:** Modify `firmware/src/ui.h`, `ui.cpp`, `firmware/sim/shot.cpp`; Create fixtures as needed.

**Interfaces:**
- Consumes: Tasks 1–4.
- Produces (`ui.h`):
  ```cpp
  struct AgentsUi { int16_t gridScroll; };                       // what main.cpp owns for the card
  // uiRender gains a trailing `const AgentsUi *agents = nullptr` (null = scroll 0).
  struct PromptUi { PromptScreen screen; ButtonsOff off; int8_t lit; int16_t scroll; const char *queuedName; };
  void uiAgentPrompt(Display &lcd, const AgentsData &a, const AgentTile &tile, const PromptUi &v,
                     bool online, uint32_t ageMs, const BatteryView &bat, UiLink link);
  PromptMetrics uiPromptMetrics(const AgentPrompt &q);           // line counts with the real fonts
  ```
- Look (from the approved mockups, `.superpowers/brainstorm/65123-1792854230/content/agents-layout.html` (B) and `prompt-screen.html` (A)):
  - Grid: top bar as every card; the card's `s1` in caps at y 44, in its accent colour; tiles `0x121722` with radius 12, clipped to the view; name `V_S12` INK at the tile's top, critter (cell 3 px, ink Body = INK or BG on a waiting tile, Cut = tile colour, Flag `0xE8E4DC`, Accent `0xFF4D00`) centred, status line `V_S12` DIM, a 4 px context bar (track `0x2A3242`, fill blue; on a waiting tile track `#0002`-ish dark and fill near-black) along the bottom; a waiting tile is solid accent `0xFF8A5B` with near-black text; `unseen` = a 4 px accent dot at the tile's top-right; `exit` tiles drawn in DIM. A thin scroll indicator on the right edge when content overflows. Page dots as today.
  - Prompt: top bar; header = the tile's name in caps, accent; then (scrolling under it, clipped to `PROMPT_VIEW_Y..+PROMPT_VIEW_H`) the title in `V_B18` wrapped to 2 lines (cut with "..."), the detail in a `0x121722` box in `V_S12`, up to 3 lines, the last ending "..." when more remains, `prompt.cut` drawn as the box with "(detail cut)" in DIM; the context in caps DIM; one button per option (radius 12, `0x1E2533`, the first accent with near-black text): the key in a small circle then the label, clipped with "..."; while off, buttons `FAINT` with `MUTED` text and the lit (sent) one outlined in accent; the reason line in caps; a BACK button last. Non-`Prompt` screens: the tile's critter (pose `wait`) large, "needs you" (or "too long for the cube") in `V_B24`, the second line in caps DIM.
  - `uiRender` draws the Agents card when `cards[i].agents`, before the ring/text dispatch.
- Shots (`renderSpecial`, each loading `fixtures/agents-full.json` unless named): `@agents` (grid), `@agents-scroll` (grid scrolled to the end, 8 tiles), `@prompt`, `@prompt-settle`, `@prompt-sent`, `@prompt-noanswer`, `@prompt-tmux`, `@prompt-locked`, `@prompt-input`, `@prompt-cut`, `@prompt-long` (6 options, scrolled), `@prompt-wifi`, `@prompt-queued`, `@prompt-dropped`. `./build/cube-shot build/a fixtures/agents-full.json` also renders the grid as a normal card.
- [ ] **Step 1:** Implement; build `make` and `make build/cube-shot`.
- [ ] **Step 2: Verify** every shot above and look at each PNG: text inside the glass's rounded corners, nothing overlapping, the waiting tile orange, greyed buttons readable. Record the list of PNGs looked at in the report.
- [ ] **Step 3: Run** `make test`, `make bench` (the existing scenarios must still pass), `pio run` — PASS.
- [ ] **Step 4: Commit** `feat(firmware): draw the Agents grid and the prompt screen`.

### Task 8: Wire it up in `main.cpp`; sim inputs; bench scenarios

**Files:** Modify `firmware/src/main.cpp`, `sim/net_sim.cpp` (and `ble_sim.cpp` if it fetches separately), `sim/bench.cpp`, `sim/Makefile` (`BENCH_RUNS`).

**Interfaces:**
- Consumes: everything above.
- Behaviour:
  - `payloadViaBle`: true after `bleTake`, false after a successful `netTake`.
  - On the Agents card, `DragMode::Vertical` scrolls the grid (`ScrollY` over `agentsContentH` / `AGENTS_VIEW_H`); a tap on a `Wait` tile opens `AgentView`; a tap elsewhere does nothing; horizontal swipes change card as today; swipe down still opens the display panel. The grid's scroll resets when the deck leaves the card.
  - While `AgentView` is open: it is drawn instead of the deck (priority: after the unlock prompt, before `uiRender`); `DragMode::Vertical` scrolls its column; `SwipePrev` or BACK closes it; `SwipeNext`/`SwipeDown`/`SwipeUp` do nothing; a tap on button i calls `view.tap(i)` and on true `bleSendAnswer(q.agent, q.id, opts[i].key)`; a `false` from `bleSendAnswer` shows as `NoAnswer` at once. Auto-advance is held while it is open.
  - After every payload, link change and Volume update: `view.update(payload.agents, payloadViaBle, macLocked || screenLocked, now)` and `alert.update(payload.agents)` (on payloads only). `view.tick` and redraws as for `unlock.tick`.
  - The alert: `blocked` = `navSession.active() || editing || touchDown || bleState() == BleState::Pairing || pairingMsgUp() || waitingToPair() || unlock.screen(now) != UnlockScreen::Hidden`. When `alert.take` gives an agent: `if (idle.wake(now))` the screen comes on through the existing path; `cardIndex = payload.agents.card`; open the view on that agent unless it is already open on it; `buzzerPlay(Sound::Attention)` (silent at SOUND off by `buzzerPlay` itself); `dirty = true`.
  - Screen sleep: an open view counts as on-screen content only; it does not block sleep.
- Sim: `CUBE_PAYLOAD=<file>` makes `simFetch` read that JSON instead of the bridge; `CUBE_PAYLOAD2=<file>` with `CUBE_PAYLOAD2_MS=<n>` switches to the second file n ms after start (a prompt arriving). Over `CUBE_BLE=live` this reaches the cube as a BLE payload.
- Bench (`bench.cpp` scenario + payload): `agents` — BLE live, sleep 2 min, `mustSleep`, an Agents card of 4 running tiles whose `l`, `p`, `x` change every 5 s; `alert` — BLE live, sleep 2 min, Agents card with no Wait tile until 300 s, then `infra-network` waiting with a prompt; the run fails unless at the end the screen is on, exactly one `Attention` was played, and the drawn screen is the prompt (expose `benchPromptOpen()` from `main.cpp` under `SIM_BENCH`). Add both to `BENCH_RUNS`.
- [ ] **Step 1:** Write the bench scenarios; `make bench` — FAIL (`alert`).
- [ ] **Step 2:** Implement the wiring.
- [ ] **Step 3: Run** `make test`, `make bench`, `pio run` — PASS. Manual check in the sim: `CUBE_BLE=live CUBE_PAYLOAD=fixtures/agents-lan.json CUBE_PAYLOAD2=fixtures/agents-full.json CUBE_PAYLOAD2_MS=5000 make run` → after 5 s the window jumps to the prompt and prints `[buzz] attention`; waiting 1 s then clicking `1` prints `[ble] (sim) answer ...`; with `CUBE_LOCKED=1` the buttons say unlock Mac first. Note what you saw in the report.
- [ ] **Step 4: Commit** `feat(firmware): open prompts, answer over BLE, alert on a new prompt`.

### Task 9: `preview.html` and the docs

**Files:** Modify `bridge/preview.html`, root `CLAUDE.md`, `firmware/CLAUDE.md`, `README.md`; Create `docs/agents-acceptance.md`.

- [ ] **Step 1: `preview.html`:** replace plan 2's list view with the 2×2 grid (tiles, a simple block critter per `k`, line, context bar, waiting tile orange, vertical scroll with the wheel/drag) and a prompt screen opened by clicking an orange tile (same states as `AgentView`, decided from the payload it has: no `q` → `needs you` / `open on Bluetooth`; buttons always greyed with "answer on the cube" since the browser never answers). Must not throw on missing/odd fields (`a` entries that are null, `q.o` not an array). Check by loading `fixtures/agents-full.json` and `agents-lan.json` through a local bridge or by pasting into the page's JSON hook if it has one; note in the report how it was checked.
- [ ] **Step 2: Docs:** root `CLAUDE.md` — the contract paragraph ("The firmware does not parse `a`/`q` yet") now describes what the cube does with them; a new **Agents card** section (grid, prompt screen, `AgentView` states and copy, the alert and its blockers, the settle time, Control `09`, Volume bit 6, sim/shot/bench names); the screen-priority list in `firmware/CLAUDE.md` (agent prompt between unlock and deck) and the pure-header list; `docs/agents-acceptance.md`: on hardware — beep at each SOUND level, wake from sleep on a new prompt, the jump, a real tmux answer from the cube (after plan 4), grid scroll by drag, the alert held during a ride/editor/unlock, critter legibility at 3 px cells.
- [ ] **Step 3: Run** `make test`, `make bench`, `pio run`, `go test ./...`, `swift test` — PASS.
- [ ] **Step 4: Commit** `docs: the Agents card on the cube; preview mirrors grid and prompt`.

---

## Notes for plan 4 (not this plan)

The Mac helper reads Control `09` (fixture `answer` lines), refuses it while `ScreenLock` says locked, POSTs `{"agent","prompt","key"}` to `http://127.0.0.1:<port>/api/answer` with `X-Cube-Client: 1` once, never retries; sets Volume bit 6 whenever the screen is locked (adds a `vol_state` fixture line); polls `/api/status` faster while a tile is `wait`; passes `CUBE_AGENTS` from Bridge settings; reinstalls hooks on a port change; flips the `agents` default on.
