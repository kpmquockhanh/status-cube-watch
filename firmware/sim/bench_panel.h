#pragma once
// `make bench` (sim/bench.cpp): the panel that frames go to in the energy bench.
// It is a framebuffer with no window. Each write is counted and reported to the
// bench, which charges it the time the board's 80 MHz SPI bus needs to carry it.
// display.h selects it when SIM_BENCH is defined.

#include <cstdlib>

#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>

// Implemented in bench.cpp. Host time spent between enter and leave is the
// desktop copying pixels, not work the board does, so the bench takes it out of
// the loop's time and charges SPI time instead.
void benchPanelEnter();
void benchPanelLeave(uint32_t pixels, uint32_t windows);
void benchBacklight(uint8_t level);
void benchPanelSleep(bool asleep);
// ui.cpp calls this after sending a frame (SIM_BENCH only): the bench checks
// that the panel now holds exactly that frame, so a row the partial push
// wrongly skipped fails `make bench`.
void benchFrameSent(const uint16_t *frame);

class BenchPanel : public lgfx::Panel_FrameBufferBase {
  int _depth = 0;  // writeBlock calls writeFillRectPreclipped: count only the outer call

  struct Count {
    BenchPanel *p;
    uint32_t px, win;
    Count(BenchPanel *p, uint32_t px, uint32_t win) : p(p), px(px), win(win) {
      if (p->_depth++ == 0) benchPanelEnter();
    }
    ~Count() {
      if (--p->_depth == 0) benchPanelLeave(px, win);
    }
  };

 public:
  bool init(bool use_reset) override {
    const size_t w = _cfg.panel_width, h = _cfg.panel_height;
    _lines_buffer = (uint8_t **)calloc(h, sizeof(uint8_t *));
    uint8_t *fb = (uint8_t *)calloc(w * 4 * h, 1);
    for (size_t y = 0; y < h; y++) _lines_buffer[y] = fb + y * w * 4;
    return Panel_FrameBufferBase::init(use_reset);
  }
  lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override {
    _write_depth = _read_depth = lgfx::rgb565_2Byte;
    return lgfx::rgb565_2Byte;
  }

  // A window is one CASET/RASET/RAMWR on the board; pixels follow it.
  void setWindow(uint_fast16_t xs, uint_fast16_t ys, uint_fast16_t xe, uint_fast16_t ye) override {
    Count c(this, 0, 1);
    Panel_FrameBufferBase::setWindow(xs, ys, xe, ye);
  }
  void drawPixelPreclipped(uint_fast16_t x, uint_fast16_t y, uint32_t raw) override {
    Count c(this, 1, 1);
    Panel_FrameBufferBase::drawPixelPreclipped(x, y, raw);
  }
  void writeFillRectPreclipped(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                               uint32_t raw) override {
    Count c(this, w * h, 1);
    Panel_FrameBufferBase::writeFillRectPreclipped(x, y, w, h, raw);
  }
  void writeBlock(uint32_t raw, uint32_t len) override {
    Count c(this, len, 0);
    Panel_FrameBufferBase::writeBlock(raw, len);
  }
  void writeImage(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                  lgfx::pixelcopy_t *param, bool use_dma) override {
    Count c(this, w * h, 1);
    Panel_FrameBufferBase::writeImage(x, y, w, h, param, use_dma);
  }
  void writeImageARGB(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h,
                      lgfx::pixelcopy_t *param) override {
    Count c(this, w * h, 1);
    Panel_FrameBufferBase::writeImageARGB(x, y, w, h, param);
  }
  void writePixels(lgfx::pixelcopy_t *param, uint32_t len, bool use_dma) override {
    Count c(this, len, 0);
    Panel_FrameBufferBase::writePixels(param, len, use_dma);
  }

  // What the glass shows, row `y`, in the sprite's byte order (rotation 0).
  const uint8_t *line(int y) const { return _lines_buffer[y]; }

  // Display's constructor configures the SDL window; there is none here.
  void setScaling(int, int) {}
  void setWindowTitle(const char *) {}
};
