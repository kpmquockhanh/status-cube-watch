#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "nav_frame.h"

namespace {

using Bytes = std::vector<uint8_t>;

Bytes hex(const std::string &s) {
  Bytes out;
  std::istringstream in(s);
  std::string tok;
  while (in >> tok) out.push_back((uint8_t)std::stoul(tok, nullptr, 16));
  return out;
}

std::map<std::string, Bytes> loadFixture(const char *path) {
  std::map<std::string, Bytes> m;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream in(line);
    std::string key, name, rest;
    in >> key >> name;
    std::getline(in, rest);
    if (key == "nav") m[name] = hex(rest);
  }
  return m;
}

// Distinct non-zero pattern, so "untouched" is detectable.
NavScene sentinel() {
  NavScene s;
  memset(&s, 0xAB, sizeof s);
  return s;
}

bool untouched(const NavScene &s) {
  const NavScene ref = sentinel();
  return memcmp(&s, &ref, sizeof s) == 0;
}

bool rejects(const Bytes &b) {
  NavScene s = sentinel();
  const bool ok = navDecode(b.data(), b.size(), s);
  return !ok && untouched(s);
}

// Builds a frame: header, three strings, route points, streets.
struct Street {
  uint8_t cls;
  int nPts;
};
Bytes build(const std::string &dist, const std::string &street, const std::string &eta, int nRoute,
            const std::vector<Street> &streets, uint8_t ver = 1, uint8_t state = 1, uint8_t turn = 0,
            int16_t panX = 0, int16_t panY = 0) {
  Bytes b{ver, state, turn, 0, 0, 0};
  if (ver == 2) {
    for (int16_t v : {panX, panY}) b.push_back((uint8_t)(v & 0xFF)), b.push_back((uint8_t)((uint16_t)v >> 8));
  }
  for (const std::string *s : {&dist, &street, &eta}) {
    b.push_back((uint8_t)s->size());
    b.insert(b.end(), s->begin(), s->end());
  }
  b.push_back(nRoute & 0xFF);
  b.push_back(nRoute >> 8);
  for (int i = 0; i < nRoute; i++) {
    b.push_back(1), b.push_back(0), b.push_back(2), b.push_back(0);
  }
  b.push_back((uint8_t)streets.size());
  for (const Street &st : streets) {
    b.push_back(st.cls);
    b.push_back((uint8_t)st.nPts);
    for (int i = 0; i < st.nPts; i++) {
      b.push_back(3), b.push_back(0), b.push_back(4), b.push_back(0);
    }
  }
  return b;
}

void testFixtures() {
  const auto fx = loadFixture("fixtures/nav-frames.txt");
  CHECK(fx.size() == 4);
  if (fx.size() != 4) return;

  {
    const Bytes &b = fx.at("turn");
    NavScene s = sentinel();
    CHECK(navDecode(b.data(), b.size(), s));
    CHECK(s.state == NavState::Navigating);
    CHECK(s.turn == NavTurn::Right);
    CHECK(s.exit == 0 && s.dir == 0 && s.flags == 0);
    CHECK(s.panX == 0 && s.panY == 0);  // version 1 carries no pan
    CHECK(std::string(s.dist) == "150 m");
    CHECK(std::string(s.street) == "L\xc3\xaa L\xe1\xbb\xa3i");
    CHECK(std::string(s.eta) == "12 min");
    CHECK(s.nRoute == 3);
    CHECK(s.route[0].x == 120 && s.route[0].y == 200);
    CHECK(s.route[1].x == 120 && s.route[1].y == 90);
    CHECK(s.route[2].x == 200 && s.route[2].y == 80);
    CHECK(s.nStreets == 1);
    CHECK(s.streetCls[0] == 2 && s.streetStart[0] == 0 && s.streetLen[0] == 2);
    CHECK(s.nStreetPts == 2);
    CHECK(s.streetPts[0].x == 0 && s.streetPts[0].y == 150);
    CHECK(s.streetPts[1].x == 240 && s.streetPts[1].y == 160);
  }
  {
    const Bytes &b = fx.at("arrived");
    NavScene s = sentinel();
    CHECK(navDecode(b.data(), b.size(), s));
    CHECK(s.state == NavState::Arrived);
    CHECK(s.turn == NavTurn::Arrive);
    CHECK(std::string(s.dist) == "" && std::string(s.eta) == "");
    CHECK(std::string(s.street) == "B\xe1\xba\xbfn Th\xc3\xa0nh");
    CHECK(s.nRoute == 0 && s.nStreets == 0 && s.nStreetPts == 0);
  }
  {
    const Bytes &b = fx.at("offroute");
    NavScene s = sentinel();
    CHECK(navDecode(b.data(), b.size(), s));
    CHECK(s.state == NavState::OffRouteNoSignal);
    CHECK(s.turn == NavTurn::Rejoin);
    CHECK(s.dir == 64 && s.flags == 1);
    CHECK(std::string(s.dist) == "80 m");
    CHECK(s.nRoute == 2);
    CHECK(s.route[0].x == -64 && s.route[0].y == 304);
    CHECK(s.route[1].x == 300 && s.route[1].y == -64);
    CHECK(s.nStreets == 0);
  }
  {
    const Bytes &b = fx.at("panned");
    NavScene s = sentinel();
    CHECK(navDecode(b.data(), b.size(), s));
    CHECK(s.state == NavState::Navigating && s.turn == NavTurn::Straight);
    CHECK(s.panX == -40 && s.panY == 30);
    CHECK(std::string(s.dist) == "200 m");
    CHECK(s.nRoute == 0 && s.nStreets == 0);
  }
}

