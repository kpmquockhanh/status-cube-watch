// Turns usage data into the card deck the device renders.
//
// All formatting happens here, on the host: the firmware just draws whatever
// strings it is handed. That means you can redesign the dashboard by editing
// this file and reloading -- no reflash.
//
// The deck is deliberately short. The only numbers that actually change what
// you do next are the two rate-limit windows, so those share ONE card (usageCard
// below) with the unread-mail count tucked into it. A card carrying a `g` (a
// whole percentage) is drawn as a ring: `g` fills the arc, `v` sits in the
// middle of it, `s1` captions `v`, and `s2` labels the percentage in the gap at
// the bottom. A `g2` adds a second, inner ring (see usageCard). A card without
// `g` keeps the old big-number layout. The spend and token breakdowns are still computed below
// and are one config flag away (`extraCards`), but they are off by default.

const money = (usd) => {
  if (usd >= 1000) return `$${(usd / 1000).toFixed(usd >= 10_000 ? 0 : 1)}k`;
  if (usd >= 100) return `$${usd.toFixed(0)}`;
  return `$${usd.toFixed(2)}`;
};

const tokens = (n) => {
  if (n >= 1e9) return `${(n / 1e9).toFixed(1)}B`;
  if (n >= 1e6) return `${(n / 1e6).toFixed(1)}M`;
  if (n >= 1e3) return `${(n / 1e3).toFixed(0)}K`;
  return String(n);
};

const ago = (ms) => {
  const s = Math.max(0, Math.round(ms / 1000));
  if (s < 60) return `${s}s ago`;
  if (s < 3600) return `${Math.round(s / 60)}m ago`;
  if (s < 86400) return `${Math.round(s / 3600)}h ago`;
  return `${Math.round(s / 86400)}d ago`;
};

// "47m" / "4h 12m" / "16h" / "2d 3h". Precision drops as the horizon grows:
// minutes matter with a couple of hours left on the session, and are noise
// with a day left on the week. It also keeps the string inside the ring --
// the middle of the dial fits about six characters at the design's size.
const until = (ms) => {
  const m = Math.max(0, Math.round(ms / 60_000));
  if (m < 60) return `${m}m`;
  const h = Math.floor(m / 60);
  // Zero-padded so the countdown does not jitter in width as it ticks down.
  if (h < 10) return `${h}h ${String(m % 60).padStart(2, '0')}m`;
  if (h < 24) return `${h}h`;
  return `${Math.floor(h / 24)}d ${h % 24}h`;
};

const prettyModel = (id) =>
  (id ?? 'unknown')
    .replace(/^claude-/, '')
    .replace(/-(\d)-(\d)$/, ' $1.$2')
    .replace(/-(\d)$/, ' $1')
    .replace(/^(\w)/, (c) => c.toUpperCase());

function totals(events) {
  const t = { cost: 0, input: 0, cacheRead: 0, cacheWrite: 0, output: 0, count: 0 };
  for (const e of events) {
    t.cost += e.cost;
    t.input += e.input;
    t.cacheRead += e.cacheRead;
    t.cacheWrite += e.cacheWrite;
    t.output += e.output;
    t.count++;
  }
  t.allTokens = t.input + t.cacheRead + t.cacheWrite + t.output;
  t.inTokens = t.input + t.cacheRead + t.cacheWrite;
  return t;
}

function topBy(events, key, metric = (e) => e.cost) {
  const m = new Map();
  for (const e of events) {
    const k = e[key] ?? 'unknown';
    m.set(k, (m.get(k) ?? 0) + metric(e));
  }
  return [...m.entries()].sort((a, b) => b[1] - a[1]);
}

// Both rate-limit windows -> ONE card of two concentric rings: the outer ring
// is the 5h session (`g`, `c`), the inner one the 7d week (`g2`, `c2`). The
// middle of the dials shows how long until the session comes back, and `rows`
// are the two legend lines under them. `five` / `seven` are the
// {utilization, resets_at} objects straight off the usage endpoint; null means
// we could not read it, in which case the device draws an empty track rather
// than a zeroed arc -- an unfilled ring reads as a legible nothing, a missing
// ring reads as a bug.
const tone = (pct) => (pct >= 85 ? 'red' : pct >= 60 ? 'amber' : 'green');

function windowView(window, now) {
  if (!window || typeof window.utilization !== 'number') return null;
  const pct = Math.round(window.utilization);
  const resetsAt = Date.parse(window.resets_at ?? '');
  const known = Number.isFinite(resetsAt);
  // A reset already in the past means the window lapsed and you are starting
  // clean, which is worth saying outright rather than counting down to zero.
  const lapsed = known && resetsAt <= now;
  return { pct, known, lapsed, left: known ? (lapsed ? 'idle' : until(resetsAt - now)) : '--' };
}

// Unread inbox count, shown as a number beside an envelope in the gap of the
// rings. `mail.error` with no earlier count means we have never had a reading,
// so show "--" in red rather than a zero. Returns {} when mail is not set up.
function mailBadge(mail) {
  if (!mail) return {};
  if (mail.unread == null) return { m: '--', mc: 'red' };
  return {
    m: mail.unread.toLocaleString('en-US'),
    mc: mail.stale ? 'red' : mail.unread === 0 ? 'green' : 'amber',
  };
}

