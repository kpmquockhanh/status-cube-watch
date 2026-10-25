#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "check.h"
#include "detail_frame.h"

namespace {

std::string toHex(const uint8_t *b, size_t n) {
  std::string hex;
  for (size_t i = 0; i < n; i++) {
    char h[4];
    snprintf(h, sizeof h, "%02x", b[i]);
    hex += h;
  }
  return hex;
}

// The rest of a fixture line after its words: hex bytes, joined.
std::string wantHex(std::istringstream &in) {
  std::string want;
  unsigned b;
  while (in >> std::hex >> b) {
    char h[4];
    snprintf(h, sizeof h, "%02x", b);
    want += h;
  }
  return want;
}

// detail <agent|-> <bytes>   and   hat <agent> <n|none|auto> <bytes>
void testFixture() {
  std::ifstream f("fixtures/ble-frames.txt");
  std::string line;
  int details = 0, hats = 0;
  while (std::getline(f, line)) {
    if (line.rfind("detail ", 0) == 0) {
      std::istringstream in(line.substr(7));
      std::string agent;
      in >> agent;
      const std::string want = wantHex(in);
      uint8_t got[BLE_DETAIL_MAX];
      const size_t len = detailEncode(agent == "-" ? nullptr : agent.c_str(), got, sizeof(got));
      CHECK(len > 0 && toHex(got, len) == want);
      details++;
    } else if (line.rfind("hat ", 0) == 0) {
      std::istringstream in(line.substr(4));
      std::string agent, hat;
      in >> agent >> hat;
      const std::string want = wantHex(in);
      const uint8_t code = hat == "none" ? HAT_NONE : hat == "auto" ? HAT_AUTO : (uint8_t)std::stoi(hat);
      uint8_t got[BLE_HAT_MAX];
      const size_t len = hatEncode(agent.c_str(), code, got, sizeof(got));
      CHECK(len > 0 && toHex(got, len) == want);
      hats++;
    }
  }
  CHECK(details == 2);
  CHECK(hats == 3);
}

void testDetail() {
  uint8_t o[BLE_DETAIL_MAX];
  CHECK(detailEncode("", o, sizeof(o)) == 2 && o[0] == 0x0A && o[1] == 0);  // "" = closed
  CHECK(detailEncode(nullptr, o, sizeof(o)) == 2 && o[1] == 0);
  CHECK(detailEncode("0123456789abcdef", o, sizeof(o)) == 0);  // 16 chars
  CHECK(detailEncode("012345678901234", o, sizeof(o)) == BLE_DETAIL_MAX);  // 15: the longest
  CHECK(detailEncode("a\x7f", o, sizeof(o)) == 0);
  CHECK(detailEncode("a\x1f", o, sizeof(o)) == 0);
  CHECK(detailEncode("a\xc2\xb7", o, sizeof(o)) == 0);
  CHECK(detailEncode("a1", nullptr, 0) == 0);
  // Too small: 0 and `out` untouched.
  memset(o, 0xAA, sizeof(o));
  CHECK(detailEncode("a1", o, 3) == 0);
  CHECK(detailEncode(nullptr, o, 1) == 0);
  bool untouched = true;
  for (uint8_t b : o) untouched = untouched && b == 0xAA;
  CHECK(untouched);
  CHECK(detailEncode("a1", o, 4) == 4);
}

void testHat() {
  uint8_t o[BLE_HAT_MAX];
  CHECK(hatEncode("a1", 0, o, sizeof(o)) == 5 && o[0] == 0x0B && o[4] == 0);
  CHECK(hatEncode("a1", 103, o, sizeof(o)) == 5 && o[4] == 103);
  CHECK(hatEncode("a1", 104, o, sizeof(o)) == 0);
  CHECK(hatEncode("a1", 0xFD, o, sizeof(o)) == 0);
  CHECK(hatEncode("a1", HAT_NONE, o, sizeof(o)) == 5 && o[4] == 0xFE);
  CHECK(hatEncode("a1", HAT_AUTO, o, sizeof(o)) == 5 && o[4] == 0xFF);
  CHECK(hatEncode("", 1, o, sizeof(o)) == 0);  // a hat always names an agent
  CHECK(hatEncode(nullptr, 1, o, sizeof(o)) == 0);
  CHECK(hatEncode("0123456789abcdef", 1, o, sizeof(o)) == 0);
  CHECK(hatEncode("a\x7f", 1, o, sizeof(o)) == 0);
  CHECK(hatEncode("012345678901234", 1, o, sizeof(o)) == BLE_HAT_MAX);
  memset(o, 0xAA, sizeof(o));
  CHECK(hatEncode("a1", 1, o, 4) == 0);
  bool untouched = true;
  for (uint8_t b : o) untouched = untouched && b == 0xAA;
  CHECK(untouched);
  CHECK(hatEncode("a1", 1, o, 5) == 5);
  CHECK(BLE_CTRL_DETAIL == 0x0A && BLE_CTRL_HAT == 0x0B && BLE_FW_REV == 11);
  CHECK(BLE_DETAIL_MAX == 17 && BLE_HAT_MAX == 18);
}

}  // namespace

int main() {
  testFixture();
  testDetail();
  testHat();
  return checksDone("detail_frame_test");
}
