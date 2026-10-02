// Desktop stand-in for ota.cpp: there is nothing to flash in a simulator.

#include "../src/ota.h"

void otaBegin(Display &) {}
void otaHandle() {}
void otaEnd() {}
