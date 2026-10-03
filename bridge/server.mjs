#!/usr/bin/env node
// Bridge between Claude usage data and the ESP32 display.
//
// Why a bridge instead of calling Anthropic from the device: the ESP32 would
// otherwise need to hold an admin credential, terminate TLS against
// api.anthropic.com, and parse a large paginated JSON payload -- all of which
// are painful on a microcontroller. Here the host does the aggregation and the
// device polls one small, pre-formatted JSON document over plain HTTP on the
// LAN.
//
//   GET /api/status  -> the payload the firmware consumes
//   GET /            -> a browser mock of the device, for developing the UI
//   GET /health      -> liveness + last refresh result. Always 200 while the
//                       process serves (the Mac helper reads any other status
//                       as "no bridge"); a degraded bridge says so in the body.

import http from 'node:http';
import { networkInterfaces } from 'node:os';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { LocalSource } from './sources/local.mjs';
import { AdminSource } from './sources/admin.mjs';
import { buildPayload } from './cards.mjs';
import { getUsageLimits } from './limits.mjs';
import { getUnreadMail } from './sources/gmail.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));

// Config values come from env (strings) or config.json (any JSON type). A bad
// value falls back to the default with a warning rather than becoming NaN.
function num(name, raw, def, { min, max = Infinity, integer = false }) {
  if (raw == null || raw === '') return def;
  const n = Number(raw);
  if (Number.isFinite(n) && n >= min && n <= max && (!integer || Number.isInteger(n))) return n;
  console.warn(`config: ignoring ${name}=${JSON.stringify(raw)} (want ${integer ? 'an integer' : 'a number'} ${min}..${max}), using ${def}`);
  return def;
}

// The env var wins over config.json in both directions, so CUBE_EXTRA_CARDS=0
// switches off a config's `extraCards: true`.
function bool(name, ...raws) {
  for (const raw of raws) {
    if (raw == null || raw === '') continue;
    if (typeof raw === 'boolean') return raw;
    const s = String(raw).trim().toLowerCase();
    if (['1', 'true', 'yes', 'on'].includes(s)) return true;
    if (['0', 'false', 'no', 'off'].includes(s)) return false;
    console.warn(`config: ignoring ${name}=${JSON.stringify(raw)} (want 1/0 or true/false)`);
  }
  return false;
}

// An env var set to "" counts as unset, so it does not mask config.json.
const env = (name) => process.env[name] || undefined;

async function loadConfig() {
  let file = {};
  try {
    file = JSON.parse(await readFile(path.join(here, 'config.json'), 'utf8'));
  } catch {
    // config.json is optional; env vars alone are enough.
  }
  const source = env('CUBE_SOURCE') ?? file.source ?? 'local';
  return {
    source,
    port: num('port', env('CUBE_PORT') ?? file.port, 8787, { min: 1, max: 65535, integer: true }),
    // Unset = every interface, which the cube needs to poll over WiFi.
    // 127.0.0.1 keeps the bridge off the LAN (fine for BLE via the Mac helper).
    host: env('CUBE_HOST') ?? (file.host || undefined),
    refreshMs: num('refreshMs', env('CUBE_REFRESH_MS') ?? file.refreshMs,
      source === 'admin' ? 60_000 : 5_000, { min: 1_000, max: 86_400_000 }),
    adminKey: process.env.ANTHROPIC_ADMIN_KEY ?? file.adminKey,
    oauthToken: process.env.ANTHROPIC_ADMIN_OAUTH_TOKEN ?? file.oauthToken,
    // The deck is the two rate-limit gauges by default. Flip this on to swipe
    // through the spend and token breakdowns as well.
    extraCards: bool('extraCards', env('CUBE_EXTRA_CARDS'), file.extraCards),
    // Unread-mail card over IMAP. Needs a Gmail app password; absent = no card.
    gmailUser: process.env.CUBE_GMAIL_USER ?? file.gmail?.user,
    gmailPassword: process.env.CUBE_GMAIL_PASSWORD ?? file.gmail?.appPassword,
  };
}

const cfg = await loadConfig();
// The Usage & Cost API documents sustained polling at once per minute.
if (cfg.source === 'admin' && cfg.refreshMs < 60_000) {
  console.warn('admin source: raising refresh interval to the documented 60s minimum');
  cfg.refreshMs = 60_000;
}

const source =
  cfg.source === 'admin'
    ? new AdminSource({ adminKey: cfg.adminKey, oauthToken: cfg.oauthToken })
    : new LocalSource();

// Served until the first refresh lands: the usage card with empty rings and
// "starting" for a caption, so the cube has something valid to draw.
const STARTING = { five_hour: null, seven_day: null, error: 'starting up' };
let payload = buildPayload([], { source: source.label, limits: STARTING });
let refreshed = false;
let lastError = null;   // the source (transcripts / Admin API) failed
let limitsError = null; // the rate-limit read failed
let lastOk = 0;

