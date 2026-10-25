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
- [ ] back and a swipe right return to the Agents card.
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

## Detail and hats

- [ ] A tap on a working, idle or hungry tile opens its detail screen; within a couple of seconds over BLE its
      fields fill in from `d` (title, doing now, context, last prompt, meta); over WiFi it reads
      "details need Bluetooth".
- [ ] `< back` and a swipe right close it; the Mac's log shows the `0A 00` and the next payload carries no `d`.
- [ ] The tile starting to wait closes the detail (and the prompt alert opens as usual); the agent ending
      closes it too.
- [ ] Screen sleep closes the detail and its hat picker; after waking, the deck shows the grid.
- [ ] Reconnect: with the detail open, turn the Mac's Bluetooth off and on (or restart the Mac app); within
      about 5 s of the link coming back the cube sends `0A` again and the fields fill in.
- [ ] A tap on the critter opens the hat picker; the tile's current shape and colour (or `auto`) are outlined.
- [ ] Picking a shape, a colour, `none` and `auto` each changes the critter at once and, within a few seconds,
      on the grid tile too (and on no other agent, even one in the same folder); the bridge's `hats.json` holds the
      folder's override (removed again by `auto`).
- [ ] With the Mac app quit, a pick still shows for about 4 s and then the old hat returns; the serial log
      reads `[ble] hat dropped`.
- [ ] `< back` and a swipe right in the picker return to the detail screen, not the grid.

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
      minutes (a steady 4150 mV or more, or a rise of 20 mV in 120 s); unplugged and running on battery it
      switches to 2 frames/s once the 120 s window shows a fall of 10 mV, even above 4150 mV. Time both cases:
      (a) a half-charged cell unplugged, and (b) a full cell just unplugged (time from unplugging to the
      switch), and check that a cube with no cell fitted always counts as external.
- [ ] Battery drain: charge to full, unplug, leave the Agents card on screen with the backlight at its usual
      level and the screen sleep off, and log the cell once a minute for an hour with the logging build; do the
      same on the usage card. The simulator's bench cannot model panel or SPI current, so this is the only
      measure of what 2 frames/s costs. If the difference is more than a few percent per hour, lower
      `ANIM_BATTERY_MS`'s rate to one frame a second in `anim_pacer.h`.
- [ ] The frames do not keep the screen awake: with the Agents card showing, the screen still sleeps on schedule.

- [ ] With waiting agents left unanswered the screen still sleeps on schedule and the beep does not repeat.
