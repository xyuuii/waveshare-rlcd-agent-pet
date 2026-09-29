#pragma once

#include <stdint.h>

#include <U8g2lib.h>

#include "models.h"

// Draws a full-screen (400x300) clock face into the U8g2 buffer.
// Colour convention matches the rest of the UI: draw colour 1 = paper, 0 = ink.
// The caller clears/fills the buffer and calls sendBuffer().
void drawClockFace(U8G2& u8g2, const ClockView& view, uint32_t tickMs);
