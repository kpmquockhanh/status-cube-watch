# Route Choice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The preview screen offers Calm, Fastest and Fewest turns routes with turns / lights / main-road share, the rider picks one, and the ride (including reroutes and the offline saved ride) keeps that style.

**Architecture:** The nav server gets one GraphHopper profile per style (Fastest keeps the name `moto_vn`, so the app in the field keeps working during deploy), all sharing a new base custom model that discourages alleys, with turn costs enabled. The app asks for the styles in parallel, decodes per-route stats from GraphHopper path details, and a pure `NavCore` function turns the results into rows (detour limit, merging duplicates). `RideEngine` remembers the style for reroutes and `RideCache` stores it.

**Tech Stack:** GraphHopper 11.1 (Docker, custom models, CH), nginx, Node (check script, no deps); Swift 6 package `NavCore` (Swift Testing), SwiftUI app `CubeNav` (xcodegen).

**Spec:** `docs/superpowers/specs/2026-10-23-route-choice-design.md`

## Global Constraints

- Map only: no paid or third-party traffic/routing service.
- No change to the firmware, the BLE Nav frame or the cube's ride screen.
- Styles and order: **Calm**, **Fastest**, **Fewest turns** (exact copy).
- Profiles: `moto_vn` = Fastest, `moto_calm` = Calm off-peak, `moto_calm_rush` = Calm at rush hour, `moto_turns` = Fewest turns.
- Rush hour: Monday–Friday, 07:00–09:00 and 16:30–19:00 phone local time, start inclusive, end exclusive, evaluated when a request is made (planning and each reroute).
- Main roads: `road_class` PRIMARY and TRUNK.
- Alleys: `road_class` RESIDENTIAL, SERVICE, LIVING_STREET, UNCLASSIFIED are discouraged in **every** profile (priority < 1, never 0, so the first/last stretch still works).
- Detour limit: a Calm or Fewest turns route is hidden when `time > 1.2 × fastest.time` **and** `time − fastest.time > 300 s`.
- Turns counted: instructions with `NavTurn` `.left, .right, .sharpLeft, .sharpRight, .uTurn, .roundabout`.
- Server limits: host 4 cores / 7.8 GB, nothing else on it; refresh within its 4 h timeout with the atomic flip and rollback kept; every endpoint behind `X-Nav-Key`; `motorroad=yes` / `motorcycle=no` ways stay filtered out.
- Latency: one route request answers in ≤ 1 s server-side on the HCMC trip; rows appear as each style arrives.
- Deploying to `contabo` changes a live service: ask the user before each deploy step.

## Review Focus

1. **Some styles fail, others succeed** (timeout, 5xx, profile missing on an old server): rows for the successes, no error banner; the error and Try again only when all fail. → Task 4 test `missingStylesStillListed`, Task 6 step.
2. **Fastest is missing**: no detour filter can be applied; Calm and Fewest turns are shown as they are. → Task 4 test `noFastestNoFilter`.
3. **Short trip where all three come back identical**: one row named "Calm · Fastest · Fewest turns", riding as Calm. → Task 4 test `identicalRoutesMerge`.
4. **Response without the signals detail** (old graph during deploy, or the signals encoded value unavailable): decodes fine, `lights == nil`, the row omits lights. → Task 3 test `missingSignalsDetailIsNil`, Task 4 test on `routeFacts`.
5. **Rush-hour edges**: 07:00 is rush, 09:00 is not, 16:29 is not, 18:59 is, Saturday 08:00 is not. → Task 3 test `rushHourEdges`.

---

### Task 1: Server style profiles (local build and measurement)

**Files:**
- Modify: `nav-server/graphhopper/config.yml`
- Create: `nav-server/graphhopper/moto_base.json`, `nav-server/graphhopper/style_fast.json`, `nav-server/graphhopper/style_calm.json`, `nav-server/graphhopper/style_calm_rush.json`, `nav-server/graphhopper/style_turns.json`
- Delete: `nav-server/graphhopper/moto_vn.json` (its rules move into `moto_base.json`)
- Modify: `nav-server/graphhopper/Dockerfile` (COPY the new JSON files)
- Create: `nav-server/styles-check.mjs`
- Modify: `nav-server/NOTES.md`

