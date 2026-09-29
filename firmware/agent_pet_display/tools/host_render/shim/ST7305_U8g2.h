#pragma once

// Host stand-in for lib/ST7305_U8g2. It keeps the exact U8g2 geometry of the
// real panel (300x400 native, 38x50 tiles, same buffer layout and rotation),
// but instead of pushing tiles over SPI it lets the harness read the buffer.

#include <U8g2lib.h>

class ST7305_U8g2 {
 public:
  ST7305_U8g2(int sck = 11, int mosi = 12, int dc = 5, int cs = 40, int rst = 41);
  ~ST7305_U8g2();

  void begin(uint8_t tile_buf_height = 0, const u8g2_cb_t* rotation = U8G2_R0);
  void reset() {}
  void fullInit() {}

  U8G2* getU8g2() { return &u8g2_wrapper; }

 private:
  U8G2 u8g2_wrapper;
  uint8_t* _my_buf = nullptr;
};

// Landscape (as the user sees it, after U8G2_R1) pixel query: true = light.
bool hostPanelPixelIsLight(int x, int y);
// Number of sendBuffer() calls since start; lets tests check frame pushes.
uint32_t hostPanelFlushCount();
// Raw native buffer (304x400 vertical-LSB layout, 15200 bytes) for blit tests.
uint8_t* hostPanelBuffer();
