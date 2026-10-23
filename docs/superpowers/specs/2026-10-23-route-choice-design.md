# Route choice: Calm, Fastest, Fewest turns

## Why

The motorbike nav app (`ios-nav/`) asks the nav server for one route and rides it. The routes get
there but do not feel like the way a rider would go: they cut through small streets with many turns,
through traffic lights, and along big main roads at rush hour. The rider wants to see a couple of
routes before setting off, each with the reasons it is or is not a calm ride, and pick one.

Today: `NavServer.route(from:to:)` (`ios-nav/NavCore/Sources/NavCore/NavServer.swift`) POSTs one request
with `profile: moto_vn` to GraphHopper 11.1 (`nav-server/`, Vietnam OSM extract, CH enabled for that
profile). `moto_vn.json` has `distance_influence: 90`, no turn costs, and nothing about traffic
signals or time of day.

## Goals

- The preview screen (`PreviewView.swift`) offers up to three routes, one per style:
  - **Calm**: avoids big main roads during rush hour and avoids traffic lights; fewer turns is a bonus.
  - **Fastest**: the quickest ride time.
  - **Fewest turns**: the route with the fewest turns, at a reasonable cost in time.
- Each route row shows: style name, ride time, number of turns, number of traffic lights, and the
  share of the route on main roads (percent of distance).
- The rider taps a row to pick it, then Start ride. The ride follows the picked route.
- A reroute during the ride (`RideEngine` reroute path) uses the same style the rider picked.
- The offline saved route (`RideCache`, "Start saved route (offline)") is the route that was ridden,
  with its style kept so a reroute from a saved ride also uses that style.

## Non-goals

- No live traffic, no paid or third-party traffic/routing service. Everything comes from the
  self-hosted GraphHopper and the OSM data already imported. (Decided by the rider.)
- No change to the cube: the firmware, the BLE Nav frame and the ride screen stay as they are.
- No user-tunable weights or sliders. Three fixed styles.
- No changing style mid-ride.

## Anti-goals (would make this a failure even if it works)

- Calm that is not visibly calmer than Fastest during rush hour (similar main-road share, similar
  lights). If it cannot be told apart, the feature has failed.
- The preview becoming slow: today one route comes back in roughly 0.2 s. Waiting several seconds for
  three routes before anything shows is not acceptable; show what arrives.
- Breaking the existing server: GraphHopper running out of memory, or the monthly refresh
  (`refresh.sh`, cron, rollback on unhealthy) no longer completing.

## The screen (decided: list layout, "B")

Same preview screen as today, map on top, panel at the bottom. In the panel, under the destination
name, a list of route rows instead of the single Distance / Ride time / Arrive stat row:

```
● Calm          5 turns · 3 lights · main roads 20%      18 min
● Fastest       9 turns · 7 lights · main roads 70%      15 min
● Fewest turns  3 turns · 6 lights · main roads 55%      17 min
[            Start ride            ]
```

- Each style has a fixed colour dot. The picked row has the route-blue outline (`Theme.route`).
- The map draws every route; the picked one in route blue on top, the others grey underneath.
- Tapping a row picks it and highlights it on the map.
- Distance and arrival time move into the row or are dropped; the builder decides (the ride time is
  the number that matters).
- Visual reference: the "B · List of routes" mockup from brainstorming
  (`.superpowers/brainstorm/*/content/route-choice.html`, local only).

## Behaviour details

- **Rush hour** (Calm only): weekdays 07:00–09:00 and 16:30–19:00, phone local time at the moment
  of the request. Outside those windows Calm does not penalise main roads (it still avoids lights
  and turns), so it may come out the same as Fastest. *My call; the rider did not set the times.*
