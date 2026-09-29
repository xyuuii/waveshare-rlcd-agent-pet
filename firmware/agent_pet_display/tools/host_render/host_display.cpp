#include <Arduino.h>

#include "ST7305_U8g2.h"

namespace {

constexpr int kNativeWidth = 300;
constexpr int kNativeHeight = 400;
constexpr int kTileWidth = 38;
constexpr int kTileHeight = 50;
constexpr int kRowBytes = kTileWidth * 8;

uint32_t gMillis = 0;
uint32_t gFlushCount = 0;
uint8_t* gBuffer = nullptr;

u8x8_display_info_t gHostDisplayInfo = {
    /* chip_enable_level = */ 0,
    /* chip_disable_level = */ 1,
    /* post_chip_enable_wait_ns = */ 0,
    /* pre_chip_disable_wait_ns = */ 0,
    /* reset_pulse_width_ms = */ 0,
    /* post_reset_wait_ms = */ 0,
    /* sda_setup_time_ns = */ 0,
    /* sck_pulse_width_ns = */ 0,
    /* sck_clock_hz = */ 24000000,
    /* spi_mode = */ 0,
    /* i2c_bus_clock_100kHz = */ 4,
    /* data_setup_time_ns = */ 0,
    /* write_pulse_width_ns = */ 0,
    /* tile_width = */ kTileWidth,
    /* tile_height = */ kTileHeight,
    /* default_x_offset = */ 0,
    /* flip_mode_x_offset = */ 0,
    /* pixel_width = */ kNativeWidth,
    /* pixel_height = */ kNativeHeight,
};

uint8_t hostDisplayCallback(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
  (void)arg_int;
  switch (msg) {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
      u8x8_d_helper_display_setup_memory(u8x8, &gHostDisplayInfo);
      return 1;
    case U8X8_MSG_DISPLAY_INIT:
    case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
    case U8X8_MSG_DISPLAY_SET_CONTRAST:
    case U8X8_MSG_DISPLAY_SET_FLIP_MODE:
      return 1;
    case U8X8_MSG_DISPLAY_DRAW_TILE: {
      const u8x8_tile_t* tile = static_cast<const u8x8_tile_t*>(arg_ptr);
      if (tile && tile->y_pos == kTileHeight - 1) {
        ++gFlushCount;
      }
      return 1;
    }
    default:
      return 0;
  }
}

uint8_t hostByteCallback(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
  (void)u8x8;
  (void)msg;
  (void)arg_int;
  (void)arg_ptr;
  return 1;
}

}  // namespace

uint32_t millis() {
  return gMillis;
}

void delay(uint32_t ms) {
  gMillis += ms;
}

void pinMode(int, int) {}
void digitalWrite(int, int) {}
int digitalRead(int) {
  return HIGH;
}

void hostSetMillis(uint32_t value) {
  gMillis = value;
}

ST7305_U8g2::ST7305_U8g2(int, int, int, int, int) {}

ST7305_U8g2::~ST7305_U8g2() {
  if (_my_buf) {
    free(_my_buf);
    _my_buf = nullptr;
    gBuffer = nullptr;
  }
}

void ST7305_U8g2::begin(uint8_t tile_buf_height, const u8g2_cb_t* rotation) {
  u8g2_t* u = u8g2_wrapper.getU8g2();
  u8x8_Setup(u8g2_GetU8x8(u), hostDisplayCallback, u8x8_dummy_cb, hostByteCallback, u8x8_dummy_cb);
  uint8_t tbh = tile_buf_height == 0 ? kTileHeight : tile_buf_height;
  if (tbh > kTileHeight) {
    tbh = kTileHeight;
  }
  const size_t bufferSize = static_cast<size_t>(kRowBytes) * tbh;
  _my_buf = static_cast<uint8_t*>(calloc(bufferSize, 1));
  gBuffer = _my_buf;
  u8g2_SetupBuffer(u, _my_buf, tbh, u8g2_ll_hvline_vertical_top_lsb, rotation);
  u8g2_InitDisplay(u);
  u8g2_SetPowerSave(u, 0);
}

bool hostPanelPixelIsLight(int x, int y) {
  if (!gBuffer || x < 0 || y < 0 || x >= kNativeHeight || y >= kNativeWidth) {
    return true;
  }
  // U8G2_R1 maps landscape (x, y) to native (299 - y, x); bytes are vertical, LSB on top.
  const int nativeX = kNativeWidth - 1 - y;
  const int nativeY = x;
  const uint8_t byte = gBuffer[(nativeY / 8) * kRowBytes + nativeX];
  return (byte >> (nativeY & 7)) & 1;
}

uint32_t hostPanelFlushCount() {
  return gFlushCount;
}

uint8_t* hostPanelBuffer() {
  return gBuffer;
}
