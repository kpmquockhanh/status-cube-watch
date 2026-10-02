// USD per million tokens. Source: platform.claude.com/docs/en/about-claude/pricing
//
// Cache pricing follows the standard multipliers on the input rate unless a
// model overrides `cacheRead`:
//   5-minute cache write : 1.25x input
//   1-hour cache write   : 2.00x input
//   cache read           : 0.10x input
const MODELS = {
  'claude-fable-5-1':  { in: 10, out: 50, cacheRead: 0.25 },
  'claude-mythos-5-1': { in: 10, out: 50, cacheRead: 0.25 },
  'claude-fable-5':    { in: 10, out: 50 },
  'claude-opus-5':     { in: 5,  out: 25 },
  'claude-opus-4-8':   { in: 5,  out: 25 },
  'claude-opus-4-7':   { in: 5,  out: 25 },
  'claude-opus-4-6':   { in: 5,  out: 25 },
  'claude-sonnet-5':   { in: 2,  out: 10 },
  'claude-sonnet-4-6': { in: 3,  out: 15 },
  'claude-haiku-4-5':  { in: 1,  out: 5  },
};

// Anything not in the table falls back to Sonnet-tier rates so an unknown model
// still contributes a plausible number instead of silently costing nothing.
const FALLBACK = { in: 2, out: 10 };

export function rates(model) {
  if (!model) return FALLBACK;
  if (MODELS[model]) return MODELS[model];
  // Tolerate date-suffixed ids like `claude-haiku-4-5-20251001`.
  const hit = Object.keys(MODELS).find((id) => model.startsWith(id));
  return hit ? MODELS[hit] : FALLBACK;
}

// `u` is the raw `message.usage` object from a Claude Code transcript line.
export function costUSD(model, u) {
  const r = rates(model);
  const cacheRead = r.cacheRead ?? r.in * 0.1;
  const c5 = u.cache_creation?.ephemeral_5m_input_tokens ?? 0;
  const c1h = u.cache_creation?.ephemeral_1h_input_tokens ?? 0;
  return (
    ((u.input_tokens ?? 0) * r.in +
      (u.cache_read_input_tokens ?? 0) * cacheRead +
      c5 * r.in * 1.25 +
      c1h * r.in * 2.0 +
      (u.output_tokens ?? 0) * r.out) /
    1_000_000
  );
}

export function knownModels() {
  return Object.keys(MODELS);
}
