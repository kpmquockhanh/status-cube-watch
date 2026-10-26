# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

**Upgrading from the Node bridge:** run `./agent.sh install` (or `./agent.sh restart`) once after pulling: an installed LaunchAgent still runs the deleted `server.mjs` and would respawn-loop at the next login (`./agent.sh status` warns). The Mac app keeps its bundled Node copy until `../mac-helper/install.sh install` is run again, which switches it to the binary.

Scope: `bridge/`. The repo-root `CLAUDE.md` already covers the data flow, config precedence and the payload contract. This file covers working inside the bridge: Go ≥ 1.24, one module dependency (`golang.org/x/text`, pinned at v0.34.0 so the module stays on Go 1.24), built to a single binary `cube-bridge`. `go test ./...` is the test suite.

## Commands

```sh
go test ./... && go vet ./...            # the suite; run both before committing
go build -o cube-bridge ./cmd/cube-bridge && ./cube-bridge   # serves on :8787 (`config.json` is read from the working directory, so run from `bridge/` or pass `-config <path>`); CUBE_PORT=8788 if a LaunchAgent already holds 8787 (a busy port exits 1; SIGINT/SIGTERM stop it with exit 0)
curl -s localhost:8787/api/status        # the payload the cube gets
curl -s localhost:8787/health            # always 200 while serving; {ok, error, source, lastError, limitsError, lastOk}
./cube-bridge hooks install|uninstall [--port N] [--settings PATH]   # add/remove the bridge's own Claude Code hooks (default ~/.claude/settings.json; use --settings with a temp file when trying it)
echo '{"hook_event_name":"Stop","session_id":"x"}' | ./cube-bridge hook --port 8787   # what a hook runs: POST stdin to /hook, print nothing, exit 0
CUBE_AGENTS=0 ./cube-bridge   # agent tracking is on by default; this turns it off
./agent.sh restart                       # reload the LaunchAgent copy (label com.claude-status-cube.bridge, logs in ~/Library/Logs/claude-status-cube/)
```

There is no hot reload: the code is compiled in, so after editing it, rebuild and restart the bridge. Depending on how it was started, that means Ctrl-C and rerun, `go build -o cube-bridge ./cmd/cube-bridge && ./agent.sh restart` (`agent.sh restart` also migrates an old Node-era LaunchAgent to the binary), or, for the Mac helper, `../mac-helper/install.sh install`: the app runs its own bundled copy of the binary, so **Restart bridge** alone reloads the old code (or launch the app with `CUBE_BRIDGE_DIR` pointing at this directory, where `cube-bridge` must be built). That copy gets its settings from the app ("Bridge settings…") as env vars, which win over any `config.json`. `preview.html` is re-read from the working directory on every request when the file is there (the binary embeds a copy, `assets.go`, for when it is not), so a browser reload is enough for it when you run from `bridge/`.

To try a card layout without a network, a Keychain or a running server, call `cards.Build(events, cards.Opts{Limits: &limits.Limits{...}, Mail: ...}, now)` from a Go test or a throwaway `main` inside this module (`internal/` packages import only from here) and print `json.Marshal` of the result; the cases in `internal/cards/testdata/cases-utc.json` show the shapes (`limits.Window` has a `*float64` `Utilization` and an RFC 3339 `ResetsAt`). To see what the real firmware renderer draws, feed the JSON to the simulator's frame grab:

```sh
./cube-bridge & sleep 2; curl -s localhost:8787/api/status > /tmp/p.json; kill %1
../firmware/sim/build/cube-shot /tmp/p /tmp/p.json   # -> /tmp/p-0.png
```

Starting the real server reads the Claude Code token through `security find-generic-password`, which can bring up a Keychain prompt.

The unread-mail badge is configured with `gmail.user` / `gmail.appPassword` in `config.json`, or `CUBE_GMAIL_USER` / `CUBE_GMAIL_PASSWORD`. It needs a Gmail app password. When neither is set, mail is `null` and the badge is left out.

## `internal/server`

- It starts listening before the first refresh, because the first usage read can take seconds (Keychain, network). Until that refresh lands, it serves a `STARTING` payload: empty rings with the caption "starting".
- The refresh loop is a chain (`loop`: refresh, then wait `RefreshMs`), not a ticker. The next refresh is scheduled only after the current one finishes, so slow Admin API or IMAP calls can never overlap. Each refresh replaces the current payload (an `atomic.Pointer`), and `/api/status` serializes it on each request. `server.Deps` carries the limits and mail readers, the `Source`, a clock, the preview loader and the `Log` / `ErrLog` loggers, so tests swap them for stubs.
- Each refresh makes three reads, and none of them can take the others down:
  - **Limits and mail** are fetched in parallel. Each returns its own `error` field and never fails the refresh; a panic in either is re-raised on the refresh goroutine and caught by `safeRefresh`, which logs it and leaves the payload as it was.
  - **The source** (`Source.Refresh`) runs only when `extraCards` is on. If it returns an error, a `BRIDGE ERROR` text card is put first, the source's last `events` are kept, and `/health` reports `lastError`.