async function refresh() {
  // The gauges come from the usage endpoint and the sparkline from the local
  // transcripts; neither should be able to take the other down, so the limits
  // are fetched independently and report their own failure on the card.
  const [limits, mail] = await Promise.all([
    getUsageLimits(),
    getUnreadMail({ user: cfg.gmailUser, password: cfg.gmailPassword }),
  ]);
  limitsError = limits.error ?? null;
  // Built after source.refresh(): the admin source sets `billed` there, and
  // reading it first would always show the previous refresh's billing.
  const common = () => ({
    source: source.label,
    hasRequestCounts: source.hasRequestCounts !== false,
    billed: source.billed ?? null,
    extraCards: cfg.extraCards,
    limits,
    mail,
  });
  try {
    // Transcript scanning now only feeds the optional spend cards, so the
    // default two-gauge deck never touches ~/.claude/projects at all.
    const events = cfg.extraCards ? await source.refresh() : [];
    payload = buildPayload(events, common());
    lastError = null;
    lastOk = Date.now();
  } catch (err) {
    lastError = err.message ?? String(err);
    console.error('refresh failed:', lastError);
    payload = buildPayload(source.events ?? [], { ...common(), error: lastError });
  }
  refreshed = true;
}

// A chain rather than setInterval: the next refresh is only scheduled once
// this one has finished, so a slow Admin API or IMAP round trip can never
// stack up overlapping refreshes.
async function refreshLoop() {
  try {
    await refresh();
  } catch (err) {
    console.error('refresh failed:', err?.message ?? err);
  }
  setTimeout(refreshLoop, cfg.refreshMs).unref?.();
}

const send = (res, code, body, type = 'application/json') => {
  // An explicit Content-Length keeps the response un-chunked. The device has
  // to parse this with a very small HTTP client, so one framing less to handle.
  const buf = Buffer.from(body);
  res.writeHead(code, {
    'content-length': buf.length,
    'content-type': type,
    'cache-control': 'no-store',
  });
  res.end(buf);
};

// A throw in an async request handler is an unhandled rejection, which ends the
// process: one odd request from anywhere on the LAN must not take the bridge down.
async function handle(req, res) {
  try {
    await route(req, res);
  } catch (err) {
    console.error(`request ${req.url} failed:`, err.message);
    if (!res.headersSent) send(res, 500, JSON.stringify({ error: 'internal error' }));
  }
}

async function route(req, res) {
  let url;
  try {
    url = new URL(req.url, 'http://localhost');
  } catch {
    return send(res, 400, JSON.stringify({ error: 'bad request' }));
  }
  if (url.pathname === '/api/status') return send(res, 200, JSON.stringify(payload));
  if (url.pathname === '/health') {
    const error = lastError ?? limitsError ?? (refreshed ? null : 'starting up');
    return send(
      res,
      200,
      JSON.stringify({ ok: !error, error, source: source.label, lastError, limitsError, lastOk }),
    );
  }
  if (url.pathname === '/' || url.pathname === '/index.html') {
    const html = await readFile(path.join(here, 'preview.html'), 'utf8');
    return send(res, 200, html, 'text/html; charset=utf-8');
  }
  send(res, 404, JSON.stringify({ error: 'not found' }));
}

const wildcard = !cfg.host || cfg.host === '0.0.0.0' || cfg.host === '::';
const loopback = /^(127\.|::1$|localhost$)/.test(cfg.host ?? '');

const server = http.createServer(handle);
server.on('error', (err) => {
  console.error(`cannot listen on ${cfg.host ?? '*'}:${cfg.port}: ${err.message}`);
  process.exit(1);
});

// A host pinned to one LAN address would shut out the Mac helper, which always
// reads http://127.0.0.1:<port>. Loopback never leaves the machine, so it is
// added alongside. Losing it only costs the helper, hence a warning, not exit.
if (!wildcard && !loopback) {
  const local = http.createServer(handle);
  local.on('error', (err) => {
    console.warn(`cannot also listen on 127.0.0.1:${cfg.port} (${err.message}); the Mac helper will not reach this bridge`);
  });
  local.listen({ port: cfg.port, host: '127.0.0.1' });
}

// Listen first and refresh in the background: the first usage read can take
// seconds (Keychain, network), and nothing should be refused meanwhile.
server.listen({ port: cfg.port, host: cfg.host }, () => {
  console.log(`claude-status-cube bridge  source=${source.label}  refresh=${cfg.refreshMs}ms  host=${cfg.host ?? '*'}`);
  console.log(`  preview   http://${wildcard ? 'localhost' : cfg.host}:${cfg.port}/`);
  for (const ip of wildcard ? lanAddresses() : loopback ? [] : [cfg.host]) {
    console.log(`  device    http://${ip}:${cfg.port}/api/status`);
  }
  refreshLoop();
});

// Printed at startup so you can paste the right LAN address into config.h.
function lanAddresses() {
  return Object.values(networkInterfaces())
    .flat()
    .filter((n) => n && n.family === 'IPv4' && !n.internal)
    .map((n) => n.address);
}
