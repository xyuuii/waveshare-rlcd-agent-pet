#pragma once

#include "models.h"

class ScreenRenderer {
 public:
  void begin();
  void render(const DisplayState& display, const PowerState& power);
};
