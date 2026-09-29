#include "egg_builtin.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "doto_font.h"
#include "models.h"
#include "pet_sprites.h"

namespace {

constexpr int kWidth = 400;
constexpr int kHeight = 300;
constexpr uint32_t kDurationMs = 12000;

void clearPaper(U8G2& g) {
  g.clearBuffer();
  g.setDrawColor(1);
  g.drawBox(0, 0, kWidth, kHeight);
  g.setDrawColor(0);
}

void dotoText(U8G2& g, DotoFontSize size, int x, int y, const char* text, int scale) {
  const int height = dotoTextHeight(size);
  int cursor = x;
  for (const char* p = text; *p; ++p) {
    const int advance = dotoGlyphAdvance(size, *p);
    for (int row = 0; row < height; ++row) {
      for (int col = 0; col < advance; ++col) {
        if (dotoGlyphPixel(size, *p, static_cast<uint8_t>(col), static_cast<uint8_t>(row))) {
          g.drawBox(cursor + col * scale, y + row * scale, scale, scale);
        }
      }
    }
    cursor += advance * scale;
  }
}

void sprite(U8G2& g, const PetBitmapFrame& frame, int x, int y, bool mirror) {
  for (int row = 0; row < frame.height; ++row) {
    const int py = y + row;
    if (py < 0 || py >= kHeight) {
      continue;
    }
    for (int col = 0; col < frame.width; ++col) {
      const int px = x + (mirror ? frame.width - 1 - col : col);
      if (px < 0 || px >= kWidth) {
        continue;
      }
      if (petBitmapPixel(frame, static_cast<uint8_t>(col), static_cast<uint8_t>(row))) {
        g.drawPixel(px, py);
      }
    }
  }
}

uint32_t hash32(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return value;
}

void starfield(U8G2& g, uint32_t elapsedMs) {
  for (uint32_t index = 0; index < 48; ++index) {
    const uint32_t h = hash32(index + 1);
    const int layer = static_cast<int>(h % 3) + 1;  // 1..3, nearer stars move faster
    const int speed = layer * 40;                   // px per second
    const int y = static_cast<int>((h >> 8) % 250) + 10;
    const int x = static_cast<int>((h >> 4) % 420 + 420 - (elapsedMs * speed / 1000) % 420) % 420 - 10;
    if (x < 0 || x >= kWidth) {
      continue;
    }
    if (layer == 3) {
      g.drawBox(x, y, 2, 2);
    } else {
      g.drawPixel(x, y);
    }
  }
}

void confetti(U8G2& g, uint32_t elapsedMs) {
  for (uint32_t index = 0; index < 36; ++index) {
    const uint32_t h = hash32(index * 7919 + 17);
    const int x = static_cast<int>(h % kWidth);
    const int fall = static_cast<int>((elapsedMs * (60 + (h >> 20) % 60)) / 1000);
    const int y = (static_cast<int>((h >> 9) % kHeight) + fall) % (kHeight + 20) - 10;
    if (y < 0 || y >= kHeight - 4) {
      continue;
    }
    if (((h >> 3) & 1) != 0) {
      g.drawBox(x, y, 3, 3);
    } else {
      g.drawLine(x, y, x + 3, y + 3);
    }
  }
}

}  // namespace

uint32_t builtinEggDurationMs() {
  return kDurationMs;
}

