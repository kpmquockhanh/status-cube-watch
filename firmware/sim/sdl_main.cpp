// SDL entry point. LovyanGFX pumps the window's event loop on the main thread
// and runs setup()/loop() on another, which is why main.cpp needs no changes.

#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

void setup();
void loop();

static int runFirmware(bool *running) {
  setup();
  while (*running) loop();
  return 0;
}

int main(int, char **) { return lgfx::Panel_sdl::main(runFirmware); }
