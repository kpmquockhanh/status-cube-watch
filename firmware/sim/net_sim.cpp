// Desktop replacement for net.cpp: a plain socket HTTP GET instead of
// WiFi + HTTPClient. The parsing it feeds is the real payloadFromJson(), so
// the simulator and the board agree on how a payload is interpreted.

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <string>

#include <ArduinoJson.h>

#include "../src/config.h"
#include "../src/net.h"

namespace {

char g_error[64] = "";
bool g_online = false;

void fail(const char *msg) { strlcpy(g_error, msg, sizeof(g_error)); }

struct Url {
  std::string host, port, path;
};

bool parseUrl(const std::string &url, Url &out) {
  if (url.compare(0, 7, "http://") != 0) return false;   // the bridge is plain HTTP on the LAN
  const std::string rest = url.substr(7);
  const size_t slash = rest.find('/');
  const std::string authority = rest.substr(0, slash);
  out.path = slash == std::string::npos ? "/" : rest.substr(slash);
  const size_t colon = authority.find(':');
  out.host = authority.substr(0, colon);
  out.port = colon == std::string::npos ? "80" : authority.substr(colon + 1);
  return !out.host.empty();
}

// CUBE_BRIDGE_URL lets you point the simulator somewhere else without editing
// config.h -- handy for trying a payload before you commit to it.
std::string bridgeUrl() {
  const char *env = getenv("CUBE_BRIDGE_URL");
  return env && *env ? env : BRIDGE_URL;
}

bool httpGet(const Url &u, std::string &body) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *res = nullptr;
  if (getaddrinfo(u.host.c_str(), u.port.c_str(), &hints, &res) != 0 || !res) {
    fail("dns failed");
    return false;
  }

  int fd = -1;
  for (addrinfo *a = res; a; a = a->ai_next) {
    fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
    if (fd < 0) continue;
    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) {
    fail("connect refused");
    return false;
  }

  const std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host +
                          "\r\nConnection: close\r\n\r\n";
  if (send(fd, req.data(), req.size(), 0) < 0) {
    close(fd);
    fail("send failed");
    return false;
  }

  std::string raw;
  char buf[4096];
  ssize_t n;
  while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) raw.append(buf, n);
  close(fd);

  const size_t sep = raw.find("\r\n\r\n");
  if (sep == std::string::npos) {
    fail("truncated response");
    return false;
  }
  const int code = raw.size() > 12 ? atoi(raw.c_str() + 9) : 0;
  if (code != 200) {
    snprintf(g_error, sizeof(g_error), "http %d", code);
    return false;
  }

  body = raw.substr(sep + 4);

  // The bridge sends Content-Length, but any HTTP/1.1 server may chunk, and a
  // client that only handles one framing is a trap waiting for someone who
  // puts a proxy in front of this.
  std::string headers = raw.substr(0, sep);
  for (char &c : headers) c = tolower(c);
  if (headers.find("transfer-encoding: chunked") != std::string::npos) {
    std::string decoded;
    size_t pos = 0;
    while (pos < body.size()) {
      const size_t eol = body.find("\r\n", pos);
      if (eol == std::string::npos) break;
      const size_t len = strtoul(body.substr(pos, eol - pos).c_str(), nullptr, 16);
      if (len == 0) break;                      // terminating chunk
      if (eol + 2 + len > body.size()) {
        fail("truncated chunk");
        return false;
      }
      decoded.append(body, eol + 2, len);
      pos = eol + 2 + len + 2;                  // skip the chunk's trailing CRLF
    }
    body.swap(decoded);
  }
  return true;
}

}  // namespace

bool netBegin() {
  Serial.printf("[net] simulator -> %s\n", bridgeUrl().c_str());
  return true;
}

void netStart() { Serial.println("[net] simulator wifi on"); }
void netStop() { Serial.println("[net] simulator wifi off"); }

bool netOnline() { return g_online; }

const char *netLastError() { return g_error; }

bool netFetch(Payload &out) {
  Url u;
  if (!parseUrl(bridgeUrl(), u)) {
    fail("bad BRIDGE_URL");
    g_online = false;
    return false;
  }

  std::string body;
  if (!httpGet(u, body)) {
    g_online = false;
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    snprintf(g_error, sizeof(g_error), "json: %s", err.c_str());
    g_online = false;
    return false;
  }

  Payload p{};
  if (!payloadFromJson(doc, p, g_error, sizeof(g_error))) {
    g_online = false;
    return false;
  }

  g_error[0] = '\0';
  g_online = true;
  out = p;
  return true;
}