**Interfaces:**
- Produces: profiles `moto_vn`, `moto_calm`, `moto_calm_rush`, `moto_turns` on `POST /route`; each response path carries `details.road_class` and, if Step 1 finds one, the signals detail named `SIGNALS_DETAIL` (record the exact key in NOTES.md; Task 3 uses it). Signals detail value meaning a traffic light: record it too (e.g. `traffic_signals`).

- [ ] **Step 1: Verify GraphHopper 11.1 capabilities and record them in `nav-server/NOTES.md` under "Route styles"**

  Read the 11.1 docs in the jar's version (`docs/core/custom-models.md`, `docs/core/turn-restrictions.md`, `config-example.yml` at tag `11.1` on GitHub) and answer, with the config line that does it:
  1. How to enable turn costs per profile (`turn_costs: {vehicle_types: [...], u_turn_costs: ...}`) and whether CH works with them (edge-based CH).
  2. Whether custom models support a per-turn penalty (`turn_penalty` with `change_angle` or similar). If yes, use it for turn costs per style; if not, turn cost is only the profile's `u_turn_costs` plus `distance_influence` and Fewest turns relies on priority for big through-roads (record which).
  3. Which encoded value marks traffic signals (e.g. `crossing == TRAFFIC_SIGNALS`) and whether it is set for `highway=traffic_signals` nodes or only `crossing=traffic_signals`; whether it can be used in a custom model and returned as a path detail.
  If no signals encoded value exists: Calm has no light rule, no signals detail is requested, and the app shows no light counts (Task 3 handles `nil`). Record the decision.

- [ ] **Step 2: Write `styles-check.mjs` (the test)**

  `node styles-check.mjs` with env `NAV_URL` (default `http://localhost:8989`, no key needed locally), `NAV_KEY` (optional), `TRIPS` (optional JSON `[{name, from:[lon,lat], to:[lon,lat]}]`, default the `smoke.sh` HCMC trip `[106.6980,10.7725] → [106.6588,10.8185]`). For each trip it requests all four profiles with `details: ["road_class", SIGNALS_DETAIL?]` and prints a table per profile: time (min), distance, turns (instruction signs −98, ±8, ±3, ±2, ±6), lights (count of detail intervals with the signals value), main-road share (% of distance on primary/trunk, segment lengths by haversine over the path points), alley share outside the first and last 300 m, server latency. Exit 1 unless, per trip:
  - `moto_calm_rush` main-road share ≤ `moto_vn` share − 20 points;
  - `moto_calm_rush` lights ≤ `moto_vn` lights, unless `moto_vn` has fewer than 2 lights mapped;
  - `moto_turns` turns < `moto_vn` turns;
  - every profile's alley share outside the first/last 300 m ≤ 10%;
  - Calm and Fewest turns within the detour limit (Global Constraints), or the script prints which one would be hidden (a warning, not a failure);
  - each request ≤ 1 s.

- [ ] **Step 3: Run it against today's server config to see it fail**

  Build the image locally (`docker build -f nav-server/graphhopper/Dockerfile nav-server -t nav-graphhopper:11.1`), import the Vietnam extract with the same filter and flags `refresh.sh` uses (`osmium tags-filter -i … w/motorroad=yes w/motorcycle=no`, `java -Xmx3g … import config.yml`), serve on 8989, run `node nav-server/styles-check.mjs`.
  Expected: FAIL (profiles `moto_calm` etc. unknown).

- [ ] **Step 4: Write the profiles**

  `config.yml`: four profiles, each `custom_model_files: [moto_base.json, style_<x>.json]` (moto_vn uses `style_fast.json`), turn costs per Step 1, all four in `profiles_ch`, `graph.encoded_values` extended with what Step 1 needs (keep the existing ones).
  `moto_base.json`: today's `moto_vn.json` priority and speed rules, plus alley priorities RESIDENTIAL 0.5, UNCLASSIFIED 0.6, SERVICE 0.3, LIVING_STREET 0.3 (starting values; tune).
  Styles (starting values; tune):
  - `style_fast.json`: `distance_influence` 70.
  - `style_calm.json`: lights penalty (priority × 0.7 on signal edges, if available), turn penalty moderate, `distance_influence` 50.
  - `style_calm_rush.json`: `style_calm.json` rules plus PRIMARY and TRUNK priority × 0.35.
  - `style_turns.json`: strong turn penalty (or the Step 1 fallback), SECONDARY/TERTIARY priority × 1.0, alleys as base, `distance_influence` 40.

