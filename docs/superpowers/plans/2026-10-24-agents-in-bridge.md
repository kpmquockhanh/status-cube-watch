# Claude Agents in the Bridge — Implementation Plan (2 of 4)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Go bridge tracks every open Claude Code session on the Mac (copied and trimmed from agents-deck), learns their prompts through global hooks, answers prompts by typing into the session's pane of the user's own tmux, and publishes an Agents card in the payload: full (with the prompt) on loopback, grid-only on the LAN.

**Architecture:** A new `internal/agents` package holds agents-deck's Claude tracking (hook folding, transcript tail, subagents, pane option parsing, session discovery) around a mutex-guarded `Tracker`, plus a thin wrapper over the user's default tmux server. `internal/hooksettings` installs/uninstalls the bridge's hook entries in `~/.claude/settings.json` without disturbing anything else. `internal/cards` gains the Agents card and a hard payload-size guard. The server keeps two payloads (full and LAN), picks one per request, and adds two loopback-only POST endpoints (`/hook`, `/api/answer`) behind Host/Origin/header checks. `cube-bridge` gains `hook` and `hooks install|uninstall` subcommands.

**Tech Stack:** Go 1.24 (stdlib + `golang.org/x/text`), tmux 3.x CLI, Claude Code ≥ 2.1 (`claude agents --json`, hooks), bash.

