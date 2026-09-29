#include "host_frame.h"

#include <stdio.h>

#include "ST7305_U8g2.h"

bool hostWritePanelPbm(const std::string& path) {
  FILE* file = fopen(path.c_str(), "wb");
  if (!file) {
    return false;
  }
  constexpr int kWidth = 400;
  constexpr int kHeight = 300;
  fprintf(file, "P4\n%d %d\n", kWidth, kHeight);
  for (int y = 0; y < kHeight; ++y) {
    unsigned char row[kWidth / 8] = {0};
    for (int x = 0; x < kWidth; ++x) {
      if (!hostPanelPixelIsLight(x, y)) {
        row[x / 8] |= static_cast<unsigned char>(0x80 >> (x & 7));  // PBM: 1 = black
      }
    }
    fwrite(row, 1, sizeof(row), file);
  }
  fclose(file);
  return true;
}

int hostCountDarkPixels(int x, int y, int w, int h) {
  int count = 0;
  for (int row = y; row < y + h; ++row) {
    for (int col = x; col < x + w; ++col) {
      if (!hostPanelPixelIsLight(col, row)) {
        ++count;
      }
    }
  }
  return count;
}
