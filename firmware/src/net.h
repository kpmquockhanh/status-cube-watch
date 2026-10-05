#pragma once
#include "payload.h"

// Joins the stored network. Returns false if it is still not connected after
// about 20 s, so the caller can fall back to the setup portal.
bool netBegin();
bool netOnline();
// Starts joining the stored network and returns at once; netOnline() turns
// true when it is up. netBegin() is this plus a wait of up to ~20 s.
void netStart();
// WiFi.begin() with the stored network, whatever mode the radio is in (the
// setup portal joins from AP+STA). netStart() is this in station mode.
void netJoinSaved();
// Turns the radio off (BLE is carrying the data). A fetch still running is
// given a few seconds to finish first; its result is dropped.
void netStop();

enum class NetFetch : uint8_t { None, Ok, Failed };

// Starts fetching the bridge payload and returns at once: the request runs on
// a task of its own, so a slow or dead bridge (or a DNS lookup that hangs)
// never holds up the loop. Ignored while a fetch is still running.
void netRequest();
// The result of the last netRequest(), once. None while it is still running
// or when none was made. Ok: the parsed payload is copied to `out`. Failed:
// `out` is untouched, so the display keeps the last good data instead of
// blanking, and netLastError() says why. Call it every loop pass.
NetFetch netTake(Payload &out);
const char *netLastError();
