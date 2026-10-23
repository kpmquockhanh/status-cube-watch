# nav-server notes (measured on contabo, 4 cores / 7.8 GB, Vietnam extract 2026-10-21)

- First `refresh.sh` run: about 17 min end to end (download ~1 min, GraphHopper import + CH ~4 min,
  tiles ~10 min), exit 0.
- Import with `-Xmx5g`: JVM heap peaked near 2.5 GB total (log `totalMB:2484`), so 5g is ample.
- Graph on disk 569 MB; tiles 1.6 GB (158,658 tile files at zoom 15, 2.78 M ways).
- Serving: `-Xmx2g` loads the graph; container RSS about 1.3 GB. `/route?profile=moto_vn` works
  (Saigon centre to Tan Binh, 219 ms cold).
- Image CMD uses `-Xmx2g` and `graph.location=/data/graph`; mount `/root/nav/data` at `/data`.

## Data layout (refresh.sh)
`/root/nav/data/graph` and `/root/nav/data/tiles` are relative symlinks into `/root/nav/data/releases/`,
flipped atomically with `mv -T`. Task 3 must mount the whole `/root/nav/data` directory (not the
symlinks), e.g. `/root/nav/data:/data`, and use `/data/graph` and `/data/tiles`.
Rollback: an unhealthy restart flips the links back and restarts. Import is capped at -Xmx3g / 4 GB.
GraphHopper 11 has no MOTORLINK road class: `highway=motorway_link` is stored as MOTORWAY, so the
MOTORWAY rule in moto_vn.json already bans ramps.

## Route styles (GraphHopper 11.1, checked against the 11.1 source and docs)

- **Turn costs**: per profile, `turn_costs: {vehicle_types: [motorcar, motor_vehicle], u_turn_costs: 60}`
  (the key is `vehicle_types`; the 11.1 docs page that says `restrictions` is wrong). CH works with
  them: the CH preparation becomes edge-based, which costs more import time and memory.
- **Turn penalty**: custom models take a `turn_penalty` list, e.g.
  `{"if": "change_angle >= 25 && change_angle < 80", "add": "3"}` (seconds added per turn; see
  `custom_models/avoid_turns.json` in the 11.1 jar). `change_angle` needs the `orientation` encoded
  value in `graph.encoded_values`.
- **Traffic signals**: encoded value `crossing`, value `TRAFFIC_SIGNALS`, usable in a custom model
  (`crossing == TRAFFIC_SIGNALS`) and as a path detail (`details: ["crossing"]`, interval value
  `"traffic_signals"`). But `OSMCrossingParser` only sets it from `crossing:signals=yes` or
  `crossing=traffic_signals` on a node of the way, **not** from `highway=traffic_signals`, which is how
  most Vietnamese junction lights are mapped. So `refresh.sh` adds `crossing:signals=yes` to every
  `highway=traffic_signals` node of the routing input before import (osmium, see the script). The
  value lands on each edge that has the node, so a light at a junction marks the edges on both sides;
  consecutive marked edges merge into one path-detail interval, so counting intervals counts lights
  (two lights with no unlit edge between them count once: an undercount, acceptable).
- App contract: signals detail key `crossing`, light value `traffic_signals`.

## Four route profiles (measured locally in Docker, 2026-10-23; not yet on contabo)
- Import of the four turn-cost CH profiles (`-Xmx3g` in 4 GB): 4078 s, heap peak about 2.9 GB of 3 GB
  (tight), graph 1.6 GB on disk.
- Serving the four: `-Xmx3g` dies with `OutOfMemoryError` while loading; `-Xmx4500m` in a 5500m
  container works, RSS about 2.1 GB. Compose and the image CMD use that.
- Lights: OSM puts most signals on approach nodes in the middle of an edge, so a rule needing
  `prev_crossing == TRAFFIC_SIGNALS && crossing == TRAFFIC_SIGNALS` rarely fires. The styles add the
  penalty on entering a lit edge (`crossing == TRAFFIC_SIGNALS && prev_crossing != TRAFFIC_SIGNALS`).
- Tuned for Hanoi with per-request custom models (`ch.disable`, `allow_turn_penalty_in_request` in a
  scratch config only) on six trips: the rider's commute, Hoan Kiem→Cau Giay, Lang Ha→Hai Ba Trung,
  Tay Ho→Dong Da, Ha Dong→Hoang Mai, Long Bien→Hoan Kiem (the defaults in `styles-check.mjs`).
  - Calm (`style_calm.json`): lit edge +60 s, right +8 / left +15, alleys ×0.6, distance 50.
    Passes all six; on average 4 fewer lights for 0.7 min more.
  - Calm in rush hour (`style_calm_rush.json`): primary/trunk ×0.2, alleys ×0.6, lit edge +80 s,
    right +8 / left +15. Main-road share falls by 42 points on average, 1.5 min more. Lang Ha→Hai Ba
    Trung has 16 lights against 14: in central Hanoi the side streets have more lights than the main
    road. Neighbouring weights (×0.15–0.2, +60–100 s) give the same 5 of 6.
  - Fewest turns (`style_turns.json`, unchanged): right +45 / left +60. Never more turns than
    fastest except Ha Dong→Hoang Mai (5 against 4); the commute stays at 5, since two of those turns
    are the lanes at each end.
  - The bar the check uses (spec, rider's call): Calm rush main share ≤ max(half of fastest,
    fastest − 20), skipped when fastest is under 20%; Fewest turns ≤ fastest.
