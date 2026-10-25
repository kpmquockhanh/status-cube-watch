// Headless frame grab. Renders cards through the real ui.cpp and writes PNGs,
// so the layout can be compared against the design without a window (and
// without a screen-recording permission prompt).
//
//   make shot                       # every card of the live bridge payload
//   ./build/cube-shot out card.json # <out>-0.png, <out>-1.png, ...
//   ./build/cube-shot out @portal   # <out>-portal.png: a screen that is not a
//                                   # payload card (see renderSpecial):
//                                   # @portal, @ota, @ble-pair, @ble-wait, @waiting, @dev-edit,
//                                   # @unlock|unlock-wait, @nav-turn|vn|long|longeta|reroute|offroute|arrived|lost|busy, @vol-56|muted|full|zero|fixed|noout|long|none|press|playing|paused,
//                                   # @agents, @agents-scroll, @agents-hats, @agents-long, @prompt, @prompt-settle|sent|noanswer|tmux|locked|input|cut|long|wifi|queued|dropped
//                                   # @detail, @detail-loading|wifi|err|long
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
  static const Payload none{};
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
  static const Payload none{};
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

// A saved payload parsed into `out` (a Payload is too big for a stack).
bool loadPayload(const char *file, Payload &out) {
  JsonDocument doc;
  char err[64] = "";
  if (deserializeJson(doc, slurp(file)) != DeserializationError::Ok || !payloadFromJson(doc, out, err, sizeof(err))) {
    fprintf(stderr, "bad payload %s: %s\n", file, err[0] ? err : "not JSON");
    return false;
  }
  return true;
}

// The Agents card: fixtures/agents-full.json at the top, ("scroll")
// fixtures/agents-many.json's eight tiles scrolled to the end, or ("long")
// fixtures/agents-long.json's 16-character names and lines.
bool agentsShot(Display &lcd, const char *state) {
  static Payload p;
  const bool scroll = !strcmp(state, "scroll"), longer = !strcmp(state, "long");
  if (!scroll && !longer && state[0]) return false;
  const char *file = scroll ? "fixtures/agents-many.json" : longer ? "fixtures/agents-long.json" : "fixtures/agents-full.json";
  if (!loadPayload(file, p)) return false;
  const int16_t end = (int16_t)(agentsContentH(p.agents.nTiles) - AGENTS_VIEW_H);
  const AgentsUi ui{scroll && end > 0 ? end : (int16_t)0};
  uiRender(lcd, p, p.agents.card, true, 4000, batteryViewFromEnv(), linkFromEnv(), nullptr, &ui);
  return true;
}

// Every hat shape on a tile, in two PNGs (<prefix>-agents-hats-1.png: shapes
// 0-7, -2.png: 8-12). Each is the grid at the top with the rest of its rows,
// scrolled to the end, stacked under it (240 x 480): the view band only.
bool hatsShot(Display &lcd) {
  static Payload p;
  static const char *const NAMES[13] = {"hard hat", "cap",   "beanie",    "helmet",    "wizard",  "beret",  "chef",
                                        "crown",    "headphones", "propeller", "top hat", "cowboy", "bandana"};
  static const char *const POSES[4] = {"think", "idle", "bash", "celebrate"};
  static const AgentKind KINDS[4] = {AgentKind::Run, AgentKind::Idle, AgentKind::Run, AgentKind::Idle};
  if (!loadPayload("fixtures/agents-many.json", p)) return false;
  static std::vector<uint8_t> out((size_t)LCD_WIDTH * (LCD_HEIGHT + AGENTS_VIEW_H) * 3);
  for (int part = 0; part < 2; part++) {
    const int first = part * 8, n = part == 0 ? 8 : 5;
    AgentsData &a = p.agents;
    a.nTiles = (uint8_t)n;
    for (int i = 0; i < n; i++) {
      AgentTile &t = a.tiles[i];
      const int shape = first + i;
      snprintf(t.id, sizeof(t.id), "h%02d", shape);
      snprintf(t.name, sizeof(t.name), "%s", NAMES[shape]);
      snprintf(t.line, sizeof(t.line), "hat %d \xC2\xB7 colour %d", shape, shape % 8);
      snprintf(t.pose, sizeof(t.pose), "%s", POSES[i % 4]);
      t.kind = KINDS[i % 4];
      t.ctx = (uint8_t)(10 + shape * 7);
      t.unseen = false;
      t.hat = (int16_t)(shape * 8 + shape % 8);
    }
    const int16_t end = (int16_t)(agentsContentH(a.nTiles) - AGENTS_VIEW_H);
    for (int pass = 0; pass < 2; pass++) {
      const AgentsUi ui{pass ? end : (int16_t)0};
      uiRender(lcd, p, a.card, true, 4000, batteryViewFromEnv(), linkFromEnv(), nullptr, &ui);
      char path[512];
      snprintf(path, sizeof(path), "%s-agents-hats-%d.png", g_prefix, part + 1);
      if (!grab(lcd, path)) return false;  // fills g_rgb; overwritten below
      const size_t row = (size_t)LCD_WIDTH * 3;
      if (pass == 0) {
        memcpy(out.data(), g_rgb.data(), row * LCD_HEIGHT);
      } else {
        memcpy(out.data() + row * LCD_HEIGHT, g_rgb.data() + row * AGENTS_VIEW_Y, row * AGENTS_VIEW_H);
        if (!writePng(path, out.data(), LCD_WIDTH, LCD_HEIGHT + AGENTS_VIEW_H)) return false;
        printf("%s\n", path);
      }
    }
  }
  return true;
}

