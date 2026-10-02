#pragma once
#include "display.h"

// Runs the setup portal: the cube starts its own open WiFi network
// (claude-cube-XXXX), serves a one-page form at 192.168.4.1 and answers every
// DNS query with its own address so phones pop the page up by themselves.
// Never returns: saving the form reboots the board.
//
// autoRetry: reboot after a minute with nobody joined, so a cube that merely
// lost its WiFi (power cut, router still booting) goes back to retrying the
// stored network by itself. Pass false when the user asked for the portal.
[[noreturn]] void portalRun(Display &lcd, bool autoRetry);
