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
CUBE_AGENTS=1 ./cube-bridge             # track Claude Code agents (off by default; config key `agents`)
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

- Only with `agents` on (`CUBE_AGENTS`, config `agents`). Off: no tracker, `/hook` and `/api/answer` answer 404, the payload is unchanged. `hooks install` does not depend on the flag.
- `POST /hook` takes a Claude Code hook event, `POST /api/answer` takes `{"agent","prompt","key"}` and answers 204 / 400 / 404 / 405 / 409 (not in tmux, or the prompt moved on) / 500. Both go through the one loopback guard (loopback remote, `Host` `127.0.0.1:<port>` or `localhost:<port>`, no `Origin`) and need `X-Cube-Client: 1`; a request that fails gets 404. `GET /api/status` uses the same guard to pick the full payload (with `q`) over the LAN one. Both variants are at most 2048 bytes (`cards.MaxPayloadBytes`).
- Testing seams: `agents.Runner` (`func(ctx, name, args...) ([]byte, error)`) stands in for every external command (tmux, ps, lsof, the login shell), and `agents.Discoverer` (`Sessions(ctx)`) for finding running sessions; tests must never run real `claude`, `tmux`, `ps` or `lsof`. Hook installation takes a settings path so tests use temp files; never point it at the real `~/.claude/settings.json`.
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
  - Only printable ASCII is kept. Typographic characters are mapped to ASCII look-alikes, accents are stripped, and everything else is dropped. The device fonts have no glyphs past 0x7E, and a truncated UTF-8 sequence would draw as garbage.
  - Each field is cut to its buffer size in `firmware/src/payload.h`, minus 1 for the NUL (the `Fit` calls in `fitCard`, `cards.go`).

  A new string field needs an entry in `fitCard` as well as in `payload.h`.
- `tone()` sets the 60/85 thresholds. They must match the firmware `NOTCHES`, `preview.html` and the Mac helper's `MenuModel`.
- `windowView` treats a window whose `resets_at` has already passed as lapsed. It drops that window's percentage, drawing an empty track instead of the old full ring, and shows `reset` / `idle`.
- `until()` (`format.go`) loses precision as the horizon grows ("47m", "4h 05m", "16h", "2d 3h"). All ring cards share one font size, set by the longest `v`, so one long value shrinks every ring.
- A failed limits read is shortened to one caption word by `shortReason`. The full message goes in `s2`, which the firmware does not draw on a dual card; only the preview shows it.

## Other notes

- `internal/pricing` matches model ids by the longest prefix, and only on a `-` boundary: `claude-opus-5-5` is not priced as `claude-opus-5`, and dated ids resolve to their base model. An unknown model falls back to Sonnet-tier rates, so new models need an entry. Cache writes cost 1.25x (5 min) or 2x (1 h) the input rate, and cache reads `read`x (0.1 unless set). Fast mode is used only when the model has a `fast` entry. US-only inference adds 1.1x on top of everything.
- `/api/status` and `/` are served without authentication, on every interface by default. Nothing secret may go into the payload. The OAuth token, Gmail app password and admin key stay on the host, and the cube only ever sees rendered strings.
- `agent.sh` runs the compiled `cube-bridge` as a LaunchAgent, not a LaunchDaemon: a daemon runs outside the login session and cannot read the login Keychain. Don't install it alongside `mac-helper/install.sh`, because the two would fight over the port. The Mac helper adopts any bridge already serving the port.
- The card builder is golden-tested: `internal/cards/cards_test.go::TestGolden` replays `testdata/cases-utc.json` and `cases-hcm.json` (two time zones) and compares `Build`'s payload with each case's `want`. The JSON was generated once from the Node bridge (`testdata/gen.mjs`, kept for the record; it no longer runs) and is now the reference. To add a case, add an entry `{name, now, events, opts, want}` to both files by hand, with `want` written out from what the layout must produce, then run `go test ./internal/cards`.