- [ ] **Step 5: Re-import, tune until `styles-check.mjs` passes, measure**

  Run: `node nav-server/styles-check.mjs`
  Expected: exit 0. Record in `NOTES.md`: import time, peak heap, graph size on disk, serving RSS, per-profile latency, and the final weights. If import exceeds `-Xmx3g` or RSS leaves under 1.5 GB free on a 7.8 GB host, either raise `mem_limit`/`-Xmx` in `docker-compose.yml` and `refresh.sh` (host has room; record it) or move `moto_turns` / `moto_calm` to LM instead of CH if that meets the 1 s latency. Record which.

- [ ] **Step 6: Commit**

```bash
git add nav-server/graphhopper nav-server/styles-check.mjs nav-server/NOTES.md nav-server/docker-compose.yml nav-server/refresh.sh
git commit -m "feat(nav-server): Calm, Fastest and Fewest turns profiles with turn costs and alley penalty"
```

---

### Task 2: Deploy the profiles to contabo

**Files:**
- Modify: `nav-server/smoke.sh` (route check runs each of the four profiles; still asserts no MOTORWAY)
- Modify: `nav-server/README.md` (profiles, `styles-check.mjs`, new memory figures)

**Interfaces:**
- Consumes: Task 1 config and `styles-check.mjs`.
- Produces: `https://nav.devxdev.site/route` answering all four profiles.

- [ ] **Step 1: Extend `smoke.sh`** so `route()` loops over `moto_vn moto_calm moto_calm_rush moto_turns` for the HCMC and Hanoi trips.
- [ ] **Step 2: Ask the user before deploying.** Then follow README "Deploy" step 1 (copy), rebuild the image on the host, run `refresh.sh` once by hand (`timeout 4h`), and watch `/var/log/nav-refresh.log` end in `[refresh] done`.
- [ ] **Step 3: Verify live**

  Run: `NAV_KEY=... ./nav-server/smoke.sh` and `NAV_URL=https://nav.devxdev.site NAV_KEY=... node nav-server/styles-check.mjs`
  Expected: `smoke: all checks passed`; styles-check exit 0.
- [ ] **Step 4: Commit**

```bash
git add nav-server/smoke.sh nav-server/README.md
git commit -m "chore(nav-server): smoke-test every route style, document the profiles"
```

---

### Task 3: NavCore styles, rush hour, route stats

**Files:**
- Create: `ios-nav/NavCore/Sources/NavCore/RouteStyle.swift`
- Modify: `ios-nav/NavCore/Sources/NavCore/Route.swift` (stats on `Route`, decoding details)
- Modify: `ios-nav/NavCore/Sources/NavCore/NavServer.swift` (profile in request)
- Modify: `ios-nav/NavCore/Tests/NavCoreTests/RideEngineTests.swift` (`FakeNavServer` signature, records `lastProfile`)
- Test: `ios-nav/NavCore/Tests/NavCoreTests/RouteStyleTests.swift`, `ios-nav/NavCore/Tests/NavCoreTests/RouteTests.swift`

**Interfaces:**
- Produces:
  - `public enum RouteStyle: String, CaseIterable, Sendable { case calm, fastest, fewestTurns }` (case order = display order); `var title: String` ("Calm", "Fastest", "Fewest turns"); `func profile(rushHour: Bool) -> String`.
  - `public func isRushHour(_ date: Date, calendar: Calendar = .current) -> Bool`.
  - `public struct RouteStats: Equatable, Sendable { turns: Int; lights: Int?; mainRoadShare: Double /* 0...1 */ }`; `Route.stats: RouteStats` (default `RouteStats(turns: 0, lights: nil, mainRoadShare: 0)` in the existing init so other call sites compile).
  - `NavServer.route(from: Coord, to: Coord, profile: String) async throws -> Route` (replaces `route(from:to:)`).
  - `RouteDecoding.signalsDetail: String?` and `RouteDecoding.signalsValue: String` set to Task 1's recorded key/value (`nil` if none).

- [ ] **Step 1: Write the failing tests**

