#pragma once

#include <stdint.h>

#include <U8g2lib.h>

// The offline easter egg: an original procedural animation drawn with U8g2.
// Used when the bridge has no animation uploaded (or is unreachable).
uint32_t builtinEggDurationMs();
void drawBuiltinEggFrame(U8G2& g, uint32_t elapsedMs);

// Title card shown while a streamed animation is about to start.
void drawEggCountdown(U8G2& g, const char* title, int secondsLeft);
// Shown briefly when a stream could not be played.
void drawEggMessage(U8G2& g, const char* line1, const char* line2);
