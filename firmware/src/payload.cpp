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

bool payloadFromJson(const JsonDocument &doc, Payload &out, char *err, size_t errLen) {
  Payload p{};
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
  }
  if (p.nCards == 0) {
    strlcpy(err, "no cards in payload", errLen);
    return false;
  }

  p.valid = true;
  out = p;
  return true;
}
