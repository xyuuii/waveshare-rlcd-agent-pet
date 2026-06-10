#pragma once

#include <stdint.h>

#include "models.h"

struct PetBitmapFrame {
  uint8_t width;
  uint8_t height;
  const char* bits;
};

bool petBitmapPixel(const PetBitmapFrame& frame, uint8_t x, uint8_t y);
const PetBitmapFrame& bitmapForMode(PetMode mode, uint32_t tickMs);
const char* activePetSpecies();