```swift
// RouteStyleTests.swift
@Test func profiles() {
    #expect(RouteStyle.fastest.profile(rushHour: true) == "moto_vn")
    #expect(RouteStyle.calm.profile(rushHour: true) == "moto_calm_rush")
    #expect(RouteStyle.calm.profile(rushHour: false) == "moto_calm")
    #expect(RouteStyle.fewestTurns.profile(rushHour: false) == "moto_turns")
    #expect(RouteStyle.allCases.map(\.title) == ["Calm", "Fastest", "Fewest turns"])
}
@Test func rushHourEdges() {
    // calendar fixed to Asia/Ho_Chi_Minh, Gregorian; 2026-10-19 is a Monday
    #expect(isRushHour(at("2026-10-19 07:00"), calendar: cal))
    #expect(!isRushHour(at("2026-10-19 09:00"), calendar: cal))
    #expect(!isRushHour(at("2026-10-19 16:29"), calendar: cal))
    #expect(isRushHour(at("2026-10-19 18:59"), calendar: cal))
    #expect(!isRushHour(at("2026-10-24 08:00"), calendar: cal))   // Saturday
}
// RouteTests.swift
@Test func statsFromFixture()      // route-hcmc: turns == count of instructions with the six turn kinds; 0 < mainRoadShare < 1; mainRoadShare equals the primary+trunk interval lengths / total (computed in the test from the JSON)
@Test func missingSignalsDetailIsNil()   // inline JSON with only road_class details → stats.lights == nil, decodes
@Test func signalsCounted()        // inline JSON with signals detail intervals [[0,1,"traffic_signals"],[1,3,"missing"],[3,4,"traffic_signals"]] → lights == 2 (skip if signalsDetail == nil)
@Test func requestNamesProfile()   // HTTPNavServer with a URLProtocol stub: body JSON has "profile":"moto_turns" and "details" containing "road_class"
```

- [ ] **Step 2: Run to see them fail**

  Run: `cd ios-nav/NavCore && swift test`
  Expected: compile errors (`RouteStyle`, `isRushHour`, `stats` undefined).

- [ ] **Step 3: Implement** `RouteStyle.swift` (enum, titles, profiles, `isRushHour` with weekday 2...6 in Gregorian numbering and minute-of-day in `[420, 540) ∪ [990, 1140)`); decode `details.road_class` and the signals detail in `RouteDecoding.decode` (main-road share by haversine length of `points[from...to]` with value `primary` or `trunk`, case-insensitive); `HTTPNavServer.route(from:to:profile:)` puts `profile` and the details list in the body; update `FakeNavServer` and every `route(from:to:)` call site (`RideEngine` uses `RouteStyle.fastest.profile(rushHour: false)` until Task 5).

- [ ] **Step 4: Run tests**

  Run: `cd ios-nav/NavCore && swift test`
  Expected: all pass.

- [ ] **Step 5: Commit**

```bash
git add ios-nav/NavCore
git commit -m "feat(ios-nav): route styles, rush hour, per-route turns/lights/main-road stats"
```

---

### Task 4: Route options (detour limit, merging, facts text)

**Files:**
- Create: `ios-nav/NavCore/Sources/NavCore/RouteOptions.swift`
- Modify: `ios-nav/NavCore/Sources/NavCore/Formatting.swift` (`routeFacts`)
- Test: `ios-nav/NavCore/Tests/NavCoreTests/RouteOptionsTests.swift`, `ios-nav/NavCore/Tests/NavCoreTests/FormattingTests.swift`

**Interfaces:**
- Consumes: `RouteStyle`, `Route.stats` (Task 3).
- Produces:
  - `public struct RouteOption: Equatable, Sendable { styles: [RouteStyle]; route: Route; var style: RouteStyle { styles[0] }; var title: String /* titles joined " · " */ }`
  - `public func routeOptions(_ routes: [RouteStyle: Route]) -> [RouteOption]` — ordered Calm, Fastest, Fewest turns; detour limit applied only when Fastest is present; a route is merged into an earlier row when `sameRoute` holds.
  - `func sameRoute(_ a: Route, _ b: Route) -> Bool` — distances within 2% and every point of each within 25 m of the other's polyline.
  - `public func defaultPick(_ options: [RouteOption], last: RouteStyle?) -> Int?` — index of the row containing `last`, else the row containing `.calm`, else 0; `nil` when empty.
  - `public func routeFacts(_ s: RouteStats) -> String` — "5 turns · 3 lights · main roads 20%"; "1 turn", "1 light"; lights part omitted when `nil`; percent rounded.