function usageCard(limits, mail, now) {
  const five = windowView(limits?.five_hour, now);
  const seven = windowView(limits?.seven_day, now);
  const row = (k, w) => ({ k, p: w ? `${w.pct}%` : '--', r: w && w.known ? w.left : '' });
  return {
    t: 'CLAUDE',
    v: five ? five.left : '--',
    s1: !five ? 'no data' : !five.known ? '' : five.lapsed ? 'reset' : 'to reset',
    s2: !five && !seven ? String(limits?.error ?? 'unavailable').slice(0, 39) : '',
    c: five ? tone(five.pct) : 'ink',
    g: five ? five.pct : -1,
    c2: seven ? tone(seven.pct) : 'ink',
    g2: seven ? seven.pct : -1,
    rows: [row('5H', five), row('7D', seven)],
    ...mailBadge(mail),
  };
}

export function buildPayload(events, opts = {}) {
  const { source = 'local', hasRequestCounts = true, billed = null, error = null,
          limits = null, mail = null, extraCards = false } = opts;
  const now = Date.now();

  const cards = [usageCard(limits, mail, now)];

  if (extraCards) cards.push(...spendCards(events, { source, hasRequestCounts, billed, now }));

  if (error) {
    cards.unshift({ t: 'BRIDGE ERROR', v: 'offline', s1: String(error).slice(0, 28), s2: '', c: 'red' });
  }

  return {
    v: 1,
    ts: Math.floor(now / 1000),
    src: source,
    // The gauges come straight from the server, so nothing on screen is an
    // estimate unless the extra spend cards are switched on.
    est: !limits?.five_hour || extraCards,
    cards,
  };
}

// The original spend deck. Off by default; set `extraCards: true` in
// bridge/config.json to swipe through these as well.
function spendCards(events, { source, hasRequestCounts, billed, now }) {
  const startOfToday = new Date();
  startOfToday.setHours(0, 0, 0, 0);
  const startOfMonth = new Date();
  startOfMonth.setDate(1);
  startOfMonth.setHours(0, 0, 0, 0);

  const today = events.filter((e) => e.t >= startOfToday.getTime());
  const month = events.filter((e) => e.t >= startOfMonth.getTime());
  const dayT = totals(today);
  const monthT = totals(month);

  // The cost report is billing truth; prefer it over our estimate when present.
  let monthCost = monthT.cost;
  let todayCost = dayT.cost;
  if (billed) {
    monthCost = billed.total;
    const key = new Date().toISOString().slice(0, 10);
    if (billed.byDay.has(key)) todayCost = billed.byDay.get(key);
  }

  // Straight-line projection from the month so far.
  const daysIn = Math.max(1, (now - startOfMonth.getTime()) / 86_400_000);
  const daysInMonth = new Date(
    startOfMonth.getFullYear(),
    startOfMonth.getMonth() + 1,
    0,
  ).getDate();
  const projected = (monthCost / daysIn) * daysInMonth;

  const cacheTotal = dayT.cacheRead + dayT.cacheWrite;
  const cacheHit = cacheTotal > 0 ? Math.round((dayT.cacheRead / cacheTotal) * 100) : 0;

  const modelRank = topBy(today, 'model');
  const projectRank = topBy(today, 'project');
  const last = events.length ? events[events.length - 1] : null;

  const reqLine = hasRequestCounts
    ? `${dayT.count} request${dayT.count === 1 ? '' : 's'}`
    : `${modelRank.length} model${modelRank.length === 1 ? '' : 's'}`;

  return [
    { t: 'TODAY', v: money(todayCost), s1: `${tokens(dayT.allTokens)} tokens`, s2: reqLine, c: 'accent' },
    {
      t: 'THIS MONTH',
      v: money(monthCost),
      s1: `proj. ${money(projected)}`,
      s2: `day ${Math.ceil(daysIn)} of ${daysInMonth}`,
      c: 'blue',
    },
    {
      t: 'TOKENS TODAY',
      v: tokens(dayT.allTokens),
      s1: `in ${tokens(dayT.inTokens)} / out ${tokens(dayT.output)}`,
      s2: `cache hit ${cacheHit}%`,
      c: cacheHit >= 80 ? 'green' : 'amber',
    },
    {
      t: 'TOP MODEL',
      v: modelRank.length ? prettyModel(modelRank[0][0]) : '--',
      s1: modelRank.length
        ? `${money(modelRank[0][1])} · ${Math.round((modelRank[0][1] / (dayT.cost || 1)) * 100)}% of spend`
        : 'no usage today',
      s2: modelRank[1] ? `then ${prettyModel(modelRank[1][0])}` : 'only model today',
      c: 'violet',
    },
    {
      t: source === 'admin' ? 'TOP WORKSPACE' : 'TOP PROJECT',
      v: projectRank.length ? projectRank[0][0].slice(0, 23) : '--',
      s1: projectRank.length ? money(projectRank[0][1]) : 'idle',
      s2: last ? `active ${ago(now - last.t)}` : 'no activity',
      c: 'green',
    },
  ];
}
