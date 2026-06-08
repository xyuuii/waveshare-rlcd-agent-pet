#pragma once

#include <stdint.h>

#include "models.h"

struct PetSpriteFrame {
  const char* lines[5];
};

const PetSpriteFrame& spriteForMode(PetMode mode, uint32_t tickMs);
const char* activePetSpecies();
