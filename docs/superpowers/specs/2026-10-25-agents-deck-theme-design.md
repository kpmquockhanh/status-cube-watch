# The agents-deck look on the cube: design

Date: 2026-10-25. Status: draft for approval.

## Intent

The cube now shows Claude agents (plans 1–4 of `2026-10-24-go-bridge-agents-design.md`), but in this
repo's own style: Inter, soft orange `#FF8A5B`, rounded tiles, small still critters without hats.
The user wants it to look like agents-deck (`~/code/agents-deck`), the project the agent tracking was
copied from, and showed agents-deck's panel board as the target (`docs/images/panel-board-v2.png` in
that repo; the prompt is `panel-permission-v2.png`). They want the whole cube to share that look, not
only the Agents screens, and the critters to move the way agents-deck's do.

## Goals

1. **One theme for the whole cube**, agents-deck's:
   - palette (agents-deck `ws169-panel/src/ui.c` and web `app.css`): background `#0A0A0A`, panel
     `#141412`, ink `#E8E4DC`, dim `#7A776F`, line `#262622`, accent orange `#FF4D00`, empty slot
     `#0B0B0B`; the named colours teal `#4FB3A6`, blue `#6F8FD8`, yellow `#E8B94A`, green `#6DBE6A`,
     purple `#A68BD8`, pink `#E58BB0`, sky `#5EC4D6`.
   - monospace type: Roboto Mono (headings, names, big values; bold) and Share Tech Mono (small labels),
     the fonts agents-deck's web deck uses (both SIL OFL; the TTFs and OFL texts are in
     `agents-deck/backend/internal/web/static/fonts/`).
   - square-cornered panels with thin outlines, orange used only for attention.
   Applies to: the usage card, the Agents card, the prompt screen, the Volume card, the display
   settings panel, the unlock prompt, the pairing / "waiting for a Mac" / "PAIRING OPEN" / OTA / portal
   / "No data yet" screens, the top bar and page dots, `preview.html`, and the Mac app's menu tints.
2. **The Agents card looks like agents-deck's panel board**: dark tiles with a thin outline; a small
   square chip in the top corner in the tool's colour (Claude `#C98B6B`; black on a waiting tile); the
   critter at 4 px per cell (72×52 px) wearing its hat; the bold folder name; a small meta line (dim; orange
   for hungry/error; e.g. `edit · 41% ctx`); a thin context bar at the bottom. A waiting tile is solid
   orange with a black critter and black text; a hungry tile (finished a turn nobody looked at) has an
   orange border and sliding diagonal orange stripes; a finished/exited tile is grey. Where agents-deck's
   firmware (`ws169-panel/src/ui.c`) and its board image differ, **the image wins** (the user chose it).
3. **Hats, automatic per folder**, exactly agents-deck's rule (`backend/internal/hats/hats.go` `Auto`):
   FNV-1a 32 of the cleaned working folder; shape = `Shapes[v % 13]`, colour = `Colors[(v >> 8) % 8]`,
   shapes in agents-deck's order (hard hat, cap, beanie, helmet, wizard, beret, chef, crown,
   headphones, propeller, top hat, cowboy, bandana), colours teal, blue, yellow, green, purple, pink,
   sky, ink. The bridge computes it and sends it per tile; the firmware draws agents-deck's hat art
   (`ws169-panel/src/hats.c`, generated, and its drawing code in `critter.c`). No manual overrides.
4. **agents-deck's animations**: the critters' full motion (walk, bob, hop, blink, the props, the
   faded shapes that plan 3 drew solid) and the hungry tile's sliding stripes, redrawn 10 times a
   second **on USB power**, and **about 2 times a second on battery**. Only while the Agents card or
   the prompt screen is on screen and the screen is on. Animation frames never count as activity for
   screen sleep.
5. **The prompt screen looks like agents-deck's** (`panel-permission-v2.png`): the whole screen orange,
   the agent's small critter (with hat) and name at the top, the question in bold black, the command or
   file in a black box with off-white text, the context line under it, the options as stacked buttons
   (the first solid black with orange text, the rest outlined in black), a short footer line. All
   plan-3 states keep working (settling, sending…, no answer from Mac, not in tmux, unlock Mac first,
   answer at the terminal, open on Bluetooth, answer X first, too long for the cube), drawn in this
   style; greyed buttons stay readable.
6. **Usage rings in the new palette with two warning steps**: below 60% ink, 60–85% yellow `#E8B94A`,
   85% and over orange `#FF4D00` (decided: the user took the recommendation). The ring geometry,
   notches, animations and the legend rows stay; text and colours follow the theme.

## Non-goals

- The navigation map and its turn banner (built for daylight on a bike) keep their look.
- No change to what any screen shows or how it behaves (layout may shift to fit the new font and
  tiles, but content, gestures, timing rules, the alert and the answer rules stay as plans 1–4 left them).
- No hat overrides, no hats for anything but agents, no account colours (Claude only).
- No new settings in the portal / BLE / Mac for the theme or animation speed.

## Anti-goals

- Battery life on battery noticeably worse than today when the Agents card is not on screen; the
  animation cost must stop the moment the card/prompt is off screen or the screen sleeps.
- Animation that keeps the screen awake, or that makes the cube miss touches.
- Formatting creeping into the firmware: status words, meta lines (`edit · 41% ctx`), hats chosen from
  the folder — all decided in the bridge; the firmware maps a pose and a hat id to a drawing.