void testPan() {
  NavScene s = sentinel();
  const Bytes b = build("a", "b", "c", 2, {{1, 2}}, 2, 1, 0, NAV_PAN_MAX, -NAV_PAN_MAX);
  CHECK(navDecode(b.data(), b.size(), s));
  CHECK(s.panX == NAV_PAN_MAX && s.panY == -NAV_PAN_MAX && s.nRoute == 2);
  CHECK(rejects(build("a", "b", "c", 0, {}, 2, 1, 0, NAV_PAN_MAX + 1, 0)));
  CHECK(rejects(build("a", "b", "c", 0, {}, 2, 1, 0, 0, -NAV_PAN_MAX - 1)));
  CHECK(rejects(Bytes{2, 1, 0, 0, 0, 0, 0, 0}));  // pan cut short
}

void testBuilderFrameDecodes() {  // the builder itself is valid, so rejections below are due to the one change
  NavScene s = sentinel();
  const Bytes b = build("a", "b", "c", 2, {{1, 2}, {3, 5}});
  CHECK(navDecode(b.data(), b.size(), s));
  CHECK(s.nRoute == 2 && s.nStreets == 2 && s.nStreetPts == 7);
  CHECK(s.streetCls[1] == 3 && s.streetStart[1] == 2 && s.streetLen[1] == 5);
}

void testRejections() {
  const std::vector<Street> one = {{1, 2}};
  CHECK(rejects(build("a", "b", "c", 0, one, 3)));            // version 3
  CHECK(rejects(build("a", "b", "c", 0, one, 0)));            // version 0
  CHECK(rejects(build("a", "b", "c", 0, one, 1, 5)));         // state 5
  CHECK(rejects(build("a", "b", "c", 0, one, 1, 1, 13)));     // turn 13
  CHECK(rejects(build(std::string(16, 'x'), "b", "c", 0, one)));   // dist 16 bytes
  CHECK(rejects(build("a", std::string(64, 'x'), "c", 0, one)));   // street 64 bytes
  CHECK(rejects(build("a", "b", std::string(24, 'x'), 0, one)));   // eta 24 bytes
  CHECK(rejects(build("a", "b", "c", 257, {})));              // nRoute 257
  CHECK(rejects(build("a", "b", "c", 0, {{1, 1}})));          // street with 1 point
  CHECK(rejects(build("a", "b", "c", 0, {{4, 2}})));          // cls 4
  std::vector<Street> ninety7(97, Street{0, 2});
  CHECK(rejects(build("a", "b", "c", 0, ninety7)));           // 97 streets
  CHECK(rejects(Bytes()));
  CHECK(rejects(Bytes{1, 1, 0}));                             // shorter than the fixed header

  Bytes ok = build("a", "b", "c", 3, {{1, 2}});
  Bytes trunc(ok.begin(), ok.end() - 1);
  CHECK(rejects(trunc));
  Bytes extra = ok;
  extra.push_back(0);
  CHECK(rejects(extra));
  NavScene s = sentinel();
  CHECK(navDecode(nullptr, 0, s) == false && untouched(s));
}

void testTotalStreetPoints() {
  // exactly 640 points is fine, 641 is not
  std::vector<Street> okv;
  for (int i = 0; i < 10; i++) okv.push_back({0, 64});
  NavScene s = sentinel();
  Bytes b = build("a", "b", "c", 0, okv);
  CHECK(navDecode(b.data(), b.size(), s));
  CHECK(s.nStreetPts == 640);
  std::vector<Street> bad = okv;
  bad.back().nPts = 63;
  bad.push_back({0, 3});  // 639 + 3 = 642
  CHECK(rejects(build("a", "b", "c", 0, bad)));
  bad.back().nPts = 2;  // 639 + 2 = 641
  CHECK(rejects(build("a", "b", "c", 0, bad)));
}

