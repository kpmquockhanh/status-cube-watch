// The bridge's web board: every agent the bridge tracks, its detail, and its
// prompt to answer, laid out as agents-deck's web dashboard
// (backend/internal/web/static/app.js @ 699bba8; the critter is its own,
// critter.js). Vanilla ES module, CSP-safe: no inline handlers, no eval, no
// style attributes, only CSSOM properties.
//
// Data: GET /api/board (only for this page opened on the Mac itself, at
// http://127.0.0.1:<port>/board/), refetched at once when GET /api/changes
// answers 204 and every 5 s for the ages. Every word is the bridge's
// (internal/cards: tile lines, detail fields, prompt text); the board only
// counts tiles and draws strings, as the cube does.

import { spriteSVG, HAT_COLORS, HAT_SHAPES, HAT_COLOR_NAMES } from './critter.js';

const $ = (id) => document.getElementById(id);

let board = null;       // last /api/board
let selectedId = null;  // agent in the detail panel
let pickerOpen = false; // its hat picker
let online = false;
const sending = new Map(); // prompt id -> { key, status: 'sending' | 'sent' }

// ---------------------------------------------------------------- dom helpers
function h(tag, attrs = {}, ...kids) {
  const el = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v == null || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const kid of kids.flat()) if (kid != null && kid !== false) el.append(kid);
  return el;
}
const setText = (el, t) => { if (el.textContent !== t) el.textContent = t; };
const setSprite = (el, pose, hat = null, opts = {}) => {
  const key = `${pose}|${hat ? `${hat.shape}/${hat.color}` : ''}|${opts.bubble === false ? 'nb' : ''}`;
  if (el.dataset.pose !== key) { el.dataset.pose = key; el.innerHTML = spriteSVG(pose, 'claude', hat, opts); }
};

// ---------------------------------------------------------------- the payload's words
// k (run|wait|idle|err|exit|start) as agents-deck's status class.
const STATUS = { run: 'running', wait: 'waiting', idle: 'idle', err: 'error', exit: 'exited', start: 'starting' };
const statusOf = (a) => STATUS[a.k] || 'idle';
const hungry = (a) => a.p === 'hungry';
// h = shape * 8 + colour, in agents-deck's order (cards.HatOf); absent = no hat.
const hatOf = (code) => Number.isInteger(code) && code >= 0 && code < HAT_SHAPES.length * 8
  ? { shape: HAT_SHAPES[code >> 3], color: HAT_COLOR_NAMES[code & 7] } : null;
const titleOf = (a) => a.d?.t || a.n;

// ---------------------------------------------------------------- api
async function api(path, body) {
  try {
    const r = await fetch(path, {
      method: body === undefined ? 'GET' : 'POST',
      headers: { 'X-Cube-Client': '1', ...(body === undefined ? {} : { 'Content-Type': 'application/json' }) },
      body: body === undefined ? undefined : JSON.stringify(body),
      cache: 'no-store',
    });
    if (r.ok) return { ok: true, status: r.status };
    const j = await r.json().catch(() => ({}));
    return { ok: false, status: r.status, error: j.error || `http ${r.status}` };
  } catch {
    return { ok: false, status: 0, error: 'bridge not answering' };
  }
}

// ---------------------------------------------------------------- tiles (keyed)
const tiles = new Map(); // id -> { el, spr, name, meta, subs, ring, bar, ... }

// The tile border doubles as a context gauge: filled clockwise from the top centre.
const RING_D = 'M50 1H99V99H1V1Z';
function makeRing() {
  const ns = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(ns, 'svg');
  svg.setAttribute('class', 'ring'); svg.setAttribute('viewBox', '0 0 100 100');
  svg.setAttribute('aria-hidden', 'true');
  const path = (cls) => {
    const p = document.createElementNS(ns, 'path');
    p.setAttribute('class', cls); p.setAttribute('d', RING_D); p.setAttribute('pathLength', '100');
    svg.append(p); return p;
  };
  path('rt');
  const fill = path('rf');
  fill.setAttribute('stroke-dasharray', '0 100');
  return { svg, fill };
}

