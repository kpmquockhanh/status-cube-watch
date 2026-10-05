#include "check.h"
#include "readings_key.h"

namespace {

// The dual-ring usage card cards.mjs sends by default, plus a text card.
Payload deck() {
  Payload p{};
  p.valid = true;
  p.nCards = 2;
  Card &u = p.cards[0];
  strcpy(u.title, "CLAUDE");
  strcpy(u.value, "4h 09m");
  strcpy(u.sub1, "to reset");
  u.gauge = 14;
  u.gauge2 = 53;
  u.nRows = 2;
  strcpy(u.rows[0].key, "5H");
  strcpy(u.rows[0].pct, "14%");
  strcpy(u.rows[0].reset, "4h 09m");
  strcpy(u.rows[1].key, "7D");
  strcpy(u.rows[1].pct, "53%");
  strcpy(u.rows[1].reset, "1d 1h");
  strcpy(u.mail, "12");
  Card &t = p.cards[1];
  strcpy(t.title, "TODAY");
  strcpy(t.value, "$4.20");
  strcpy(t.sub1, "1.2M tokens");
  strcpy(t.sub2, "active 3m ago");
  t.gauge = GAUGE_NONE;
  t.gauge2 = GAUGE_NONE;
  return p;
}

void testSamePayloadSameKey() {  // the Mac's 5 s heartbeat
  CHECK(readingsKey(deck()) == readingsKey(deck()));
}

void testClockTextIsIgnored() {
  const uint32_t k = readingsKey(deck());
  Payload p = deck();
  strcpy(p.cards[0].value, "4h 08m");  // the countdown in the ring
  strcpy(p.cards[0].rows[0].reset, "4h 08m");
  strcpy(p.cards[0].sub1, "reset");
  strcpy(p.cards[0].sub2, "anything");
  strcpy(p.cards[1].sub1, "proj. $90");
  strcpy(p.cards[1].sub2, "active 4m ago");
  p.cards[0].color = ACC_AMBER;
  CHECK(readingsKey(p) == k);
}

void testGaugesCount() {
  const uint32_t k = readingsKey(deck());
  Payload p = deck();
  p.cards[0].gauge = 15;
  CHECK(readingsKey(p) != k);
  p = deck();
  p.cards[0].gauge2 = 54;
  CHECK(readingsKey(p) != k);
  p = deck();
  p.cards[0].gauge = GAUGE_BLANK;  // the reading was lost
  CHECK(readingsKey(p) != k);
}

void testMailCounts() {
  const uint32_t k = readingsKey(deck());
  Payload p = deck();
  strcpy(p.cards[0].mail, "13");
  CHECK(readingsKey(p) != k);
  p.cards[0].mail[0] = '\0';  // badge gone
  CHECK(readingsKey(p) != k);
}

void testTextCardValueCounts() {
  const uint32_t k = readingsKey(deck());
  Payload p = deck();
  strcpy(p.cards[1].value, "$4.31");
  CHECK(readingsKey(p) != k);
}

void testDeckShapeCounts() {
  const uint32_t k = readingsKey(deck());
  Payload p = deck();
  p.nCards = 1;
  CHECK(readingsKey(p) != k);
  p = deck();
  strcpy(p.cards[1].title, "MONTH");
  CHECK(readingsKey(p) != k);
  CHECK(readingsKey(Payload{}) != k);  // nothing received yet
}

void testFieldsDoNotRunTogether() {
  Payload a = deck(), b = deck();
  strcpy(a.cards[1].title, "AB");
  strcpy(a.cards[1].value, "C");
  strcpy(b.cards[1].title, "A");
  strcpy(b.cards[1].value, "BC");
  CHECK(readingsKey(a) != readingsKey(b));
}

void testUnterminatedBufferStaysInBounds() {
  Payload p = deck();
  memset(p.cards[1].value, 'x', sizeof(p.cards[1].value));  // no NUL
  const uint32_t k = readingsKey(p);
  strcpy(p.cards[1].sub1, "9.9M tokens");  // the field after it in memory
  CHECK(readingsKey(p) == k);
}

}  // namespace

int main() {
  testSamePayloadSameKey();
  testClockTextIsIgnored();
  testGaugesCount();
  testMailCounts();
  testTextCardValueCounts();
  testDeckShapeCounts();
  testFieldsDoNotRunTogether();
  testUnterminatedBufferStaysInBounds();
  return checksDone("readings_key_test");
}