- Every response has an explicit `Content-Length` (never chunked), because the cube parses it with a very small HTTP client. Every request goes through `Handler()`, which recovers from a panic in a handler (answering 500 if nothing was written yet) so one bad request cannot end the process.
- When `host` is pinned to a LAN address, the server also opens a second listener on `127.0.0.1` for the Mac helper. Failing to open that one is only a warning.

## `internal/agents` and the agent endpoints

- Only with `agents` on (on by default; `CUBE_AGENTS`, config `agents`; the env var wins both ways). Off: no tracker, `/hook`, `/api/answer` and `/api/hat` answer 404, the payload is unchanged. `hooks install` does not depend on the flag.
- `GET /api/changes` (same guard and `X-Cube-Client: 1`; 404 otherwise or with agents off, 405 for anything but GET) waits until the next agents rebuild (`watchAgents`, after the debounce) or hat set and answers 204, or 304 after `changesWait` (8 s, inside the server's 10 s `WriteTimeout`); `notifyChanged` closes and replaces one channel, so every waiting request wakes. The Mac app long-polls it so status changes reach the cube at once.
- `POST /hook` takes a Claude Code hook event, `POST /api/answer` takes `{"agent","prompt","key"}` and answers 204 / 400 / 404 / 405 / 409 (not in tmux, or the prompt moved on) / 500. Both go through the one loopback guard (loopback remote, `Host` `127.0.0.1:<port>` or `localhost:<port>`, no `Origin`) and need `X-Cube-Client: 1`; a request that fails gets 404. `GET /api/status` uses the same guard to pick the full payload (with `q`) over the LAN one. Both variants are at most 2048 bytes (`cards.MaxPayloadBytes`).
- The hook entry in `~/.claude/settings.json` is one entry shared by both installers: `./agent.sh install` and `../mac-helper/install.sh install` each replace it with their own binary path and port, and uninstalling either removes it (reinstall the other afterwards if it still runs). The port is baked into the command at install time, so after changing `port` / `CUBE_PORT`, install again. The hook command waits at most 300 ms for the bridge, except `Stop` and `SessionEnd` (`endHooks` in `cmd/cube-bridge/hook.go`): 3 s, posted again every 250 ms while the bridge does not answer (a restart), since a lost one leaves the tile running until the next turn. The tracker logs lifecycle hooks (`lifecycleHooks`, `TrackerDeps.Log`) as `agents: hook Stop: <agent id> is idle`, never the prompt.
- The web board (`board/`, `GET /board/`, data from `GET /api/board`) is the one browser page let past the guard: `boardRequest` accepts an `Origin` equal to `http://` + the already-pinned `Host` (plus `Sec-Fetch-Site: same-origin` when sent), so `/api/answer`, `/api/hat` and `/api/changes` (`boardClient`, still with `X-Cube-Client: 1`) and `/api/board` take requests from it; `/hook` and the full `/api/status` do not. Its files are re-read from `board/` in the working directory on every request (embedded copy otherwise, `assets.go`), so a reload shows an edit. To see it with prompts and every pose without real sessions, serve a fixture through a browser's request interception; never answer a real agent to test it.
- Two loopback-guard edge cases, both accepted: a request in absolute form (`GET http://127.0.0.1:8787/api/status`) takes its `Host` from the URI, which only a local non-browser client can send, so it passes like any local tool; and the bridge's own preview page (`/`), opened at `http://127.0.0.1:<port>/`, gets the full payload, because a same-origin `GET` carries no `Origin` header. Open the preview through a LAN address to see what the cube sees.
- Answers (`Tracker.Answer`): keys are one digit `1`-`9` (prompts never offer more than 9 options). The send runs with the request's context detached from cancellation; a send that may have typed (timeout, kill) keeps the prompt claimed so a retry gets 409, and only a send tmux refused up front (no server, no pane) gives it back. A menu (no numbers) is re-read just before typing and answered with arrows from the cursor's current row; a menu no longer on screen answers 409. tmux runs with `LC_CTYPE=en_US.UTF-8` (launchd sets no locale, and without one capture-pane turns `❯`/`│` into `_`).
- Tracker state: a session with no hook seen since the bridge started (`HookSeen` is in memory) takes its status from `claude agents --json` (`busy`, `blocked`) and the transcript's end (`transcriptTurn`); `busy` wins except after Claude's final reply (`end_turn`), since a background task (a Monitor, a background shell) keeps a session `busy` after its turn ended. A message's `content` may be a plain string (typed prompts, task notifications), not blocks. Any hook moves a `starting` agent to `idle`; a hook that sets a status or prompt replaces an on-screen menu prompt (the menu scan then leaves it alone); after a menu, an agent that sends hooks is `idle`, a hookless one `starting`. Prompt ids are `p<start time, base 36>-<n>`, unique across bridge restarts. Every tracker lock is released by `defer` in a small function, so a panic cannot wedge it. A transcript line longer than 16 MiB is skipped.
- Discovery: `claude agents --json` at most every 10 s, every 60 s once no agent was seen for 10 minutes; `claude` is looked up through `zsh -lc` (which reads `~/.zprofile`, not `~/.zshrc`, so under launchd it can miss a `~/.local/bin` that only `.zshrc` adds), else at the installers' locations (`claudeCandidates`: `~/.local/bin`, `~/.claude/local`, `/opt/homebrew/bin`, `/usr/local/bin`; only `NewDiscovery` sets them, so tests never find a real one); a failed lookup is retried every 5 minutes; the process scan cannot tell two sessions in one folder apart (each process takes the newest unclaimed transcript there), so a missed lookup shows up as a wrong title; falling back to the process scan, and recovering, are each logged once.
- Testing seams: `agents.Runner` (`func(ctx, name, args...) ([]byte, error)`) stands in for every external command (tmux, ps, lsof, the login shell), and `agents.Discoverer` (`Sessions(ctx)`) for finding running sessions; tests must never run real `claude`, `tmux`, `ps` or `lsof`. Hook installation takes a settings path so tests use temp files; never point it at the real `~/.claude/settings.json`.
- `POST /api/hat` `{"agent","hat"}` (hat 0..103, -1 = no hat, null = automatic; same guard and `X-Cube-Client: 1` as `/api/answer`; 204 then a republish, 400 / 404 (agents off, unknown agent) / 405 / 500 (the hats file could not be written)) stores a per-session override (keyed by `cards.HatKey`) in `internal/hatstore`: `hats.json` under `os.UserConfigDir()/claude-status-cube/` (`CUBE_HATS_FILE` overrides; keys are `filepath.Clean` of the tile's folder `cmp.Or(Cwd, Folder)`; written via temp file + rename; a corrupt file is warned about once and treated as empty). `cards.Opts.Hats` (the store's `Get`) makes the override beat `HatOf` in `tileOf`. Tests must use a temp path, never the real config dir.
- `GET /api/status?detail=<agent id>` (loopback guard only; a LAN request ignores it) adds `d` (`cards.AgentDetail`, `internal/cards/detail.go`) to the Agents card of the full payload: `cards.WithDetail` on a copy, so the shared payload is never touched. `publishLocked` stores the agent snapshot the full payload was built from (`fullAgents`) beside it, so the route finds the agent without locking the tracker; `d.ha` is 1 when `Hats.Get` has no override for the agent's folder. Field maxima are byte limits; `d.p` is cut to 60 bytes, then dropped, then `d`, so the full variant's `q` and tiles are never trimmed for it.
- Tile order (`sortAgents`): agents in tmux as their panes sit on screen (`Tmux.Panes` ranks every pane by session in tmux's order, window index, `pane_left`, `pane_top`; the tracker keeps it as `Agent.PaneOrder` and a change counts as a change, so a swap shows within the 3 s sync), then the rest by `byPriority` (status group, waiting longest first, folder, id). `byPriority` also picks the `MaxTiles` agents, the prompt `q` and, in `fitBudget`, keeps waiting tiles while it drops others. The board uses the same order.
- Tiles follow agents-deck's board (`internal/cards/agents.go`, `hats.go`): `HatOf` is its `Auto` hash (FNV-1a 32, shape `v % 13`, colour `(v >> 8) % 8`), applied here to the session's `HatKey` (`session:` + session id, else agent id) instead of agents-deck's folder, so every session wears its own hat, stable across restarts; the tile's `h` is shape x 8 + colour; the tile's `n` is the session's `Title` (`displayTitle`: a rename, else Claude's own title, else the folder), so they read apart; the status line is its meta line (`edit · 41% ctx`, `allow bash?`, `pick 1 of 3`, `hungry · feed me`, `done · 12m ago`, `starting...`, `exited`, at most `TileLineChars` characters, dropping the word `ctx` before cutting; the tool verb and error text are folded with `Fit` first, and only then joined with `sep`); `s` is the subagent badge (`""` for one subagent, `"×N"` for more); an agent that finished a turn and was seen less than `celebrateFor` (6 s) ago has pose `celebrate`. All of it is formatted here: the firmware draws the strings and the hat code as they come.
- Files adapted from agents-deck start with an `// Adapted from agents-deck ... @ 699bba8, <path>` line; `internal/agents/NOTICE.md` names the repository, commit and files.

## External reads fail soft

`internal/limits` (OAuth usage endpoint) and `internal/mail` (IMAP over plain TLS) follow the same pattern. A new external input should follow it too:
- They keep a cache (timestamp plus data, guarded by a mutex) for 60 s.
- On failure they return the last good value, tagged with `error`, instead of failing the refresh. Limits keep it for at most 10 minutes; after that they return `null` windows, which draw as empty tracks. Mail keeps its count marked `stale`.
- Retries are spaced out with `retryAt`. After a 429, the limits read waits for `Retry-After`, clamped to between 5 and 30 minutes.
- Every call has a timeout so the refresh loop never stalls: Keychain 3 s, usage endpoint 4 s, IMAP 8 s, Admin API 15 s per page.
- Mail shares a single in-flight request rather than opening a new login every 5 s.

Sources (`internal/sources`: `local.go`, `admin.go`) implement the `sources.Source` interface:
- `Label()`
- `Refresh(ctx)`, which returns `[]Event` and an error
- `Events()`, the last good events (also after a failed `Refresh`)
- `HasRequestCounts()`
- `Billed()`, nil unless admin: a `*Billed` (`ByDay map[YYYY-MM-DD]usd`, `Total`, the Admin cost report, which takes priority over estimates)

Each `sources.Event` is `{T, Model, Project, Session, Input, CacheRead, CacheWrite, Output, WebSearch, Cost}` (an empty string is a missing model/project/session), with `Cost` computed by `pricing.CostUSD` from the raw `message.usage` object. Events only feed `spendCards`.

## `internal/cards`

- `cards.Build(events, opts, now)` builds the deck. `usageCard` is always present. `spendCards` is added only when `extraCards` is on. A `BRIDGE ERROR` card is put first when the source failed. Every card then goes through `fitCard`.
- `fitCard` / `Fit` enforce two firmware limits:
  - Only printable ASCII is kept. Typographic characters are mapped to ASCII look-alikes, accents are stripped, and everything else is dropped. The two exceptions are added after folding, on the Agents card only: a tile line's separator `·` (U+00B7, `sep`) and the subagent badge's `×` (U+00D7); the firmware's `rm11` font carries both. The device fonts have no glyphs past 0x7E, and a truncated UTF-8 sequence would draw as garbage.
  - Each field is cut to its buffer size in `firmware/src/payload.h`, minus 1 for the NUL (the `Fit` calls in `fitCard`, `cards.go`).

  A new string field needs an entry in `fitCard` as well as in `payload.h`.
- `tone()` sets the 60/85 thresholds and the ring colour: `ink` below 60, `amber` at 60-84, `accent` from 85. The thresholds must match the firmware `NOTCHES`, `preview.html` and the Mac helper's `MenuModel.tint`.
- `windowView` treats a window whose `resets_at` has already passed as lapsed. It drops that window's percentage, drawing an empty track instead of the old full ring, and shows `reset` / `idle`.
- `until()` (`format.go`) loses precision as the horizon grows ("47m", "4h 05m", "16h", "2d 3h"). All ring cards share one font size, set by the longest `v`, so one long value shrinks every ring.
- A failed limits read is shortened to one caption word by `shortReason`. The full message goes in `s2`, which the firmware does not draw on a dual card; only the preview shows it.

## Other notes

- `internal/pricing` matches model ids by the longest prefix, and only on a `-` boundary: `claude-opus-5-5` is not priced as `claude-opus-5`, and dated ids resolve to their base model. An unknown model falls back to Sonnet-tier rates, so new models need an entry. Cache writes cost 1.25x (5 min) or 2x (1 h) the input rate, and cache reads `read`x (0.1 unless set). Fast mode is used only when the model has a `fast` entry. US-only inference adds 1.1x on top of everything.
- `/api/status` and `/` are served without authentication, on every interface by default. Nothing secret may go into the payload. The OAuth token, Gmail app password and admin key stay on the host, and the cube only ever sees rendered strings.
- `agent.sh` runs the compiled `cube-bridge` as a LaunchAgent, not a LaunchDaemon: a daemon runs outside the login session and cannot read the login Keychain. Don't install it alongside `mac-helper/install.sh`, because the two would fight over the port. The Mac helper adopts any bridge already serving the port.
- The card builder is golden-tested: `internal/cards/cards_test.go::TestGolden` replays `testdata/cases-utc.json` and `cases-hcm.json` (two time zones) and compares `Build`'s payload with each case's `want`. The JSON was generated once from the Node bridge (`testdata/gen.mjs`, kept for the record; it no longer runs) and is now the reference. To add a case, add an entry `{name, now, events, opts, want}` to both files by hand, with `want` written out from what the layout must produce, then run `go test ./internal/cards`.