**Spec:** `docs/superpowers/specs/2026-10-24-go-bridge-agents-design.md` (goals 2, 3, 4, 7 (bridge side), 7a; the Agents card's data for goal 5 and the prompt for goal 6). Plan 1 (Go bridge at parity) is merged. Plan 3 draws the card and prompt on the cube; plan 4 carries answers over BLE and adds the Mac helper's lock policy.

**Source code to copy:** agents-deck at `~/code/agents-deck`, commit `699bba8`, package `backend/internal/claude` (and pieces of `store`, `model`, `tmux`, `daemon`, `cmd/agentctl`). Each task names the files it copies from. Copy, then cut: everything for iTerm, Orca, herdr, cmux, Codex, Gemini, accounts, hats, the private tmux server, Deck Link, TLS/pairing, demo/sim, persistence of agents, resume/restore and focus is left out.

## Global Constraints

- Go ≥ 1.24; third-party modules: only `golang.org/x/text`. The repo is private; agents-deck has no licence and the user chose to copy it: every copied file keeps a first-line comment `// Adapted from agents-deck (github.com/brushknight/agents-deck) @ 699bba8, <original path>.` and `bridge/internal/agents/NOTICE.md` names the repository, the commit and the files.
- Behaviour is off by default until plan 3 ships the firmware: config key `agents` (bool) / env `CUBE_AGENTS`, with Task 1's precedence rules. Off = no tracking, no Agents card, `/hook` and `/api/answer` answer 404. Hook installation is independent of the flag (hooks are harmless while it is off).
- Port 8787 and existing endpoints unchanged. New: `POST /hook`, `POST /api/answer` (loopback only, see "Loopback guard").
- **Loopback guard** (one function, used by `/hook`, `/api/answer` and the full-payload choice): the connection's remote address is a loopback IP **and** the `Host` header is exactly `127.0.0.1:<port>` or `localhost:<port>` **and** there is no `Origin` header. `/hook` and `/api/answer` additionally require the header `X-Cube-Client: 1`. A request failing the guard gets the LAN payload (for `GET /api/status`) or 404 (for the POSTs).
- **Payload size:** every `/api/status` body, either variant, is ≤ 2048 bytes (the Mac helper drops anything larger — `CubeProtocol.maxPayload`). The card builder enforces it (Task 6).
- The LAN payload never contains prompt title, detail, context, options or last prompts. Tile text never contains a tool's detail (no file paths, commands, URLs).
- tmux: the user's default server only (no `-L`), binary `/opt/homebrew/bin/tmux`, `/usr/local/bin/tmux`, `/usr/bin/tmux` (first that exists), every call with a 2 s timeout.
- `claude agents --json`: at most every 10 s, resolved through `/bin/zsh -lc 'command -v claude'` once (launchd's PATH is minimal), 15 s timeout, falling back to the process scan.
- Hook command contract: reads stdin (≤ 8 MiB), POSTs it to `http://127.0.0.1:<port>/hook` with a 1 s timeout, prints nothing, always exits 0.
- `~/.claude/settings.json`: only the bridge's own hook entries are added/removed; everything else, including key order, survives; written atomically (temp file + rename) with a one-time backup `settings.json.cube-bridge.bak`; a file that is not valid JSON is never written.
- All formatting (status words, ordering, clipping) is in `internal/cards`; strings go through `cards.Fit` (printable ASCII).

## Review Focus

1. **A hook fires while the bridge is down or wedged** — Claude Code must not slow down or show an error: expect `cube-bridge hook` to return in ≤ 1.2 s with exit 0 and no output, and the installed command to exit 0 even when the binary has been deleted. *(Task 4 tests `TestHookBridgeDown`, `TestHookCommandBinaryMissing`.)*
2. **Large hook payloads** — a `PermissionRequest` for a `Write` of a 3 MB file must still produce a prompt (agents-deck read only 1 MiB and would silently drop it). *(Task 3 `TestHookLargePayload`, Task 4 `TestHookReadsLargeStdin`.)*
3. **A `settings.json` the user wrote by hand** — unknown keys, existing hooks for the same events (the user's `~/code` auto-allow `PreToolUse` hook, a `Stop` hook, two `Notification` hooks), odd key order: expect them byte-equivalent after install + uninstall, and a broken JSON file left untouched with an error. *(Task 5 tests.)*
4. **Eight agents with long names and a six-option question** — expect the full payload to stay ≤ 2048 bytes by dropping tiles/prompt text in a fixed order, never by exceeding. *(Task 6 `TestAgentsPayloadBudget`.)*
5. **The prompt changes between the cube showing it and the tap** (answered at the terminal, or a new prompt) — expect the stale answer refused with 409 and nothing typed into the pane. *(Task 3 `TestAnswerStale`, Task 7 `TestAnswerEndpointStale`.)*

---

## File map

| File | Responsibility |
|---|---|
| `bridge/internal/agents/NOTICE.md` | provenance of the copied code |
| `bridge/internal/agents/model.go` | `Agent`, `Status`, `Prompt`, `Option`, `Activity`, `Subagent` (trimmed `model.go`) |
| `bridge/internal/agents/entry.go` | `entry` (private bookkeeping), `SetStatus`, `NextPromptID`, `displayFolder` |
| `bridge/internal/agents/hooks.go` | `Event`, `apply`, `permissionPrompt`, `questionPrompt`, `ToolDetail`, `EndCompact` |
| `bridge/internal/agents/transcript.go` | `tail` (model, context, branch, titles, API errors), `subagents`, `transcriptTurn`, `subagentTail`, `birthTime` |
| `bridge/internal/agents/pane.go` | `ParseOptions`, `RefineOptions`, `ParseCursorMenu` |
| `bridge/internal/agents/models.go` | context window + short label per model id |
| `bridge/internal/agents/discover.go` | `Live`, `activeSessions`, `liveSessions`, `lookPathLogin` |
| `bridge/internal/agents/tmux.go` | `Tmux` (default server): `Panes`, `Capture`, `SendKeys` |
| `bridge/internal/agents/tracker.go` | `Tracker`: `Hook`, `Sync`, `Answer`, `Snapshot`, `Subscribe`; errors |
| `bridge/internal/hooksettings/hooksettings.go` | `Install`, `Uninstall`, `Command` on `settings.json` |
| `bridge/internal/cards/agents.go` | `Tile`, `AgentPrompt`, `agentsCard`, payload budget |
| `bridge/internal/server/*` | two payloads, loopback guard, `/hook`, `/api/answer`, tracker wiring |
| `bridge/cmd/cube-bridge/main.go` | subcommands `hook`, `hooks install|uninstall` |
| `bridge/agent.sh`, `mac-helper/install.sh` | install/uninstall the hooks |
| `bridge/preview.html`, `mac-helper/Sources/CubeLinkCore/MenuModel.swift` | tolerate the Agents card |

Each Go file has a `_test.go` beside it. Tests never call the real `claude`, `tmux`, `ps`, `lsof` or `~/.claude`: every external command goes through an injectable runner, every path is a parameter.

---

### Task 1: Config flag and the copied model

**Files:**
- Modify: `bridge/internal/config/config.go`, `config_test.go`
- Create: `bridge/internal/agents/NOTICE.md`, `model.go`, `entry.go`, `models.go`, `models_test.go`, `entry_test.go`

**Interfaces:**
- Produces:
  ```go
  // config
  type Config struct{ /* existing */ ; Agents bool } // key "agents", env CUBE_AGENTS, same precedence as extraCards

  // agents
  type Status string // starting|running|waiting|idle|error|exited (constants Starting…Exited)
  type Agent struct {
      ID, SessionID, Folder, Cwd, Branch, Model, ModelLabel, Title string
      Status      Status
      StatusSince time.Time
      Activity    *Activity // nil unless running
      Waiting     *Prompt   // nil unless waiting
      ErrorText   string
      LastPrompt  string
      Context     struct{ Used, Window int64 }
      Subagents   []Subagent
      Unseen      bool   // finished a turn nobody looked at
      Pane        string // tmux pane id ("%12"), "" = not in tmux
  }
  type Activity struct{ Tool, Detail string }
  type Prompt struct { ID, Kind, Title, Detail, Context string; Options []Option } // Kind permission|question|input
  type Option struct { Key, Label string; Primary bool }
  type Subagent struct{ ID, Title, Type, Tool string }
  func Label(modelID string) string   // "opus 5.5"; else id without "claude-"
  func Window(modelID string) int64   // context window; 200_000 when unknown
  func agentID(sessionID string) string // "l" + 5 hex of sha256("claude-live:"+id), as agents-deck
  ```
  `entry` (unexported) = `Agent` + `Transcript string`, `TTY string`, `Offset int64`, `Pending string`, `PromptN int`, `Menu bool`, `CustomTitle string`, `AITitle string`, `Background bool`, `CompactManual bool`, `HookSeen bool`, `EndedAt time.Time`; methods `SetStatus(s Status, now time.Time)` (stamps `StatusSince` only on change; clears `Waiting` unless waiting, `ErrorText` unless error, `Activity` unless running) and `NextPromptID() string` ("p1", "p2", …).

- [ ] **Step 1: Failing tests.**
  - `config_test.go: TestAgentsFlag`: default false; file `{"agents": true}` → true; `CUBE_AGENTS=0` beats it; `CUBE_AGENTS=on` with no file → true.
  - `models_test.go: TestLabelWindow`: `Label("claude-opus-5-5")=="opus 5.5"`, `Window("claude-opus-5-5")==1_000_000`, `Label("claude-haiku-4-5-20251001")=="haiku 4.5"`, `Window(…haiku…)==200_000`, `Label("claude-x-9")=="x-9"`, `Window("")==200_000` (table copied from agents-deck `internal/pricing/pricing.go`, labels and windows only).
  - `entry_test.go: TestSetStatus`: running→waiting stamps `StatusSince`; waiting→waiting keeps it; leaving waiting clears `Waiting`; leaving running clears `Activity`. `TestAgentID`: stable, 6 chars, `[a-z0-9]`, starts with `l`, differs for two session ids.
- [ ] **Step 2: Run, see FAIL.** `cd bridge && go test ./internal/config/ ./internal/agents/`
- [ ] **Step 3: Implement.** Copy the needed types from agents-deck `internal/model/model.go` and `internal/store/store.go` (`SetStatus`, `NextPromptID`, `NewID`-style id) with the time passed in; write `NOTICE.md` (repository URL, commit `699bba8`, the list of source files used by this plan, and the line "No licence was published; copied with the owner of this private repository's consent. Do not make this repository public without the agents-deck author's permission.").
- [ ] **Step 4: Run, see PASS** (`go vet ./... && go test ./...`).
- [ ] **Step 5: Commit** `feat(bridge): agents flag and copied agent model`.

---

### Task 2: Transcript tail, subagents and pane parsing

**Files:**
- Create: `bridge/internal/agents/transcript.go`, `transcript_test.go`, `pane.go`, `pane_test.go`, `birth_darwin.go`, `birth_other.go`

**Interfaces:**
- Consumes: `entry`, `Label`, `Window` (Task 1).
- Produces:
  ```go
  func tail(e *entry, now time.Time) bool                 // agents-deck claude.Tail, minus tokens/cost
  func subagents(transcript string, now time.Time, window, toolWindow time.Duration) []Subagent
  func transcriptTurn(path string, now time.Time, window time.Duration) (running bool, tool string)
  func ParseOptions(pane string) []Option
  func RefineOptions(p *Prompt, pane string) bool
  func ParseCursorMenu(pane string) (title string, opts []Option, ok bool)
  ```

- [ ] **Step 1: Failing tests.** Port agents-deck's own tests for these functions (`internal/claude/claude_test.go`: the `permissionPane`, `questionPane`, `trustPane` fixtures and `ParseOptions`/`RefineOptions`/`ParseCursorMenu` cases; `sessions_test.go`: subagent and transcript-turn cases), adapted to the injected `now`. Add:
  - `TestTailContextAndModel`: a temp transcript with two assistant lines (usage `input 10, cache_read 400_000, cache_creation 100`, model `claude-opus-5-5`) → `Context.Used == 400_110`, `Window == 1_000_000`, `ModelLabel == "opus 5.5"`; a later `ai-title` line sets `AITitle`; `gitBranch` sets `Branch`.
  - `TestTailAPIError`: an `isApiErrorMessage` assistant line → `Status == error`, `ErrorText == "overloaded"` for content text `API Error: Overloaded`; a following normal reply → running again.
  - `TestTailPartialAndHugeLines`: a 3 MB line followed by a half-written line → one read consumes the complete lines only and resumes the partial one later (no `bufio.Scanner` line cap: split on `\n` like Plan 1's local source).
  - `TestTailTruncated`: file shrinks → re-read from 0.
- [ ] **Step 2: Run, see FAIL.**
- [ ] **Step 3: Implement** by copying agents-deck `internal/claude/transcript.go`, `sessions.go` (`Subagents`, `TranscriptTurn`, `subagentTail`), `pane.go`, `birth_*.go`; drop `SetUsage`/`Totals`/cost; replace `time.Now()` with the parameter; replace the `bufio.Scanner` in `Tail` with a byte split.
- [ ] **Step 4: PASS.** **Step 5: Commit** `feat(bridge): transcript tail, subagents and pane parsing from agents-deck`.

---

### Task 3: Hooks folding, tmux wrapper and the Tracker

**Files:**
- Create: `bridge/internal/agents/hooks.go`, `hooks_test.go`, `tmux.go`, `tmux_test.go`, `discover.go`, `discover_test.go`, `tracker.go`, `tracker_test.go`

**Interfaces:**
- Consumes: Tasks 1–2.
- Produces:
  ```go
  type Runner func(ctx context.Context, name string, args ...string) ([]byte, error) // exec seam

  type Tmux struct{ Bin string; Run Runner }
  func NewTmux() *Tmux
  func (t *Tmux) Panes(ctx context.Context) map[string]string      // tty ("/dev/ttys012") -> pane id ("%12"); empty when no server
  func (t *Tmux) Capture(ctx context.Context, pane string, back int) string
  func (t *Tmux) SendKeys(ctx context.Context, pane, literal string, keys ...string) error

  type Live struct{ PID int; TTY, Cwd, SessionID, Transcript, Name string; Started time.Time
                    Background, Busy, Blocked, Reported bool }
  type Discoverer interface{ Sessions(ctx context.Context) []Live } // claude agents --json (≤ every 10 s) else process scan

  var (ErrNotFound, ErrStale, ErrBadKey, ErrNotInTmux = errors…)
  type Tracker struct{ /* mutex, entries by id, subscribers */ }
  type TrackerDeps struct {
      Tmux     *Tmux
      Discover Discoverer
      Projects string           // ~/.claude/projects (or $CLAUDE_CONFIG_DIR/projects)
      Now      func() time.Time
      After    func(time.Duration, func()) // time.AfterFunc seam for the option re-scan
  }
  func NewTracker(d TrackerDeps) *Tracker
  func (t *Tracker) Hook(body []byte) error                        // fold one hook payload
  func (t *Tracker) Sync(ctx context.Context)                      // discovery, panes, tail, subagents, hookless status, expiry
  func (t *Tracker) Answer(ctx context.Context, agentID, promptID, key string) error
  func (t *Tracker) Snapshot() []Agent                             // copies
  func (t *Tracker) Subscribe() (<-chan struct{}, func())          // tick after every change
  ```

**Behaviour to keep from agents-deck** (copy, then adapt): `claude.Apply` and its helpers (`hooks.go`), `daemon.Hook` (the 250 ms / 900 ms `RefineOptions` re-scan, now via `After` and `Tmux.Capture(pane, 40)`), `daemon.Answer` (prompt-id check, key check, arrow-key menus via `Up`×n/`Down`×idx/`Enter`, the status after an answer), `daemon.subagents`, `daemon.liveStatus` (only for sessions with no hook seen yet: `HookSeen == false`), `daemon.scanMenu` (only for agents in `starting`), `claude.ActiveSessions`/`LiveSessions`/`isInteractiveClaude`/`sessionArg`/`newestTranscript`/`processCwds`/`ttysOf` (`discover.go`, through `Runner`). **Changes:**
  - Agents are keyed by `agentID(session_id)`. A hook for an unknown session **creates** the agent (from `cwd`, `transcript_path`); discovery also creates them. Hooks whose `transcript_path` contains `/subagents/` never create an agent.
  - Panes come from `Tmux.Panes` matched on the session's TTY (from discovery); `Agent.Pane == ""` when none.
  - An agent leaves the tracker when it has been `exited` for 60 s, or when discovery has run successfully twice in a row without it.
  - `Answer` on an agent with `Pane == ""` returns `ErrNotInTmux` without typing.
  - The hook body is read whole (no 1 MiB cut); `Detail` stays clipped to 2000 runes as in agents-deck.
  - Drop: `Unseen`-clearing by focused pane, focus, interrupt, dismiss, resume, accounts, hats, external apps.

- [ ] **Step 1: Failing tests** (fake `Runner`, fake `Discoverer`, fixed clock, `After` that runs synchronously):
  - `hooks_test.go`: port agents-deck's `Apply` cases. Add `TestHookLargePayload` (Review Focus 2): a `PermissionRequest` for `Write` whose `tool_input.content` is 3 MB → `Status == waiting`, `Waiting.Kind == "permission"`, `Waiting.Title == "create this file?"`, `len([]rune(Waiting.Detail)) <= 2000`.
  - `tmux_test.go`: `Panes` parses `list-panes -a -F '#{pane_tty} #{pane_id}'` output; no server (runner error) → empty map; `SendKeys` issues `send-keys -t %12 -l 1` then nothing else for a literal; `Capture` passes `-p -J -t %12 -S -40`; the binary has no `-L` argument anywhere.
  - `discover_test.go`: `claude agents --json` fixture (interactive + background, one bad session id) → `Live`s with TTYs from a `ps -o pid=,tty=` fixture; runner failure → process-scan path from `ps -axww …` and `lsof` fixtures; port agents-deck's `live_test.go` cases.
  - `tracker_test.go`:
    - `TestHookCreatesAgent`: `SessionStart` for an unknown session → one agent, `Folder == "proj"`, `Status == idle`.
    - `TestPermissionFlow`: `PermissionRequest` (Bash `terraform apply`) → waiting, `Waiting.ID == "p1"`, options from the pane fixture after the re-scan (3 options, labels from `permissionPane`).
    - `TestAnswer`: agent with `Pane "%3"` waiting on `p1` → `Answer(id,"p1","1")` sends `send-keys -t %3 -l 1`, status becomes running; a "no" label → idle.
    - `TestAnswerStale` (Review Focus 5): after `PostToolUse` cleared the prompt, `Answer(id,"p1","1")` → `ErrStale`, no `send-keys`; wrong key → `ErrBadKey`; unknown id → `ErrNotFound`.
    - `TestAnswerNotInTmux`: `Pane == ""` → `ErrNotInTmux`, nothing sent.
    - `TestMenuAnswer`: a `starting` agent whose pane shows `trustPane` → `Sync` makes it waiting with 2 options; `Answer(…, "2")` sends `Up Up Down Enter`.
    - `TestHooklessStatus`: a discovered session with no hook and a transcript whose last line is an assistant `tool_use` → running with `Activity.Tool`; after an `end_turn` → idle, `Unseen`.
    - `TestExpiry`: exited for 61 s → gone after `Sync`; missing from two successful discoveries → gone; a failed discovery removes nothing.
    - `TestSubagentHookIgnored`: a hook with a `/subagents/` transcript path creates nothing.
    - `TestSubscribe`: a change ticks the channel; a no-op hook does not.
- [ ] **Step 2: FAIL.** **Step 3: Implement.** **Step 4: PASS** (`go test -race ./internal/agents/`).
- [ ] **Step 5: Commit** `feat(bridge): agent tracker with hooks, discovery and tmux answers`.

---

### Task 4: `hook` and `hooks` subcommands

**Files:**
- Modify: `bridge/cmd/cube-bridge/main.go`, `main_test.go`
- Create: `bridge/internal/hooksettings/command.go` (only `Command`, used here and in Task 5)

**Interfaces:**
- Consumes: nothing new (`hooks install|uninstall` dispatch is added in Task 5, next to the code it calls).
- Produces:
  ```go
  // hooksettings
  // Command is the hook command line written into settings.json: it runs the bridge's
  // `hook` subcommand when the binary exists and always exits 0.
  func Command(binary string, port int) string // `[ -x 'B' ] && 'B' hook --port P >/dev/null 2>&1; exit 0` with B shell-quoted
  // main
  func runHook(stdin io.Reader, port int, client *http.Client) // never prints, never fails
  ```
  Argument handling: `cube-bridge` (serve, as today), `cube-bridge hook [--port N]` (default 8787); anything else → usage on stderr, exit 2 (`parseArgs` keeps rejecting stray words for the serve path). Task 5 adds `hooks install|uninstall`.

- [ ] **Step 1: Failing tests.**
  - `TestHookPosts`: an `httptest` server on loopback → `runHook` POSTs the exact stdin bytes to `/hook` with `X-Cube-Client: 1` and `Content-Type: application/json`.
  - `TestHookReadsLargeStdin` (Review Focus 2): 3 MB stdin arrives whole; 9 MB is cut at 8 MiB (and still posts).
  - `TestHookBridgeDown` (Review Focus 1): nothing listening on the port → returns within 1.2 s, writes nothing to stdout/stderr.
  - `TestHookSlowBridge`: a server that never answers → returns within 1.2 s.
  - `TestHookCommandBinaryMissing` (Review Focus 1): `sh -c "$(Command("/nonexistent/cube-bridge", 8787))"` exits 0 with empty stdout and stderr; a path containing a space and a `'` round-trips through the quoting.
  - `TestArgs`: `hook --port 9000` parsed; `hook --port x` → exit 2; `banana` → exit 2; no arguments → serve path unchanged.
- [ ] **Step 2: FAIL.** **Step 3: Implement** (`http.Client{Timeout: time.Second}`, `io.LimitReader(stdin, 8<<20)`, errors swallowed). **Step 4: PASS.**
- [ ] **Step 5: Commit** `feat(bridge): hook subcommand`.

---

### Task 5: Installing the hooks into `settings.json`

**Files:**
- Create: `bridge/internal/hooksettings/hooksettings.go`, `hooksettings_test.go`
- Modify: `bridge/cmd/cube-bridge/main.go` (wire `hooks install|uninstall`), `main_test.go`

**Interfaces:**
- Produces:
  ```go
  var Events = []string{"SessionStart","UserPromptSubmit","PreToolUse","PostToolUse","PermissionRequest","Notification","Stop","SessionEnd","PreCompact"}
  func Install(path, command string) error   // idempotent; replaces the bridge's old entries
  func Uninstall(path string) error           // removes only the bridge's entries; a missing file is fine
  func IsOurs(command string) bool            // the command contains "' hook --port " and "cube-bridge"
  ```
  Each event gets one matcher group `{"matcher": "*", "hooks": [{"type":"command","command": <command>,"timeout": 5}]}` (`matcher` only on `PreToolUse`, `PostToolUse`, `PermissionRequest`, as agents-deck's `HooksSettings`), appended after the user's existing groups for that event. Uninstall removes any hook whose command `IsOurs`, then any group left with no hooks, then an event key left empty, then a `hooks` object left empty.

- [ ] **Step 1: Failing tests** with this fixture as the user's file (the real shape of the user's settings, Review Focus 3):
  ```json
  {"model":"opus","hooks":{"PreToolUse":[{"matcher":"*","hooks":[{"type":"command","command":"jq -r .cwd | auto-allow"}]}],"Stop":[{"hooks":[{"type":"command","command":"/Users/u/.claude/hooks/notify-finished.py"}]}],"Notification":[{"matcher":"permission_prompt","hooks":[{"type":"command","command":"osascript a"}]},{"matcher":"agent_needs_input","hooks":[{"type":"command","command":"osascript b"}]}]},"permissions":{"allow":["Bash(ls:*)"]},"zzz":1}
  ```
  - `TestInstallKeepsUserHooks`: after `Install`, every one of the user's hook groups is still present, first, and unchanged; each of the 9 events has exactly one group with our command; top-level key order is `model, hooks, permissions, zzz`.
  - `TestInstallIdempotent`: installing twice (and once with a different port) leaves exactly one of our groups per event, with the latest command.
  - `TestUninstallRestores`: `Install` then `Uninstall` → semantically equal to the fixture and with the same key order at every level.
  - `TestBrokenJSON`: a file containing `{"hooks": ` → `Install` returns an error mentioning the path, and the file is byte-identical afterwards.
  - `TestMissingFile`: `Install` on a missing path creates `{"hooks":{…}}`; `Uninstall` on a missing path returns nil.
  - `TestBackupAndAtomic`: the first `Install` writes `settings.json.cube-bridge.bak` with the original bytes (not overwritten by later installs); the file mode stays as it was (0600 or 0644); no temp files remain.
  - `TestHooksArgs`: `hooks install --settings /tmp/x.json --port 9000` and `hooks uninstall` parsed (default settings path `~/.claude/settings.json`, default port 8787); `hooks frob` and bare `hooks` → exit 2.
  - `TestMainHooksInstall`: `cube-bridge hooks install --settings <tmp> --port 9000` writes commands containing `hook --port 9000` and the absolute path of the test binary (`os.Executable`, symlinks resolved).
- [ ] **Step 2: FAIL.** **Step 3: Implement** an order-preserving JSON object (a small `[]struct{Key string; Value json.RawMessage}` decoded with `json.Decoder.Token`) for the top level, `hooks`, and each event array element; untouched values stay as their raw bytes; the result is re-indented with two spaces. **Step 4: PASS.**
- [ ] **Step 5: Commit** `feat(bridge): install and remove the bridge's Claude Code hooks`.

---

### Task 6: The Agents card and the size budget

**Files:**
- Create: `bridge/internal/cards/agents.go`, `agents_test.go`
- Modify: `bridge/internal/cards/cards.go` (`Card` fields, `Opts`, `Build`)

**Interfaces:**
- Consumes: `agents.Agent` (Task 1).
- Produces (payload contract, added fields; plan 3 parses them):
  ```go
  type Tile struct {
      I string `json:"i"`           // agent id
      N string `json:"n"`           // folder name, Fit 16
      L string `json:"l"`           // status line, Fit 16: "needs you" | "Edit +3" | "thinking" | "idle 12m" | "error" | "exited" | "starting"
      K string `json:"k"`           // run|wait|idle|err|exit|start
      P string `json:"p"`           // pose: idle|start|run|think|read|write|bash|web|plan|delegate|compact|wait|err|exit|hungry
      X int    `json:"x"`           // context used, 0..100
      U int    `json:"u,omitempty"` // 1 = finished a turn nobody looked at
  }
  type AgentPrompt struct {
      I string         `json:"i"`           // agent id
      P string         `json:"p"`           // prompt id
      K string         `json:"k"`           // permission|question|input
      T string         `json:"t"`           // title, Fit 40
      D string         `json:"d"`           // detail, Fit 120
      C string         `json:"c"`           // context, Fit 40
      O []PromptOption `json:"o,omitempty"` // ≤ 6, label Fit 40
      X string         `json:"x,omitempty"` // why buttons are off: "tmux" (not in a tmux pane); "" = answerable
  }
  type PromptOption struct{ K, L string } // json "k","l"
  // Card gains: A []Tile `json:"a,omitempty"`, Q *AgentPrompt `json:"q,omitempty"`
  // Opts gains: Agents []agents.Agent; ShowAgents bool; Full bool (loopback variant)
  const MaxPayloadBytes = 2048
  const MaxTiles = 8
  ```
  The Agents card: `{t:"AGENTS", v:"<n open>", s1:"<summary>", s2:"", c:"accent"|"blue"|"ink", a:[…], q?}`, appended after the usage card (before the extra cards), present only when `ShowAgents` and at least one agent is open (spec: left out when none). `s1` = `"1 NEEDS YOU"` / `"2 NEED YOU"` when any wait, else `"3 RUNNING"` when any run, else `"ALL IDLE"`; `c` = `accent` when any wait, `blue` when any run, else `ink`. Tiles: waiting first (longest-waiting first), then running, starting, error, idle, exited; ties by folder name; at most `MaxTiles`. `q` (only when `Full`) = the longest-waiting agent's prompt. Pose from status and `Activity.Tool`: `Read|Grep|Glob|LS→read`, `Edit|MultiEdit|Write|NotebookEdit→write`, `Bash→bash`, `WebFetch|WebSearch→web`, `TodoWrite|ExitPlanMode→plan`, `Task|Agent→delegate`, `Compact→compact`, other running→`run`, running with no activity→`think`, waiting→`wait`, error→`err`, exited→`exit`, idle with `Unseen`→`hungry`, idle→`idle`, starting→`start`. Status line from status, tool name (never the detail) and `len(Subagents)` (`"Edit +3"`), idle age in `cards.until`-style units (`"idle 12m"`).

- [ ] **Step 1: Failing tests.**
  - `TestAgentsCardOrderAndSummary`: four agents (waiting since 2 min, waiting since 5 min, running Edit with 3 subagents, idle 12 min unseen) → tiles in order [5-min waiter, 2-min waiter, runner, idle], lines `needs you`, `needs you`, `Edit +3`, `idle 12m`, poses `wait`, `wait`, `write`, `hungry`, `s1 == "2 NEED YOU"`, `c == "accent"`, `q.i` = the 5-min waiter.
  - `TestLANVariantHasNoPromptText`: same input with `Full == false` → no `q`; marshalled card JSON contains none of the prompt's title, detail, option labels or the agents' `LastPrompt` / `Activity.Detail` strings.
  - `TestNotInTmux`: waiting agent with `Pane == ""` → `q.x == "tmux"`.
  - `TestNoAgentsNoCard`: `ShowAgents` with zero agents → no card; `ShowAgents == false` with agents → no card, payload identical to Plan 1's output.
  - `TestAgentsPayloadBudget` (Review Focus 4): 8 agents with 60-character non-ASCII folder names, every one waiting, the oldest with a question of 6 options of 200 characters each and a 3000-character detail; usage card with mail; `ExtraCards` on → every `Build` result marshals to ≤ 2048 bytes; then the same with `ExtraCards` off keeps all 8 tiles and the prompt. Trimming order when over budget: drop prompt `d`, then options beyond 4, then tiles from the end down to 4, then the prompt, then the extra cards; test each step fires in that order with a crafted input.
  - Plan 1's golden test still passes untouched.
- [ ] **Step 2: FAIL.** **Step 3: Implement.** **Step 4: PASS.**
- [ ] **Step 5: Commit** `feat(bridge): Agents card with a payload budget`.

---

### Task 7: Server: two payloads, loopback guard, `/hook`, `/api/answer`

**Files:**
- Modify: `bridge/internal/server/server.go`, `server_test.go`, `bridge/cmd/cube-bridge/main.go`

**Interfaces:**
- Consumes: `agents.Tracker` (Task 3), `cards.Opts` additions (Task 6).
- Produces:
  ```go
  // Deps gains:
  Agents interface {
      Hook([]byte) error
      Sync(context.Context)
      Answer(ctx context.Context, agentID, promptID, key string) error
      Snapshot() []agents.Agent
      Subscribe() (<-chan struct{}, func())
  } // nil when config Agents is off
  func loopbackRequest(r *http.Request, port int) bool  // the Loopback guard (Global Constraints), minus the X-Cube-Client part
  ```
  - Two `atomic.Pointer[cards.Payload]`: full and LAN, both rebuilt by every refresh and also within 200 ms of any tracker change (a goroutine on `Subscribe`, debounced 150 ms, rebuilding from the last limits/mail/events without re-reading them), so a new prompt reaches the cube without waiting for the 5 s refresh. The refresh goroutine stores its last inputs (limits, mail, a copy of the events slice, billed, error) under the server mutex; the rebuild goroutine uses only those — it never calls `Source` (not goroutine-safe).
  - `GET /api/status` serves the full payload when `loopbackRequest`, else the LAN one.
  - `POST /hook` (guard + `X-Cube-Client: 1`, body ≤ 8 MiB) → `Tracker.Hook`, 204; any error → 204 too (hooks never learn about bridge problems), logged once per distinct message.
  - `POST /api/answer` (guard + header) body `{"agent":"…","prompt":"…","key":"1"}` → 204; `ErrStale` 409; `ErrBadKey` 400; `ErrNotFound` 404; `ErrNotInTmux` 409 with `{"error":"not in tmux"}`; anything else 500 `{"error":…}`.
  - `Agents == nil` → `/hook` and `/api/answer` 404, cards built without agents.
  - The tracker's `Sync` runs every 3 s on its own goroutine (not inside the refresh chain), until shutdown.
  - `main.go` builds `agents.NewTracker` (real `Tmux`, real discoverer, `Projects` from `$CLAUDE_CONFIG_DIR/projects` or `~/.claude/projects`) only when `cfg.Agents`.

- [ ] **Step 1: Failing tests** (stub `Agents`; requests built with `httptest.NewRequest` and an explicit `RemoteAddr`/`Host`, plus one real-listener test):
  - `TestLoopbackGuard`: remote `127.0.0.1:5000` + Host `127.0.0.1:8787` → true; Host `localhost:8787` → true; Host `evil.test:8787` → false (DNS rebinding); remote `192.168.1.20:5000` → false; an `Origin` header present → false; Host with another port → false; IPv6 `[::1]` remote with Host `localhost:8787` → true.
  - `TestStatusVariants`: a loopback request gets a payload whose Agents card has `q`; a LAN request (same server, remote `192.168.1.20`) gets no `q` and no prompt text anywhere in the body.
  - `TestHookEndpoint`: guarded loopback POST with the header → `Hook` called with the exact body, 204; without `X-Cube-Client` → 404, not called; from a LAN address → 404; `Agents == nil` → 404.
  - `TestAnswerEndpoint` and `TestAnswerEndpointStale` (Review Focus 5): each tracker error maps to its status; a browser-style request (Origin set) → 404 and `Answer` not called.
  - `TestPromptPushedQuickly`: a tracker change tick → within 400 ms (fake debounce clock or real 150 ms) the full payload shows the new prompt, with no extra limits/mail reads.
  - `TestAgentsOffUnchanged`: with `Agents == nil`, `/api/status` equals Plan 1's output for the same stubs.
  - `TestSyncLoopStops`: `Sync` is called repeatedly while running and not after `ctx` is cancelled.
- [ ] **Step 2: FAIL.** **Step 3: Implement.** **Step 4: PASS** (`go test -race ./...`).
- [ ] **Step 5: Manual check** (no port 8787): `CUBE_AGENTS=1 CUBE_PORT=8799 ./cube-bridge` from `bridge/`, then `./cube-bridge hooks install --settings /private/tmp/claude-501/s.json --port 8799` and run `echo '{"hook_event_name":"SessionStart","session_id":"11111111-2222-3333-4444-555555555555","cwd":"/tmp/x","transcript_path":"/tmp/x.jsonl"}' | ./cube-bridge hook --port 8799`; `curl -s -H 'Host: 127.0.0.1:8799' 127.0.0.1:8799/api/status` shows an AGENTS card with one tile; `curl` with `-H 'Host: x:8799'` shows the LAN variant. Stop the bridge. Do not touch the real `~/.claude/settings.json`.
- [ ] **Step 6: Commit** `feat(bridge): serve agents, hook intake and answers on loopback`.

---

### Task 8: Install scripts, tolerant renderers and docs

**Files:**
- Modify: `bridge/agent.sh`, `mac-helper/install.sh`, `bridge/preview.html`, `mac-helper/Sources/CubeLinkCore/MenuModel.swift`, `mac-helper/Tests/CubeLinkCoreTests/MenuModelTests.swift` (or the existing menu test file), `CLAUDE.md`, `bridge/CLAUDE.md`, `README.md`, `bridge/config.example.json`

- [ ] **Step 1: Failing Swift test** `testAgentsCardRow`: a payload with the usage card plus `{"t":"AGENTS","v":"3","s1":"1 NEEDS YOU","s2":"","c":"accent","a":[…]}` → the menu shows one row `AGENTS  1 NEEDS YOU` (label `AGENTS`, value `3`, no gauge) and the bar title still follows the usage ring.
- [ ] **Step 2: Implement** the `MenuModel` change; `preview.html` draws an Agents card as a simple list (`n`, `l` per tile, accent for `k == "wait"`) and, when `q` is present, the prompt title and option labels under it — enough to read the data, mirroring nothing yet of plan 3's layout (state that in a comment).
- [ ] **Step 3: Install scripts.** `agent.sh install` runs `"$DIR/cube-bridge" hooks install --port "$PORT"` after building, `uninstall` runs `hooks uninstall` before removing the plist; `mac-helper/install.sh install` runs `"$APP_DST/Contents/Resources/bridge/cube-bridge" hooks install --port "$port"` after copying the app, `uninstall` runs `… hooks uninstall` before deleting it. Both print one line saying what they did to `~/.claude/settings.json`. Verify with `bash -n` and, through each script's library hook (`CUBE_AGENT_LIB=1` in agent.sh; add an equivalent guard to install.sh if needed), against a temp settings file — never the real one, and never run `install`/`uninstall` for real.
- [ ] **Step 4: Docs.** Root `CLAUDE.md`: a new **Agents** paragraph in Architecture (tracker, hooks in the global settings, tmux panes, the two variants and the loopback guard, the 2048-byte budget, the `agents` flag and that it stays off until plan 3's firmware, the provenance note) and the payload contract additions (`a`, `q`, tile and prompt fields) with "Changing the contract" now naming `internal/cards/agents.go`; `bridge/CLAUDE.md`: commands (`hooks install|uninstall`, `hook`), the endpoints, testing seams (`Runner`, `Discoverer`); `README.md`: how to turn agents on and what the hooks do; `config.example.json`: `"agents": false` with a comment line.
- [ ] **Step 5: Verify.** `cd bridge && go vet ./... && go test -race ./...`; `swift test --package-path mac-helper`; open `preview.html` against the Task 7 manual-check bridge (port 8799) and see the list.
- [ ] **Step 6: Commit** `feat: install the hooks from the install scripts; renderers tolerate the Agents card; docs`.