function makeTile(a) {
  const spr = h('span', { class: 'spr' });
  const name = h('span', { class: 'name' });
  const meta = h('span', { class: 'meta' });
  const ring = makeRing();
  const subs = h('span', { class: 'subs', 'aria-hidden': 'true', hidden: true });
  const bar = h('span', { class: 'ctxbar', 'aria-hidden': 'true' }); // the ring, as a bar, on phones
  const chip = h('span', { class: 'chip', 'aria-hidden': 'true' });
  // A running tile's ground: the dot matrix and its wave (::before, ::after)
  // and four rising specks (board.css .fx).
  const fx = h('span', { class: 'fx', 'aria-hidden': 'true' }, h('i'), h('i'), h('i'), h('i'));
  const el = h('button', { type: 'button', class: 'tile', 'data-id': a.i }, fx, chip, subs, spr, name, meta, ring.svg, bar);
  el.addEventListener('click', () => openDetail(a.i));
  return { el, spr, name, meta, subs, bar, ring: ring.fill, subsKey: null, dash: '', cls: '', label: '' };
}

function updateTile(t, a) {
  const st = statusOf(a);
  const cls = `tile st-${st} tool-claude` + (hungry(a) ? ' hungry' : '') + (a.i === selectedId ? ' selected' : '');
  if (t.cls !== cls) { t.el.className = cls; t.cls = cls; }
  setSprite(t.spr, a.p, hatOf(a.h));
  const dash = `${a.x} 100`;
  if (t.dash !== dash) { t.ring.setAttribute('stroke-dasharray', dash); t.bar.style.width = `${a.x}%`; t.dash = dash; }
  setText(t.name, titleOf(a));
  setText(t.meta, a.l);
  // Subagents at work: a small critter in the top-left corner (+ the bridge's "×N").
  const subsKey = a.s ?? null;
  if (t.subsKey !== subsKey) {
    t.subsKey = subsKey;
    t.subs.hidden = subsKey === null;
    if (subsKey !== null) {
      const m = h('span', { class: 'subs-spr' });
      setSprite(m, 'run');
      t.subs.replaceChildren(m, subsKey ? h('span', { class: 'subs-n', text: subsKey }) : '');
    }
  }
  const label = `${titleOf(a)}, ${st === 'waiting' ? 'needs you' : st}: ${a.l}${a.d?.n ? `, ${a.d.n}` : ''}`;
  if (t.label !== label) { t.el.setAttribute('aria-label', label); t.label = label; }
  if (a.i === selectedId) t.el.setAttribute('aria-expanded', 'true'); else t.el.removeAttribute('aria-expanded');
}

function renderGrid() {
  const grid = $('grid');
  const list = board?.agents || [];
  const seen = new Set();
  list.forEach((a, i) => {
    seen.add(a.i);
    let t = tiles.get(a.i);
    if (!t) { t = makeTile(a); tiles.set(a.i, t); }
    updateTile(t, a);
    if (grid.children[i] !== t.el) grid.insertBefore(t.el, grid.children[i] || null);
  });
  for (const [id, t] of tiles) if (!seen.has(id)) { t.el.remove(); tiles.delete(id); }
  if (board) showEmpty(list.length ? '' : 'no agents running · start claude in a terminal');
}

function showEmpty(text) {
  $('empty').hidden = !text;
  $('grid').hidden = !!text;
  if (text) {
    setText($('empty-text'), text);
    setSprite($('empty-art'), 'idle');
  }
}

function renderTop() {
  const n = { running: 0, waiting: 0, idle: 0, error: 0 };
  for (const a of board?.agents || []) {
    const st = statusOf(a);
    if (st in n) n[st]++;
  }
  for (const k of Object.keys(n)) setText($(`n-${k}`), String(n[k]));
  $('c-waiting').classList.toggle('hot', n.waiting > 0);
  $('c-error').classList.toggle('hot', n.error > 0);
  document.title = n.waiting ? `(${n.waiting}) agents` : 'agents';
}

// ---------------------------------------------------------------- detail
const panel = $('detail');
let det = null; // { key, update(a), answer?(key) }

function openDetail(id) {
  if (selectedId !== id) pickerOpen = false;
  selectedId = id;
  render();
}

function closeDetail() {
  selectedId = null; pickerOpen = false; det = null;
  panel.hidden = true; panel.replaceChildren();
  render();
}

const closeIcon = () => {
  const s = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  s.setAttribute('viewBox', '0 0 12 12'); s.setAttribute('aria-hidden', 'true');
  s.innerHTML = '<path d="M1 1L11 11M11 1L1 11" stroke="currentColor" stroke-width="1.6" fill="none"/>';
  return s;
};

