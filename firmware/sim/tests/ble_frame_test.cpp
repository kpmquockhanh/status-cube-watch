#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "ble_frame.h"
#include "check.h"

namespace {

using Bytes = std::vector<uint8_t>;

Bytes hex(const std::string &s) {
  Bytes out;
  std::istringstream in(s);
  std::string tok;
  while (in >> tok) out.push_back((uint8_t)std::stoul(tok, nullptr, 16));
  return out;
}

Bytes frame(uint8_t seq, uint8_t idx, uint8_t total, const std::string &data, uint8_t ver = BLE_PROTO_VER) {
  Bytes b{ver, seq, idx, total};
  b.insert(b.end(), data.begin(), data.end());
  return b;
}

FrameResult feed(FrameAssembler &a, const Bytes &b) { return a.feed(b.data(), b.size()); }

struct Case {
  std::string name, json;
  int seq = 0;
  std::vector<Bytes> frames;
};

std::vector<Case> loadFixture(const char *path) {
  std::vector<Case> cases;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    const size_t sp = line.find(' ');
    const std::string key = line.substr(0, sp);
    const std::string rest = sp == std::string::npos ? "" : line.substr(sp + 1);
    if (key == "case") {
      cases.push_back(Case{});
      cases.back().name = rest;
    } else if (cases.empty()) {
      continue;
    } else if (key == "seq") {
      cases.back().seq = std::stoi(rest);
    } else if (key == "json") {
      cases.back().json = rest;
    } else if (key == "frame") {
      cases.back().frames.push_back(hex(rest));
    }
  }
  return cases;
}

void testGoldenFixture() {
  const std::vector<Case> cases = loadFixture("fixtures/ble-frames.txt");
  CHECK(cases.size() >= 2);  // a missing fixture must fail, not pass vacuously
  for (const Case &c : cases) {
    FrameAssembler a;
    for (size_t i = 0; i < c.frames.size(); i++) {
      const FrameResult r = feed(a, c.frames[i]);
      CHECK(r == (i + 1 < c.frames.size() ? FrameResult::Partial : FrameResult::Complete));
    }
    CHECK(std::string(a.json()) == c.json);
    CHECK(a.size() == c.json.size());
    CHECK(a.seq() == c.seq);
  }
}

void testNewSeqDiscardsPartial() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 2, "aaaa")) == FrameResult::Partial);
  CHECK(feed(a, frame(2, 0, 1, "ok")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "ok");
  CHECK(a.seq() == 2);
}

void testSeqRestartAfterPartial() {  // the helper crashed mid-payload and restarted at seq 0
  FrameAssembler a;
  CHECK(feed(a, frame(5, 0, 2, "half")) == FrameResult::Partial);
  CHECK(feed(a, frame(0, 0, 1, "fresh")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "fresh");
}

void testSeqWrap() {
  FrameAssembler a;
  CHECK(feed(a, frame(255, 0, 1, "a")) == FrameResult::Complete);
  CHECK(feed(a, frame(0, 0, 1, "b")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "b");
}

void testDuplicateChunkIgnored() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 2, "ab")) == FrameResult::Partial);
  CHECK(feed(a, frame(1, 0, 2, "ab")) == FrameResult::Ignored);
  CHECK(feed(a, frame(1, 1, 2, "cd")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "abcd");
}

void testGapIsBadAndRecovers() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 3, "ab")) == FrameResult::Partial);
  CHECK(feed(a, frame(1, 2, 3, "ef")) == FrameResult::Bad);  // idx 1 was skipped
  CHECK(feed(a, frame(2, 0, 1, "good")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "good");
}

void testJoiningMidPayloadIsBad() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 1, 2, "cd")) == FrameResult::Bad);
}

void testBadHeaders() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 1, "x", 2)) == FrameResult::Bad);  // unknown version
  CHECK(feed(a, frame(1, 0, 0, "x")) == FrameResult::Bad);     // total 0
  CHECK(feed(a, frame(1, 0, 17, "x")) == FrameResult::Bad);    // more than 16 chunks
  CHECK(feed(a, frame(1, 3, 3, "x")) == FrameResult::Bad);     // idx >= total
  CHECK(feed(a, frame(1, 0, 1, "")) == FrameResult::Bad);      // header only, no data
  const uint8_t tiny[2] = {1, 2};
  CHECK(a.feed(tiny, 2) == FrameResult::Bad);                  // shorter than the header
  CHECK(a.feed(nullptr, 0) == FrameResult::Bad);
}

void testTotalChangeMidPayloadIsBad() {
  FrameAssembler a;
  CHECK(feed(a, frame(1, 0, 3, "ab")) == FrameResult::Partial);
  CHECK(feed(a, frame(1, 1, 4, "cd")) == FrameResult::Bad);
}

void testOverflowIsBadAndRecovers() {
  FrameAssembler a;
  const std::string chunk(200, 'x');
  for (uint8_t i = 0; i < 10; i++) CHECK(feed(a, frame(1, i, 16, chunk)) == FrameResult::Partial);  // 2000 bytes
  CHECK(feed(a, frame(1, 10, 16, chunk)) == FrameResult::Bad);                                       // 2200 > 2048
  CHECK(feed(a, frame(2, 0, 1, "ok")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "ok");
}

void testExactlyMaxPayload() {
  FrameAssembler a;
  const std::string chunk(128, 'y');  // 16 x 128 = 2048
  for (uint8_t i = 0; i < 15; i++) CHECK(feed(a, frame(1, i, 16, chunk)) == FrameResult::Partial);
  CHECK(feed(a, frame(1, 15, 16, chunk)) == FrameResult::Complete);
  CHECK(a.size() == BLE_MAX_PAYLOAD);
  CHECK(a.json()[BLE_MAX_PAYLOAD] == '\0');
}

void testStaleReplayIgnoredUntilCleared() {
  FrameAssembler a;
  CHECK(feed(a, frame(3, 0, 1, "one")) == FrameResult::Complete);
  CHECK(feed(a, frame(3, 0, 1, "one")) == FrameResult::Ignored);  // a late retransmit of a finished payload
  a.clear();                                                      // a new connection starts clean
  CHECK(feed(a, frame(3, 0, 1, "two")) == FrameResult::Complete);
  CHECK(std::string(a.json()) == "two");
}

void testClearForgetsLastDone() {  // reconnect after a helper restart that reuses a seq
  FrameAssembler a;
  CHECK(feed(a, frame(9, 0, 2, "ab")) == FrameResult::Partial);
  a.clear();
  CHECK(feed(a, frame(9, 1, 2, "cd")) == FrameResult::Bad);  // the half payload is gone, not resumed
  CHECK(feed(a, frame(9, 0, 1, "ok")) == FrameResult::Complete);
}

}  // namespace

int main() {
  testGoldenFixture();
  testNewSeqDiscardsPartial();
  testSeqRestartAfterPartial();
  testSeqWrap();
  testDuplicateChunkIgnored();
  testGapIsBadAndRecovers();
  testJoiningMidPayloadIsBad();
  testBadHeaders();
  testTotalChangeMidPayloadIsBad();
  testOverflowIsBadAndRecovers();
  testExactlyMaxPayload();
  testStaleReplayIgnoredUntilCleared();
  testClearForgetsLastDone();
  return checksDone("ble_frame_test");
}
