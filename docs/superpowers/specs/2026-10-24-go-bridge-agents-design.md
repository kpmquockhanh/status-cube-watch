# Go bridge with Claude agents on the cube: design

Date: 2026-10-24. Status: draft for approval.

## Intent

The user runs several Claude Code agents at once, each in its own pane of their own tmux. They want the
cube to show all of their open agents at a glance, tell them when one is waiting, and let them answer a
permission prompt or question from the cube instead of hunting for the pane.

[agents-deck](https://github.com/brushknight/agents-deck) (checked out at `~/code/agents-deck`, a Go
daemon by another author) already tracks Claude Code agents well: hooks, transcripts, `claude agents
--json`, prompt options read off the pane, answers typed into tmux. Rather than run it next to the
bridge, the user wants **one program**: the bridge rewritten in Go, with agents-deck's Claude tracking
copied into it and everything else of agents-deck left out. The cube keeps talking to the bridge the
way it does today (WiFi polling of `/api/status`, and BLE through `ClaudeCubeLink.app`).

## Goals

1. **The bridge becomes a Go program** in `bridge/`, with feature parity with the Node bridge it
   replaces: usage rings from the OAuth usage endpoint (Keychain, else `~/.claude/.credentials.json`;
   60 s cache, fail-soft 10 min, 429 back-off), the optional spend/token cards (`local` and `admin`
   sources, pricing), the Gmail unread badge, `config.json` + env-var precedence exactly as today,
   `GET /api/status`, `/` (preview page), `/health`, the `STARTING` payload, the second loopback
   listener when `host` is pinned, explicit `Content-Length` on every response. All formatting stays
   in the bridge (the Go counterpart of `cards.mjs`). The Node files are deleted once the Go bridge
   is at parity.
2. **Agent tracking from agents-deck, Claude only.** Every Claude Code session open on the Mac shows
   up, however it was started: discovery through `claude agents --json` with agents-deck's process-scan
   fallback, state from hook events, details from the session transcript (title, model, context used
   / window, activity tool + detail, subagents, last prompt, error).
3. **Global hooks.** The bridge installs its hook entries into the user's global
   `~/.claude/settings.json` (the user chose this over screen-reading only). Hook events are the same
   set agents-deck subscribes to (`SessionStart`, `UserPromptSubmit`, `PreToolUse`, `PostToolUse`,
   `PermissionRequest`, `Notification`, `Stop`, `SessionEnd`, `PreCompact`). The hook command forwards
   the payload to the running bridge and prints nothing.