- [ ] **Step 1: Write the failing tests**

```swift
@Test func orderAndTitles()          // routes for all three, all different → titles ["Calm", "Fastest", "Fewest turns"]
@Test func detourHidden()            // fastest 600 s; calm 900 s (+50%, +300 s exactly → shown); turns 1000 s → hidden
@Test func shortTripNotCut()         // fastest 300 s, calm 420 s (+40%, +120 s) → shown
@Test func noFastestNoFilter()       // only calm 3000 s and turns 4000 s → both shown
@Test func missingStylesStillListed()// only calm → one row, style .calm
@Test func identicalRoutesMerge()    // same Route for all three → one row, title "Calm · Fastest · Fewest turns", style .calm
@Test func defaultPickRules()        // last .fewestTurns present → its index; last nil → calm's row; calm hidden → 0; [] → nil
// FormattingTests
@Test func facts() {
    #expect(routeFacts(RouteStats(turns: 5, lights: 3, mainRoadShare: 0.2)) == "5 turns · 3 lights · main roads 20%")
    #expect(routeFacts(RouteStats(turns: 1, lights: 1, mainRoadShare: 0.704)) == "1 turn · 1 light · main roads 70%")
    #expect(routeFacts(RouteStats(turns: 2, lights: nil, mainRoadShare: 0)) == "2 turns · main roads 0%")
}
```

- [ ] **Step 2: Run to see them fail** — `cd ios-nav/NavCore && swift test` → compile errors.
- [ ] **Step 3: Implement** in `RouteOptions.swift` and `Formatting.swift` (point-to-segment distance in a local equirectangular frame, as `RouteTracker` does).
- [ ] **Step 4: Run tests** — `swift test` → all pass.
- [ ] **Step 5: Commit**

```bash
git add ios-nav/NavCore
git commit -m "feat(ios-nav): route options with detour limit, merged duplicates and facts text"
```

---

### Task 5: Ride keeps its route and style (engine and cache)

**Files:**
- Modify: `ios-nav/NavCore/Sources/NavCore/RideEngine.swift`
- Modify: `ios-nav/NavCore/Sources/NavCore/RideCache.swift`
- Test: `ios-nav/NavCore/Tests/NavCoreTests/RideEngineTests.swift`

**Interfaces:**
- Consumes: `RouteStyle`, `isRushHour`, `NavServer.route(from:to:profile:)` (Task 3).
- Produces:
  - `RideEngine.start(route: Route, to: Coord, style: RouteStyle) async throws` — fetches corridor tiles only (no route request), then begins and saves.
  - `RideEngine.start(from:to:style: RouteStyle = .fastest)` — existing method, now passes the style's profile.
  - `RideEngine.style: RouteStyle` (read-only); every reroute requests `style.profile(rushHour: isRushHour(now()))`.
  - `RideCache.version = 3`; `save`/`saveInBackground(route:tiles:destination:style:)`; `CachedRide.style`; `resumeLast` restores it. A version-2 file reads as no ride (existing rule).

- [ ] **Step 1: Write the failing tests**

```swift
@Test func startWithRouteMakesNoRouteRequest()  // start(route: lRoute(), to:, style: .calm) → server.routeCalls == 0, state .navigating
@Test func rerouteKeepsStyle()                  // start(route:…, style: .fewestTurns), go off route → server.lastProfile == "moto_turns"
@Test func cachedRideKeepsStyle()               // start(route:…, style: .fewestTurns); new engine resumeLast() → style == .fewestTurns; its reroute asks "moto_turns"
```

- [ ] **Step 2: Run to see them fail** — `swift test` → compile errors.
- [ ] **Step 3: Implement** (store `style` in `begin`; the reroute task captures the profile string computed on the main actor at request time; `DTO` gains `style: String`, unknown raw value → `.fastest`).
- [ ] **Step 4: Run tests** — `swift test` → all pass, existing engine tests unchanged apart from the cache signature.
- [ ] **Step 5: Commit**

