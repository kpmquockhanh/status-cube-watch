#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"
#include "volume_frame.h"

namespace {

std::vector<uint8_t> hex(std::istringstream &in) {
  std::vector<uint8_t> out;
  std::string tok;
  while (in >> tok) out.push_back((uint8_t)std::stoul(tok, nullptr, 16));
  return out;
}

// The lines of the shared fixture that start with `kind `, without it.
std::vector<std::string> fixtureLines(const char *kind) {
  std::ifstream f("fixtures/ble-frames.txt");
  const std::string prefix = std::string(kind) + " ";
  std::vector<std::string> out;
  std::string line;
  while (std::getline(f, line))
    if (line.rfind(prefix, 0) == 0) out.push_back(line.substr(prefix.size()));
  return out;
}

// vol_request <level> <muted> <bytes>
void testRequestFixture() {
  int n = 0;
  for (const std::string &l : fixtureLines("vol_request")) {
    std::istringstream in(l);
    unsigned level = 0, muted = 0;
    in >> level >> muted;
    const std::vector<uint8_t> want = hex(in);
    uint8_t got[BLE_VOLUME_REQ_LEN];
    volumeRequestEncode((uint8_t)level, muted != 0, got);
    CHECK(want.size() == BLE_VOLUME_REQ_LEN && memcmp(got, want.data(), BLE_VOLUME_REQ_LEN) == 0);
    n++;
  }
  CHECK(n == 3);  // a missing fixture must fail, not pass vacuously
}

// media <key> <bytes>
void testMediaFixture() {
  int n = 0;
  for (const std::string &l : fixtureLines("media")) {
    std::istringstream in(l);
    unsigned key = 0;
    in >> key;
    const std::vector<uint8_t> want = hex(in);
    uint8_t got[BLE_MEDIA_REQ_LEN];
    mediaRequestEncode((MediaKey)key, got);
    CHECK(want.size() == BLE_MEDIA_REQ_LEN && memcmp(got, want.data(), BLE_MEDIA_REQ_LEN) == 0);
    n++;
  }
  CHECK(n == 3);
}

// vol_state <level|none> <flags hex> "<name>" <bytes>
void testStateFixture() {
  int n = 0;
  for (const std::string &l : fixtureLines("vol_state")) {
    const size_t q0 = l.find('"'), q1 = l.find('"', q0 + 1);
    std::istringstream head(l.substr(0, q0));
    std::string levelTok, flagsTok;
    head >> levelTok >> flagsTok;
    const std::string name = l.substr(q0 + 1, q1 - q0 - 1);
    std::istringstream rest(l.substr(q1 + 1));
    const std::vector<uint8_t> bytes = hex(rest);
    const unsigned level = levelTok == "none" ? VOLUME_NO_DEVICE : (unsigned)std::stoul(levelTok);
    const unsigned flags = (unsigned)std::stoul(flagsTok, nullptr, 16);

    MacVolume v{};
    CHECK(volumeParse(bytes.data(), bytes.size(), v));
    CHECK(v.known);
    CHECK(v.level == level);
    CHECK(v.muted == ((flags & VOL_FLAG_MUTED) != 0));
    CHECK(v.canSet == ((flags & VOL_FLAG_CAN_SET) != 0));
    CHECK(v.canMute == ((flags & VOL_FLAG_CAN_MUTE) != 0));
    CHECK(v.playKnown == ((flags & VOL_FLAG_PLAY_KNOWN) != 0));
    CHECK(v.playing == ((flags & VOL_FLAG_PLAYING) != 0));
    CHECK(v.macLocked == ((flags & VOL_FLAG_MAC_LOCKED) != 0));
    CHECK(name == v.name);
    n++;
  }
  CHECK(n == 7);
}

// Every rejection leaves the previous state untouched.
void testRejects() {
  MacVolume before{};
  before.known = true;
  before.level = 77;
  strcpy(before.name, "KEEP");
  auto rejected = [&](const std::vector<uint8_t> &d) {
    MacVolume out = before;
    const bool ok = volumeParse(d.empty() ? nullptr : d.data(), d.size(), out);
    return !ok && volumeSame(out, before);
  };
  CHECK(rejected({}));                                     // nothing
  CHECK(rejected({1, 50}));                                // under 3 bytes
  std::vector<uint8_t> big(27, 'A');
  big[0] = 1;
  big[1] = 50;
  big[2] = 0;
  CHECK(rejected(big));                                    // over 26 bytes
  CHECK(rejected({2, 50, 0}));                             // unknown version
  CHECK(rejected({1, 101, 0}));                            // level over 100
  CHECK(rejected({1, 0xFE, 0}));                           // ...and not FF
  CHECK(rejected({1, 50, 0, 'A', 0x1F}));                  // control byte in the name
  CHECK(rejected({1, 50, 0, 'A', 0x7F}));                  // DEL
  CHECK(rejected({1, 50, 0, 'C', 'a', 'f', 0xC3, 0xA9}));  // UTF-8: the Mac folds names first
}

void testAccepts() {
  MacVolume v{};
  const uint8_t empty[] = {1, 0, VOL_FLAG_CAN_SET};
  CHECK(volumeParse(empty, sizeof(empty), v) && v.known && v.level == 0 && v.name[0] == '\0' && v.canSet);

  uint8_t full[VOLUME_FRAME_MAX];
  full[0] = 1;
  full[1] = 100;
  full[2] = 0xC0;  // only unknown flag bits: ignored
  for (size_t i = 3; i < sizeof(full); i++) full[i] = 'x';
  CHECK(volumeParse(full, sizeof(full), v));
  CHECK(strlen(v.name) == VOLUME_NAME_MAX && !v.muted && !v.canSet && !v.canMute);

  const uint8_t none[] = {1, VOLUME_NO_DEVICE, 0};
  CHECK(volumeParse(none, sizeof(none), v) && v.level == VOLUME_NO_DEVICE);

  // A shorter name after a longer one leaves no tail behind.
  const uint8_t a[] = {1, 5, 0, 'A', 'B', 'C'}, b[] = {1, 5, 0, 'Z'};
  CHECK(volumeParse(a, sizeof(a), v) && volumeParse(b, sizeof(b), v) && strcmp(v.name, "Z") == 0);
}

void testEncodeClamps() {
  uint8_t m[BLE_VOLUME_REQ_LEN];
  volumeRequestEncode(150, true, m);
  CHECK(m[0] == BLE_CTRL_VOLUME && m[1] == 100 && m[2] == 1);
}

}  // namespace

int main() {
  testRequestFixture();
  testMediaFixture();
  testStateFixture();
  testRejects();
  testAccepts();
  testEncodeClamps();
  return checksDone("volume_frame_test");
}