4. **Answer prompts in the user's own tmux.** The bridge maps each Claude process to its pane in the
   user's tmux (process TTY ↔ `#{pane_tty}`), reads the dialog's options with agents-deck's pane
   parser (`capture-pane` + `ParseOptions`/`RefineOptions`), and answers with `tmux send-keys` of the
   option's key. An answer carries the prompt id; if the agent's current prompt is no longer that id
   the answer is refused (agents-deck's 409 rule).
5. **An Agents card on the cube**, a 2×2 grid of tiles (layout B of the mockups), one tile per open
   agent:
   - folder name;
   - a pixel critter whose pose follows the state (working / the kind of tool, waiting, idle-asleep,
     error, exited), ported from agents-deck's critter to LovyanGFX;
   - one short status line (e.g. `Edit · +3`, `needs you`, `idle 12m`), composed by the bridge;
   - a context bar along the bottom.
   A waiting agent's tile is solid accent (orange) and waiting tiles sort first. With more than four
   agents the grid scrolls vertically inside the card. The card is one deck card whatever the number of
   agents (counts once against `MAX_CARDS`).
6. **The prompt screen** (layout A): tapping an orange tile opens the prompt: agent and tool on top,
   the question, the command/file in a small box (clipped to a few lines), then one button per option
   exactly as Claude offers them, the first highlighted, long labels clipped, the list scrolling when
   it does not fit. Swipe right or BACK returns to the grid. When the prompt goes away (answered at the
   terminal, or by the cube) the screen closes by itself.
7. **Answering is BLE only.** A tap on an option goes cube → new Control message → `ClaudeCubeLink.app`
   → bridge on `127.0.0.1` → tmux. The bridge accepts answers only on its loopback listener. A cube
   that has the agents over WiFi only shows the prompt with greyed-out buttons and `answer over
   Bluetooth`. Agents not in a tmux pane show greyed buttons and `not in tmux`. Prompts of kind
   `input` (free text) show their text with no buttons (`answer at the terminal`).
   **Not while the Mac is locked.** While the Mac's screen is locked, the prompt screen greys its
   buttons with `unlock Mac first`, and the Mac helper refuses (drops and logs) any answer that arrives
   while `ScreenLock` says locked, a policy check in `CubeLinkCore` with a `swift test`, like
   `UnlockPolicy`. The cube learns the lock state the way it does today (Volume flag bit 5). Physical
   access to the cube must never approve a command on a locked Mac.
7a. **Prompt contents stay off the LAN.** The bridge builds two variants of the payload. The
   loopback listener (the Mac helper, hence BLE) gets everything. Any other listener (WiFi cubes, the
   LAN) gets the grid only: tile names, state, pose, context and the status line, but no prompt
   title, detail, options or last prompts. A WiFi-only cube tapping an orange tile shows `needs you`
   and `open on Bluetooth` instead of the prompt. `preview.html` and the sim shots cover both
   variants.
8. **Attention.** When an agent starts waiting (a new prompt id the cube has not alerted on), the cube
   wakes the screen (also from screen sleep), jumps to that prompt screen, and plays the buzzer's
   reserved `Attention` tune once, at the SOUND level (SOUND off = silent). With several waiting, it
   opens the longest-waiting one; the others stay orange on the grid. It does not interrupt: a
   navigation ride (the beep is held back too), an open display settings panel (it waits until the
   panel closes), a finger mid-drag, a pairing screen, the unlock prompt (the Mac is locked: the
   unlock prompt keeps the screen; the prompt can't be answered anyway). In those cases the tile
   still turns orange.
9. **The Mac helper runs the Go binary** instead of `node bridge/server.mjs`: `build-app.sh` bundles
   the compiled bridge (plus `preview.html`), `BridgeSupervisor` launches it, `install.sh` and
   `bridge/agent.sh` stop looking for `node`. The app's Bridge settings keep reaching the child as the
   same env vars. The app also forwards the new Control answer message to the bridge's loopback
   listener.

## Non-goals

- Everything in agents-deck that is not Claude tracking: iTerm, Orca, herdr, cmux, Codex, Gemini,
  Deck Link (USB serial boards, protobuf), the TLS listener with its CA and device pairing, the web
  dashboard, demo/sim mode, multiple Claude accounts and handoff, `agentctl new/attach/resume/restore`
  and the other CLI verbs, agents-deck's private tmux server (`-L agentsterm`), hat editing.
- From the cube: interrupt, focus, dismiss, a detail view for tiles that are not waiting (a tap on
  such a tile does nothing for now), typing free-text answers.
- Answering over WiFi.
- Linux and Windows.
- Changing the usage card, the Volume card, unlock, navigation or any existing behaviour beyond what
  the new card and alert need.

## Anti-goals (a failure even if it works)

- A stale tap answering a newer prompt. Every answer is checked against the current prompt id.
- Anything on the LAN able to approve a command. Answers never come in over the non-loopback listener.
- Anything on the LAN able to read what an agent is asking or was asked (commands, file paths, the
  user's prompts). Those go only to the loopback listener.
- Someone at the desk approving a command while the Mac is locked.
- Hooks that slow down or break Claude sessions. When the bridge is not running the hook command exits
  0 at once, prints nothing and never blocks the session (short connect timeout).
- Damaging the user's `~/.claude/settings.json`: the bridge adds only its own entries, recognises them
  on later runs (idempotent install), removes only them on uninstall, preserves everything else
  (including key order where practical), and writes atomically.
- Formatting creeping into the firmware. Status words, ordering, colours and clipping are the bridge's;
  the firmware maps a pose to a drawing and draws strings.
- Regressions in what works today: the usage card, BLE push, Volume, unlock, nav, the portal, sleep,
  and the Mac helper's menu.
- The Mac helper needing `node`.

## Constraints

- **Licence and provenance.** agents-deck has no licence file. The user decided to copy its code
  anyway: this repository is private and stays private. Keep a short note in the copied packages (or a
  `bridge/NOTICE`) that names the source repository and the commit copied from, so that is not lost
  if the repo's visibility ever changes.
- **Go.** agents-deck's `go.mod` says `go 1.24.1`; this Mac has Go 1.23.0, so building needs Go ≥ 1.24
  (state it in the README and CLAUDE.md). Aim for zero third-party Go modules: agents-deck's only one
  (protobuf) belongs to Deck Link, which is not copied. The binary targets macOS on Apple Silicon.
- **The repo's rules still hold:** all formatting in the bridge; payload strings printable ASCII cut to
  the firmware's buffers; `MAX_CARDS = 8`; firmware pure logic in header-only/pure files with a host
  test each, hardware behind headers with sim stand-ins, `gnu++11` on the board; `make test`,
  `make bench`, `swift test` and `make shot` keep passing.
- **Size of the BLE payload.** `BLE_MAX_PAYLOAD` is 2048 bytes and the whole payload (usage card plus
  the agents) must fit. The builder sets per-field limits and a tile cap so the worst case fits, and
  decides how much prompt text travels (see "Left to the builder").
- **Contract changes touch every renderer.** A new card type means updating the Go card builder,
  `payload.h/.cpp`, `ui.cpp`, `preview.html` (it mirrors the grid and the prompt screen by hand),
  and `MenuModel.swift` (at least: it must not misread the new card; showing agents in the menu bar is
  optional). The new Control message means updating `docs/ble-protocol.md`, the fixture
  `firmware/sim/fixtures/ble-frames.txt`, `net_ble.cpp`, `Protocol.swift`, and bumping `fw_rev`.
- **tmux.** The user runs Claude in panes of their default tmux server (tmux 3.7c). Pane lookup works
  on the user's tmux, not on a private server.
- **The cube already has** a buzzer `Attention` melody slot (`melody.h`), screen sleep
  (`idle_sleep.h`), an editor/pairing/nav screen priority in `main.cpp`, and `GestureTracker` with
  drag mode; the alert and the scrolling grid build on those.

## Decided with the user

| Question | Decision |
|---|---|
| Use agents-deck as a separate daemon, or merge? | Merge: rewrite the bridge in Go and put agents-deck's Claude backend inside it. |
| Copy or fork agents-deck? | Copy into this repo, keeping only the Claude part. |
| Transports | As the bridge today: WiFi polling and BLE via the Mac helper. |
| What the cube shows | All open agents, on one grid card (mockup B). |
| Tap a waiting tile | The prompt with every option as a button (mockup A). |
| How the bridge learns about prompts | Global hooks in `~/.claude/settings.json`. |
| Where answers may come from | BLE only. |
| When an agent needs you | Wake the screen, jump to the prompt, beep. |
| Licence | Private repo; copy the code. |
| Answering while the Mac is locked | No: buttons greyed (`unlock Mac first`) and the Mac helper refuses answers while locked. The unlock prompt outranks the alert. |
| Agent details over WiFi | Grid only; prompt text, options and last prompts only on the loopback (BLE) payload. |

## Context worth knowing

The user's global `~/.claude/settings.json` already has hooks: a `PreToolUse` hook that auto-allows
every tool when the session's cwd is under `~/code`, a `Stop` hook (`notify-finished.py`) and two
`Notification` hooks (macOS notifications). The installer must leave all of them untouched. Because of
the auto-allow, permission prompts rarely appear for sessions under `~/code`; in practice the cube will
mostly answer `AskUserQuestion` questions and prompts from sessions elsewhere, so those prompt kinds
deserve the most attention in parsing and acceptance tests.

## My calls (not discussed; easy to change)

- The Agents card is **left out of the deck when no agent is open**, rather than showing "No agents".
- **No hats** on the critters in the first version (the folder name tells agents apart; hats add art
  and payload bytes). The builder may add agents-deck's automatic per-folder hats later.
- One beep per new prompt, never repeated while it stays unanswered.
- The alert fires even for an agent whose pane the user is looking at. Holding it back for the focused
  pane would need front-pane detection and a payload flag; it can be added later if the beeps annoy.
- A tap on a tile that is not waiting does nothing.
- Hooks are installed and removed by a bridge subcommand (e.g. `bridge hooks install|uninstall`),
  called by `bridge/agent.sh install|uninstall` and `mac-helper/install.sh install|uninstall`.
- One Go module dependency is allowed: `golang.org/x/text`, for the NFKD accent stripping that the
  Node bridge did with `normalize('NFKD')` (Go's standard library has no Unicode normalisation, and
  folder names can be Vietnamese).
- The bridge keeps its port (8787), endpoints and LaunchAgent label, so a cube and an installed app
  need no reconfiguration.

## Left to the builder

- **Package layout** of the Go bridge and which agents-deck files are copied whole, trimmed or
  rewritten. Expected sources: `internal/claude` (hooks, live, transcript, pane, sessions), `model`,
  `store`, `tmux`, `paths`, `pricing` (merge with the bridge's pricing table), and the Claude-only parts
  of `daemon` (hook intake, answer, live-session adoption, subagents). Strip everything that refers to
  the excluded harnesses.
- **Hook transport**: a `bridge hook` subcommand talking to the bridge over a 0600 unix socket (as
  agents-deck does) or over loopback HTTP; either way, fail open and fast.
- **Session → pane mapping** details: refreshing it, sessions that move between panes, panes in
  sessions created after the bridge started.
- **Payload shape** for the grid and prompts. A sketch: one card with an `a` array of tiles `{id,
  name, line, kind, pose, ctx}` sorted by the bridge, and for waiting agents a prompt `{agent,
  prompt id, title, detail, context, options[{key, label}]}`. Field names, limits and how many
  prompts travel at once (all waiting ones vs. only the longest-waiting) are the builder's, within the
  2048-byte BLE budget and the firmware's fixed buffers. The LAN variant omits the prompt fields
  (goal 7a), and the firmware must tell "no prompt text because WiFi" from "no prompt text because
  kind `input`".
- **The Control message** for an answer (next free opcode is `09`; it must carry agent id, prompt id
  and key), and the Mac helper's loopback call to the bridge, including what happens on a refused
  (stale) answer: the cube simply sees the next payload.
- **Critter port**: which poses map to which agent states and tools, and how the 18×13-cell sprite
  scales on a ~100 px tile; agents-deck's `firmware/devices/ws169-panel/src/critter.c` (LVGL) and
  `t4s3-panel` are the references.
- **Scrolling** inside the grid card versus the deck's horizontal swipes (vertical drag scrolls the
  grid; horizontal still changes card), using `GestureTracker`'s drag mode as the Volume card does.
- **Where the alert lives** in `main.cpp`'s screen priority, as a pure, host-tested state machine
  (like `unlock_prompt.h`), plus a `make bench` scenario if it touches sleep or the CPU policy.
- **Order of work**, provided the Node bridge stays usable until the Go bridge reaches parity.
- **Sim and shots**: a saved payload with agents for `cube-shot`, `@agents`-style special shots for
  the prompt and greyed states, and a `CUBE_*` sim variable for a prompt arriving.
- Documentation: root `CLAUDE.md`, `bridge/CLAUDE.md` (rewritten for Go), `firmware/CLAUDE.md`,
  README, `docs/ble-protocol.md`, and an acceptance checklist for the parts only hardware can confirm
  (beep, wake, a real tmux answer from the cube).