- **Main roads** for the share and for Calm's penalty: GraphHopper `road_class` PRIMARY and TRUNK
  (SECONDARY is the builder's call, measured on real Saigon routes).
- **No alleys, for every style.** Avoiding main roads must not mean zigzagging through hẻm: all three
  styles, Fastest included, discourage `residential`, `service`, `living_street` and unclassified
  alleys (except the first/last stretch to reach the start or destination), and prefer
  SECONDARY/TERTIARY. So Fastest is not today's `moto_vn` model unchanged; every style gets this new
  base. Decide the encoded values for it before the re-import.
- **Detour limit**: Calm or Fewest turns is hidden when it is both more than 20% and more than
  5 minutes longer than Fastest (the "and" keeps short trips from being cut by a few minutes). If it
  is hidden, the list simply shows fewer rows. *Agreed with the rider as my recommendation.*
- **Default pick**: the first time, Calm. After that, the style the rider picked last (persist it).
  *My call.*
- **Duplicates**: if two styles return the same route (same geometry, or near enough), show one row
  carrying both names (e.g. "Calm · Fastest"). Picking it rides it with the first style named for
  rerouting purposes. *My call.*
- **Partial results**: rows appear as their routes arrive. If one style fails, show the others; if
  all fail, the existing error message and Try again.
- **Traffic light counts** come from OSM `highway=traffic_signals` nodes. Coverage in Vietnam is
  patchy, so counts undercount; no disclaimer is needed in the UI. *My call.*
- **Turn count**: count instructions that are real turns (left/right/sharp/U-turn/roundabout), not
  continue/keep/slight. The builder may refine.
- **Start ride uses the picked route.** Today `RideController.start(to:)` calls
  `RideEngine.start(from:to:)`, which fetches a new route. It must ride the route the rider saw
  (pass the picked `Route`, or re-request with the same style; the rider must not get a different
  route than the one picked).

## Server constraints

- Host: contabo, 4 cores, 7.8 GB RAM. GraphHopper container `mem_limit: 3500m`, `-Xmx3g`; import peaks
  near 2.5 GB heap with one profile; graph 569 MB on disk.
- Monthly refresh must stay within its 4 h timeout and keep the atomic flip and rollback.
- `/route` goes through nginx with the `X-Nav-Key` check; new endpoints or parameters must too.
- `motorroad=yes` / `motorcycle=no` ways are already removed from routing input; keep that.

## Left to the builder

- **How styles map to GraphHopper**: separate profiles (e.g. `moto_fast`, `moto_calm_rush`,
  `moto_calm`, `moto_turns`) with CH or LM each, or one profile queried with per-request custom
  models (`ch.disable`, slower), or a mix. The constraint is memory and import time above and the
  latency anti-goal. Measure on the server; raising the container memory is acceptable if the host
  has room, and the measurements go in `nav-server/NOTES.md`.
- **Turn costs**: enabling them (`turn_costs` encoded values, `turn_costs` in profiles, edge-based
  CH) needs a re-import; how much a turn costs per style is the builder's tuning.
- **Traffic signals**: how to penalise them in Calm (GraphHopper 11 support, a custom encoded value,
  or pre-processing the OSM data in `refresh.sh`) and how to count them per route (path details if
  available, or from the route geometry).
- **Rush hour mechanics**: how the app tells the server which Calm to use (two profiles, or a
  parameter). The app decides rush hour from local time.
- **Request shape**: three requests in parallel from the app, or one server-side call; whatever
  meets the latency anti-goal.
- **Tuning**: weights for each style, within the alley rule and detour limit above.

## Acceptance trip and pass bar

- The trip that matters is the rider's **home → office** ride. The builder asks the rider for the
  two points (or reads them from the app's recent places) when tuning starts; the `smoke.sh` HCMC
  route is a second check.
- Pass, for a weekday rush-hour request on that trip: Calm's main-road share is at least
  20 percentage points lower than Fastest's, Calm has no more traffic lights than Fastest (unless
  the trip has almost no signals mapped), no style runs through alleys beyond the first/last
  stretch, and every shown route is within the detour limit. Fewest turns has fewer turns than
  Fastest. *Thresholds are my call; the rider can move them.*
- Tests: route decoding of the new fields, style persistence, duplicate merging, reroute keeps style,
  cached ride keeps style, in `NavCore` tests.
- Docs: `ios-nav/README.md`, `nav-server/README.md`, `nav-server/NOTES.md`.
