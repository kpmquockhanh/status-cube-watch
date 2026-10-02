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
//   GET /health      -> liveness + last refresh result

import http from 'node:http';
import { networkInterfaces } from 'node:os';
import { readFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { LocalSource } from './sources/local.mjs';
import { AdminSource } from './sources/admin.mjs';
import { buildPayload } from './cards.mjs';
import { getUsageLimits } from './limits.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));

async function loadConfig() {
  let file = {};
  try {
    file = JSON.parse(await readFile(path.join(here, 'config.json'), 'utf8'));
  } catch {
    // config.json is optional; env vars alone are enough.
  }
  const source = process.env.CUBE_SOURCE ?? file.source ?? 'local';
  return {
    source,
    port: Number(process.env.CUBE_PORT ?? file.port ?? 8787),
    refreshMs:
      Number(process.env.CUBE_REFRESH_MS ?? file.refreshMs ?? (source === 'admin' ? 60_000 : 5_000)),
    adminKey: process.env.ANTHROPIC_ADMIN_KEY ?? file.adminKey,
    oauthToken: process.env.ANTHROPIC_ADMIN_OAUTH_TOKEN ?? file.oauthToken,
    // The deck is the two rate-limit gauges by default. Flip this on to swipe
    // through the spend and token breakdowns as well.
    extraCards: process.env.CUBE_EXTRA_CARDS === '1' || file.extraCards === true,
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

let payload = buildPayload([], { source: source.label, error: 'starting up' });
let lastError = null;
let lastOk = 0;

async function refresh() {
  // The gauges come from the usage endpoint and the sparkline from the local
  // transcripts; neither should be able to take the other down, so the limits
  // are fetched independently and report their own failure on the card.
  const limits = await getUsageLimits();
  const common = {
    source: source.label,
    hasRequestCounts: source.hasRequestCounts !== false,
    billed: source.billed ?? null,
    extraCards: cfg.extraCards,
    limits,
  };
  try {
    // Transcript scanning now only feeds the optional spend cards, so the
    // default two-gauge deck never touches ~/.claude/projects at all.
    const events = cfg.extraCards ? await source.refresh() : [];
    payload = buildPayload(events, common);
    lastError = null;
    lastOk = Date.now();
  } catch (err) {
    lastError = err.message ?? String(err);
    console.error('refresh failed:', lastError);
    payload = buildPayload(source.events ?? [], { ...common, error: lastError });
  }
}

await refresh();
setInterval(refresh, cfg.refreshMs).unref?.();

const send = (res, code, body, type = 'application/json') => {
  // An explicit Content-Length keeps the response un-chunked. The device has
  // to parse this with a very small HTTP client, so one framing less to handle.
  const buf = Buffer.from(body);
  res.writeHead(code, {
    'content-length': buf.length,
    'content-type': type,
    'cache-control': 'no-store',
    'access-control-allow-origin': '*',
  });
  res.end(buf);
};

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');
  if (url.pathname === '/api/status') return send(res, 200, JSON.stringify(payload));
  if (url.pathname === '/health') {
    return send(
      res,
      lastError ? 503 : 200,
      JSON.stringify({ ok: !lastError, source: source.label, lastError, lastOk }),
    );
  }
  if (url.pathname === '/' || url.pathname === '/index.html') {
    const html = await readFile(path.join(here, 'preview.html'), 'utf8');
    return send(res, 200, html, 'text/html; charset=utf-8');
  }
  send(res, 404, JSON.stringify({ error: 'not found' }));
});

server.listen(cfg.port, () => {
  console.log(`claude-status-cube bridge  source=${source.label}  refresh=${cfg.refreshMs}ms`);
  console.log(`  preview   http://localhost:${cfg.port}/`);
  for (const ip of lanAddresses()) {
    console.log(`  device    http://${ip}:${cfg.port}/api/status`);
  }
});

// Printed at startup so you can paste the right LAN address into config.h.
function lanAddresses() {
  return Object.values(networkInterfaces())
    .flat()
    .filter((n) => n && n.family === 'IPv4' && !n.internal)
    .map((n) => n.address);
}
