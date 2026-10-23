import { readFileSync } from 'node:fs';

// Checks that the route styles differ the way the app promises.
// Env: NAV_URL (default http://localhost:8989), NAV_KEY (optional),
//      TRIPS (optional JSON [{name, from:[lon,lat], to:[lon,lat]}]).
// Prints one table per trip and exits 1 if a pass-bar check fails.

const URL_BASE = process.env.NAV_URL || 'http://localhost:8989';
const KEY = process.env.NAV_KEY || '';
// trips.json: Hanoi first (the commute is the acceptance trip), one Saigon trip as a regression check.
const TRIPS = JSON.parse(process.env.TRIPS || readFileSync(new URL('./trips.json', import.meta.url), 'utf8'));
const PROFILES = ['moto_vn', 'moto_calm', 'moto_calm_rush', 'moto_turns'];
const SIGNALS_DETAIL = 'crossing';
const SIGNALS_VALUE = 'traffic_signals';
const MAIN = new Set(['primary', 'trunk']);
const ALLEY = new Set(['residential', 'service', 'living_street', 'unclassified']);
const TURN_SIGNS = new Set([-98, -8, 8, -3, 3, -2, 2, -6, 6]);
const END_STRETCH_M = 300;
const MAX_LATENCY_MS = 1000;
// calm_rush must cut main-road share to half of fastest's, or 20 points under it if that is lower;
// below this share fastest is already off the main roads and there is nothing to cut.
const MAIN_CHECK_MIN_PCT = 20;

function haversine([lon1, lat1], [lon2, lat2]) {
  const r = Math.PI / 180;
  const a = Math.sin((lat2 - lat1) * r / 2) ** 2
    + Math.cos(lat1 * r) * Math.cos(lat2 * r) * Math.sin((lon2 - lon1) * r / 2) ** 2;
  return 2 * 6371008.8 * Math.asin(Math.sqrt(a));
}

async function route(profile, from, to) {
  const headers = { 'content-type': 'application/json' };
  if (KEY) headers['X-Nav-Key'] = KEY;
  const body = {
    points: [from, to], profile, instructions: true, points_encoded: false, locale: 'en',
    details: ['road_class', SIGNALS_DETAIL],
  };
  const t0 = performance.now();
  const res = await fetch(`${URL_BASE}/route`, { method: 'POST', headers, body: JSON.stringify(body) });
  const wall = performance.now() - t0;
  const json = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(`${profile}: HTTP ${res.status} ${json.message || ''}`.trim());
  return { path: json.paths[0], took: json.info?.took ?? wall, wall };
}

function stats(path) {
  const pts = path.points.coordinates;
  const seg = pts.slice(1).map((p, i) => haversine(pts[i], p));
  const total = seg.reduce((a, b) => a + b, 0);
  const startOf = [0];
  for (const d of seg) startOf.push(startOf[startOf.length - 1] + d);
  let main = 0, alley = 0, middle = 0;
  for (const [from, to, value] of path.details.road_class) {
    const cls = String(value).toLowerCase();
    for (let i = from; i < to; i++) {
      if (MAIN.has(cls)) main += seg[i];
      const mid = startOf[i] + seg[i] / 2;
      if (mid < END_STRETCH_M || mid > total - END_STRETCH_M) continue;
      middle += seg[i];
      if (ALLEY.has(cls)) alley += seg[i];
    }
  }
  const signals = path.details[SIGNALS_DETAIL];
  return {
    timeMin: path.time / 60000,
    km: path.distance / 1000,
    turns: path.instructions.filter((i) => TURN_SIGNS.has(i.sign)).length,
    lights: signals ? signals.filter((d) => String(d[2]).toLowerCase() === SIGNALS_VALUE).length : null,
    mainPct: total ? (100 * main) / total : 0,
    alleyPct: middle ? (100 * alley) / middle : 0,
  };
}

async function checkTrip(trip) {
  const rows = {};
  const failures = [];
  for (const profile of PROFILES) {
    try {
      const r = await route(profile, trip.from, trip.to);
      rows[profile] = { ...stats(r.path), took: r.took, wall: r.wall, time: r.path.time };
    } catch (e) {
      failures.push(e.message);
    }
  }
  console.log(`\n${trip.name}`);
  console.log('profile          min    km  turns lights main%  alley%  took');
  for (const [p, s] of Object.entries(rows)) {
    console.log(`${p.padEnd(15)} ${s.timeMin.toFixed(1).padStart(5)} ${s.km.toFixed(1).padStart(5)} ${String(s.turns).padStart(6)}`
      + ` ${String(s.lights ?? '-').padStart(6)} ${s.mainPct.toFixed(0).padStart(5)} ${s.alleyPct.toFixed(0).padStart(7)} ${Math.round(s.took).toString().padStart(5)}ms`);
  }
  const fast = rows.moto_vn, rush = rows.moto_calm_rush, turns = rows.moto_turns;
  if (fast && rush) {
    const mainBar = Math.max(fast.mainPct / 2, fast.mainPct - 20);
    if (fast.mainPct >= MAIN_CHECK_MIN_PCT && rush.mainPct > mainBar)
      failures.push(`calm_rush main roads ${rush.mainPct.toFixed(0)}% above ${mainBar.toFixed(0)}% (fastest ${fast.mainPct.toFixed(0)}%)`);
    if (fast.lights != null && fast.lights >= 2 && rush.lights > fast.lights)
      failures.push(`calm_rush lights ${rush.lights} > fastest ${fast.lights}`);
  }
  if (fast && turns && turns.turns > fast.turns)
    failures.push(`turns ${turns.turns} more than fastest ${fast.turns}`);
  for (const [p, s] of Object.entries(rows)) {
    if (s.alleyPct > 10) failures.push(`${p} alleys ${s.alleyPct.toFixed(0)}% outside the ends`);
    if (s.took > MAX_LATENCY_MS) failures.push(`${p} took ${Math.round(s.took)} ms`);
    if (fast && p !== 'moto_vn' && s.time > 1.2 * fast.time && s.time - fast.time > 300000)
      console.log(`warning: ${p} would be hidden (detour limit)`);
  }
  for (const f of failures) console.log(`FAIL: ${f}`);
  return failures.length === 0;
}

let ok = true;
for (const trip of TRIPS) ok = (await checkTrip(trip)) && ok;
console.log(ok ? '\nstyles-check: pass' : '\nstyles-check: FAILED');
process.exit(ok ? 0 : 1);
