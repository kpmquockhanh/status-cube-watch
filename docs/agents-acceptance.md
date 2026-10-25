# Agents card: hardware acceptance checklist

The grid, the prompt screen, `AgentView`, the alert and Control `09` are host-tested and
benched, and the simulator draws every screen. Nothing here has run on a real cube yet.
Work through this after flashing. You need the bridge with agents on (the default), a Mac bonded
over Bluetooth with the Mac app running, and a few Claude Code sessions (some in tmux).

## Look

- [ ] The cube reads as agents-deck's board: near-black background, ink text, orange accent, square
      corners, lower-case labels (BLE / WIFI and the age label stay upper case). Compare with
      `./build/cube-shot build/shot @agents-hats` and `@agents-long`.
- [ ] The 11 px Share Tech Mono (meta lines, small labels) is legible at arm's length and at a desk
      distance, on the usage card, the Agents tiles, the prompt screen and the display panel; note any
      line that is not, with its text.
- [ ] Every hat shape shows on a tile with its colour and does not clip at the tile's top or sides; the
      same folder always gets the same hat.
- [ ] The rings are white below 60 %, yellow at 60-84 %, orange from 85 %; the navigation screens look as before.

## Grid

- [ ] The Agents card shows after the usage card; tiles read clearly at arm's length.
- [ ] The critter is legible at 4 px cells for each pose (run, write, bash, wait, idle, err, exit, hungry, celebrate); a finished, seen agent cheers for about 6 s and fades back to idle.
- [ ] A waiting tile is orange with a black critter; an errored one has an ink critter and an orange meta line; an unseen finished turn shows orange stripes (hungry).
- [ ] With more than four tiles a vertical drag scrolls, and does not open the display panel.
      With four or fewer, or a drag that starts on the caption, swipe down opens the panel.
- [ ] Scrolling stays smooth (no tearing, no stutter against the BLE push).

## Prompt screen

- [ ] A tap on a waiting tile opens its prompt; a tap on any other tile does nothing.
- [ ] Title, detail box, context and buttons fit; a long prompt scrolls to its last button and back.
- [ ] BACK and a swipe right return to the Agents card.
- [ ] Buttons are grey for about a second after the prompt appears, then live.
- [ ] An input prompt, an agent outside tmux and a locked Mac each show their reason line and no live buttons.
- [ ] Over WiFi only (Bluetooth off) the screen says "needs you / open on Bluetooth".

## Answering

- [ ] A tap on an option answers in the agent's tmux pane, the button lights and the line reads "sending...".
- [ ] The prompt closes or changes within a few seconds of the answer.
- [ ] Without the Mac app running, the line reads "no answer from Mac" after 8 s and a tap works again.
- [ ] A real answer from the cube is typed into the agent's tmux pane and Claude continues (check one permission prompt and one question).
- [ ] With the Mac locked, a tap does nothing: no key reaches the pane, and the app's log says the answer was ignored.
- [ ] A timed-out prompt that is shown again keeps its buttons grey for about a second before a tap works.
- [ ] Unticking "Track Claude Code agents" in Bridge settings and applying removes the AGENTS card; ticking it brings it back.
- [ ] A second tap on the same prompt never sends a second answer.
- [ ] A tap just as a newer prompt replaces the first never answers the newer one.

## Alert

- [ ] A new prompt beeps once at each SOUND level (LOW, MED, HIGH), and not at off.
- [ ] A new prompt wakes the cube from screen sleep, jumps to the Agents card and opens the prompt.
- [ ] The Mac's 5 s resend does not beep again; a second agent that starts waiting does.
- [ ] The alert is held while a ride runs, the display panel is open, a finger is down, a BLE passkey
      or PAIRING OPEN shows, or the unlock prompt covers the deck, and fires after it ends.
- [ ] An agent answered while the alert was held does not alert afterwards.

## Power

- [ ] Animation: the critters move about ten times a second on the wall charger and twice a second on battery,
      only on the Agents card or a prompt; on any other card there are no animation frames.
- [ ] The external-power guess, with the logging build (`pio run -e batlog -t upload`, then `pio device
      monitor -e batlog`): on the wall charger the cell reads as external (10 frames/s) within about two
      minutes; unplugged and running on battery it switches to 2 frames/s once the voltage has stopped rising
      and sunk under 4150 mV. Repeat with a full cell just unplugged: note how long it keeps animating at
      10 frames/s, and that a cube with no cell fitted always counts as external.
- [ ] Battery drain: charge to full, unplug, leave the Agents card on screen with the backlight at its usual
      level and the screen sleep off, and log the cell once a minute for an hour with the logging build; do the
      same on the usage card. The simulator's bench cannot model panel or SPI current, so this is the only
      measure of what 2 frames/s costs. If the difference is more than a few percent per hour, lower
      `ANIM_BATTERY_MS`'s rate to one frame a second in `anim_pacer.h`.
- [ ] The frames do not keep the screen awake: with the Agents card showing, the screen still sleeps on schedule.

- [ ] With waiting agents left unanswered the screen still sleeps on schedule and the beep does not repeat.
