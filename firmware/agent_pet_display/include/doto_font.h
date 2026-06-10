#pragma once

#include <stdint.h>

enum class DotoFontSize : uint8_t {
  Small,
  Medium,
  Hero,
};

int dotoTextWidth(DotoFontSize size, const char* text);
int dotoTextHeight(DotoFontSize size);
int dotoGlyphAdvance(DotoFontSize size, char ch);
bool dotoGlyphPixel(DotoFontSize size, char ch, uint8_t x, uint8_t y);