// The prompt screen of the waiting agent in fixtures/agents-full.json, in one
// of AgentView's states (set here directly, not through AgentView).
bool promptShot(Display &lcd, const char *state) {
  static Payload p;
  const char *file = !strcmp(state, "long")   ? "fixtures/agents-many.json"
                     : !strcmp(state, "wifi") ? "fixtures/agents-lan.json"
                                              : "fixtures/agents-full.json";
  if (!loadPayload(file, p)) return false;
  AgentsData &a = p.agents;
  PromptUi v{PromptScreen::Prompt, ButtonsOff::No, -1, 0, nullptr};
  const AgentTile *tile = &a.tiles[0];
  UiLink link = linkFromEnv();
  if (!strcmp(state, "")) {
    // ready: settled, nothing sent
  } else if (!strcmp(state, "settle")) {
    v.off = ButtonsOff::Settling;
  } else if (!strcmp(state, "sent")) {
    v.off = ButtonsOff::Sent;
    v.lit = 1;
  } else if (!strcmp(state, "noanswer")) {
    v.off = ButtonsOff::NoAnswer;
    v.lit = 1;
  } else if (!strcmp(state, "tmux")) {
    a.prompt.notInTmux = true;
    v.off = ButtonsOff::NotTmux;
  } else if (!strcmp(state, "locked")) {
    v.off = ButtonsOff::Locked;
  } else if (!strcmp(state, "input")) {
    a.prompt.kind = PromptKind::Input;
    a.prompt.nOpts = 0;
    snprintf(a.prompt.title, sizeof(a.prompt.title), "What should the branch be called?");
    a.prompt.detail[0] = '\0';
    v.off = ButtonsOff::Input;
  } else if (!strcmp(state, "cut")) {
    a.prompt.cut = true;
    a.prompt.detail[0] = '\0';
  } else if (!strcmp(state, "long")) {  // six options, scrolled to the end
    v.scroll = (int16_t)(uiPromptLayout(a.prompt, v.off).contentH() - PROMPT_VIEW_H);
  } else if (!strcmp(state, "wifi")) {
    v.screen = PromptScreen::OverWifi;
    link = UiLink::Wifi;
  } else if (!strcmp(state, "queued")) {  // a second waiting tile while q is the first's
    a.tiles[1].kind = AgentKind::Wait;
    tile = &a.tiles[1];
    v.screen = PromptScreen::Queued;
    v.queuedName = a.tiles[0].name;
  } else if (!strcmp(state, "dropped")) {
    a.hasPrompt = false;
    a.promptDropped = true;
    v.screen = PromptScreen::Dropped;
  } else {
    return false;
  }
  uiAgentPrompt(lcd, a, *tile, v, true, 4000, batteryViewFromEnv(), link);
  return true;
}

// The detail screen of fixtures/agents-full.json's running agent l00002, whose
// `d` the fixture carries: as it arrives over BLE (""), before `d` comes
// ("loading"), from a WiFi payload ("wifi"), with a last error ("err").
bool detailShot(Display &lcd, const char *state) {
  static Payload p;
  if (!loadPayload("fixtures/agents-full.json", p)) return false;
  AgentsData &a = p.agents;
  const AgentTile *tile = agentsTile(a, "l00002");
  if (!tile) return false;
  bool viaBle = true;
  UiLink link = linkFromEnv();
  if (!strcmp(state, "")) {
  } else if (!strcmp(state, "loading")) {
    a.detail.present = false;
  } else if (!strcmp(state, "wifi")) {
    a.detail.present = false;  // the LAN variant never carries `d`
    viaBle = false;
    link = UiLink::Wifi;
  } else if (!strcmp(state, "err")) {
    snprintf(a.detail.err, sizeof(a.detail.err), "npm test exited 1: 3 failing in session.test.ts");
  } else if (!strcmp(state, "long")) {  // every field at the bridge's maximum
    snprintf(a.detail.title, sizeof(a.detail.title), "fix the refresh-token race in session...");
    snprintf(a.detail.status, sizeof(a.detail.status), "running \xc2\xb7 1h 38m");
    snprintf(a.detail.doing, sizeof(a.detail.doing), "edit \xc2\xb7 src/auth/very/deep/path/session.ts");
    snprintf(a.detail.prompt, sizeof(a.detail.prompt),
             "fix the refresh-token race in session.ts and add a regression test for concurrent logins, then "
             "run the whole suite and tell me what else breaks...");
    snprintf(a.detail.subs, sizeof(a.detail.subs), "12 subagents");
  } else {
    return false;
  }
  uiAgentDetail(lcd, a, *tile, tile->hat, viaBle, true, 4000, batteryViewFromEnv(), link);
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
  } else if (!strcmp(name, "agents-hats")) {
    if (!hatsShot(lcd)) {
      fprintf(stderr, "could not write the hats shots\n");
      g_rc = 1;
    }
    return true;
  } else if (!strcmp(name, "agents") || !strncmp(name, "agents-", 7)) {
    if (!agentsShot(lcd, name[6] ? name + 7 : "")) return false;
  } else if (!strcmp(name, "prompt") || !strncmp(name, "prompt-", 7)) {
    if (!promptShot(lcd, name[6] ? name + 7 : "")) return false;
  } else if (!strcmp(name, "detail") || !strncmp(name, "detail-", 7)) {
    if (!detailShot(lcd, name[6] ? name + 7 : "")) return false;
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
  static Payload payload;
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
