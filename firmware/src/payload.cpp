#include "payload.h"

Accent accentFromName(const char *name) {
  if (!name) return ACC_INK;
  if (!strcmp(name, "accent")) return ACC_ACCENT;
  if (!strcmp(name, "blue")) return ACC_BLUE;
  if (!strcmp(name, "green")) return ACC_GREEN;
  if (!strcmp(name, "amber")) return ACC_AMBER;
  if (!strcmp(name, "violet")) return ACC_VIOLET;
  if (!strcmp(name, "red")) return ACC_RED;
  return ACC_INK;
}

namespace {

AgentKind kindFromName(const char *k) {
  if (!strcmp(k, "run")) return AgentKind::Run;
  if (!strcmp(k, "wait")) return AgentKind::Wait;
  if (!strcmp(k, "err")) return AgentKind::Err;
  if (!strcmp(k, "exit")) return AgentKind::Exit;
  if (!strcmp(k, "start")) return AgentKind::Start;
  return AgentKind::Idle;
}

PromptKind promptKindFromName(const char *k) {
  if (!strcmp(k, "permission")) return PromptKind::Permission;
  if (!strcmp(k, "question")) return PromptKind::Question;
  return PromptKind::Input;
}

// A prompt needs both ids to be answerable; one without them is treated as
// absent rather than drawn with buttons that could not be sent.
bool parsePrompt(JsonObjectConst q, AgentPrompt &pr) {
  strlcpy(pr.agent, q["i"] | "", sizeof(pr.agent));
  strlcpy(pr.id, q["p"] | "", sizeof(pr.id));
  if (!pr.agent[0] || !pr.id[0]) return false;
  pr.kind = promptKindFromName(q["k"] | "");
  strlcpy(pr.title, q["t"] | "", sizeof(pr.title));
  strlcpy(pr.detail, q["d"] | "", sizeof(pr.detail));
  strlcpy(pr.context, q["c"] | "", sizeof(pr.context));
  pr.notInTmux = !strcmp(q["x"] | "", "tmux");
  pr.cut = (q["cut"] | 0) != 0;
  for (JsonObjectConst o : q["o"].as<JsonArrayConst>()) {
    if (pr.nOpts >= PROMPT_OPTS) break;
    const char *k = o["k"] | "";
    if (!k[0] || k[1] || k[0] < '1' || k[0] > '9') continue;  // one digit 1-9 only
    PromptOpt &opt = pr.opts[pr.nOpts++];
    opt.key = k[0];
    strlcpy(opt.label, o["l"] | "", sizeof(opt.label));
  }
  return true;
}

// A detail without an id cannot be matched to the screen that asked for it,
// so it is treated as absent. `out` arrives zeroed.
void parseDetail(JsonObjectConst d, AgentDetail &out) {
  strlcpy(out.id, d["i"] | "", sizeof(out.id));
  if (!out.id[0]) return;
  out.present = true;
  strlcpy(out.title, d["t"] | "", sizeof(out.title));
  strlcpy(out.status, d["s"] | "", sizeof(out.status));
  strlcpy(out.doing, d["w"] | "", sizeof(out.doing));
  strlcpy(out.ctx, d["c"] | "", sizeof(out.ctx));
  strlcpy(out.prompt, d["p"] | "", sizeof(out.prompt));
  strlcpy(out.meta, d["m"] | "", sizeof(out.meta));
  strlcpy(out.err, d["e"] | "", sizeof(out.err));
  strlcpy(out.subs, d["n"] | "", sizeof(out.subs));
  out.hatAuto = (d["ha"] | 0) != 0;
}

void parseAgents(JsonObjectConst c, uint8_t cardIndex, AgentsData &a) {
  a.present = true;
  a.card = cardIndex;
  for (JsonObjectConst t : c["a"].as<JsonArrayConst>()) {
    if (a.nTiles >= AGENT_TILES) break;
    AgentTile &tile = a.tiles[a.nTiles++];
    strlcpy(tile.id, t["i"] | "", sizeof(tile.id));
    strlcpy(tile.name, t["n"] | "", sizeof(tile.name));
    strlcpy(tile.line, t["l"] | "", sizeof(tile.line));
    strlcpy(tile.pose, t["p"] | "", sizeof(tile.pose));
    tile.kind = kindFromName(t["k"] | "");
    const int x = t["x"] | 0;
    tile.ctx = x < 0 ? 0 : (x > 100 ? 100 : (uint8_t)x);
    tile.unseen = (t["u"] | 0) != 0;
    tile.hasSubs = t["s"].is<const char *>();
    strlcpy(tile.subs, t["s"] | "", sizeof(tile.subs));
    // Only an integer 0..103 counts; anything else (absent, string, float, out of range) is no hat.
    tile.hat = -1;
    if (t["h"].is<int>()) {
      const int h = t["h"].as<int>();
      if (h >= 0 && h <= 103) tile.hat = (int16_t)h;
    }
  }
  if (c["q"].is<JsonObjectConst>()) a.hasPrompt = parsePrompt(c["q"], a.prompt);
  a.promptDropped = !a.hasPrompt && (c["qc"] | 0) != 0;
  if (c["d"].is<JsonObjectConst>()) parseDetail(c["d"], a.detail);
}

}  // namespace