void drawBuiltinEggFrame(U8G2& g, uint32_t elapsedMs) {
  clearPaper(g);
  const float t = static_cast<float>(elapsedMs) / 1000.0f;

  if (elapsedMs < 9000) {
    starfield(g, elapsedMs);
    // Bouncing title: each letter rides a sine wave.
    static const char* kTitle = "GUGUGAGA!";
    const int scale = 2;
    int width = 0;
    for (const char* p = kTitle; *p; ++p) {
      width += dotoGlyphAdvance(DotoFontSize::Hero, *p) * scale;
    }
    int x = (kWidth - width) / 2;
    int index = 0;
    for (const char* p = kTitle; *p; ++p, ++index) {
      const char letter[2] = {*p, 0};
      const int y = 34 + static_cast<int>(lroundf(sinf(t * 5.0f + index * 0.7f) * 12.0f));
      dotoText(g, DotoFontSize::Hero, x, y, letter, scale);
      x += dotoGlyphAdvance(DotoFontSize::Hero, *p) * scale;
    }

    // A parade of pets hopping from right to left.
    static const PetMode kModes[3] = {PetMode::Celebrate, PetMode::Working, PetMode::Thinking};
    for (int pet = 0; pet < 3; ++pet) {
      const int period = 540;
      const int travel = static_cast<int>((elapsedMs * 110 / 1000 + pet * 180) % period);
      const int px = kWidth - travel + 40;
      const float hop = fabsf(sinf(t * 6.0f + pet * 1.3f));
      const int py = 196 - static_cast<int>(lroundf(hop * 26.0f));
      const PetBitmapFrame& frame = bitmapForMode(kModes[pet], elapsedMs + pet * 333);
      sprite(g, frame, px - frame.width / 2, py - frame.height / 2, false);
    }
    g.drawHLine(0, 236, kWidth);
    for (int x = static_cast<int>(elapsedMs * 110 / 1000) % 16; x < kWidth; x += 16) {
      g.drawPixel(kWidth - x, 240);
    }
    return;
  }

  // Finale: confetti and a centered bow.
  confetti(g, elapsedMs);
  const PetBitmapFrame& frame = bitmapForMode(PetMode::Celebrate, elapsedMs);
  const int scale = 2;
  const int sx = (kWidth - frame.width * scale) / 2;
  const int sy = 40;
  for (int row = 0; row < frame.height; ++row) {
    for (int col = 0; col < frame.width; ++col) {
      if (petBitmapPixel(frame, static_cast<uint8_t>(col), static_cast<uint8_t>(row))) {
        g.drawBox(sx + col * scale, sy + row * scale, scale, scale);
      }
    }
  }
  static const char* kThanks = "THANKS FOR WATCHING";
  const int width = dotoTextWidth(DotoFontSize::Medium, kThanks);
  g.setDrawColor(1);
  g.drawBox((kWidth - width) / 2 - 8, 206, width + 16, 26);
  g.setDrawColor(0);
  g.drawFrame((kWidth - width) / 2 - 8, 206, width + 16, 26);
  dotoText(g, DotoFontSize::Medium, (kWidth - width) / 2, 211, kThanks, 1);
}

void drawEggCountdown(U8G2& g, const char* title, int secondsLeft) {
  clearPaper(g);
  g.drawRFrame(60, 70, 280, 160, 12);
  g.drawRFrame(62, 72, 276, 156, 10);
  // Play triangle.
  g.drawTriangle(92, 110, 92, 150, 124, 130);
  const char* name = title && title[0] ? title : "EASTER EGG";
  char line[32];
  snprintf(line, sizeof(line), "%.20s", name);
  dotoText(g, DotoFontSize::Medium, 140, 112, line, 1);
  dotoText(g, DotoFontSize::Small, 140, 136, "BOOT = STOP", 1);
  char count[12];
  snprintf(count, sizeof(count), "%d", secondsLeft < 0 ? 0 : secondsLeft);
  const int width = dotoTextWidth(DotoFontSize::Hero, count) * 2;
  dotoText(g, DotoFontSize::Hero, (kWidth - width) / 2, 168, count, 2);
}

void drawEggMessage(U8G2& g, const char* line1, const char* line2) {
  clearPaper(g);
  const int w1 = dotoTextWidth(DotoFontSize::Medium, line1 ? line1 : "");
  const int w2 = dotoTextWidth(DotoFontSize::Small, line2 ? line2 : "");
  g.drawRFrame(40, 100, 320, 100, 10);
  dotoText(g, DotoFontSize::Medium, (kWidth - w1) / 2, 126, line1 ? line1 : "", 1);
  dotoText(g, DotoFontSize::Small, (kWidth - w2) / 2, 158, line2 ? line2 : "", 1);
}
