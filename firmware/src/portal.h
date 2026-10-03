#pragma once
#include "display.h"

// Runs the setup portal: the cube starts its own open WiFi network
// (claude-cube-XXXX), serves a one-page form at 192.168.71.1 and answers every
// DNS query with its own address so phones pop the page up by themselves.
// Runs in AP+STA mode: while no phone is on the AP and no Mac is pairing, the
// station side makes a short attempt at the saved network every 30 s, and once
// joined the form is also reachable at the cube's LAN address (shown on the
// screen). Every request must name the cube by address and every POST carry the
// form's token, so another page on the LAN cannot drive the form.
// Never returns: saving the form reboots the board.
//
// autoRetry: the cube got here because the saved network did not answer
// (power cut, router still booting), so it reboots into the normal boot flow
// as soon as the station side joins that network again, unless a phone is on
// the form or a Mac is pairing. Pass false when the user asked for the portal.
[[noreturn]] void portalRun(Display &lcd, bool autoRetry);
