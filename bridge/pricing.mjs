// USD per million tokens. Source: platform.claude.com/docs/en/about-claude/pricing
//
// Cache pricing is a multiplier on the input rate:
//   5-minute cache write : 1.25x input
//   1-hour cache write   : 2.00x input
//   cache read           : `read` x input (0.10x unless the model overrides it)
//
// `fast` is the fast-mode input/output rate, used when a transcript line says
// `usage.speed: "fast"`; the cache multipliers apply on top of it. Models with
// no `fast` entry are billed at standard rates whatever the line says (Opus 4.6
// accepts fast mode but runs and bills it at standard speed).
//
// US-only inference (`usage.inference_geo: "us"`) is 1.1x on every category,
// on top of fast mode and the cache multipliers. Only Claude 4.6 and later
// accept the parameter, so older models never record "us".
const MODELS = {
  'claude-fable-5-1':  { in: 10,  out: 50, read: 0.025 },
  'claude-mythos-5-1': { in: 10,  out: 50, read: 0.025 },
  'claude-fable-5':    { in: 10,  out: 50 },
  'claude-mythos-5':   { in: 10,  out: 50 },
  'claude-opus-5-5':   { in: 4,   out: 20, read: 0.05, fast: { in: 8, out: 40 } },
  'claude-opus-5':     { in: 5,   out: 25, fast: { in: 10, out: 50 } },
  'claude-opus-4-8':   { in: 5,   out: 25, fast: { in: 10, out: 50 } },
  'claude-opus-4-7':   { in: 5,   out: 25 },
  'claude-opus-4-6':   { in: 5,   out: 25 },
  'claude-opus-4-5':   { in: 5,   out: 25 },
  'claude-opus-4-1':   { in: 15,  out: 75 },
  'claude-opus-4':     { in: 15,  out: 75 }, // also claude-opus-4-0, claude-opus-4-20250514
  'claude-sonnet-5-5': { in: 2,   out: 10 },
  'claude-sonnet-5':   { in: 2,   out: 10 },
  'claude-sonnet-4-6': { in: 3,   out: 15 },
  'claude-sonnet-4-5': { in: 3,   out: 15 },
  'claude-sonnet-4':   { in: 3,   out: 15 }, // also claude-sonnet-4-0, claude-sonnet-4-20250514
  'claude-haiku-4-5':  { in: 1,   out: 5  },
  'claude-3-5-haiku':  { in: 0.8, out: 4  },
};

// Anything not in the table falls back to Sonnet-tier rates so an unknown model
// still contributes a plausible number instead of silently costing nothing.
const FALLBACK = { in: 2, out: 10 };

// Longest matching id wins, and only on a '-' boundary: `claude-opus-5-5`
// must not be priced as `claude-opus-5`, and date-suffixed ids like
// `claude-haiku-4-5-20251001` resolve to their base model.
const BY_LENGTH = Object.keys(MODELS).sort((a, b) => b.length - a.length);

export function rates(model) {
  if (!model) return FALLBACK;
  if (MODELS[model]) return MODELS[model];
  const hit = BY_LENGTH.find((id) => model.startsWith(id) && model[id.length] === '-');
  return hit ? MODELS[hit] : FALLBACK;
}

// `u` is the raw `message.usage` object from a Claude Code transcript line.
export function costUSD(model, u) {
  const r = rates(model);
  const { in: inRate, out: outRate } = u.speed === 'fast' && r.fast ? r.fast : r;
  const c5 = u.cache_creation
    ? u.cache_creation.ephemeral_5m_input_tokens ?? 0
    : u.cache_creation_input_tokens ?? 0; // no split recorded: assume 5-minute writes
  const c1h = u.cache_creation?.ephemeral_1h_input_tokens ?? 0;
  const geo = u.inference_geo === 'us' ? 1.1 : 1;
  return (
    geo *
    ((u.input_tokens ?? 0) * inRate +
      (u.cache_read_input_tokens ?? 0) * inRate * (r.read ?? 0.1) +
      c5 * inRate * 1.25 +
      c1h * inRate * 2.0 +
      (u.output_tokens ?? 0) * outRate) /
    1_000_000
  );
}

export function knownModels() {
  return Object.keys(MODELS);
}
