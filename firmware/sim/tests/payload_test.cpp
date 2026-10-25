#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "payload.h"

namespace {

// Payload is ~2 KB: kept off the stack here too, as on the board.
Payload g_p;

std::string slurp(const char *path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool parse(const std::string &json, Payload &out = g_p) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  char err[64];
  return payloadFromJson(doc, out, err, sizeof(err));
}

const char *USAGE = "{\"t\":\"CLAUDE\",\"v\":\"1h\",\"g\":10,\"g2\":20}";

// A deck of the usage card and an Agents card holding `body`.
std::string deckWith(const std::string &body) {
  return std::string("{\"cards\":[") + USAGE + ",{\"t\":\"AGENTS\",\"v\":\"1\"," + body + "}]}";
}

void testParsesTilesAndPrompt() {
  CHECK(parse(slurp("fixtures/agents-full.json")));
  const AgentsData &a = g_p.agents;
  CHECK(a.present && a.card == 1 && g_p.cards[1].agents);
  CHECK(!g_p.cards[0].agents);
  CHECK(a.nTiles == 5);
  CHECK(a.tiles[0].kind == AgentKind::Wait);
  CHECK(!strcmp(a.tiles[0].id, "l00001") && !strcmp(a.tiles[0].name, "infra-network"));
  CHECK(!strcmp(a.tiles[1].pose, "write") && a.tiles[1].ctx == 41 && a.tiles[1].unseen);
  CHECK(!a.tiles[0].unseen);
  CHECK(a.hasPrompt && !a.promptDropped);
  CHECK(a.prompt.kind == PromptKind::Permission);
  CHECK(!strcmp(a.prompt.agent, "l00001") && !strcmp(a.prompt.id, "p1abc-1"));
  CHECK(!strcmp(a.prompt.title, "Run this command?"));
  CHECK(a.prompt.nOpts == 3 && a.prompt.opts[1].key == '2');
  CHECK(!strcmp(a.prompt.opts[2].label, "No"));
  CHECK(!a.prompt.notInTmux && !a.prompt.cut);
  const AgentTile *t = agentsTile(a, "l00003");
  CHECK(t && !strcmp(t->name, "claude-status..."));
  CHECK(agentsTile(a, "nope") == nullptr);
}

void testLanHasNoPrompt() {
  CHECK(parse(slurp("fixtures/agents-lan.json")));
  CHECK(g_p.agents.present && !g_p.agents.hasPrompt && !g_p.agents.promptDropped);
}

void testFlags() {
  const std::string q =
      "\"q\":{\"i\":\"a\",\"p\":\"p\",\"k\":\"question\",\"t\":\"T\",\"d\":\"\",\"cut\":1,\"x\":\"tmux\","
      "\"o\":[{\"k\":\"1\",\"l\":\"Yes\"}]}";
  CHECK(parse(deckWith("\"a\":[]," + q)));
  CHECK(g_p.agents.hasPrompt && g_p.agents.prompt.cut && g_p.agents.prompt.notInTmux);
  CHECK(g_p.agents.prompt.kind == PromptKind::Question);
  CHECK(parse(deckWith("\"a\":[],\"qc\":1")));
  CHECK(g_p.agents.promptDropped && !g_p.agents.hasPrompt);
}

void testBadKeysDropped() {
  CHECK(parse(deckWith(
      "\"a\":[],\"q\":{\"i\":\"a\",\"p\":\"p\",\"k\":\"question\",\"t\":\"T\",\"o\":["
      "{\"k\":\"10\",\"l\":\"x\"},{\"k\":\"\",\"l\":\"x\"},{\"k\":\"a\",\"l\":\"x\"},{\"k\":\"0\",\"l\":\"x\"},"
      "{\"k\":\"4\",\"l\":\"ok\"}]}")));
  CHECK(g_p.agents.prompt.nOpts == 1 && g_p.agents.prompt.opts[0].key == '4');

  std::string many;
  for (int i = 1; i <= 8; i++) {
    if (i > 1) many += ",";
    many += std::string("{\"k\":\"") + char('0' + i) + "\",\"l\":\"o\"}";
  }
  CHECK(parse(deckWith("\"a\":[],\"q\":{\"i\":\"a\",\"p\":\"p\",\"k\":\"question\",\"t\":\"T\",\"o\":[" + many +
                       "]}")));
  CHECK(g_p.agents.prompt.nOpts == PROMPT_OPTS);

  std::string tiles;
  for (int i = 0; i < 10; i++) {
    if (i) tiles += ",";
    tiles += "{\"i\":\"t" + std::to_string(i) + "\",\"n\":\"n\",\"l\":\"l\",\"k\":\"run\",\"p\":\"bash\",\"x\":1}";
  }
  CHECK(parse(deckWith("\"a\":[" + tiles + "]")));
  CHECK(g_p.agents.nTiles == AGENT_TILES);
}

void testUnknownKinds() {
  CHECK(parse(deckWith(
      "\"a\":[{\"i\":\"t\",\"n\":\"n\",\"l\":\"l\",\"k\":\"zzz\",\"p\":\"x\",\"x\":1}],"
      "\"q\":{\"i\":\"t\",\"p\":\"p\",\"k\":\"zzz\",\"t\":\"T\"}")));
  CHECK(g_p.agents.tiles[0].kind == AgentKind::Idle);
  CHECK(g_p.agents.hasPrompt && g_p.agents.prompt.kind == PromptKind::Input);
}

void testBadPromptIsAbsent() {
  CHECK(parse(deckWith("\"a\":[],\"q\":{\"i\":\"\",\"p\":\"p\",\"k\":\"permission\",\"t\":\"T\"}")));
  CHECK(!g_p.agents.hasPrompt);
  CHECK(parse(deckWith("\"a\":[],\"q\":{\"i\":\"a\",\"p\":\"\",\"k\":\"permission\",\"t\":\"T\"}")));
  CHECK(!g_p.agents.hasPrompt);
}

void testLongStringsTruncate() {
  const std::string d(200, 'd');
  CHECK(parse(deckWith("\"a\":[],\"q\":{\"i\":\"a\",\"p\":\"p\",\"k\":\"input\",\"t\":\"T\",\"d\":\"" + d + "\"}")));
  CHECK(strlen(g_p.agents.prompt.detail) == sizeof(g_p.agents.prompt.detail) - 1);
}

void testOnlyFirstAgentsCard() {
  const std::string tile = "{\"i\":\"%s\",\"n\":\"n\",\"l\":\"l\",\"k\":\"run\",\"p\":\"bash\",\"x\":1}";
  char first[160], second[160];
  snprintf(first, sizeof(first), tile.c_str(), "one");
  snprintf(second, sizeof(second), tile.c_str(), "two");
  const std::string json = std::string("{\"cards\":[{\"t\":\"AGENTS\",\"a\":[") + first +
                           "]},{\"t\":\"AGENTS\",\"a\":[" + second + "]}]}";
  CHECK(parse(json));
  CHECK(g_p.agents.card == 0 && g_p.cards[0].agents && !g_p.cards[1].agents);
  CHECK(!strcmp(g_p.agents.tiles[0].id, "one"));
}

void testInPlaceFailure() {
  CHECK(parse(slurp("fixtures/agents-full.json")));
  CHECK(g_p.valid);
  CHECK(!parse("{\"cards\":[]}"));
  CHECK(!g_p.valid);
}

void testNoAgents() {
  CHECK(parse(slurp("fixtures/usage.json")));
  CHECK(!g_p.agents.present && !g_p.cards[0].agents);
}

}  // namespace

int main() {
  testParsesTilesAndPrompt();
  testLanHasNoPrompt();
  testFlags();
  testBadKeysDropped();
  testUnknownKinds();
  testBadPromptIsAbsent();
  testLongStringsTruncate();
  testOnlyFirstAgentsCard();
  testInPlaceFailure();
  testNoAgents();
  return checksDone("payload_test");
}
