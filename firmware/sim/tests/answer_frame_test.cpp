#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "answer_frame.h"
#include "check.h"

namespace {

// answer <agent> <prompt> <key> <bytes>
void testFixture() {
  std::ifstream f("fixtures/ble-frames.txt");
  std::string line;
  int n = 0;
  while (std::getline(f, line)) {
    if (line.rfind("answer ", 0) != 0) continue;
    std::istringstream in(line.substr(7));
    std::string agent, prompt, key;
    in >> agent >> prompt >> key;
    std::string want;
    unsigned b;
    while (in >> std::hex >> b) {
      char h[4];
      snprintf(h, sizeof h, "%02x", b);
      want += h;
    }
    uint8_t got[BLE_ANSWER_MAX];
    const size_t len = answerEncode(agent.c_str(), prompt.c_str(), key[0], got, sizeof(got));
    std::string hex;
    for (size_t i = 0; i < len; i++) {
      char h[4];
      snprintf(h, sizeof h, "%02x", got[i]);
      hex += h;
    }
    CHECK(len > 0 && hex == want);
    n++;
  }
  CHECK(n == 2);
}

void testRejects() {
  uint8_t o[BLE_ANSWER_MAX];
  CHECK(answerEncode("", "p1", '1', o, sizeof(o)) == 0);
  CHECK(answerEncode("a1", "", '1', o, sizeof(o)) == 0);
  CHECK(answerEncode(nullptr, "p1", '1', o, sizeof(o)) == 0);
  CHECK(answerEncode("a1", nullptr, '1', o, sizeof(o)) == 0);
  CHECK(answerEncode("0123456789abcdef", "p1", '1', o, sizeof(o)) == 0);  // 16 chars
  CHECK(answerEncode("a1", "0123456789abcdef", '1', o, sizeof(o)) == 0);
  CHECK(answerEncode("a\x7f", "p1", '1', o, sizeof(o)) == 0);
  CHECK(answerEncode("a1", "p\x1f", '1', o, sizeof(o)) == 0);
  CHECK(answerEncode("a1", "p1", '0', o, sizeof(o)) == 0);
  CHECK(answerEncode("a1", "p1", 'a', o, sizeof(o)) == 0);
  CHECK(answerEncode("a1", "p1", '\0', o, sizeof(o)) == 0);
  CHECK(answerEncode("a1", "p1", '1', nullptr, 0) == 0);
}

void testCap() {
  uint8_t o[BLE_ANSWER_MAX];
  CHECK(answerEncode("a1", "p1", '5', o, sizeof(o)) == 8);
  CHECK(answerEncode("a1", "p1", '5', o, 8) == 8);
  CHECK(answerEncode("a1", "p1", '5', o, 7) == 0);
  CHECK(answerEncode("a1", "p1", '5', o, 0) == 0);
  // The longest message fits the documented maximum exactly.
  CHECK(answerEncode("012345678901234", "012345678901234", '9', o, sizeof(o)) == BLE_ANSWER_MAX);
  CHECK(BLE_CTRL_ANSWER == 0x09 && BLE_ANSWER_MAX == 34);
}

}  // namespace

int main() {
  testFixture();
  testRejects();
  testCap();
  return checksDone("answer_frame_test");
}
