# Agent detail screen and hat picker on the cube

## Why

On the cube's Agents card only a waiting (orange) tile does anything when tapped. The user wants what
agents-deck's panel has: tap any agent to see what it is doing, and pick the hat its folder wears
(agents-deck: "Tap the critter on an agent's card to pick another shape or colour for that folder, or
go back to automatic"; `docs/features.md`, `docs/images/panel-detail-v2.png`).

## Decided by the user

- The full detail, like agents-deck's card: title, status pill, doing now, context with numbers, the
  last prompt, model · folder · branch. The cube tells the Mac which agent is open and the Mac asks the
  bridge for that one; the last prompt goes over Bluetooth only, never in the LAN payload.
- Hats are picked on the cube by tapping the critter on the detail screen, stored per folder in the
  bridge (every session in that folder follows), with a way back to automatic.

## Goals

- A tap on a tile that is not waiting opens its **detail screen**. A tap on a waiting tile still opens
  the existing prompt screen (answers unchanged). If the agent of an open detail starts waiting, its
  prompt screen takes over (the alert already does this).
- The detail screen, in agents-deck's look (palette and fonts of the theme): back, the agent's critter
  in its hat and its title, a status pill (`running · 38m`, `needs you · 2m`, `idle · 5m`, ...; accent
  when waiting or hungry, ink otherwise), "doing now" (the verb, and over Bluetooth the tool's target,
  e.g. `edit · src/auth/session.ts`), the error text when errored, subagents count, "context" with
  `41%` and `410k / 1m` and a bar, "last prompt" (up to 4 lines), and a dim line `model · ~/folder ·
  branch`. Until the detail arrives it shows what the tile has (title, line, context %) and "loading".
  Over WiFi it says details need Bluetooth.
- A tap on the critter opens the **hat picker** for that agent's folder: the 13 shapes plus "none" and
  "auto" drawn as small critters wearing them, a row of the 8 colours, the current choice outlined;
  a tap applies at once (shown on the cube immediately, confirmed by the next payload). Back returns
  to the detail screen.
- Hat overrides live in the bridge, keyed by the cleaned folder path `HatOf` already hashes, persisted
  to a small JSON file so they survive restarts; `h` on every tile in that folder follows (`auto`
  removes the override, `none` means no hat).

## Wire changes

- **BLE Control (cube -> Mac), fw_rev 11:**
  - `0A <la> <agent>`: the detail screen for that agent is open; `la` = 0 means closed. Sent when it
    opens or closes, and again every 5 s while open and the payload's detail is missing or for
    another agent (the Mac forgets it on reconnect).
  - `0B <la> <agent> <hat>`: set the hat of that agent's folder; `hat` 0..103 = shape x 8 + colour,
    `0xFE` = none, `0xFF` = automatic.
  Pure encoders, host-tested, pinned in `docs/ble-protocol.md` (and the shared fixture where control
  messages are pinned); Swift `ControlMessage` parses both.
- **Mac app:** keeps the open agent (cleared on disconnect and on `0A` with `la` 0), asks
  `/api/status?detail=<id>` while one is open, polls every 1 s while a detail is open (as while an
  agent waits), and forces a fetch when it changes. `0B` becomes one `POST /api/hat`
  `{"agent","hat"}` (`hat` an int 0..103, `-1` none, or `null` auto) with `X-Cube-Client: 1`, sent
  once, outcome logged, like answers.
- **Bridge:**
  - `GET /api/status?detail=<id>`: only for a request that passes the loopback guard, the Agents card
    gets `d` for that agent (absent otherwise, and never in the LAN variant). `d` is pre-formatted:
    `{i, t, s, w, c, p?, m?, e?, n?, ha}` = id, title, status pill, doing now, context numbers, last
    prompt, model · folder · branch, error, subagents ("3 subagents"), `ha` 1 = the hat is automatic.
    Per-field maxima in the const block; the 2048-byte budget trims `d.p` first, then drops `d`, before
    anything else the full variant carries.
  - `POST /api/hat`: guard plus `X-Cube-Client: 1`; 204, 400 (bad body), 404 (unknown agent or agents
    off), 405. Stores or removes the override for the agent's folder and republishes.
  - `HatOf` gains the overrides: an override beats `Auto`.

## Non-goals

- Focus terminal, interrupt, stop, resume (agents-deck's card buttons).
- Tokens, cost and turns (the bridge does not track them per agent).
- The preview page mirrors neither new screen (it cannot open a detail).
- Choosing a hat from the Mac app.

## Anti-goals

- The last prompt or a tool's target in the LAN payload.
- A tap that changes a hat or opens a screen the user could not see: hit-testing uses the same layout
  numbers as drawing (as the grid and prompt do), and the picker only acts on what was drawn.
- Waking the screen or the radio more: detail polling is the Mac's; the cube sends `0A` only on open,
  close and the 5 s retry while open; the new screens keep the theme's animation pacing.

## Left to the builder

Exact layout numbers, field maxima, file names, where the hat file lives (a per-user config dir),
how the cube orders the picker, and test shapes, within the repo's existing patterns (pure headers
host-tested, the three renderers' rules, docs updated with the code).
