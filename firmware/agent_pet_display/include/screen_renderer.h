#pragma once

#include "models.h"

class U8G2;

class ScreenRenderer {
 public:
  void begin();
  void render(const DisplayState& display, const PowerState& power);
  // Forces the next render() to push the frame even if it looks unchanged
  // (e.g. after the easter-egg player drew directly into the panel).
  void invalidate();
  // Direct access for full-screen players; nullptr before begin().
  U8G2* u8g2();
};
