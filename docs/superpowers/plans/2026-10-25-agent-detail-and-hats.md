# Agent Detail Screen and Hat Picker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tap any agent on the cube to see its detail (agents-deck's card), and tap its critter there to pick its folder's hat.

**Architecture:** The cube sends BLE Control `0A` (which agent's detail is open) and `0B` (set a folder's hat) to the Mac app; the Mac asks the bridge for `/api/status?detail=<id>` (loopback only) and posts `/api/hat`. The bridge formats everything (`d` on the Agents card; hat overrides stored per folder in a JSON file, applied to every tile's `h`). The firmware parses `d`, keeps the open detail and picker in a pure state header, and draws two new screens whose layout headers are shared by drawing and hit-testing.

**Tech Stack:** Go 1.24 bridge; Swift 6 tools / Swift 5 mode Mac app (CubeLinkCore + AppKit glue); ESP32-S3 Arduino firmware (gnu++11 on the board, C++17 host tests), LovyanGFX, SDL sim, `cube-shot`.

**Spec:** `docs/superpowers/specs/2026-10-25-agent-detail-and-hats-design.md`

## Global Constraints

- Never flash a board (`pio run -t upload`), run install scripts, bind port 8787, touch `~/.claude/settings.json`, modify `~/code/agents-deck` (read-only reference), or commit `firmware/src/config.h`.
- The board compiles as gnu++11: a pure header must also pass `pio run` (single-return constexpr, no C++14/17).
- No number/unit formatting in the firmware: every string drawn comes from the payload or is fixed copy (`back`, `doing now`, `context`, `last prompt`, `loading...`, `details need Bluetooth`, `hat`, `none`, `auto`).
- agents-deck theme: palette tokens and fonts of `ui.cpp` (`rm11`, `rm13`, `rb14`..`rb40`), square corners, lower-case labels (`drawLabel` / `lowerCopy`).
- The last prompt and the tool's target never appear in the LAN payload (`d` only in the full variant, only with `?detail=`).
- Every `/api/status` body ≤ 2048 bytes (`cards.MaxPayloadBytes`); `d` is trimmed (its `p` first) and dropped before anything the full variant already carries.
- Pure firmware logic goes in headers with a `sim/tests/<name>_test.cpp`; a header change in `ble.h` updates `net_ble.cpp`, `sim/ble_sim.cpp` and `sim/bench.cpp` together; new tests and sources are listed in `sim/Makefile`.
- `BLE_FW_REV` becomes 11.
- Commits end with:
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_011fcVNuzYXLaKssGt3JKoeS`

## Review Focus

1. The agent of an open detail or picker disappears from the payload → the cube returns to the grid and sends `0A 00` (Task 5 test).
2. An older Mac app ignores `0A` / a reconnect forgets it → the cube shows the tile's data with `loading...`, re-sends `0A` every 5 s while open, never crashes (Task 4 `DetailView` test).
3. A detail and a waiting prompt both large → body ≤ 2048, `q` kept, `d.p` cut then `d` dropped (Task 2 test).
4. Two sessions in one folder: a hat set through one changes both tiles; `auto` removes the override; the file survives a restart; a corrupt `hats.json` loads as empty with one warning (Task 1 tests).
5. Critter tapped before `d` arrived → the picker opens on the tile's `h`; a pick with no Mac link logs and changes nothing visible for long (Task 4 `DetailView` pending-hat test, Task 6 shot).

---

### Task 1: Bridge hat overrides and `POST /api/hat`

**Files:**
- Create: `bridge/internal/hatstore/hatstore.go`, `bridge/internal/hatstore/hatstore_test.go`
- Modify: `bridge/internal/cards/cards.go` (Opts), `bridge/internal/cards/agents.go` (tileOf), `bridge/internal/cards/agents_test.go`, `bridge/internal/server/server.go` (Deps, route), `bridge/internal/server/*_test.go`, `bridge/cmd/cube-bridge/main.go` (wire the store), `bridge/CLAUDE.md`, root `CLAUDE.md` (Agents paragraph)

**Interfaces:**
- Produces: `hatstore.Open(path string, warn func(string)) *Store`; `(*Store).Get(folder string) (code int, ok bool)` (`code` 0..103, or -1 = no hat); `(*Store).Set(folder string, code *int) error` (nil = automatic: delete); `hatstore.DefaultPath() string` = `os.UserConfigDir()/claude-status-cube/hats.json`, overridable by env `CUBE_HATS_FILE`. Keys are `filepath.Clean(folder)`.
- Produces: `cards.Opts.Hats func(folder string) (code int, ok bool)` (nil = all automatic); a tile's hat folder is `cmp.Or(a.Cwd, a.Folder)` (as now).
- Produces: `POST /api/hat` body `{"agent": "<id>", "hat": <int 0..103 | -1 | null>}`, guard = loopback + `X-Cube-Client: 1` (as `/api/answer`); 204 stored (then republish), 400 bad JSON / hat out of range / missing agent key, 404 agents off or unknown agent or agent without a folder, 405 not POST.

- [ ] **Step 1: Tests** — `hatstore_test.go`: `TestSetGetPersist` (Set "/a/b/" 17 → Get "/a/b" = 17,true; reopen from the same file → still 17), `TestAuto` (Set nil removes; Get ok=false), `TestNone` (Set -1 → Get -1,true), `TestCorruptFile` (file `{not json` → Open warns once, Get ok=false, a later Set rewrites the file valid), `TestAtomicWrite` (no partial file: written via temp + rename in the same dir). `agents_test.go`: `TestTileHatOverride` — two agents with Cwd `/x/proj`, `Opts.Hats` returning 17 for `/x/proj` → both tiles `H == 17`; returning -1 → both `H == nil`; returning ok=false → `HatOf`. Server test `TestHatEndpoint`: 204 then the next `/api/status` tile has the new `h`; 404 for unknown agent; 400 for `"hat": 104` and `"hat": "x"`; 404 without `X-Cube-Client`; 405 on GET.
- [ ] **Step 2: Run** `cd bridge && go test ./...` — FAIL (undefined).
- [ ] **Step 3: Implement** the store (mutex, JSON object `{"<folder>": code}`, write temp + `os.Rename`, create the dir 0700), `Opts.Hats` in `tileOf` (override beats `HatOf`; -1 = no `h`), the server: `Deps.Hats` (nil when agents are off), handler `hat` next to `answer`, republish after a 204; `cmd/cube-bridge` opens the store at `DefaultPath()` when agents are on and passes `s.Get` through `Opts.Hats` in `publishLocked`.
- [ ] **Step 4: Run** `go vet ./... && go test ./...` — PASS.
- [ ] **Step 5: Docs** — `bridge/CLAUDE.md` agents section: the endpoint and the file; root `CLAUDE.md` Agents paragraph: `h` follows a per-folder override.
- [ ] **Step 6: Commit** `feat(bridge): per-folder hat overrides and POST /api/hat`

### Task 2: Bridge agent detail `d` via `?detail=`

**Files:**
- Create: `bridge/internal/cards/detail.go`, `bridge/internal/cards/detail_test.go`
- Modify: `bridge/internal/cards/cards.go` (Card gets `D *AgentDetail \`json:"d,omitempty"\``), `bridge/internal/server/server.go` (keep the full build's snapshot; route reads `detail`), server tests, `bridge/CLAUDE.md`, root `CLAUDE.md` (payload contract)

**Interfaces:**
- Consumes: Task 1 `Opts.Hats` (for `ha`).
- Produces: `type AgentDetail struct { I, T, S, W, C, P, M, E, N string; HA int }` with JSON keys `i t s w c p m e n ha` (`p m e n` omitempty, `ha` always); maxima const block `DetailTitleChars 40, DetailStatusChars 24, DetailDoingChars 48, DetailCtxChars 24, DetailPromptChars 160, DetailMetaChars 48, DetailErrChars 60, DetailSubsChars 16`.
- Produces: `cards.WithDetail(p Payload, a agents.Agent, hatAuto bool, now time.Time) Payload` — adds `d` to the Agents card, then fits the budget: cut `d.P` to 60 chars (`...`), then drop `d.P`, then drop `d`.
- Field wording (all ASCII-folded with `Fit` except the `sep` " · " joins): `t` = title (else folder); `s` = status word + sep + `agoOf(StatusSince)`, words `running`, `needs you`, `idle`, `error`, `starting`, `exited`; `w` = running: verb (`toolVerb`) + sep + the activity detail shortened (`agents.ShortPath` when it is a path, else `fitMarked`), otherwise the status word; `c` = `<pct>%` + sep + `count(used) + " / " + count(window)` where count is `410`, `41k`, `1.2m`, `1m` (trailing `.0` dropped), empty when no window; `p` = `LastPrompt` (`fitMarked`); `m` = model label (else model) + folder (`agents.ShortPath(Cwd)`) + branch, joined by sep, empty parts skipped; `e` = `ErrorText` when errored; `n` = `1 subagent` / `N subagents`; `ha` = 1 when no override applies.
- Produces: `GET /api/status?detail=<id>` — full variant only (loopback guard); unknown id → the plain full payload.

- [ ] **Step 1: Tests** — `detail_test.go`: `TestDetailFields` (a running agent: Title "fix login", Activity {Edit, "/Users/me/x/src/a.ts"}, Context 410_000/1_000_000, ModelLabel "opus 5.5", Cwd "/Users/me/x", Branch "main", LastPrompt, 3 subagents, StatusSince 38 min ago → `s` "running · 38m", `w` starts "edit · " and contains "src/a.ts", `c` "41% · 410k / 1m", `m` "opus 5.5 · ~/x · main", `n` "3 subagents"), `TestDetailCount` (999 → "999", 41_000 → "41k", 1_200_000 → "1.2m", 1_000_000 → "1m"), `TestDetailBudget` (8 tiles, a 6-option prompt with a full detail and a 2000-char last prompt → marshals ≤ 2048, `q` present, `d` present with `p` cut or absent), `TestDetailNeverLAN` (Build with Full false then WithDetail is not called by the server for LAN: server test asserts a LAN request with `?detail=` has no `"d"`). Server test: loopback `?detail=<id>` has `d.i == id`; unknown id has no `d`.
- [ ] **Step 2: Run** `go test ./...` — FAIL.
- [ ] **Step 3: Implement** `detail.go`; in the server keep `fullSnap atomic.Pointer[[]agents.Agent]` stored with the full payload in `publishLocked`, and in `route` for a loopback request with a non-empty `detail` that matches a snapshot agent, serve `cards.WithDetail(*full, agent, !overridden, now)`.
- [ ] **Step 4: Run** `go vet ./... && go test ./...` — PASS.
- [ ] **Step 5: Docs** — root `CLAUDE.md` payload contract: `d` and its keys, loopback + `?detail=` only, trim order; `bridge/CLAUDE.md` agents section.
- [ ] **Step 6: Commit** `feat(bridge): agent detail on /api/status?detail=`

### Task 3: Mac app: Control `0A`/`0B`, detail polling, hat POST

**Files:**
- Modify: `mac-helper/Sources/CubeLinkCore/Protocol.swift`, `Poll.swift`, `BridgeClient.swift`, `CubeLink.swift`, `Answer.swift` (or a new `Hat.swift` beside it for the outcome text), `mac-helper/Sources/ClaudeCubeLink/main.swift`, tests in `mac-helper/Tests/CubeLinkCoreTests/`, `mac-helper/CLAUDE.md`

**Interfaces:**
- Produces: `ControlMessage.detail(agent: String?)` from `[0x0A][la][agent]` (`la` 0 → nil; `la` 1..15 printable ASCII, else nil message); `ControlMessage.hat(agent: String, hat: HatChoice)` from `[0x0B][la][agent][hat]` with `enum HatChoice: Equatable { case code(Int), none, auto }` (`0..103` code, `0xFE` none, `0xFF` auto, anything else → nil message).
- Produces: `PollPolicy.interval(payload: Data?, detailOpen: Bool) -> TimeInterval` (1 s when `detailOpen`, else as now); `BridgeClient.fetch(detail: String?, _ done:)` (query `detail=<percent-encoded id>`); `BridgeClient.setHat(agent: String, hat: HatChoice, done: (Int?, Error?) -> Void)` — one POST `/api/hat` `{"agent", "hat": Int | -1 | null}`, header `X-Cube-Client: 1`, 5 s timeout, fresh ephemeral session, never retried; `CubeLink.onDetail: ((String?) -> Void)?`, `CubeLink.onHat: ((String, HatChoice) -> Void)?`.
- main.swift: `detailAgent` set by `onDetail` (force a tick on change), cleared whenever the link leaves ready; `tick` passes it to `fetch` and to `PollPolicy.interval`; `onHat` → `setHat`, then log `hat for <agent>: <outcome>` and force a tick.

- [ ] **Step 1: Tests** — `ProtocolTests`: parse `0a 00` → `.detail(nil)`; `0a 06 6c 31 61 32 62 33` → `.detail("l1a2b3")`; `0a 10 ...` (16) → nil; `0b 06 6c 31 61 32 62 33 11` → `.hat("l1a2b3", .code(17))`; `... fe` → `.none`; `... ff` → `.auto`; `... 68` (104) → nil. `PollTests`: `interval(payload: nil, detailOpen: true) == 1`. `BridgeClientTests` (URL building only, no network): detail id `a b` encodes as `detail=a%20b`; hat body JSON for `.code(17)`, `.none` (-1), `.auto` (null).
- [ ] **Step 2: Run** `swift test --package-path mac-helper` — FAIL.
- [ ] **Step 3: Implement** the parse cases, policy, client calls, callbacks and main.swift wiring as in Interfaces.
- [ ] **Step 4: Run** `swift test --package-path mac-helper` — PASS (all, previous 69 included).
- [ ] **Step 5: Docs** — `mac-helper/CLAUDE.md`: opcodes `01`..`0B`, detail polling, `setHat`.
- [ ] **Step 6: Commit** `feat(mac): forward the cube's detail and hat requests to the bridge`

### Task 4: Firmware data and wire: `d`, Control `0A`/`0B`, `DetailView`

**Files:**
- Create: `firmware/src/detail_frame.h`, `firmware/src/detail_view.h`, `firmware/sim/tests/detail_frame_test.cpp`, `firmware/sim/tests/detail_view_test.cpp`
- Modify: `firmware/src/payload.h/.cpp` (+ `sim/tests/payload_test.cpp`), `firmware/src/ble_frame.h` (`BLE_FW_REV` 11, `BLE_CTRL_DETAIL = 0x0A`, `BLE_CTRL_HAT = 0x0B`), `firmware/src/ble.h`, `firmware/src/net_ble.cpp`, `firmware/sim/ble_sim.cpp`, `firmware/sim/bench.cpp`, `firmware/sim/Makefile`, `firmware/sim/fixtures/ble-frames.txt` (+ the C++ and Swift fixture readers: `sim/tests/ble_frame_test.cpp` or wherever `answer` lines are read, and `mac-helper/Tests/CubeLinkCoreTests/FrameTests.swift`), `docs/ble-protocol.md`, `firmware/sim/fixtures/agents-full.json` (add a `d` for `l00002`), `firmware/tools/gen_fonts.py` + regenerated `firmware/src/fonts_gen.h` (`rm13` also gets `·` U+00B7, which `d.doing` and `d.status` carry; every other blob must stay byte-identical — regenerate with Pillow 11.3.0 from `tools/requirements.txt` in a venv)

**Interfaces:**
- Produces: `struct AgentDetail { bool present; char id[16], title[41], status[25], doing[49], ctx[25], prompt[161], meta[49], err[61], subs[17]; bool hatAuto; };` as `AgentsData::detail` (parsed from the Agents card's `d` object; absent or not an object → `present = false`).
- Produces: `size_t detailEncode(const char *agent /*nullptr or "" = closed*/, uint8_t *out, size_t cap)`; `size_t hatEncode(const char *agent, uint8_t hat, uint8_t *out, size_t cap)` (`hat` 0..103, `HAT_NONE` 0xFE, `HAT_AUTO` 0xFF; 0 on a bad id or hat). `bool bleSendDetail(const char *agent)`, `bool bleSendHat(const char *agent, uint8_t hat)` (Mac link only, `[ble] ... dropped: <reason>` on false, like `bleSendAnswer`).
- Produces (pure, `detail_view.h`): `class DetailView { void open(const char *agent, uint32_t now); void close(); bool isOpen() const; const char *agent() const; bool sendDue(const AgentsData &a, uint32_t now) const; void sent(uint32_t now); void openPicker(); void closePicker(); bool pickerOpen() const; void pick(int16_t code /*0..103, -1 none, -2 auto*/, uint32_t now); int16_t shownHat(int16_t tileHat, uint32_t now) const; bool pickedAuto(bool hatAuto, uint32_t now) const; };` — `sendDue` is true right after `open` and then every `DETAIL_RESEND_MS` = 5000 while `!a.detail.present || strcmp(a.detail.id, agent())`; a pick is shown for `HAT_PICK_HOLD_MS` = 4000 or until the tile's `h` equals it (auto: until `hatAuto`); `close` also closes the picker.
- Fixture lines (both readers): `detail l1a2b3 0a 06 6c 31 61 32 62 33`, `detail - 0a 00`, `hat l1a2b3 17 0b 06 6c 31 61 32 62 33 11`, `hat l1a2b3 none 0b 06 6c 31 61 32 62 33 fe`, `hat l1a2b3 auto 0b 06 6c 31 61 32 62 33 ff`.

- [ ] **Step 1: Tests** — `detail_frame_test`: the five fixture byte strings; 16-char id → 0; non-printable → 0; hat 104 → 0; cap too small → 0 and `out` untouched. `detail_view_test`: open → `sendDue` true; after `sent(t)` false until `t+5000` while `d` missing; false once `d.id` matches; true again at +5000 when `d` is for another agent; `close` → `isOpen` false, picker closed; `pick(17,t)` → `shownHat(5,t+100) == 17`, `shownHat(5,t+4001) == 5`, `shownHat(17,...) == 17`; `pick(-2,t)` → `pickedAuto(false,t+100)` true, `pickedAuto(false,t+4001)` false; `millis` wrap (`t` near `UINT32_MAX`) keeps the 5 s timing. `payload_test`: `agents-full.json` `d` fields land; a `d` that is a string → `present` false; over-long `p` is cut by `strlcpy` without breaking (UTF-8 is not expected: the bridge folds `p` to ASCII).
- [ ] **Step 2: Run** `cd firmware/sim && make test` — FAIL.
- [ ] **Step 3: Implement** headers, parse, `ble.h` functions in `net_ble.cpp` (same send path as `bleSendAnswer`), stand-ins in `ble_sim.cpp` (print `[ble] (sim) detail <id|->` / `[ble] (sim) hat <id> <n|none|auto>`) and `bench.cpp`, Makefile entries, fixture readers, `docs/ble-protocol.md` rows for `0A`/`0B` (fw_rev 11, Mac link only, fire and forget, the 5 s resend).
- [ ] **Step 4: Run** `make test`, `swift test --package-path ../../mac-helper`, `cd .. && pio run` — PASS / SUCCESS.
- [ ] **Step 5: Commit** `feat(firmware): agent detail payload, Control 0A/0B, DetailView`

### Task 5: Firmware detail screen

**Files:**
- Create: `firmware/src/detail_layout.h`, `firmware/sim/tests/detail_layout_test.cpp`
- Modify: `firmware/src/ui.h`, `firmware/src/ui.cpp` (`uiAgentDetail`), `firmware/src/main.cpp`, `firmware/sim/shot.cpp`, `firmware/sim/Makefile`, `firmware/sim/bench.cpp` (a `detail` scenario: open, 5 s resend, close), root `CLAUDE.md`, `firmware/CLAUDE.md` (draw order)

**Interfaces:**
- Consumes: Task 4 `DetailView`, `AgentDetail`, `bleSendDetail`.
- Produces: `detail_layout.h`: `constexpr LayoutRect DETAIL_BACK = {0, 30, 96, 30}; constexpr LayoutRect DETAIL_CRITTER = {4, 58, 80, 56};` and `int detailHit(int16_t x, int16_t y)` → `DETAIL_HIT_BACK` (1), `DETAIL_HIT_CRITTER` (2), 0 = nothing.
- Produces: `void uiAgentDetail(Display &lcd, const AgentsData &a, const AgentTile &tile, int16_t hat /*shown*/, bool viaBle, bool online, uint32_t ageMs, const BatteryView &bat, UiLink link);`
- Screen (top bar as every card): `back` (rm13, `< back`) left at y 44 and the status pill right (rm11 on a filled rect: accent when the tile waits or is hungry, else ink; text `d.status`, before `d` arrives the tile's `l`); the critter (cell 3, the tile's pose and `hat`, colours as on the tile) inside `DETAIL_CRITTER`; the title (`d.title`, else the tile's name) in `rb18`, up to 2 lines, x 88..232; `d.subs` in accent rm11 under it; `doing now` label (rm11 dim) y 122 and `d.doing` (rm13 ink, or `d.err` in accent when present) y 136; `context` label y 156 with `d.ctx` (e.g. `41% · 410k / 1m`, dim) beside it, and a 4 px bar x 16..224 at y 170 filled to the tile's `x`; `last prompt` label y 182 and `d.prompt` wrapped (rm13, up to 4 lines, `wrapText`) from y 196; `d.meta` (rm11 dim, one line, `...`) at y 266. No `d` yet: the fields show `loading...` (or `details need Bluetooth` when the payload came over WiFi).
- main.cpp: a `Tap` on a non-waiting tile opens the detail (`detailView.open`, `bleSendDetail(id)` at once via `sendDue`); `openAgentView` closes the detail first; draw priority: … unlock prompt, agent prompt screen, **detail screen**, deck; gestures on the detail: `SwipePrev` closes, `Tap` → `detailHit` (back closes; critter → `openPicker`, Task 6 draws it); closing sends `bleSendDetail(nullptr)`; each pass while open: if the agent's tile is gone close; if it now waits, close the detail and leave the prompt to the alert/tap path; `sendDue` → `bleSendDetail` + `sent`. Auto-advance is held while open (as for the prompt screen). Animation pacing counts the detail screen as an Agents screen (`shownScreen()`).

- [ ] **Step 1: Tests** — `detail_layout_test`: (10, 40) → back; (40, 80) → critter; (200, 200) → 0; edges exclusive on the right/bottom. Bench `detail`: opening sends one `0A`, a missing `d` re-sends at 5 s, the screen still sleeps on time, closing sends `0A 00`.
- [ ] **Step 2: Run** `make test` / `make bench BENCH_RUNS=detail` — FAIL.
- [ ] **Step 3: Implement** the layout header, `uiAgentDetail`, main.cpp wiring, shots `@detail` (from `agents-full.json`'s `l00002` with `d`), `@detail-loading` (no `d`), `@detail-wifi`, `@detail-err`.
- [ ] **Step 4: Run** `make test && make bench` (rc 0), render the four shots and LOOK at each, `cd .. && pio run` SUCCESS.
- [ ] **Step 5: Docs** — root `CLAUDE.md` Agents card paragraph (tap behaviour, the detail screen, `0A`), `firmware/CLAUDE.md` draw priority list.
- [ ] **Step 6: Commit** `feat(firmware): agent detail screen`

### Task 6: Firmware hat picker

**Files:**
- Create: `firmware/src/hat_picker_layout.h`, `firmware/sim/tests/hat_picker_layout_test.cpp`
- Modify: `firmware/src/ui.h`, `firmware/src/ui.cpp` (`uiHatPicker`), `firmware/src/main.cpp`, `firmware/sim/shot.cpp`, `firmware/sim/Makefile`, root `CLAUDE.md`, `docs/agents-acceptance.md` (hardware items: detail, picker, `0A` resend after reconnect)

**Interfaces:**
- Consumes: Task 4 `DetailView::pick/shownHat/pickedAuto`, `bleSendHat`; Task 5 detail screen.
- Produces: `hat_picker_layout.h`: 15 cells (0..12 shapes in agents-deck order, 13 `none`, 14 `auto`), 4 columns of 54 x 44 at `x = 12 + col * 56`, `y = 60 + row * 46`; 8 colour swatches 26 x 22 at `x = 12 + i * 28`, `y = 250`; back = `{0, 30, 96, 30}`. `struct HatPick { enum Kind : uint8_t { Nothing, Back, Shape, None, Auto, Color } kind; uint8_t index; }; HatPick hatPickerAt(int16_t x, int16_t y);`
- Produces: `void uiHatPicker(Display &lcd, const AgentTile &tile, int16_t hat /*shown*/, bool autoOn, uint32_t now, bool online, uint32_t ageMs, const BatteryView &bat, UiLink link);` — title `hat` (rm13) beside `< back`; each shape cell a cell-2 critter (idle pose, body dim, cut on panel) wearing that shape in the current colour; `none` a critter with no hat; `auto` the word `auto` (rm13); swatches in `hatMainRgb`; the current shape (or `none`/`auto` when `autoOn`) and colour outlined in ink 2 px.
- Picking: Shape → code `shape * 8 + current colour` (current colour = shown hat's `& 7`, 0 when none); Color → `current shape * 8 + i`, ignored while none; None → `HAT_NONE`; Auto → `HAT_AUTO`. Each pick: `bleSendHat(tile.id, value)` and `detailView.pick(code, now)`; a false send logs and still shows the pick for its hold.

- [ ] **Step 1: Tests** — `hat_picker_layout_test`: (13, 61) → Shape 0; (12 + 3*56 + 1, 60 + 1) → Shape 3; cell 12 → Shape 12; cell 13 → None; cell 14 → Auto; (12 + 7*28 + 1, 251) → Color 7; gaps between cells → Nothing; (10, 40) → Back.
- [ ] **Step 2: Run** `make test` — FAIL.
- [ ] **Step 3: Implement** the layout, `uiHatPicker`, main.cpp: while `pickerOpen()` draw the picker in place of the detail; `SwipePrev` / back return to the detail; taps route through `hatPickerAt`.
- [ ] **Step 4: Run** `make test && make bench` (rc 0); shots `@hat-pick` (auto on) and `@hat-pick-set` (shape 4 colour 2 outlined) — LOOK at both; `pio run` SUCCESS.
- [ ] **Step 5: Docs** — root `CLAUDE.md` (picker, `0B`, per-folder overrides), `docs/agents-acceptance.md`.
- [ ] **Step 6: Commit** `feat(firmware): pick a folder's hat on the cube`