const AgentTile *agentsTile(const AgentsData &a, const char *id) {
  if (!a.present || !id) return nullptr;
  for (uint8_t i = 0; i < a.nTiles; i++)
    if (!strcmp(a.tiles[i].id, id)) return &a.tiles[i];
  return nullptr;
}

bool payloadFromJson(const JsonDocument &doc, Payload &out, char *err, size_t errLen) {
  // Parsed in place: a Payload on a task stack would overflow it. A failure
  // leaves `out` cleared (valid false); callers that keep the last good one
  // parse into scratch and copy on success.
  Payload &p = out;
  memset(&p, 0, sizeof(p));
  // The top-level `ts` and `src` are for people reading /api/status; nothing
  // on the cube needs them, so they are not stored.
  for (JsonObjectConst c : doc["cards"].as<JsonArrayConst>()) {
    if (p.nCards >= MAX_CARDS) break;
    Card &card = p.cards[p.nCards++];
    strlcpy(card.title, c["t"] | "", sizeof(card.title));
    strlcpy(card.value, c["v"] | "--", sizeof(card.value));
    strlcpy(card.sub1, c["s1"] | "", sizeof(card.sub1));
    strlcpy(card.sub2, c["s2"] | "", sizeof(card.sub2));
    card.color = accentFromName(c["c"] | "ink");
    const int g = c["g"] | (int)GAUGE_NONE;
    card.gauge = g > 100 ? 100 : (g < GAUGE_NONE ? GAUGE_NONE : (int8_t)g);

    // Optional second ring, legend rows and mail badge (the combined usage
    // card). A card without them is parsed exactly as before.
    const int g2 = c["g2"] | (int)GAUGE_NONE;
    card.gauge2 = g2 > 100 ? 100 : (g2 < GAUGE_NONE ? GAUGE_NONE : (int8_t)g2);
    card.color2 = accentFromName(c["c2"] | "ink");
    for (JsonObjectConst r : c["rows"].as<JsonArrayConst>()) {
      if (card.nRows >= 2) break;
      LegendRow &row = card.rows[card.nRows++];
      strlcpy(row.key, r["k"] | "", sizeof(row.key));
      strlcpy(row.pct, r["p"] | "", sizeof(row.pct));
      strlcpy(row.reset, r["r"] | "", sizeof(row.reset));
    }
    strlcpy(card.mail, c["m"] | "", sizeof(card.mail));
    card.mailColor = accentFromName(c["mc"] | "ink");
    // Only the first Agents card counts; a second one is a plain card.
    if (c["a"].is<JsonArrayConst>() && !p.agents.present) {
      card.agents = true;
      parseAgents(c, p.nCards - 1, p.agents);
    }
  }
  if (p.nCards == 0) {
    strlcpy(err, "no cards in payload", errLen);
    return false;
  }

  p.valid = true;
  return true;
}
