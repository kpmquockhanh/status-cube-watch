# Motorbike navigation on the cube: design

Date: 2026-10-22. Status: approved in conversation, not yet planned.

## Intent

The owner rides a motorbike in Vietnam and wants the cube mounted on the handlebar as a
navigation display. An iPhone app (new, ours) plans a motorbike route and follows the ride; the
cube shows a small live map: the route, the streets around it, and a cursor for the rider. The
phone stays in a pocket. One glance at speed must be enough.

Why our own app rather than Google Maps: Google Maps on iOS exposes no navigation data to other
apps, and Apple MapKit cannot route motorbikes (it routes cars, walking and cycling). In Vietnam
car routing avoids the alleys a motorbike uses and follows car-only rules, so routing comes from
a self-hosted GraphHopper with a motorbike profile on OpenStreetMap data.

## Goals

- Search a place in the iPhone app, see the route, tap Start, lock the phone, ride.
- While riding, the cube shows (heading-up, "style B" from the mockups):
  - the route as a thick line, nearby streets as thin grey lines, the rider as a white arrow in
    the lower third of the screen;
  - a top banner: the next turn as a large arrow and the distance to it, counting down
    (`150 m`, `1.2 km`);
  - a bottom line: the street turned onto and the time left (`Lê Lợi · 12 min`), with Vietnamese
    accents kept.
- The map zooms by itself: closer as a turn approaches, wider on long straights. The rider never
  touches the cube while riding.
- Leaving the route reroutes automatically ("Rerouting" in the banner meanwhile).
- Arrival shows an "Arrived" screen, then the cube returns to its normal deck.
- The Mac link keeps working as today. Mac and iPhone are connected to the cube at the same
  time; navigation takes over the screen while it runs and gives it back when it ends.

## Anti-goals

- **Losing the route when mobile signal drops.** This is the failure the owner named. Once a
  ride has started, following it must need no network at all (see Offline below).
- Anything the rider has to touch on the cube while moving (gloves, rain, vibration).
- Tiny text or detail that needs a second look. High contrast, readable in sun.
- Breaking or degrading anything the Mac link does today.

## Non-goals (first version)

- Routing on the phone itself (an offline routing engine and a full Vietnam map on the phone).
- Voice prompts, speed display, speed cameras, traffic, multi-stop routes.
- Car, bicycle or walking modes.
- Android.
- Rendering map pictures (Apple Maps snapshots) on the cube; the cube draws vectors.

## The three parts

All three live in this repo:

| Part | Folder | What |
|---|---|---|
| iPhone app | `ios-nav/` (name is my call) | search, route preview, ride tracking, pushes frames to the cube over BLE |
| Server | `nav-server/` | GraphHopper (motorbike) + a street-tile service on the `contabo` VPS |
| Cube | `firmware/` (existing) | second bonded device, Navigation screen, Vietnamese glyphs |

### 1. Server (`nav-server/`)

Decided:
- Runs on the owner's VPS `contabo` (ssh alias; Debian 13, x86_64, 4 cores, 7.8 GB RAM, a 4 GB
  swap file added 2026-10-22, 89 GB free disk, Docker 29). It already runs n8n under
  `/root/n8n` with an nginx container and certbot serving `n8n.devxdev.site`. The navigation
  stack must not disturb n8n.
- Public HTTPS at **`nav.devxdev.site`**. The name already resolves, to Cloudflare addresses
  (`104.21.23.63`, `172.67.209.117`), as `n8n.devxdev.site` does: the domain is behind
  Cloudflare's proxy, so certificate issuance and the origin setup must work with Cloudflare in
  front (check its SSL mode; the builder may need the owner to adjust the Cloudflare record).
  The existing nginx gets a second server block and its own Let's Encrypt certificate from the
  existing certbot (the n8n nginx config is a template at
  `/root/n8n/nginx/prod.conf.template`, mounted into the container). Whether to extend that
  container's config or run a separate reverse proxy is the builder's call, as long as n8n keeps
  working and only one process owns ports 80/443.
- Every request carries a secret key in a header; without it the server refuses. The app stores
  the key (entered once in its settings).
