// Headless frame grab. Renders cards through the real ui.cpp and writes PNGs,
// so the layout can be compared against the design without a window (and
// without a screen-recording permission prompt).
//
//   make shot                       # every card of the live bridge payload
//   ./build/cube-shot out card.json # <out>-0.png, <out>-1.png, ...
//   ./build/cube-shot out @portal   # <out>-portal.png: a screen that is not a
//                                   # payload card (see renderSpecial):
//                                   # @portal, @ota, @ble-pair, @ble-wait, @waiting, @dev-edit,
//                                   # @unlock|unlock-wait, @nav-turn|vn|long|longeta|reroute|offroute|arrived|lost|busy, @vol-56|muted|full|zero|fixed|noout|long|none|press|playing|paused
//
// Reads the payload JSON from the file named on the command line, or stdin.

#include <zlib.h>

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "../src/display.h"
#include "battery_env.h"
#include "nav_samples.h"
#include "../src/payload.h"
#include "../src/ui.h"

Display *g_simDisplay = nullptr;  // normally lives in touch_sim.cpp

namespace {

void chunk(FILE *f, const char *tag, const uint8_t *data, size_t len) {
  const uint32_t n = htonl((uint32_t)len);
  fwrite(&n, 4, 1, f);
  fwrite(tag, 4, 1, f);
  fwrite(data, 1, len, f);
  uLong c = crc32(crc32(0, nullptr, 0), (const Bytef *)tag, 4);
  if (len) c = crc32(c, data, (uInt)len);
  const uint32_t be = htonl((uint32_t)c);
  fwrite(&be, 4, 1, f);
}

// 8-bit RGB, no interlacing, one filter-0 byte per scanline.
bool writePng(const char *path, const uint8_t *rgb, int w, int h) {
  FILE *f = fopen(path, "wb");
  if (!f) return false;
  fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);

  uint8_t ihdr[13];
  const uint32_t bw = htonl((uint32_t)w), bh = htonl((uint32_t)h);
  memcpy(ihdr, &bw, 4);
  memcpy(ihdr + 4, &bh, 4);
  ihdr[8] = 8;                                   // bit depth
  ihdr[9] = 2;                                   // truecolour
  ihdr[10] = ihdr[11] = ihdr[12] = 0;
  chunk(f, "IHDR", ihdr, sizeof(ihdr));

  std::vector<uint8_t> raw;
  raw.reserve((size_t)h * (1 + (size_t)w * 3));
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + (size_t)y * w * 3, rgb + (size_t)(y + 1) * w * 3);
  }
  uLongf zlen = compressBound(raw.size());
  std::vector<uint8_t> z(zlen);
  if (compress2(z.data(), &zlen, raw.data(), raw.size(), 9) != Z_OK) {
    fclose(f);
    return false;
  }
  chunk(f, "IDAT", z.data(), zlen);
  chunk(f, "IEND", nullptr, 0);
  fclose(f);
  return true;
}

std::string slurp(const char *path) {
  FILE *f = path ? fopen(path, "rb") : stdin;
  if (!f) return "";
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  if (path) fclose(f);
  return s;
}

const char *g_prefix = "shot";
const char *g_json = nullptr;
int g_rc = 0;

std::vector<uint16_t> g_px((size_t)LCD_WIDTH * LCD_HEIGHT);
std::vector<uint8_t> g_rgb((size_t)LCD_WIDTH * LCD_HEIGHT * 3);

// Reads the panel back and writes it as a PNG.
bool grab(Display &lcd, const char *path) {
  lcd.readRect(0, 0, LCD_WIDTH, LCD_HEIGHT, g_px.data());
  for (size_t p = 0; p < g_px.size(); p++) {
    // readRect hands back the panel's own byte order, which is swapped
    // relative to the host's; unswap before unpacking the 565 fields.
    const uint16_t v = (uint16_t)((g_px[p] >> 8) | (g_px[p] << 8));
    g_rgb[p * 3 + 0] = (uint8_t)(((v >> 11) & 0x1F) * 255 / 31);
    g_rgb[p * 3 + 1] = (uint8_t)(((v >> 5) & 0x3F) * 255 / 63);
    g_rgb[p * 3 + 2] = (uint8_t)((v & 0x1F) * 255 / 31);
  }
  return writePng(path, g_rgb.data(), LCD_WIDTH, LCD_HEIGHT);
}

// The deck with no payload yet (bridge down, no Mac): the waiting card alone.
void waitingShot(Display &lcd) {
  const Payload none{};
  for (int frame = 0; frame < 400; frame++) {
    uiRender(lcd, none, 0, false, 4000, batteryViewFromEnv(), linkFromEnv());
    if (!uiAnimating()) break;
    delay(8);
  }
}

