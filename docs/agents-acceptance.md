# Agents card: hardware acceptance checklist

The grid, the prompt screen, `AgentView`, the alert and Control `09` are host-tested and
benched, and the simulator draws every screen. Nothing here has run on a real cube yet.
Work through this after flashing. You need the bridge with agents on (the default), a Mac bonded
over Bluetooth with the Mac app running, and a few Claude Code sessions (some in tmux).

## Grid

- [ ] The Agents card shows after the usage card; tiles read clearly at arm's length.
- [ ] The critter is legible at 3 px cells for each pose (run, write, bash, wait, idle, err, exit).
- [ ] A waiting tile is orange; an errored one has a red critter; the unseen dot shows.
- [ ] With more than four tiles a vertical drag scrolls, and does not open the display panel.
      With four or fewer, or a drag that starts on the caption, swipe down opens the panel.
- [ ] Scrolling stays smooth (no tearing, no stutter against the BLE push).

## Prompt screen

- [ ] A tap on a waiting tile opens its prompt; a tap on any other tile does nothing.
- [ ] Title, detail box, context and buttons fit; a long prompt scrolls to its last button and BACK.
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

- [ ] With waiting agents left unanswered the screen still sleeps on schedule and the beep does not repeat.
