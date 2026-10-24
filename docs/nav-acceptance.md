# Navigation: hardware acceptance checklist

Host tests, the simulator and the bench pass, but navigation has not run on a real cube with a real
iPhone. Work through this with the board, the Mac helper and the iPhone app. Tick an item only when
you observed it. Logs: `pio device monitor` (cube; look for `[nav]` and `[ble]`).

## Two links at once

- [ ] Mac and iPhone both bonded and connected: the deck keeps updating from the Mac while the iPhone streams a ride; neither link drops the other.
- [ ] Start a ride: the map takes the screen within about 1 s, at full brightness, whatever the Display panel's brightness is. `[nav] ride started` is logged.
- [ ] Swipes and taps do nothing while the map is up; the screen never sleeps during the ride.
- [ ] With auto-advance on (e.g. 10 s), a long ride does not change cards under the map; when the ride ends the deck is on the card it was on.
- [ ] End the ride in the app: the deck returns at once, brightness goes back to its setting, auto-advance does not fire immediately.
- [ ] Open the display panel, then start a ride: the panel closes without saving.
- [ ] Mac locked with "Unlock Mac with cube" on, then a ride: the map stays up (no unlock prompt over it).
- [ ] Turn the phone's Bluetooth off mid-ride: `NO PHONE` shows after about 3 s and the deck returns about 20 s after the last frame.

## Pairing window

- [ ] While "PAIRING OPEN" shows, swipes and taps do nothing to the deck (no editor, no card change, no mute); a tap dismisses the message for the rest of the window.

- [ ] Portal "Pair new device" and reboot: "PAIRING OPEN" shows for 2 minutes, a new device can pair beside the bonded ones, the passkey screen takes over when it pairs, and the deck returns when the window ends.
- [ ] With the window closed, a third device cannot pair (see `docs/ble-acceptance.md`).
- [ ] Portal "Forget iPhone" leaves the Mac bonded and the Volume card in the deck; "Forget Mac" leaves the iPhone.
- [ ] iPhone bonded and no Mac: no Volume card, WiFi fills the deck when it is configured.
- [ ] Phone-only cube (iPhone bonded, no Mac) booted away from its WiFi: no `CONNECTING` wait and no setup portal; the deck (or "No data yet") shows within a few seconds and a ride started from the app takes the screen.
- [ ] A pairing attempt from a third device outside the window shows no passkey (serial: `[ble] pairing on link N would be refused: no code shown`), and a passkey already on screen for a legitimate pairing is not blanked when the other bonded device reconnects.
- [ ] With the Mac and the iPhone both connected and the window closed, the cube stops advertising (a scanner no longer sees **Claude Cube**); it advertises again when either one leaves.

## Radio during a ride

- [ ] Mac away (out of range or Bluetooth off) and WiFi configured, then a 10 min ride: WiFi never comes up during the ride (no `[net]` lines, no OTA start). The map keeps its 5 frames a second, and WiFi comes back as usual after the ride ends.
- [ ] While riding, serial prints `[nav] compose N ms (max M)` every 10 s. Record N and M on a busy city map: N is the time one map frame takes to compose and send; M must stay under 200 ms (the frame interval), or frames are being skipped.

## Readability and power

- [ ] The map is readable in direct sun at full brightness (street classes, route, rider arrow, turn banner).
- [ ] On battery below 15%: a red `NN%` shows at the banner's top right (below `NO PHONE` when that is up), fully inside the screen's rounded corner.
- [ ] 8 h battery run at full brightness with a ride streaming: record the hours and the measured mA. The bench models about 58 mA (MCU only, 240 MHz held, no radio or backlight LED), so expect more.
- [ ] A busy map (many streets) still redraws at 5 frames a second with no visible lag; the cube does not get warm.

## Mount

- [ ] On a vibrating mount (bike or a shaker) the rotation does not flip during a ride; after the ride ends it follows the cube again.
- [ ] Rotated 180 before the ride: the map stays the way the cube was standing. Rotation is frozen at ride start, so mount the cube the way it will ride before starting navigation; turning it over mid-ride leaves the map upside down until the ride ends.

## Dead zones

- [ ] Tunnel or weak GPS: the phone keeps sending frames, so the rider arrow turns grey (weak GPS) and the map stays; `NO PHONE` does not appear.
- [ ] Compass with the phone in a pocket: after ~5 s of riding, stop at a light; the arrow stays white and the map points the way the bike faces. Turn the bike while stopped: the map turns with it. At the very start of a ride (or after 2 min stopped) a stop shows a grey arrow instead.
- [ ] Bluetooth gap (phone's Bluetooth toggled off and on, or the phone moved out of range): `NO PHONE` appears after 3 s, the map comes back on the next frame without a flicker of the deck, and the ride survives a gap shorter than 20 s.
- [ ] A gap longer than 20 s ends the ride and shows the deck; the next frame starts a new one.
- [ ] Arrived: the arrival screen shows for about 8 s, then the deck. A stray Arrived frame with no ride does not take the screen.