void testMaxRoute() {
  NavScene s = sentinel();
  Bytes b = build("a", "b", "c", 256, {});
  CHECK(navDecode(b.data(), b.size(), s));
  CHECK(s.nRoute == 256);
}

void push16(Bytes &b, int v) {
  b.push_back((uint8_t)(v & 0xFF));
  b.push_back((uint8_t)((v >> 8) & 0xFF));
}

// A frame with one route point at (rx, ry) beside (120, 140), and one street
// (0, 150) -> (sx, sy).
Bytes framePts(int rx, int ry, int sx, int sy) {
  Bytes b{1, 1, 0, 0, 0, 0, 0, 0, 0};  // header, three empty strings
  push16(b, 2);
  push16(b, 120), push16(b, 140), push16(b, rx), push16(b, ry);
  b.push_back(1);  // one street
  b.push_back(2), b.push_back(2);  // cls 2, two points
  push16(b, 0), push16(b, 150), push16(b, sx), push16(b, sy);
  return b;
}

bool decodes(const Bytes &b) {
  NavScene s = sentinel();
  return navDecode(b.data(), b.size(), s);
}

// Every coordinate must lie in x [-64, 304], y [-64, 344]: the edges are in,
// one pixel past them rejects the whole frame, in the route or a street.
void testClipBox() {
  CHECK(NAV_CLIP_MIN_X == -64 && NAV_CLIP_MAX_X == 304 && NAV_CLIP_MIN_Y == -64 && NAV_CLIP_MAX_Y == 344);
  CHECK(decodes(framePts(-64, -64, 304, 344)));
  CHECK(decodes(framePts(304, 344, -64, -64)));
  CHECK(rejects(framePts(-65, 0, 0, 0)));   // route x
  CHECK(rejects(framePts(305, 0, 0, 0)));
  CHECK(rejects(framePts(0, -65, 0, 0)));   // route y
  CHECK(rejects(framePts(0, 345, 0, 0)));
  CHECK(rejects(framePts(0, 0, -65, 0)));   // street x
  CHECK(rejects(framePts(0, 0, 305, 0)));
  CHECK(rejects(framePts(0, 0, 0, -65)));   // street y
  CHECK(rejects(framePts(0, 0, 0, 345)));
  CHECK(rejects(framePts(-32768, 0, 0, 0)));  // int16 extremes
  CHECK(rejects(framePts(0, 0, 0, 32767)));
}

Bytes chunk(uint8_t seq, uint8_t idx, uint8_t total, const Bytes &body) {
  Bytes b{BLE_PROTO_VER, seq, idx, total};
  b.insert(b.end(), body.begin(), body.end());
  return b;
}

void testAssembler() {
  NavAssembler a;
  const Bytes body(100, 0x5a);
  CHECK(a.feed(chunk(1, 0, 3, body).data(), 104) == FrameResult::Partial);
  CHECK(a.feed(chunk(1, 1, 3, body).data(), 104) == FrameResult::Partial);
  CHECK(a.feed(chunk(1, 2, 3, body).data(), 104) == FrameResult::Complete);
  CHECK(a.size() == 300);
  CHECK(a.data()[299] == 0x5a);

  NavAssembler b;
  const Bytes c = chunk(1, 0, 33, body);
  CHECK(b.feed(c.data(), c.size()) == FrameResult::Bad);  // total 33 > 32

  NavAssembler d;  // 32 chunks of 256 = 8192 fits exactly
  const Bytes big(256, 1);
  for (uint8_t i = 0; i < 31; i++) {
    const Bytes x = chunk(7, i, 32, big);
    CHECK(d.feed(x.data(), x.size()) == FrameResult::Partial);
  }
  const Bytes last = chunk(7, 31, 32, big);
  CHECK(d.feed(last.data(), last.size()) == FrameResult::Complete);
  CHECK(d.size() == BLE_NAV_MAX_BYTES);
  const Bytes over = chunk(8, 0, 2, Bytes(BLE_NAV_MAX_BYTES, 1));
  CHECK(d.feed(over.data(), over.size()) == FrameResult::Partial);
  const Bytes over2 = chunk(8, 1, 2, Bytes(1, 1));
  CHECK(d.feed(over2.data(), over2.size()) == FrameResult::Bad);  // 8193 > 8192

  CHECK(BLE_FW_REV == 9);  // 9: Control 08 and version-2 frames (the dragged map)
}

}  // namespace

int main() {
  testFixtures();
  testBuilderFrameDecodes();
  testRejections();
  testPan();
  testTotalStreetPoints();
  testMaxRoute();
  testClipBox();
  testAssembler();
  return checksDone("nav_frame_test");
}
