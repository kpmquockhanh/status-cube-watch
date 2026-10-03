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
// Turns the radio off (BLE is carrying the data).
void netStop();
// Fetches and parses the bridge payload. Leaves `out` untouched on failure so
// the display keeps showing the last good data instead of blanking.
bool netFetch(Payload &out);
const char *netLastError();
