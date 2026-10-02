#pragma once
#include "payload.h"

// Joins the stored network. Returns false if it is still not connected after
// about 20 s, so the caller can fall back to the setup portal.
bool netBegin();
bool netOnline();
// Fetches and parses the bridge payload. Leaves `out` untouched on failure so
// the display keeps showing the last good data instead of blanking.
bool netFetch(Payload &out);
const char *netLastError();
