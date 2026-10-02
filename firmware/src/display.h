#pragma once
// LovyanGFX device definition for the board's ST7789V2 panel.
//
// LovyanGFX rather than LVGL: this dashboard is a couple of static cards, so
// a full-screen sprite redrawn on change is simpler, has no lv_conf.h to
// maintain, and leaves far more RAM free. If the UI grows real widgets,
// LVGL 9 is the upgrade path.

#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>
#include "board_pins.h"

#ifdef LGFX_SDL
// Desktop simulator (see sim/). Same panel geometry, same rendering code, an
// SDL window instead of the ST7789 -- so ui.cpp is exercised unmodified.
#include <lgfx/v1/platforms/sdl/Panel_sdl.hpp>

#ifndef SIM_SCALE
#define SIM_SCALE 2
#endif

class Display;
// The simulator's touch shim reads gestures back through this. Nothing
// outside the LGFX_SDL branch sees it, so the firmware build is unaffected.
extern Display *g_simDisplay;

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_sdl _panel;

 public:
  Display() {
    auto cfg = _panel.config();
    cfg.panel_width = cfg.memory_width = LCD_WIDTH;
    cfg.panel_height = cfg.memory_height = LCD_HEIGHT;
    _panel.config(cfg);
    _panel.setScaling(SIM_SCALE, SIM_SCALE);
    _panel.setWindowTitle("claude-status-cube (sim)");
    setPanel(&_panel);
    g_simDisplay = this;
  }
  // No backlight on a desktop window; accept the call and ignore it.
  void setBrightness(uint8_t) {}
};

#else  // ---- real hardware ------------------------------------------------

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  Display() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = SPI2_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 80000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_LCD_SCLK;
      cfg.pin_mosi = PIN_LCD_MOSI;
      cfg.pin_miso = -1;
      cfg.pin_dc = PIN_LCD_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = PIN_LCD_CS;
      cfg.pin_rst = PIN_LCD_RST;
      cfg.pin_busy = -1;
      cfg.panel_width = LCD_WIDTH;
      cfg.panel_height = LCD_HEIGHT;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = LCD_OFFSET_Y;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = true;     // ST7789 panels are inverted; flip if colours look wrong
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_LCD_BL;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

#endif  // LGFX_SDL
