// Route lab: a local page for looking at the route styles on a map and noting verdicts.
// No npm dependencies. Run: node nav-server/route-lab/server.mjs, then open http://localhost:8990
// Env: LAB_PORT (8990), NAV_LOCAL_URL (http://localhost:8989), NAV_LIVE_URL (https://nav.devxdev.site),
//      NAV_KEY (sent to the live server as X-Nav-Key; never sent to the page).
import { createServer } from 'node:http';
import { readFileSync, appendFileSync, existsSync } from 'node:fs';

const PORT = Number(process.env.LAB_PORT) || 8990;
const KEY = process.env.NAV_KEY || '';
const SERVERS = {
  local: { url: process.env.NAV_LOCAL_URL || 'http://localhost:8989', key: '' },
  live: { url: process.env.NAV_LIVE_URL || 'https://nav.devxdev.site', key: KEY },
};
const PROFILES = new Set(['moto_vn', 'moto_calm', 'moto_calm_rush', 'moto_turns']);
const here = (f) => new URL(f, import.meta.url);
const TRIPS = JSON.parse(readFileSync(here('../trips.json'), 'utf8'));
const VERDICTS = here('./verdicts.jsonl');
const ROUTE_TIMEOUT_MS = 15000;

function send(res, code, body, type = 'application/json') {
  res.writeHead(code, { 'content-type': type, 'cache-control': 'no-store' });
  res.end(typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body));
}

async function readJson(req) {
  let s = '';
  for await (const c of req) { s += c; if (s.length > 100_000) throw new Error('body too large'); }
  return JSON.parse(s);
}

const isLonLat = (p) => Array.isArray(p) && p.length === 2 && p.every(Number.isFinite);

// The same request the app makes (NavServer.swift), so what the lab shows is what the phone gets.
async function route({ server, profile, from, to }) {
  const s = SERVERS[server];
  if (!s) return [400, { message: `unknown server ${server}` }];
  if (!PROFILES.has(profile)) return [400, { message: `unknown profile ${profile}` }];
  if (!isLonLat(from) || !isLonLat(to)) return [400, { message: 'from/to must be [lon, lat]' }];
  if (server === 'live' && !s.key) return [400, { message: 'NAV_KEY is not set for the live server' }];
  const headers = { 'content-type': 'application/json' };
  if (s.key) headers['X-Nav-Key'] = s.key;
  const body = {
    points: [from, to], profile, instructions: true, points_encoded: false, locale: 'en',
    details: ['road_class', 'crossing'],
  };
  const t0 = performance.now();
  try {
    const r = await fetch(`${s.url}/route`, {
      method: 'POST', headers, body: JSON.stringify(body), signal: AbortSignal.timeout(ROUTE_TIMEOUT_MS),
    });
    const json = await r.json().catch(() => ({ message: `HTTP ${r.status}` }));
    json.wall = Math.round(performance.now() - t0);
    return [r.ok ? 200 : 502, r.ok ? json : { message: `HTTP ${r.status} ${json.message || ''}`.trim() }];
  } catch (e) {
    return [502, { message: `${s.url} unreachable: ${e.cause?.code || e.message}` }];
  }
}

function verdicts() {
  if (!existsSync(VERDICTS)) return [];
  return readFileSync(VERDICTS, 'utf8').split('\n').filter(Boolean).map((l) => JSON.parse(l));
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  try {
    if (req.method === 'GET' && url.pathname === '/') return send(res, 200, readFileSync(here('./index.html')), 'text/html; charset=utf-8');
    if (req.method === 'GET' && url.pathname === '/api/config')
      return send(res, 200, { trips: TRIPS, servers: Object.fromEntries(Object.entries(SERVERS).map(([k, s]) => [k, { url: s.url, ready: k !== 'live' || !!s.key }])) });
    if (req.method === 'POST' && url.pathname === '/api/route') {
      const [code, body] = await route(await readJson(req));
      return send(res, code, body);
    }
    if (req.method === 'GET' && url.pathname === '/api/verdicts') return send(res, 200, verdicts());
    if (req.method === 'POST' && url.pathname === '/api/verdicts') {
      const v = await readJson(req);
      if (!['good', 'bad'].includes(v.verdict)) return send(res, 400, { message: 'verdict must be good or bad' });
      const row = { ts: new Date().toISOString(), ...v };
      appendFileSync(VERDICTS, JSON.stringify(row) + '\n');
      return send(res, 200, row);
    }
    send(res, 404, { message: 'not found' });
  } catch (e) {
    send(res, 400, { message: e.message });
  }
});
// Loopback only: the page can spend the live key.
server.listen(PORT, '127.0.0.1', () => console.log(`route lab on http://localhost:${PORT} (local ${SERVERS.local.url}, live ${SERVERS.live.url}${KEY ? '' : ', no NAV_KEY'})`));