// The header: critter (opens the hat picker), title, subagents, status pill, close.
function header(a) {
  const spr = h('button', { type: 'button', class: 'd-spr d-spr-btn', 'aria-label': 'change hat', 'aria-expanded': String(pickerOpen) });
  spr.addEventListener('click', () => { pickerOpen = !pickerOpen; render(); });
  const title = h('h2', { class: 'd-title', id: 'd-title', tabindex: '-1' });
  const subs = h('span', { class: 'lbl' });
  const pill = h('span', { class: 'pill' });
  const close = h('button', { type: 'button', class: 'd-close', 'aria-label': 'close (esc)' }, closeIcon());
  close.addEventListener('click', closeDetail);
  const picker = hatPicker(a);
  const el = h('div', { class: 'd-head' },
    h('div', { class: 'd-top' },
      h('div', { class: 'd-id' }, spr, title, subs),
      h('div', { class: 'd-right' }, close, h('div', { class: 'd-pillrow' }, h('span', { class: 'chip tool-claude' }), pill))),
    picker.el);
  const update = (x) => {
    setSprite(spr, x.p, hatOf(x.h));
    setText(title, titleOf(x));
    setText(subs, x.d?.n || '');
    subs.hidden = !x.d?.n;
    setText(pill, x.d?.s || x.l);
    picker.update(x);
  };
  update(a);
  return { el, update };
}

// The hat picker: the 13 shapes in the current colour, none, automatic, and
// the 8 colours (inert while the session wears none), as the cube's picker.
function hatPicker(a) {
  const el = h('div', { class: 'hatpick', hidden: !pickerOpen });
  const note = h('span', { class: 'hatpick-note', role: 'status' });
  let cur = null;
  const pick = async (hat) => {
    setText(note, '');
    const r = await api('/api/hat', { agent: a.i, hat });
    if (!r.ok) setText(note, `hat · ${r.error}`);
    else load();
  };
  const render = (x) => {
    const code = Number.isInteger(x.h) ? x.h : -1;
    const auto = x.d?.ha === 1;
    const hat = hatOf(code);
    const color = hat ? code & 7 : 7;
    const key = `${code}|${auto}`;
    if (key === cur) return;
    cur = key;
    const shapes = HAT_SHAPES.map((shape, i) => {
      const on = !auto && hat?.shape === shape;
      const b = h('button', { type: 'button', class: 'hatpick-shape' + (on ? ' on' : ''), title: shape, 'aria-label': `hat: ${shape}`, 'aria-pressed': String(on) });
      setSprite(b, 'idle', { shape, color: HAT_COLOR_NAMES[color] }, { bubble: false });
      b.addEventListener('click', () => pick(i * 8 + color));
      return b;
    });
    const colors = HAT_COLOR_NAMES.map((name, i) => {
      const on = !auto && hat && color === i;
      const b = h('button', { type: 'button', class: 'hatpick-color' + (on ? ' on' : ''), title: name, 'aria-label': `colour: ${name}`, disabled: !hat });
      b.style.background = HAT_COLORS[name][0];
      b.addEventListener('click', () => pick((code >> 3) * 8 + i));
      return b;
    });
    const none = h('button', { type: 'button', class: 'toggle', text: 'no hat', 'aria-pressed': String(!auto && !hat) });
    none.addEventListener('click', () => pick(-1));
    const autoBtn = h('button', { type: 'button', class: 'toggle', text: 'automatic', 'aria-pressed': String(auto) });
    autoBtn.addEventListener('click', () => pick(null));
    el.replaceChildren(
      h('div', { class: 'hatpick-sub', text: 'this session only · the cube shows it too' }),
      h('div', { class: 'hatpick-shapes' }, shapes),
      h('div', { class: 'hatpick-colors' }, colors),
      h('div', { class: 'hatpick-foot' }, none, autoBtn, note));
  };
  render(a);
  return { el, update: render };
}

