# Navigation: hardware acceptance checklist

Host tests, the simulator and the bench pass, but navigation has not run on a real cube with a real
iPhone. Work through this with the board, the Mac helper and the iPhone app. Tick an item only when
you observed it. Logs: `pio device monitor` (cube; look for `[nav]` and `[ble]`).

## Two links at once

- [ ] Mac and iPhone both bonded and connected: the deck keeps updating from the Mac while the iPhone streams a ride; neither link drops the other.
- [ ] Start a ride: the map takes the screen within about 1 s, at full brightness, whatever the Display panel's brightness is. `[nav] ride started` is logged.
- [ ] Swipes and taps do nothing while the map is up; the screen never sleeps during the ride.
- [ ] End the ride in the app: the deck returns at once, brightness goes back to its setting, auto-advance does not fire immediately.
- [ ] Open the display panel, then start a ride: the panel closes without saving.
- [ ] Mac locked with "Unlock Mac with cube" on, then a ride: the map stays up (no unlock prompt over it).
- [ ] Turn the phone's Bluetooth off mid-ride: `NO PHONE` shows after about 3 s and the deck returns about 20 s after the last frame.

## Pairing window

- [ ] Portal "Pair new device" and reboot: "PAIRING OPEN" shows for 2 minutes, a new device can pair beside the bonded ones, the passkey screen takes over when it pairs, and the deck returns when the window ends.
- [ ] With the window closed, a third device cannot pair (see `docs/ble-acceptance.md`).
- [ ] Portal "Forget iPhone" leaves the Mac bonded and the Volume card in the deck; "Forget Mac" leaves the iPhone.
- [ ] iPhone bonded and no Mac: no Volume card, WiFi fills the deck when it is configured.

## Readability and power

- [ ] The map is readable in direct sun at full brightness (street classes, route, rider arrow, turn banner).
- [ ] 8 h battery run at full brightness with a ride streaming: record the hours and the measured mA. The bench models about 58 mA (MCU only, 240 MHz held, no radio or backlight LED), so expect more.
- [ ] A busy map (many streets) still redraws at 5 frames a second with no visible lag; the cube does not get warm.

## Mount

- [ ] On a vibrating mount (bike or a shaker) the rotation does not flip during a ride; after the ride ends it follows the cube again.
- [ ] Rotated 180 before the ride: the map stays the way the cube was standing.

## Dead zones

- [ ] Tunnel or no signal on the phone: `NO PHONE` appears after 3 s, the map comes back on the next frame without a flicker of the deck, and the ride survives a gap shorter than 20 s.
- [ ] A gap longer than 20 s ends the ride and shows the deck; the next frame starts a new one.
- [ ] Arrived: the arrival screen shows for about 8 s, then the deck. A stray Arrived frame with no ride does not take the screen.