- Illegible text: the monospace fonts at small sizes must stay readable on the 240×280 panel.
- Breaking the shared contract: every renderer (`ui.cpp`, `preview.html`, `MenuModel.swift`, the
  Go card builder) updated together; payload stays ≤ 2048 bytes.

## Constraints

- The repo's rules hold (root `CLAUDE.md`, `firmware/CLAUDE.md`, `bridge/CLAUDE.md`, `mac-helper/CLAUDE.md`):
  pure logic in host-tested headers, `gnu++11` on the board, `make test`, `make bench`, `pio run`,
  `go test`, `swift test` keep passing; `make bench` gains/updates scenarios for the animation cost
  (on USB vs battery, card on vs off screen) and still enforces sleep.
- Fonts are generated by `firmware/tools/gen_fonts.py` (pinned Pillow); new font files and their OFL
  licence texts are committed; the frozen `b44/b36/b30` Inter fonts may be retired if nothing uses them.
  Fonts cover printable ASCII only (Vietnamese nav fonts stay as they are).
- Flash: fonts and hat art must fit (today 25% of 6.5 MB used).
- The payload change (a hat per tile) goes through `cards.Fit`/limits and the existing trim order;
  older firmware must not break on the new field.
- Provenance: art and code copied from agents-deck keep the "Adapted from agents-deck … @ 699bba8"
  first line, as the copied bridge code does.
- How the cube knows it is on external power: the board has no VBUS or charge-status pin (the
  ETA6098 charger does not report to the ESP32; `docs/ESP32-S3-Touch-LCD-1.69.md`), and the user
  powers it from a **wall charger** (no USB data host), so USB-CDC detection does not help. Rule
  (decided): **on external power** when no cell is fitted (today's `BAT_NO_CELL_MV`), or when the
  filtered cell voltage is at or above 4.15 V, or has risen by at least 20 mV over the last 2 minutes;
  otherwise **on battery**. Pure and host-tested in `battery_util.h`. A cell just unplugged at full
  charge reads "external" for a few minutes (accepted). The thresholds are checked on hardware with
  the existing `batlog` build (acceptance item).

## Decided with the user

| Question | Decision |
|---|---|
| Which look | agents-deck's panel board and prompt (the images in its `docs/images/`). |
| How far | The whole cube, not only the Agents screens. |
| Animation | agents-deck's full animation: full speed on USB, slowed on battery. |
| Ring warning colours | My recommendation: ink → yellow at 60% → orange at 85%. |
| How it is powered on USB | A wall charger, so "on USB" is judged from the battery voltage. |

## My calls (easy to change)

- 10 frames/s on USB (agents-deck's panel rate), ~2 frames/s on battery.
- Hats chosen exactly like agents-deck's `Auto`, so the same folder wears the same hat on both.
- The corner chip is always Claude's tan (`#C98B6B`), since only Claude is tracked.
- The nav screens keep their look.
- The Mac menu's tints follow the ring colours (ink / yellow / orange).

- **Existing screens keep their geometry and touch zones** (Volume card zones, display panel and
  editor layout, portal, unlock, pairing): only colours, fonts and corner styles change, and text is
  refitted to the new fonts. Only the Agents card and the prompt screen are re-laid out. Host-tested
  geometry (`volume_slider.h`, `editor_layout.h`) stays as it is.
- **The prompt keeps plan 3's scrolling column** (`PromptLayout`: title, detail box, context, reason
  line, buttons, BACK) and every plan-3 state; it is restyled to agents-deck's orange screen, not
  re-laid out to the image's fixed positions. Up to 6 options scroll as today.
- **The hat travels as one small integer per tile** (shape × 8 + colour, 0–103; absent = no hat),
  computed in the bridge; it is trimmed with the tile, so it never displaces prompt text. Firmware
  older than this ignores the field (ArduinoJson drops unknown keys) and draws no hat. The Mac
  helper's 2048-byte limit is unchanged.
- **Animation is a faithful port** of agents-deck's `critter.c` timing, poses, props and faded shapes
  (an alpha per shape) and of `hats.c`'s hat art, hand-ported to pure host-tested C++ as plan 3 did,
  not "similar motion".
- **Battery budget**: with the Agents card on screen on battery, the bench's modelled MCU current may
  rise at most 20% over the same card drawn still; the CPU stays at 80 MHz between frames on battery.
  If 2 frames/s cannot meet that, the battery rate drops to 1 frame/s. On USB, 240 MHz while
  animating is fine.
- **Sim shots**: every screen that has a shot today is re-rendered and looked at; new shots cover the
  hat set and the hungry stripes.
- The Mac menu's 5h tint uses the same steps (ink / yellow / orange at 60/85).

## Left to the builder

- Exact sizes and positions on 240×280 (agents-deck's panel is square), how many tiles fit per screen
  (2×2 as today, or what agents-deck's `ui.c` does), the font sizes to generate.
- How the payload carries the hat (field name, encoding) within the budget.
- How the hat art from `hats.c` is turned into the cube's pure, host-tested critter shapes.
- The CPU clock while animating (the bench decides), and how frame pacing interacts with
  `cpu_policy.h`.
- Sim shots for every restyled screen, and an acceptance checklist item for the look on hardware.