// Everything but a prompt: doing now (or the error), context, last prompt, meta.
function buildInfo(a) {
  const hd = header(a);
  const doing = h('span', { class: 'val' });
  const ctx = h('span', { class: 'h' });
  const on = h('span', { class: 'on' });
  const ticks = h('div', { class: 'ticks', 'aria-hidden': 'true' }, on, h('span', { class: 'off' }));
  const ctxSec = h('div', { class: 'sec' }, h('div', { class: 'ctx-head' }, h('span', { class: 'lbl', text: 'context' }), ctx), ticks);
  const last = h('span', { class: 'val' });
  const lastSec = h('div', { class: 'sec' }, h('span', { class: 'lbl', text: 'last prompt' }), last);
  const doingLbl = h('span', { class: 'lbl' });
  const foot = h('div', { class: 'd-foot' });
  panel.replaceChildren(hd.el, h('div', { class: 'sec' }, doingLbl, doing), ctxSec, lastSec, h('div', { class: 'grow' }), foot);
  const update = (x) => {
    const st = statusOf(x);
    panel.className = 'detail' + (st === 'error' ? ' p-error' : st === 'idle' || st === 'exited' || st === 'starting' ? ' p-dim' : '');
    hd.update(x);
    const d = x.d || {};
    setText(doingLbl, d.e ? 'error' : 'doing now');
    setText(doing, d.e || d.w || x.l);
    doing.classList.toggle('err', !!d.e);
    setText(ctx, d.c || 'no context yet');
    ticks.style.setProperty('--pct', String(x.x));
    ticks.classList.toggle('hot', x.x >= 85);
    setText(last, d.p || '—');
    lastSec.hidden = !d.p;
    setText(foot, d.m || '');
  };
  update(a);
  return update;
}

// A waiting agent: its prompt, answered with POST /api/answer. One answer per
// prompt id; the bridge refuses (409) a key for a prompt that moved on.
function buildPrompt(a) {
  const q = a.q;
  const hd = header(a);
  const note = h('div', { class: 'note', role: 'status', 'aria-live': 'polite' });
  const opts = [];
  const isQ = q.k === 'question';
  let selected = q.o?.[0]?.k;
  const why = q.x === 'tmux' ? 'not in tmux · answer at the terminal' : '';

  const setBusy = (busy, sentKey) => {
    for (const o of opts) { o.el.disabled = busy || !!why; o.el.classList.toggle('sent', o.key === sentKey); }
    if (sendBtn) sendBtn.disabled = busy || !!why;
  };

  const answer = async (key) => {
    if (why || sending.get(q.p)) return;
    sending.set(q.p, { key, status: 'sending' });
    setBusy(true, null);
    note.className = 'note'; note.textContent = `sending ${key}…`;
    const r = await api('/api/answer', { agent: q.i, prompt: q.p, key });
    if (r.ok) {
      sending.set(q.p, { key, status: 'sent' });
      setBusy(true, key);
      note.textContent = `sent ${key} · waiting for the agent…`;
      load();
      return;
    }
    sending.delete(q.p);
    setBusy(false, null);
    note.className = 'note bad';
    note.textContent = r.status === 409 && r.error !== 'not in tmux' ? 'prompt changed · showing the latest' : `not sent · ${r.error}`;
    load();
  };

  const optList = h('div', { class: 'opts', role: isQ ? 'radiogroup' : 'group', 'aria-labelledby': 'p-title' });
  for (const o of q.o || []) {
    const el = h('button', { type: 'button', class: 'opt' + (o.k === q.o[0].k && !isQ ? ' primary' : ''), 'aria-keyshortcuts': o.k,
      ...(isQ ? { role: 'radio', 'aria-checked': String(o.k === selected) } : {}) },
      h('span', { class: 'n', text: o.k }), h('span', { class: 't' }, h('span', { text: o.l })));
    el.addEventListener('click', () => {
      if (!isQ) return answer(o.k);
      selected = o.k;
      for (const x of opts) x.el.setAttribute('aria-checked', String(x.key === selected));
      setText(sendBtn, `send ${selected}`);
    });
    opts.push({ key: o.k, el });
    optList.append(el);
  }

  let sendBtn = null;
  const kids = [hd.el];
  if (isQ) {
    panel.className = 'detail p-question';
    sendBtn = h('button', { type: 'button', class: 'btn acc', text: `send ${selected}` });
    sendBtn.addEventListener('click', () => selected && answer(selected));
    kids.push(h('h3', { class: 'p-title', id: 'p-title', text: q.t }));
    if (q.d) kids.push(h('span', { class: 'val', text: q.d }));
    kids.push(h('div', { class: 'sec grow' }, optList), h('div', { class: 'actions' }, sendBtn), note);
  } else {
    panel.className = 'detail p-accent';
    kids.push(h('div', { class: 'sec' },
      h('h3', { class: 'p-title', id: 'p-title', text: q.t }),
      q.d ? h('pre', { class: 'codeblock', tabindex: '0', 'aria-label': 'prompt detail', text: q.d }) : null,
      q.c ? h('span', { class: 'lbl', text: q.c }) : null));
    if (q.k === 'input' || !q.o?.length) {
      kids.push(h('div', { class: 'sec grow' }, h('span', { class: 'val', text: 'the agent wants free-text input — answer it in its terminal.' })), note);
    } else {
      kids.push(h('div', { class: 'sec grow' }, optList), note);
    }
  }
  panel.replaceChildren(...kids);

  const s = sending.get(q.p);
  if (s) { setBusy(true, s.status === 'sent' ? s.key : null); note.textContent = s.status === 'sent' ? `sent ${s.key} · waiting for the agent…` : `sending ${s.key}…`; }
  else setBusy(false, null);
  if (why) { note.className = 'note bad'; note.textContent = why; }

  det.answer = (key) => { if (!isQ && opts.some((o) => o.key === key)) answer(key); };
  return (x) => hd.update(x);
}