- **Routing:** GraphHopper (open source, self-hosted, Docker) built from the OpenStreetMap
  Vietnam extract (Geofabrik), with a motorbike profile, returning turn instructions with their
  turn-type codes, street names and the route geometry.
- **Streets:** a small service on the same server that returns the streets in an area as compact
  polylines (with a road class, so major roads can be drawn heavier), cut from the same extract,
  so map and routing always agree. Format and tiling are the builder's call; it must be cheap to
  fetch a corridor along a whole route in one go (see Offline).
- **Monthly refresh:** download the new extract, rebuild GraphHopper's graph and the street data
  in the background, switch over only when the build succeeded; the old data serves meanwhile.
- The setup (compose file, nginx snippet, refresh script, a smoke-test script) is in
  `nav-server/` and deployed from there. A README there says how.
- The owner allows the builder to deploy directly over `ssh contabo` (root): editing the n8n
  nginx template, issuing the certificate, and running the GraphHopper import on the box. Back
  up anything edited first, and check n8n still answers after each nginx change.

### 2. iPhone app (`ios-nav/`)

Decided:
- Swift / SwiftUI, deployment target iOS 17 (the owner's phone is an iPhone 12 Pro). Search uses Apple's place search (MapKit `MKLocalSearchCompleter` /
  `MKLocalSearch`), results as you type. MapKit is used for search and for the on-phone map
  only; **all routing comes from the server**.
- Flow: search, pick a place, see the route on the phone's map with distance and arrival time,
  tap Start. A short list of recent places for one-tap starts (my call).
- Start connects to the cube (pairing once with the cube's passkey, as the Mac does), then the
  phone can be locked: background location and background Bluetooth keep the ride going.
- The phone does all the work: follows progress along the route, decides the next turn and the
  distance to it, detects leaving the route, picks the zoom, projects everything into the cube's
  screen coordinates heading-up, and sends the cube frames that are ready to draw (my call: this
  keeps the firmware a "draws what it is sent" device, as the payload contract does for the
  Claude cards). Frame rate and encoding are the builder's call; it must look smooth at riding
  speed and fit BLE throughput.
- Off route with signal: request a new route, banner shows "Rerouting".
- Settings in the app: server URL (default `https://nav.devxdev.site`) and the key.
- **Distribution: free Apple account, rebuilt from Xcode about once a week** (the owner chose
  this over AltStore/SideStore and a paid account). A free-provisioned app stops launching 7 days
  after it was signed, so the app reads its own expiry (`embedded.mobileprovision`) and shows a
  clear warning on its main screen when fewer than 2 days are left (my call). `ios-nav/`'s README
  explains the weekly rebuild.
- Shared Swift code with `mac-helper/` (`CubeLinkCore`: frame encoding, BLE protocol constants)
  should be reused rather than copied where practical; how is the builder's call.

### 3. Cube (`firmware/`)

Decided:
- **Two bonds, both connected at once:** one Mac, one iPhone. Today `ble_auth.h` allows one bond
  and the cube stops advertising while connected; both change. The cube keeps advertising while
  one link is up, accepts up to two bonds, and knows which bond is which kind (the phone
  identifies itself on its first secured connection). Pairing a new device of a kind already
  bonded replaces that bond. "Forget" works per device (Mac / iPhone / both) in the portal.
- **"Pair new device"** must be reachable on the cube without the Mac (today a second device is
  refused while a bond exists). Where it lives (display panel, portal, a gesture) is the
  builder's call; the portal must have it.
- The Mac link is unchanged: payloads, Volume card, unlock, settings.
- **Navigation screen:** while the phone reports an active ride, the cube shows only the
  navigation screen, whatever card was up. Swipes and auto-advance do not leave it. When the
  ride ends (arrival, stopped on the phone, or the phone link drops for longer than a grace
  period the builder picks), the deck returns to the card that was showing.
- **Screen always on during navigation** (no screen sleep), brightness at full during the ride
  and restored afterwards (my call: handlebar, direct sun).
- **Mounted upright** (portrait, as on the desk). **Auto-rotate is frozen while navigating**
  (my call: handlebar vibration must not flip the screen); it resumes when the ride ends.
- **Power: the cube runs from its own 4000 mAh cell on the bike.** Navigation must fit a power
  budget: target at least 8 hours of navigating per charge at full brightness (my call; the
  owner did not give a ride length). Add a `make bench` navigation scenario to model it, keep
  redraws to what changed and cap the frame rate as needed. When the battery gets low mid-ride
  the navigation screen shows it (threshold is the builder's call); navigation keeps running.
- New BLE characteristic (or characteristics) for navigation frames, documented in
  `docs/ble-protocol.md` and pinned by `firmware/sim/fixtures/ble-frames.txt` like the rest of
  the protocol; `fw_rev` bumps (8). The frame layout is the builder's call.
- **Vietnamese street names with accents.** The fonts (`firmware/tools/gen_fonts.py`, Inter)
  cover printable ASCII only today; add the Vietnamese letters (precomposed Latin + Latin
  Extended Additional, U+1EA0..U+1EF9 and the Latin-1 / Latin Extended-A letters Vietnamese
  uses) at the sizes the navigation screen uses. Flash is not a concern: the app partition is
  6.5 MB and 1.5 MB is used. Strings sent to the cube are UTF-8.
- The turn arrow comes from GraphHopper's turn-type code, drawn by the cube from a fixed set
  (left, right, slight / sharp left and right, U-turn, roundabout with exit, keep left / right,
  straight, arrive).
- No touch input during navigation.

## Offline (the anti-goal, in detail)

- At Start the phone downloads the whole route **and** the streets in a corridor along it
  (a few hundred metres each side; width is the builder's call) and keeps them for the ride.
- From then on, progress, turn countdown, zoom and the cube's map work with no network. The cube
  link is Bluetooth and never depends on signal.
- Leaving the route with no signal: keep drawing the rider against the downloaded streets; the
  banner shows "Off route · no signal" with an arrow and distance back to the nearest point of
  the route; retry routing and reroute as soon as signal returns.
- No signal at Start: the app says so plainly rather than starting a half-loaded ride.
- (My call, optional) repeating the last route can work from its saved copy.

## Constraints and facts

- The cube is a Waveshare ESP32-S3-Touch-LCD-1.69: 240x280, ESP32-S3, 16 MB flash, PSRAM,
  NimBLE. See `docs/ESP32-S3-Touch-LCD-1.69.md` and the root `CLAUDE.md` for how the firmware,
  simulator and tests are organised, and `docs/ble-protocol.md` for the current BLE protocol.
- `firmware/` code shared with the simulator may only use `sim/Arduino.h`; new pure logic gets
  host tests (`make test`), new screens a `make shot` view (`@nav-…`).
- The board compiles as gnu++11 (see `firmware/CLAUDE.md`).
- The BLE path has not yet been run on a real cube (`docs/ble-acceptance.md`); expect hardware
  surprises with two simultaneous connections.
- Building and installing the iPhone app needs Xcode and the owner's free Apple ID; the install
  expires 7 days after each build (see Distribution above).

## Verification

- Server: a smoke-test script that routes between two known Hanoi / Ho Chi Minh City points with
  the motorbike profile and fetches a street corridor, against `nav.devxdev.site` with the key.
- Phone: the ride-following logic (progress, next turn, off-route, zoom, projection) is pure
  Swift with tests that replay recorded or synthetic GPS tracks, including a signal drop mid-ride.
- Cube: host tests for the navigation frame decoder and the two-bond rules; the simulator can
  show the navigation screen from a saved frame (`make shot` with `@nav-…` views), including
  Vietnamese names, "Rerouting", "Off route · no signal" and "Arrived".
- On the bike, the owner checks readability in sun and behaviour through a tunnel or dead zone.

## Left to the builder

Frame encoding and rate; the street data format and tiling; corridor width; zoom rules; the
off-route threshold; where "Pair new device" lives on the cube besides the portal; the grace
period before a dropped phone link ends navigation on the cube; the proxy arrangement on
`contabo`; how `ios-nav/` shares code with `mac-helper/`.

## Order of work (suggested)

1. Server: GraphHopper motorbike + streets + HTTPS + key, with the smoke test.
2. Cube: two bonds and pairing, navigation frame + screen in the simulator, fonts.
3. iPhone app: search, route preview, ride following with offline corridor, BLE push.
4. On the bike.
