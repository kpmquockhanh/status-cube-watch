// Headless frame grab. Renders cards through the real ui.cpp and writes PNGs,
// so the layout can be compared against the design without a window (and
// without a screen-recording permission prompt).
//
//   make shot                       # every card of the live bridge payload
//   ./build/cube-shot out card.json # <out>-0.png, <out>-1.png, ...
//   ./build/cube-shot out @portal   # <out>-portal.png: a screen that is not a
//                                   # payload card (see renderSpecial):
//                                   # @portal, @ota, @pomo-ready|focus|paused|break|done|long|edit
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
#include "../src/payload.h"
#include "../src/pomodoro.h"
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

// Drives the real state machine to a named state and draws it with an empty
// payload (the bridge-down case: the Pomodoro is the only card), so the shot
// shows what the board would draw rather than a hand-built view.
bool pomodoroShot(Display &lcd, const char *state) {
  const uint32_t MIN = 60000;
  Pomodoro p(PomoConfig{25 * MIN, 5 * MIN, 15 * MIN, 4});
  uint32_t now = 1000;
  auto run = [&](uint32_t ms) {
    now += ms;
    p.tick(now);
  };

  if (!strcmp(state, "ready")) {
  } else if (!strcmp(state, "focus")) {
    p.longPress(now);
    run(7 * MIN + 3000);
  } else if (!strcmp(state, "paused")) {
    p.longPress(now);
    run(7 * MIN + 3000);
    p.longPress(now);
  } else if (!strcmp(state, "break")) {
    p.longPress(now);
    run(25 * MIN);
    p.longPress(now);
    run(90000);
  } else if (!strcmp(state, "done")) {
    p.longPress(now);
    run(25 * MIN);
  } else if (!strcmp(state, "long")) {  // three full cycles, a fourth focus, then the long break
    for (int i = 0; i < 3; i++) {
      p.longPress(now);
      run(25 * MIN);
      p.longPress(now);
      run(5 * MIN);
    }
    p.longPress(now);
    run(25 * MIN);
    p.longPress(now);
    run(60000);
  } else {
    return false;
  }

  const Payload none{};
  const PomoView v = p.view();
  for (int frame = 0; frame < 400; frame++) {
    uiRender(lcd, none, 0, true, 4000, v, batteryViewFromEnv());
    if (!uiAnimating()) break;
    delay(8);
  }
  return true;
}

// Screens that are not payload cards. Selected with `@name` on the command
// line; the grab goes to <prefix>-<name>.png. Returns false for an unknown name.
bool renderSpecial(Display &lcd, const char *name) {
  if (!strcmp(name, "portal")) {
    uiPortal(lcd, "claude-cube-A1B2");
  } else if (!strcmp(name, "ota")) {
    uiOta(lcd, 62);
  } else if (!strcmp(name, "pomo-edit")) {
    uiPomodoroEditor(lcd, PomoSettings{30, 5, 15, 4});
  } else if (!strncmp(name, "pomo-", 5)) {
    if (!pomodoroShot(lcd, name + 5)) return false;
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

  const PomoView idle = Pomodoro(PomoConfig{25 * 60000UL, 5 * 60000UL, 15 * 60000UL, 4}).view();
  for (uint8_t i = 0; i < payload.nCards; i++) {
    // Draw until the entry animation has settled, so the grab shows the
    // resting layout rather than a frame part-way through the sweep.
    for (int frame = 0; frame < 400; frame++) {
      uiRender(lcd, payload, i, true, 4000, idle, batteryViewFromEnv());
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
