// Generated once from the Node bridge at commit 041dc90; the JSON it wrote is now
// the reference and the Node bridge is gone, so this script no longer runs (it
// imports the deleted cards.mjs). Kept as a record of how the cases were made.
//
// Golden cases for the Go card builder: runs the Node one (cards.mjs) on fixed
// inputs and prints [{name, now, events, opts, want}] as JSON.
//
//   TZ=UTC node gen.mjs > cases-utc.json
//   TZ=Asia/Ho_Chi_Minh node gen.mjs > cases-hcm.json
//
// Deterministic: every clock read is pinned to the case's `now`.
import { buildPayload } from '../../../cards.mjs';

const Real = Date;
function at(now, fn) {
  globalThis.Date = class extends Real {
    constructor(...a) { super(...(a.length ? a : [now])); }
    static now() { return now; }
  };
  try { return fn(); } finally { globalThis.Date = Real; }
}

const MIN = 60_000;
const H = 60 * MIN;
const D = 24 * H;
const NOW = Date.UTC(2026, 9, 24, 15, 30);
const iso = (ms) => new Real(ms).toISOString();

const win = (utilization, resetsIn, now = NOW) => ({ utilization, resets_at: iso(now + resetsIn) });
const good = { five_hour: win(72, 2.5 * H), seven_day: win(31, 80 * H) };

// One usage event; `t` is an offset from `now` (negative = in the past).
let n = 0;
const ev = (dt, model, project, { input = 0, cacheRead = 0, cacheWrite = 0, output = 0, cost = 0 } = {}, now = NOW) => ({
  t: now + dt, model, project, session: `s${++n}`,
  input, cacheRead, cacheWrite, output, webSearch: 0, cost,
});

const OPUS = 'claude-opus-5-5';
const HAIKU = 'claude-haiku-4-5-20251001';

// A month of events: today (two models plus one with no model, two projects
// plus one with none), yesterday, earlier this month, last month. The last
// one in the array is 90 s old.
const mixed = (now = NOW) => [
  ev(-40 * D, OPUS, 'alpha', { input: 10_000, output: 2_000, cost: 100 }, now),
  ev(-21 * D, OPUS, 'alpha', { input: 50_000, cacheRead: 200_000, output: 9_000, cost: 20 }, now),
  ev(-1 * D, HAIKU, 'beta', { input: 3_000, output: 500, cost: 5 }, now),
  ev(-5 * H, HAIKU, 'beta', { input: 4_000, cacheRead: 20_000, cacheWrite: 6_000, output: 800, cost: 0.3 }, now),
  ev(-3 * H, null, null, { input: 100, output: 50, cost: 0.1 }, now),
  ev(-2 * H, OPUS, 'alpha', { input: 12_000, cacheRead: 150_000, cacheWrite: 30_000, output: 4_000, cost: 1.2 }, now),
  ev(-90_000, HAIKU, 'alpha', { input: 1_000, cacheRead: 5_000, output: 200, cost: 0.05 }, now),
];