// The Volume card in a named state, fed through the real VolumeSlider, with an
// empty payload (the deck is the waiting card, then Volume), drawn until the fill settles.
bool volumeShot(Display &lcd, const char *state) {
  MacVolume m{};
  m.known = true;
  m.canSet = m.canMute = true;
  snprintf(m.name, sizeof(m.name), "MacBook Pro Speakers");
  int8_t pressed = -1;
  if (!strcmp(state, "56")) {
    m.level = 56;
  } else if (!strcmp(state, "playing") || !strcmp(state, "paused")) {  // the Mac reports Now Playing
    m.level = 56;
    m.playKnown = true;
    m.playing = !strcmp(state, "playing");
  } else if (!strcmp(state, "press")) {  // play/pause just tapped
    m.level = 56;
    pressed = (int8_t)MediaKey::PlayPause;
  } else if (!strcmp(state, "muted")) {
    m.level = 56;
    m.muted = true;
  } else if (!strcmp(state, "full")) {
    m.level = 100;
  } else if (!strcmp(state, "zero")) {
    m.level = 0;
  } else if (!strcmp(state, "fixed")) {
    m.level = 100;
    m.canSet = false;
    snprintf(m.name, sizeof(m.name), "HDMI");
  } else if (!strcmp(state, "noout")) {
    m.level = VOLUME_NO_DEVICE;
    m.canSet = m.canMute = false;
    m.name[0] = '\0';
  } else if (!strcmp(state, "long")) {
    m.level = 30;
    snprintf(m.name, sizeof(m.name), "LG UltraFine Display Au");  // 23 bytes: the most the Mac sends
  } else if (!strcmp(state, "none")) {
    m = MacVolume{};
  } else {
    return false;
  }
  VolumeSlider s;
  s.fromMac(m, 0);
  VolumeView vv = s.view();
  vv.pressed = pressed;
  const Payload none{};
  for (int frame = 0; frame < 400; frame++) {
    uiRender(lcd, none, uiVolumeIndex(none), true, 4000, batteryViewFromEnv(), linkFromEnv(), &vv);
    if (!uiAnimating()) break;
    delay(8);
  }
  return true;
}

// A navigation scene by name (nav_samples.h); `lost` also shows NO PHONE.
// CUBE_NAV_PAN=<x>,<y> draws it dragged that far (the frame itself unpanned).
bool navShot(Display &lcd, const char *name) {
  static NavScene scene;
  if (!navSample(name, scene)) return false;
  int px = 0, py = 0;
  if (const char *e = getenv("CUBE_NAV_PAN")) sscanf(e, "%d,%d", &px, &py);
  uiNav(lcd, scene, !strcmp(name, "lost"), batteryViewFromEnv(), (int16_t)px, (int16_t)py);
  return true;
}

// Screens that are not payload cards. Selected with `@name` on the command
// line; the grab goes to <prefix>-<name>.png. Returns false for an unknown name.
bool renderSpecial(Display &lcd, const char *name) {
  if (!strcmp(name, "portal")) {
    uiPortal(lcd, "claude-cube-A1B2", "192.168.71.1", "192.168.1.57");
  } else if (!strcmp(name, "ota")) {
    uiOta(lcd, 62);
  } else if (!strcmp(name, "ble-pair")) {
    uiBlePair(lcd, 482913);
  } else if (!strcmp(name, "ble-wait")) {
    uiBlePair(lcd, 0);
  } else if (!strcmp(name, "unlock")) {
    uiUnlock(lcd, false);
  } else if (!strcmp(name, "unlock-wait")) {
    uiUnlock(lcd, true);
  } else if (!strcmp(name, "waiting")) {
    waitingShot(lcd);
  } else if (!strcmp(name, "dev-edit")) {
    uiDeviceEditor(lcd, DeviceSettings{160, 15, 0, 5, 2});
  } else if (!strncmp(name, "nav-", 4)) {
    if (!navShot(lcd, name + 4)) return false;
  } else if (!strncmp(name, "vol-", 4)) {
    if (!volumeShot(lcd, name + 4)) return false;
  } else {
    return false;
  }
  char path[512];
  snprintf(path, sizeof(path), "%s-%s.png", g_prefix, name);
  if (!grab(lcd, path)) {
    fprintf(stderr, "could not write %s\n", path);
    g_rc = 1;
  } else {
    printf("%s\n", path);
  }
  return true;
}

int render(bool *running) {
  static Display lcd;
  lcd.init();
  lcd.setRotation(0);
  uiBegin(lcd);

  if (g_json && g_json[0] == '@') {
    if (!renderSpecial(lcd, g_json + 1)) {
      fprintf(stderr, "unknown view: %s\n", g_json);
      g_rc = 1;
    }
    *running = false;
    fflush(stdout);
    _exit(g_rc);
  }

  JsonDocument doc;
  Payload payload{};
  const std::string text = slurp(g_json);
  char err[64] = "";
  if (deserializeJson(doc, text) != DeserializationError::Ok ||
      !payloadFromJson(doc, payload, err, sizeof(err))) {
    fprintf(stderr, "bad payload: %s\n", err[0] ? err : "not JSON");
    g_rc = 1;
    *running = false;
    return 1;
  }

  for (uint8_t i = 0; i < payload.nCards; i++) {
    // Draw until the entry animation has settled, so the grab shows the
    // resting layout rather than a frame part-way through the sweep.
    for (int frame = 0; frame < 400; frame++) {
      uiRender(lcd, payload, i, true, 4000, batteryViewFromEnv(), linkFromEnv());
      if (!uiAnimating()) break;
      delay(8);
    }
    char path[512];
    snprintf(path, sizeof(path), "%s-%u.png", g_prefix, (unsigned)i);
    if (!grab(lcd, path)) {
      fprintf(stderr, "could not write %s\n", path);
      g_rc = 1;
    } else {
      printf("%s  (%s)\n", path, payload.cards[i].title);
    }
  }

  // Panel_sdl::main keeps pumping its event loop after `running` clears, and
  // there is nothing left to pump for: leave straight from here.
  *running = false;
  fflush(stdout);
  _exit(g_rc);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc > 1) g_prefix = argv[1];
  if (argc > 2) g_json = argv[2];
  // SDL still backs the panel, but a dummy video driver keeps it off-screen.
  setenv("SDL_VIDEODRIVER", "dummy", 0);
  lgfx::Panel_sdl::main(render);
  return g_rc;
}
