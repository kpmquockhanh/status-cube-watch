#pragma once
#include "display.h"

// Over-the-air flashing: after one USB flash, `pio run -t upload` with
// upload_protocol = espota reaches the cube as claude-cube.local.
//
// otaBegin once WiFi is up; otaHandle every loop. A transfer runs entirely
// inside otaHandle(), so the loop (polling, drawing) is paused while an image
// arrives without anyone having to arrange it.
void otaBegin(Display &lcd);
void otaHandle();