const cases = [
  // usage
  ['usage: 72%/31%', { limits: good }],
  ['usage: 72.5 rounds up', { limits: { five_hour: win(72.5, 2.5 * H), seven_day: win(31, 80 * H) } }],
  ['usage: 59', { limits: { five_hour: win(59, H), seven_day: win(59, 80 * H) } }],
  ['usage: 60', { limits: { five_hour: win(60, H), seven_day: win(60, 80 * H) } }],
  ['usage: 84', { limits: { five_hour: win(84, H), seven_day: win(84, 80 * H) } }],
  ['usage: 85', { limits: { five_hour: win(85, H), seven_day: win(85, 80 * H) } }],
  ['usage: 5h lapsed, 7d live', { limits: { five_hour: win(90, -MIN), seven_day: win(31, 80 * H) } }],
  ['usage: 7d lapsed', { limits: { five_hour: win(10, H), seven_day: win(40, -5 * MIN) } }],
  ['usage: resets_at missing', { limits: { five_hour: { utilization: 40 }, seven_day: { utilization: 20, resets_at: null } } }],
  ['usage: resets_at garbage', { limits: { five_hour: { utilization: 40, resets_at: 'soon' }, seven_day: win(20, 80 * H) } }],
  ['usage: endpoint timestamp format', {
    limits: {
      five_hour: { utilization: 12, resets_at: '2026-10-24T18:00:00.123456+00:00' },
      seven_day: { utilization: 3, resets_at: '2026-10-30T09:59:59.999999+07:00' },
    },
  }],
  ...['no Claude Code login found', 'starting up', 'usage endpoint timeout', 'usage endpoint 429',
    'usage endpoint bad JSON', 'ECONNRESET'].map((e) => [`usage: error ${e}`, { limits: { five_hour: null, seven_day: null, error: e } }]),
  ['usage: error over a last good reading', { limits: { ...good, error: 'usage endpoint 503' } }],
  ['usage: utilization is a string', { limits: { five_hour: { utilization: '12', resets_at: iso(NOW + H) }, seven_day: win(31, 80 * H) } }],
  ['usage: only 7d', { limits: { five_hour: null, seven_day: win(31, 80 * H) } }],
  ['usage: no limits at all', {}],
  ['usage: countdown 47m', { limits: { five_hour: win(5, 47 * MIN), seven_day: win(5, 80 * H) } }],
  ['usage: countdown 4h 05m', { limits: { five_hour: win(5, 4 * H + 5 * MIN), seven_day: win(5, 80 * H) } }],
  ['usage: countdown 16h', { limits: { five_hour: win(5, 16 * H + 20 * MIN), seven_day: win(5, 80 * H) } }],
  ['usage: countdown 2d 3h', { limits: { five_hour: win(5, 2 * D + 3 * H + 10 * MIN), seven_day: win(5, 80 * H) } }],
  ['usage: countdown rounds half a minute up', { limits: { five_hour: win(5, 59 * MIN + 30_000), seven_day: win(5, 80 * H) } }],

  // mail
  ['mail: unread null', { limits: good, mail: { unread: null, stale: false, error: 'IMAP login failed' } }],
  ['mail: 3 stale', { limits: good, mail: { unread: 3, stale: true } }],
  ['mail: 0', { limits: good, mail: { unread: 0, stale: false } }],
  ['mail: 1234', { limits: good, mail: { unread: 1234, stale: false } }],

  // extra cards
  ['extra: mixed, estimate', { limits: good, extraCards: true, events: mixed() }],
  ['extra: billed, today present', {
    limits: good, extraCards: true, events: mixed(),
    billed: { byDay: { '2026-10-23': 3, '2026-10-24': 7.5 }, total: 150 },
  }],
  ['extra: billed, today absent', {
    limits: good, extraCards: true, events: mixed(),
    billed: { byDay: { '2026-10-20': 2 }, total: 1500 },
  }],
  ['extra: admin, no request counts', {
    limits: good, extraCards: true, source: 'admin', hasRequestCounts: false, events: mixed(),
    billed: { byDay: { '2026-10-24': 0.5 }, total: 15000 },
  }],
  ['extra: admin, one model', {
    limits: good, extraCards: true, source: 'admin', hasRequestCounts: false,
    events: [ev(-H, OPUS, 'Default', { input: 1, cost: 0.5 })],
  }],
  ['extra: no events', { limits: good, extraCards: true, events: [] }],
  ['extra: one request, $0.50', { extraCards: true, events: [ev(-H, OPUS, 'alpha', { input: 999, cost: 0.5 })] }],
  ['extra: tokens 2500 -> 3K', { extraCards: true, events: [ev(-H, OPUS, 'alpha', { input: 2_000, output: 500, cost: 0.01 })] }],
  ['extra: tokens 1250000 -> 1.3M', {
    extraCards: true,
    events: [ev(-H, OPUS, 'alpha', { input: 250_000, cacheRead: 900_000, cacheWrite: 50_000, output: 50_000, cost: 3 })],
  }],
  ['extra: tokens in billions', {
    extraCards: true,
    events: [ev(-H, OPUS, 'alpha', { input: 500_000_000, cacheRead: 2_000_000_000, output: 1_000_000, cost: 1234.5678 })],
  }],
  ['extra: model tie keeps first seen', {
    extraCards: true,
    events: [
      ev(-3 * H, HAIKU, 'beta', { input: 10, cost: 2 }),
      ev(-2 * H, OPUS, 'alpha', { input: 10, cost: 2 }),
      ev(-H, 'claude-3-5-haiku', 'gamma', { input: 10, cost: 1 }),
    ],
  }],
  ['extra: odd model names', {
    extraCards: true,
    events: [
      ev(-3 * H, 'claude-opus-4-0', 'a', { cost: 3 }),
      ev(-2 * H, 'claude-sonnet-4-20250514', 'b', { cost: 2 }),
      ev(-H, 'gpt-x', 'c', { cost: 1 }),
    ],
  }],
  ['extra: odd model names 2', {
    extraCards: true,
    events: [ev(-2 * H, '<synthetic>', 'a', { cost: 3 }), ev(-H, 'claude-3-haiku-20240307', 'b', { cost: 1 })],
  }],
  ['extra: last event 2h ago', { extraCards: true, events: [ev(-2 * H, OPUS, 'alpha', { input: 10, cost: 0.2 })] }],
  ['extra: last event 3d ago', { extraCards: true, events: [ev(-3 * D, OPUS, 'alpha', { input: 10, cost: 0.2 })] }],
  ['extra: cache hit 90%', {
    extraCards: true,
    events: [ev(-H, OPUS, 'alpha', { input: 100, cacheRead: 900, cacheWrite: 100, output: 10, cost: 0.1 })],
  }],
  ['extra: zero cost today', { extraCards: true, events: [ev(-H, OPUS, 'alpha', { input: 100, cost: 0 })] }],

  // error card
  ['error: 60 characters', { limits: good, error: 'admin source: connection refused by api.anthropic.com:443'.padEnd(60, '!') }],

  // fit
  ['fit: project name', {
    extraCards: true, events: [ev(-H, OPUS, 'Café · résumé — “quoted” dự-án đ…×', { input: 10, cost: 0.2 })],
  }],
  ['fit: usage error text', { limits: { five_hour: null, seven_day: null, error: 'Café · résumé — “quoted” dự-án đ…×' } }],
  ['fit: whitespace and non-BMP', { limits: { five_hour: null, seven_day: null, error: 'a\tb c d﻿e\u0085f 😀 g　h  ' } }],
];

if (process.env.TZ === 'Asia/Ho_Chi_Minh') {
  // 00:30 local on the 25th, still the 24th in UTC.
  const late = Date.UTC(2026, 9, 24, 17, 30);
  cases.push(['hcm: just past local midnight', {
    now: late, limits: good, extraCards: true, events: mixed(late),
    billed: { byDay: { '2026-10-24': 7.5, '2026-10-25': 0.25 }, total: 150 },
  }]);
}

const out = cases.map(([name, c]) => {
  const now = c.now ?? NOW;
  const { now: _, events = [], ...rest } = c;
  const opts = {
    source: 'local', hasRequestCounts: true, billed: null, error: null,
    limits: null, mail: null, extraCards: false, ...rest,
  };
  const call = { ...opts, billed: opts.billed && { ...opts.billed, byDay: new Map(Object.entries(opts.billed.byDay)) } };
  const want = at(now, () => buildPayload(events, call));
  return { name, now, events, opts, want };
});

console.log(JSON.stringify(out, null, 1));
