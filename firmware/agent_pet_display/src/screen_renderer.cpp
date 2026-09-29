#include "screen_renderer.h"

#include <Arduino.h>
#include <U8g2lib.h>

#include <stdio.h>
#include <string.h>
#include <string>

#include "ST7305_U8g2.h"
#include "clock_faces.h"
#include "doto_font.h"
#include "pet_sprites.h"
#include "screen_layout.h"
#include "usage_visuals.h"

namespace {

constexpr int kLcdWidth = 400;
constexpr int kLcdHeight = 300;
constexpr int kRlcdSckPin = 11;
constexpr int kRlcdMosiPin = 12;
constexpr int kRlcdDcPin = 5;
constexpr int kRlcdCsPin = 40;
constexpr int kRlcdRstPin = 41;
constexpr int kTopBarHeight = 24;
constexpr int kFooterHeight = 28;
constexpr int kLabelChipHeight = 9;
constexpr int kUsagePanelX = 12;
constexpr int kUsagePanelWidth = 376;
constexpr int kUsageHeaderY = 34;
constexpr int kUsageSmallCardY = 60;
constexpr int kUsageSmallCardWidth = 182;
constexpr int kUsageSmallCardHeight = 40;
constexpr int kUsageBigCardX = 12;
constexpr int kUsageBigCardWidth = 376;
constexpr int kUsageBigCardHeight = 48;
constexpr int kUsageBigCardGap = 8;

ST7305_U8g2 gLcd(kRlcdSckPin, kRlcdMosiPin, kRlcdDcPin, kRlcdCsPin, kRlcdRstPin);
U8G2* gU8g2 = nullptr;

std::string clipText(const std::string& text, size_t maxChars) {
  if (text.size() <= maxChars) {
    return text;
  }
  if (maxChars <= 2) {
    return text.substr(0, maxChars);
  }
  return text.substr(0, maxChars - 2) + "..";
}

std::string clipTextToPixelWidth(const std::string& text, int maxWidth) {
  if (!gU8g2 || maxWidth <= 0 || text.empty()) {
    return maxWidth <= 0 ? std::string() : text;
  }
  if (gU8g2->getStrWidth(text.c_str()) <= maxWidth) {
    return text;
  }

  for (size_t length = text.size(); length > 0; --length) {
    const std::string candidate = text.substr(0, length - 1) + "..";
    if (gU8g2->getStrWidth(candidate.c_str()) <= maxWidth) {
      return candidate;
    }
  }
  return "..";
}

std::string clipDotoTextToPixelWidth(const std::string& text, int maxWidth, DotoFontSize size) {
  if (maxWidth <= 0 || text.empty()) {
    return maxWidth <= 0 ? std::string() : text;
  }
  if (dotoTextWidth(size, text.c_str()) <= maxWidth) {
    return text;
  }

  for (size_t length = text.size(); length > 0; --length) {
    const std::string candidate = text.substr(0, length - 1) + "..";
    if (dotoTextWidth(size, candidate.c_str()) <= maxWidth) {
      return candidate;
    }
  }
  return "..";
}

void drawDotoText(DotoFontSize size, int x, int y, const std::string& text) {
  if (!gU8g2 || text.empty()) {
    return;
  }
  gU8g2->setDrawColor(0);
  int cursorX = x;
  const int height = dotoTextHeight(size);
  for (char ch : text) {
    const int advance = dotoGlyphAdvance(size, ch);
    for (int row = 0; row < height; ++row) {
      int runStart = -1;
      for (int col = 0; col <= advance; ++col) {
        const bool isOn = col < advance &&
                          dotoGlyphPixel(size, ch, static_cast<uint8_t>(col), static_cast<uint8_t>(row));
        if (isOn && runStart < 0) {
          runStart = col;
        } else if (!isOn && runStart >= 0) {
          gU8g2->drawHLine(cursorX + runStart, y + row, col - runStart);
          runStart = -1;
        }
      }
    }
    cursorX += advance;
  }
}

std::string heroStatusText(const DisplayState& display) {
  if (display.offline) {
    return "OFFLINE";
  }
  return display.statusDetail;
}

void drawLabelChip(int x, int y, int width, const char* label) {
  gU8g2->drawRBox(x, y, width, kLabelChipHeight, 3);
  gU8g2->setDrawColor(1);
  gU8g2->setFont(u8g2_font_5x8_tf);
  gU8g2->drawStr(x + 4, y + 1, label);
  gU8g2->setDrawColor(0);
}

int chipWidthForLabel(const char* label, int minWidth, int maxWidth) {
  const int textWidth = static_cast<int>(strlen(label ? label : "")) * 5;
  int width = textWidth + 8;
  if (width < minWidth) {
    width = minWidth;
  }
  if (width > maxWidth) {
    width = maxWidth;
  }
  return width;
}

void drawMetricCard(const ScreenRect& rect, const char* label, const std::string& value) {
  gU8g2->drawRFrame(rect.x, rect.y, rect.w, rect.h, 5);
  drawLabelChip(rect.x + 8, rect.y + 5, chipWidthForLabel(label, 28, 42), label);
  drawDotoText(DotoFontSize::Medium,
               rect.x + 72,
               rect.y + 5,
               clipDotoTextToPixelWidth(value, rect.w - 84, DotoFontSize::Medium));
}

void drawRightInfoCard(const ScreenRect& rect,
                       const char* label,
                       const std::string& line1,
                       const std::string& line2) {
  gU8g2->drawRFrame(rect.x, rect.y, rect.w, rect.h, 6);
  drawLabelChip(rect.x + 8, rect.y + 6, 42, label);
  // Long values (e.g. "WIFI -61dBm") drop to the small face instead of being clipped.
  const bool fitsMedium = dotoTextWidth(DotoFontSize::Medium, line1.c_str()) <= rect.w - 20;
  const DotoFontSize line1Size = fitsMedium ? DotoFontSize::Medium : DotoFontSize::Small;
  drawDotoText(line1Size,
               rect.x + 10,
               rect.y + (fitsMedium ? 22 : 25),
               clipDotoTextToPixelWidth(line1, rect.w - 20, line1Size));
  drawDotoText(DotoFontSize::Small,
               rect.x + 10,
               rect.y + 41,
               clipDotoTextToPixelWidth(line2, rect.w - 20, DotoFontSize::Small));
}

void drawQuotaCard(const ScreenRect& rect, const char* label, const std::string& value) {
  const QuotaUsage usage = parseQuotaUsage(value);
  const QuotaBarLayout bars = overviewQuotaBarLayout();

  gU8g2->drawRFrame(rect.x, rect.y, rect.w, rect.h, 5);
  drawLabelChip(rect.x + 8, rect.y + 4, chipWidthForLabel(label, 28, 42), label);
  gU8g2->setFont(u8g2_font_t0_11b_tf);
  char fiveHour[12];
  char week[12];
  snprintf(fiveHour, sizeof(fiveHour), "5H %s", usage.fiveHourPercent >= 0 ? String(usage.fiveHourPercent).c_str() : "--");
  snprintf(week, sizeof(week), "WK %s", usage.weekPercent >= 0 ? String(usage.weekPercent).c_str() : "--");
  gU8g2->drawStr(rect.x + 56, rect.y + 9, fiveHour);
  gU8g2->drawRFrame(bars.fiveHourBar.x, bars.fiveHourBar.y, bars.fiveHourBar.w, bars.fiveHourBar.h, 2);
  if (usage.fiveHourPercent > 0) {
    const int width = ((bars.fiveHourBar.w - 2) * usage.fiveHourPercent) / 100;
    gU8g2->drawRBox(bars.fiveHourBar.x + 1, bars.fiveHourBar.y + 1, width, bars.fiveHourBar.h - 2, 1);
  }

  gU8g2->drawStr(rect.x + 56, rect.y + 18, week);
  gU8g2->drawRFrame(bars.weekBar.x, bars.weekBar.y, bars.weekBar.w, bars.weekBar.h, 2);
  if (usage.weekPercent > 0) {
    const int width = ((bars.weekBar.w - 2) * usage.weekPercent) / 100;
    gU8g2->drawRBox(bars.weekBar.x + 1, bars.weekBar.y + 1, width, bars.weekBar.h - 2, 1);
  }
}

void drawTimeCard(const DisplayState& display) {
  const ScreenRect rect = overviewTimeCardRect();
  gU8g2->drawRFrame(rect.x, rect.y, rect.w, rect.h, 6);
  drawLabelChip(rect.x + 10, rect.y + 5, 30, "TIME");
  drawDotoText(DotoFontSize::Hero,
               rect.x + 12,
               rect.y + 17,
               clipDotoTextToPixelWidth(display.sidebarTime, rect.w - 24, DotoFontSize::Hero));
  drawDotoText(DotoFontSize::Small,
               rect.x + 12,
               rect.y + 41,
               clipDotoTextToPixelWidth(display.sidebarDate, rect.w - 24, DotoFontSize::Small));
}

void drawStatusCard(const DisplayState& display) {
  const ScreenRect rect = overviewStatusCardRect();
  gU8g2->drawRFrame(rect.x, rect.y, rect.w, rect.h, 6);
  drawLabelChip(rect.x + 10, rect.y + 6, 42, "STATE");
  drawDotoText(DotoFontSize::Hero,
               rect.x + 10,
               rect.y + 21,
               clipDotoTextToPixelWidth(heroStatusText(display), rect.w - 20, DotoFontSize::Hero));
  drawDotoText(DotoFontSize::Small,
               rect.x + 10,
               rect.y + 52,
               clipDotoTextToPixelWidth(display.taskLine, rect.w - 20, DotoFontSize::Small));
}

void drawPetBitmap(const PetBitmapFrame& frame, int x, int y) {
  for (int row = 0; row < frame.height; ++row) {
    int runStart = -1;
    for (int col = 0; col <= frame.width; ++col) {
      const bool isOn = col < frame.width && petBitmapPixel(frame, static_cast<uint8_t>(col), static_cast<uint8_t>(row));
      if (isOn && runStart < 0) {
        runStart = col;
      } else if (!isOn && runStart >= 0) {
        gU8g2->drawHLine(x + runStart, y + row, col - runStart);
        runStart = -1;
      }
    }
  }
}

void drawUsageMiniCard(int x, int y, int width, const char* label, const std::string& line1, const std::string& line2) {
  gU8g2->drawRFrame(x, y, width, kUsageSmallCardHeight, 5);
  drawLabelChip(x + 8, y + 6, chipWidthForLabel(label, 28, 44), label);
  drawDotoText(DotoFontSize::Medium,
               x + 8,
               y + 16,
               clipDotoTextToPixelWidth(line1, width - 16, DotoFontSize::Medium));
  gU8g2->setFont(u8g2_font_t0_11b_tf);
  gU8g2->drawStr(x + 8, y + 28, clipText(line2, 20).c_str());
}

void drawUsageDetailCard(int y,
                         const char* label,
                         const std::string& value,
                         const std::string& hint) {
  gU8g2->drawRFrame(kUsageBigCardX, y, kUsageBigCardWidth, kUsageBigCardHeight, 6);
  drawLabelChip(kUsageBigCardX + 8, y + 6, chipWidthForLabel(label, 34, 56), label);
  drawDotoText(DotoFontSize::Medium,
               kUsageBigCardX + 8,
               y + 16,
               clipDotoTextToPixelWidth(value, kUsageBigCardWidth - 16, DotoFontSize::Medium));
  gU8g2->setFont(u8g2_font_t0_11b_tf);
  gU8g2->drawStr(kUsageBigCardX + 8, y + 32, clipText(hint, 40).c_str());
}

void drawPet(const DisplayState& display, uint32_t tick) {
  const PetBitmapFrame& sprite = bitmapForMode(display.petMode, tick);
  const ScreenRect rect = overviewPetPanelRect();
  gU8g2->drawRFrame(rect.x, rect.y, rect.w, rect.h, 8);
  gU8g2->drawRFrame(rect.x + 5, rect.y + 5, rect.w - 10, 14, 4);
  drawDotoText(DotoFontSize::Small,
               rect.x + 10,
               rect.y + 7,
               clipDotoTextToPixelWidth(display.buddyBubble, rect.w - 20, DotoFontSize::Small));
  const int spriteX = rect.x + (rect.w - sprite.width) / 2;
  const int spriteY = rect.y + 24;
  drawPetBitmap(sprite, spriteX, spriteY);
}

void renderOverviewPage(const DisplayState& display, const PowerState& power) {
  (void)power;

  drawTimeCard(display);
  drawStatusCard(display);
  drawMetricCard(overviewMetricCardRect(0), display.sidebarTokensLabel.c_str(), display.sidebarTokens);
  drawMetricCard(overviewMetricCardRect(1), display.sidebarContextLabel.c_str(), display.sidebarContext);
  if (display.sidebarQuotaStyle == "quota") {
    drawQuotaCard(overviewMetricCardRect(2), display.sidebarQuotaLabel.c_str(), display.sidebarQuota);
  } else {
    drawMetricCard(overviewMetricCardRect(2), display.sidebarQuotaLabel.c_str(), display.sidebarQuota);
  }
  drawRightInfoCard(overviewRightInfoCardRect(0), "AGENT", display.sourceLabel, heroStatusText(display));
  drawRightInfoCard(overviewRightInfoCardRect(1), "BOARD", display.networkLine, display.bridgeLine);
  drawPet(display, millis());
}

void renderUsagePage(const DisplayState& display) {
  drawDotoText(DotoFontSize::Medium, kUsagePanelX, kUsageHeaderY - 2, "USAGE DETAILS");
  drawDotoText(DotoFontSize::Small,
               kUsagePanelX + 144,
               kUsageHeaderY + 1,
               clipDotoTextToPixelWidth(display.sidebarAgent, kUsagePanelWidth - 150, DotoFontSize::Small));

  drawUsageMiniCard(kUsagePanelX, kUsageSmallCardY, kUsageSmallCardWidth, "TIME", display.sidebarTime, display.sidebarDate);
  drawUsageMiniCard(kUsagePanelX + kUsageSmallCardWidth + 12,
                    kUsageSmallCardY,
                    kUsageSmallCardWidth,
                    "CLIMATE",
                    display.sidebarClimate,
                    display.statusDetail);

  const int detailStartY = kUsageSmallCardY + kUsageSmallCardHeight + 8;
  drawUsageDetailCard(detailStartY,
                      display.sidebarTokensLabel.c_str(),
                      display.sidebarTokens,
                      display.sidebarTokensHint);
  drawUsageDetailCard(detailStartY + kUsageBigCardHeight + kUsageBigCardGap,
                      display.sidebarContextLabel.c_str(),
                      display.sidebarContext,
                      display.sidebarContextHint);
  drawUsageDetailCard(detailStartY + (kUsageBigCardHeight + kUsageBigCardGap) * 2,
                      display.sidebarQuotaLabel.c_str(),
                      display.sidebarQuota,
                      display.sidebarQuotaHint);
}

int drawFocusChip(const DisplayState& display, int rightEdge) {
  const int width = chipWidthForLabel(display.focusLabel.c_str(), 34, 52);
  const int x = rightEdge - width;
  drawLabelChip(x, 7, width, display.focusLabel.c_str());
  return x;
}

uint32_t gLastFrameHash = 0;
bool gHasFrame = false;

uint32_t frameHash() {
  const uint8_t* buffer = gU8g2->getBufferPtr();
  const size_t size = static_cast<size_t>(gU8g2->getBufferTileWidth()) * 8U * gU8g2->getBufferTileHeight();
  uint32_t hash = 2166136261u;
  for (size_t index = 0; index < size; ++index) {
    hash ^= buffer[index];
    hash *= 16777619u;
  }
  return hash;
}

// Pushing 15 KB over SPI costs power; skip it when nothing changed.
void flushIfChanged() {
  const uint32_t hash = frameHash();
  if (gHasFrame && hash == gLastFrameHash) {
    return;
  }
  gU8g2->sendBuffer();
  gLastFrameHash = hash;
  gHasFrame = true;
}

}  // namespace

