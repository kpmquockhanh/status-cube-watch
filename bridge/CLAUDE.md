# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Scope: `bridge/`. The repo-root `CLAUDE.md` already covers the data flow, config precedence and the payload contract. This file covers working inside the bridge: Node ≥20 ESM, no npm dependencies, no build step and no tests.

## Commands

```sh
node server.mjs                          # serves on :8787; CUBE_PORT=8788 if a LaunchAgent already holds 8787 (a busy port exits 1)
curl -s localhost:8787/api/status        # the payload the cube gets
curl -s localhost:8787/health            # always 200 while serving; {ok, error, source, lastError, limitsError, lastOk}
./agent.sh restart                       # reload the LaunchAgent copy (label com.claude-status-cube.bridge, logs in ~/Library/Logs/claude-status-cube/)
```

There is no hot reload. `server.mjs` imports `cards.mjs` and the sources once, so after editing them, restart the bridge. Depending on how it was started, that means Ctrl-C, `./agent.sh restart`, or **Restart bridge** in the Mac helper's menu. `preview.html` is re-read from disk on every request, so a browser reload is enough for it.

To try a card layout without a network, a Keychain or a running server, call `buildPayload` directly. To see what the real firmware renderer draws, feed the result to the simulator's frame grab:

```sh
node --input-type=module -e "
import { buildPayload } from './cards.mjs';
const at = (h) => new Date(Date.now() + h * 3600e3).toISOString();
console.log(JSON.stringify(buildPayload([], { limits: { five_hour: { utilization: 72, resets_at: at(2.5) },
  seven_day: { utilization: 31, resets_at: at(80) } }, mail: { unread: 3, stale: false } })));
" > /tmp/p.json && ../firmware/sim/build/cube-shot /tmp/p /tmp/p.json   # -> /tmp/p-0.png
```

Starting the real server reads the Claude Code token through `security find-generic-password`, which can bring up a Keychain prompt.

The unread-mail badge is configured with `gmail.user` / `gmail.appPassword` in `config.json`, or `CUBE_GMAIL_USER` / `CUBE_GMAIL_PASSWORD`. It needs a Gmail app password. When neither is set, mail is `null` and the badge is left out.

## `server.mjs`

- It starts listening before the first refresh, because the first usage read can take seconds (Keychain, network). Until that refresh lands, it serves a `STARTING` payload: empty rings with the caption "starting".
- The refresh loop is a `setTimeout` chain, not `setInterval`. The next refresh is scheduled only after the current one finishes, so slow Admin API or IMAP calls can never overlap. Each refresh replaces the module-level `payload`, and `/api/status` serializes it on each request.
- Each refresh makes three reads, and none of them can take the others down:
  - **Limits and mail** are fetched in parallel. Each returns its own `error` field and never throws.
  - **The source** (`source.refresh()`) runs only when `extraCards` is on. If it throws, a `BRIDGE ERROR` text card is put first, the source's last `events` are kept, and `/health` reports `lastError`.
- Every response has an explicit `Content-Length` (never chunked), because the cube parses it with a very small HTTP client. Every request goes through `handle()`, which catches errors. A throw in an async handler would be an unhandled rejection and end the process.
- When `host` is pinned to a LAN address, the server also opens a second listener on `127.0.0.1` for the Mac helper. Failing to open that one is only a warning.

## External reads fail soft

`limits.mjs` (OAuth usage endpoint) and `sources/gmail.mjs` (IMAP over plain `node:tls`) follow the same pattern. A new external input should follow it too:
- They keep a module-level cache `{at, data}` for 60 s.
- On failure they return the last good value, tagged with `error`, instead of throwing. Limits keep it for at most 10 minutes; after that they return `null` windows, which draw as empty tracks. Mail keeps its count marked `stale`.
- Retries are spaced out with `retryAt`. After a 429, the limits read waits for `Retry-After`, clamped to between 5 and 30 minutes.
- Every call has a timeout so the refresh loop never stalls: Keychain 3 s, usage endpoint 4 s, IMAP 8 s, Admin API 15 s per page.
- Mail shares a single in-flight request rather than opening a new login every 5 s.

Sources (`sources/local.mjs`, `sources/admin.mjs`) expose:
- `label`
- `refresh()`, which resolves to `events[]`
- `events`
- optionally `hasRequestCounts` and `billed` (`{byDay: Map<'YYYY-MM-DD', usd>, total}`, the Admin cost report, which takes priority over estimates)

Each event is `{t, model, project, session, input, cacheRead, cacheWrite, output, webSearch, cost}`, with `cost` computed by `pricing.mjs::costUSD` from the raw `message.usage` object. Events only feed `spendCards`.

## `cards.mjs`

- `buildPayload(events, opts)` builds the deck. `usageCard` is always present. `spendCards` is added only when `extraCards` is on. A `BRIDGE ERROR` card is put first when the source failed. Every card then goes through `fitCard`.
- `fitCard` / `fit` enforce two firmware limits:
  - Only printable ASCII is kept. Typographic characters are mapped to ASCII look-alikes, accents are stripped, and everything else is dropped. The device fonts have no glyphs past 0x7E, and a truncated UTF-8 sequence would draw as garbage.
  - Each field is cut to its buffer size in `firmware/src/payload.h`, minus 1 for the NUL (`CARD_BYTES`, `ROW_BYTES`).

  A new string field needs an entry in those tables as well as in `payload.h`.
- `tone()` sets the 60/85 thresholds. They must match the firmware `NOTCHES`, `preview.html` and the Mac helper's `MenuModel`.
- `windowView` treats a window whose `resets_at` has already passed as lapsed. It drops that window's percentage, drawing an empty track instead of the old full ring, and shows `reset` / `idle`.
- `until()` loses precision as the horizon grows ("47m", "4h 05m", "16h", "2d 3h"). All ring cards share one font size, set by the longest `v`, so one long value shrinks every ring.
- A failed limits read is shortened to one caption word by `shortReason`. The full message goes in `s2`, which the firmware does not draw on a dual card; only the preview shows it.

## Other notes

- `pricing.mjs` matches model ids by the longest prefix, and only on a `-` boundary: `claude-opus-5-5` is not priced as `claude-opus-5`, and dated ids resolve to their base model. An unknown model falls back to Sonnet-tier rates, so new models need an entry. Cache writes cost 1.25x (5 min) or 2x (1 h) the input rate, and cache reads `read`x (0.1 unless set). Fast mode is used only when the model has a `fast` entry. US-only inference adds 1.1x on top of everything.
- `/api/status` and `/` are served without authentication, on every interface by default. Nothing secret may go into the payload. The OAuth token, Gmail app password and admin key stay on the host, and the cube only ever sees rendered strings.
- `agent.sh` installs a LaunchAgent, not a LaunchDaemon: a daemon runs outside the login session and cannot read the login Keychain. Don't install it alongside `mac-helper/install.sh`, because the two would fight over the port. The Mac helper adopts any bridge already serving the port.