```bash
git add ios-nav/NavCore
git commit -m "feat(ios-nav): ride the picked route, reroute and cache with its style"
```

---

### Task 6: Preview screen with the route list

**Files:**
- Modify: `ios-nav/CubeNav/PreviewView.swift`
- Modify: `ios-nav/CubeNav/RideController.swift`
- Modify: `ios-nav/CubeNav/CubeNavApp.swift` (`-demo preview` sample routes)

**Interfaces:**
- Consumes: `routeOptions`, `defaultPick`, `routeFacts`, `RouteOption`, `RideEngine.start(route:to:style:)`, `RouteStyle`.
- Produces:
  - `RideController.plan(to: Coord, style: RouteStyle, at: Date = Date()) async -> Result<Route, StartError>` (replaces `plan(to:)`).
  - `RideController.start(to: Coord, option: RouteOption)` (replaces `start(to:)`); `startSaved` unchanged.
  - `@AppStorage("routeStyle")` holds the last picked `RouteStyle.rawValue`, written on Start ride.

- [ ] **Step 1: RideController** — `plan(to:style:at:)` uses `style.profile(rushHour: isRushHour(at))`; `start(to:option:)` creates the engine and calls `start(route: option.route, to:, style: option.style)`.
- [ ] **Step 2: PreviewView** — on load, a task group plans the three styles in parallel; each result goes into `routes: [RouteStyle: Route]` as it arrives and the rows are `routeOptions(routes)`; the pick is `defaultPick(options, last:)` until the rider taps a row (keep the tapped style across later arrivals). "Finding a motorbike route…" shows until the first row; the error + Try again only when all three failed (first error's text). The task group is cancelled by leaving the screen.
  Rows replace the Distance / Ride time / Arrive stats: colour dot (Calm `Theme.good`, Fastest `Theme.route`, Fewest turns `Theme.warn`; a merged row uses its first style's colour), title, `routeFacts(route.stats)` in `Theme.dim`, ride time (`formatETA`) on the right in `Theme.number(17)`; the picked row has a 2 pt `Theme.route` border on `Theme.surfaceHigh`; whole row is the tap target (≥ 44 pt), `accessibilityElement(children: .combine)` with the selected trait.
  Map: non-picked routes first in `Color(hex: 0x5A6170)` width 5, then the picked one as today (bg 11 + `Theme.route` 7).
  Start ride: `ride.start(to: place.coord, option: options[pick])`, then saves the style to `@AppStorage`.
- [ ] **Step 3: Demo** — `-demo preview` passes three sample routes (different points and stats) so the list shows three rows.
- [ ] **Step 4: Build and look**

  Run: `cd ios-nav && xcodegen && xcodebuild -project CubeNav.xcodeproj -scheme CubeNav -destination 'platform=iOS Simulator,name=iPhone 16' build`
  Expected: `** BUILD SUCCEEDED **`. Then launch in the simulator with `-demo preview` and take a screenshot: three rows, the picked one outlined, routes drawn grey/blue.
- [ ] **Step 5: Commit**

```bash
git add ios-nav/CubeNav
git commit -m "feat(ios-nav): pick Calm, Fastest or Fewest turns on the preview screen"
```

---

### Task 7: Acceptance on the rider's office trip and docs

**Files:**
- Modify: `ios-nav/README.md` (route choice, styles, rush hour), `nav-server/NOTES.md` (office-trip results), `docs/superpowers/specs/2026-10-23-route-choice-design.md` only if thresholds were moved with the rider

- [ ] **Step 1: Ask the user for the home and office points** (or a place name to geocode).
- [ ] **Step 2: Run the check on that trip**

  Run: `NAV_URL=https://nav.devxdev.site NAV_KEY=... TRIPS='[{"name":"office","from":[lon,lat],"to":[lon,lat]}]' node nav-server/styles-check.mjs`
  Expected: exit 0. If it fails, tune Task 1's style files, redeploy (ask first), re-run; record final numbers in `NOTES.md`.
- [ ] **Step 3: Rider check** — the user rides or previews the office trip on the phone at rush hour and confirms Calm looks calmer.
- [ ] **Step 4: Commit**

```bash
git add ios-nav/README.md nav-server/NOTES.md
git commit -m "docs: route choice styles and office-trip results"
```