void ScreenRenderer::begin() {
  gLcd.begin(0, U8G2_R1);
  gU8g2 = gLcd.getU8g2();
  gU8g2->setFontPosTop();
  gU8g2->setFont(u8g2_font_t0_11b_tf);
}

U8G2* ScreenRenderer::u8g2() {
  return gU8g2;
}

void ScreenRenderer::invalidate() {
  gHasFrame = false;
}

void ScreenRenderer::render(const DisplayState& display, const PowerState& power) {
  if (!gU8g2) {
    return;
  }

  if (display.page == ScreenPage::Clock) {
    gU8g2->clearBuffer();
    gU8g2->setDrawColor(1);
    gU8g2->drawBox(0, 0, kLcdWidth, kLcdHeight);
    gU8g2->setDrawColor(0);
    gU8g2->setFontPosTop();
    drawClockFace(*gU8g2, display.clock, millis());
    flushIfChanged();
    return;
  }

  char battery[32];
  if (display.showBatteryDetail && power.lowBattery) {
    snprintf(battery, sizeof(battery), "%d%% LOW %.2fV", power.percent, power.voltageMv / 1000.0f);
  } else if (display.showBatteryDetail && power.charging) {
    snprintf(battery, sizeof(battery), "%d%% CHG %.2fV", power.percent, power.voltageMv / 1000.0f);
  } else {
    snprintf(battery, sizeof(battery), "%d%%", power.percent);
  }

  gU8g2->clearBuffer();
  gU8g2->setDrawColor(1);
  gU8g2->drawBox(0, 0, kLcdWidth, kLcdHeight);
  gU8g2->setDrawColor(0);
  gU8g2->drawFrame(0, 0, kLcdWidth, kLcdHeight);
  gU8g2->drawHLine(8, kTopBarHeight, kLcdWidth - 16);

  const int batteryWidth = dotoTextWidth(DotoFontSize::Small, battery);
  const int batteryX = kLcdWidth - 12 - batteryWidth;
  const int focusChipLeft = drawFocusChip(display, batteryX - 8);
  drawDotoText(DotoFontSize::Small, 12, 5, clipDotoTextToPixelWidth(display.linkLabel, 76, DotoFontSize::Small));
  drawDotoText(DotoFontSize::Small, 92, 5, clipDotoTextToPixelWidth(display.sidebarClimate, 72, DotoFontSize::Small));
  const int sourceX = 170;
  const int sourceWidth = focusChipLeft - sourceX - 8;
  drawDotoText(DotoFontSize::Small,
               sourceX,
               5,
               clipDotoTextToPixelWidth(display.sourceLabel, sourceWidth, DotoFontSize::Small));
  drawDotoText(DotoFontSize::Small, batteryX, 5, battery);

  if (display.page == ScreenPage::Usage) {
    renderUsagePage(display);
  } else {
    renderOverviewPage(display, power);
  }

  const int footerY = kLcdHeight - kFooterHeight;
  const char* footerLabel = display.page == ScreenPage::Usage ? "KEYS" : "LIVE";
  gU8g2->drawHLine(8, footerY, kLcdWidth - 16);
  drawLabelChip(12, footerY + 6, chipWidthForLabel(footerLabel, 32, 40), footerLabel);
  drawDotoText(DotoFontSize::Small,
               58,
               footerY + 7,
               clipDotoTextToPixelWidth(display.footerMessage, 260, DotoFontSize::Small));
  char pageLabel[16];
  snprintf(pageLabel, sizeof(pageLabel), "P%d/%d", screenPageNumber(display.page), kScreenPageCount);
  drawDotoText(DotoFontSize::Small, 338, footerY + 7, pageLabel);
  flushIfChanged();
}