function renderDetail() {
  const a = selectedId && board?.agents.find((x) => x.i === selectedId);
  if (!a) {
    if (selectedId && board) { selectedId = null; pickerOpen = false; det = null; panel.hidden = true; panel.replaceChildren(); }
    return;
  }
  panel.hidden = false;
  // Rebuilt when what it shows changes shape; otherwise its text is updated in place.
  const key = `${a.i}|${a.q ? `${a.q.p}|${a.q.x || ''}` : 'info'}|${pickerOpen}`;
  if (det?.key !== key) {
    det = { key };
    det.update = a.q ? buildPrompt(a) : buildInfo(a);
  } else {
    det.update(a);
  }
}

function render() {
  renderTop();
  renderGrid();
  renderDetail();
}

// ---------------------------------------------------------------- live data
function setConn(mode) { // live | down | connecting
  online = mode === 'live';
  const c = $('conn');
  c.className = `conn ${mode}`;
  setText($('conn-label'), mode === 'live' ? 'live' : mode === 'down' ? 'reconnecting…' : 'connecting…');
  $('banner').hidden = mode !== 'down' || !board;
}

let loading = null;
let again = false;
async function load() {
  if (loading) { again = true; return loading; } // one fetch at a time; a change meanwhile fetches once more
  loading = (async () => {
    do {
      again = false;
      try {
        const r = await fetch('/api/board', { cache: 'no-store' });
        if (r.ok) {
          board = await r.json();
          // Forget answers to prompts that are gone.
          const live = new Set(board.agents.map((a) => a.q?.p).filter(Boolean));
          for (const p of sending.keys()) if (!live.has(p)) sending.delete(p);
          setConn('live');
          render();
        } else {
          const j = await r.json().catch(() => ({}));
          setConn('live');
          board = null;
          tiles.clear(); $('grid').replaceChildren();
          renderTop();
          showEmpty(j.error === 'agents are off'
            ? 'agent tracking is off in the bridge · turn on "agents" (CUBE_AGENTS=1)'
            : `open this board on the Mac itself: http://127.0.0.1:${location.port || 80}/board/`);
        }
      } catch {
        setConn('down');
      }
    } while (again);
  })();
  try { await loading; } finally { loading = null; }
}

// GET /api/changes answers 204 as soon as the agents change, 304 after 8 s.
async function watch() {
  for (;;) {
    const r = await api('/api/changes');
    if (r.ok && r.status === 204) { load(); continue; }
    if (r.status === 304) continue;
    await new Promise((res) => setTimeout(res, r.status === 404 ? 30000 : 2000));
  }
}

function tickClock() {
  const d = new Date();
  setText($('clock'), `${String(d.getHours()).padStart(2, '0')}:${String(d.getMinutes()).padStart(2, '0')}`);
}

document.addEventListener('keydown', (e) => {
  if (e.metaKey || e.ctrlKey || e.altKey) return;
  if (e.key === 'Escape' && selectedId) {
    if (pickerOpen) { pickerOpen = false; render(); } else closeDetail();
    return;
  }
  if (/^[1-9]$/.test(e.key) && det?.answer && !e.target.closest?.('input, textarea')) det.answer(e.key);
});

setConn('connecting');
tickClock();
setInterval(tickClock, 1000);
load();
setInterval(load, 5000); // the bridge words the ages ("done · 12m ago"); refetch to keep them current
watch();
