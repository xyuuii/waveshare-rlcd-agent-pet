#include "clock_faces.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "clock_model.h"
#include "doto_font.h"
#include "pet_sprites.h"

namespace {

constexpr int kWidth = 400;
constexpr int kHeight = 300;

// ---------------------------------------------------------------- primitives

inline void ink(U8G2& g) {
  g.setDrawColor(0);
}

inline void paper(U8G2& g) {
  g.setDrawColor(1);
}

int clampInt(int value, int low, int high) {
  return value < low ? low : (value > high ? high : value);
}

void fillTriangle(U8G2& g, int x0, int y0, int x1, int y1, int x2, int y2) {
  g.drawTriangle(clampInt(x0, 0, kWidth - 1),
                 clampInt(y0, 0, kHeight - 1),
                 clampInt(x1, 0, kWidth - 1),
                 clampInt(y1, 0, kHeight - 1),
                 clampInt(x2, 0, kWidth - 1),
                 clampInt(y2, 0, kHeight - 1));
}

// Convex polygon as a triangle fan.
void fillConvex(U8G2& g, const int* xs, const int* ys, int count) {
  for (int index = 1; index + 1 < count; ++index) {
    fillTriangle(g, xs[0], ys[0], xs[index], ys[index], xs[index + 1], ys[index + 1]);
  }
}

// A tapered bar from (x0, y0) to (x1, y1): width w0 at the start, w1 at the end.
void taperedBar(U8G2& g, float x0, float y0, float x1, float y1, float w0, float w1) {
  const float dx = x1 - x0;
  const float dy = y1 - y0;
  const float length = sqrtf(dx * dx + dy * dy);
  if (length < 0.5f) {
    return;
  }
  const float nx = -dy / length;
  const float ny = dx / length;
  const int xs[4] = {
      static_cast<int>(lroundf(x0 + nx * w0 * 0.5f)),
      static_cast<int>(lroundf(x0 - nx * w0 * 0.5f)),
      static_cast<int>(lroundf(x1 - nx * w1 * 0.5f)),
      static_cast<int>(lroundf(x1 + nx * w1 * 0.5f)),
  };
  const int ys[4] = {
      static_cast<int>(lroundf(y0 + ny * w0 * 0.5f)),
      static_cast<int>(lroundf(y0 - ny * w0 * 0.5f)),
      static_cast<int>(lroundf(y1 - ny * w1 * 0.5f)),
      static_cast<int>(lroundf(y1 + ny * w1 * 0.5f)),
  };
  fillConvex(g, xs, ys, 4);
}

// Knocks pixels out of a region so solid ink reads as a light "ghost".
// level 1 keeps half the pixels, level 2 keeps a quarter.
void lighten(U8G2& g, int x, int y, int w, int h, int level) {
  const int x0 = clampInt(x, 0, kWidth);
  const int y0 = clampInt(y, 0, kHeight);
  const int x1 = clampInt(x + w, 0, kWidth);
  const int y1 = clampInt(y + h, 0, kHeight);
  paper(g);
  for (int yy = y0; yy < y1; ++yy) {
    for (int xx = x0; xx < x1; ++xx) {
      const bool keep = level >= 2 ? ((xx & 1) == 0 && (yy & 1) == 0) : (((xx + yy) & 1) == 0);
      if (!keep) {
        g.drawPixel(xx, yy);
      }
    }
  }
  ink(g);
}

// ---------------------------------------------------------------- text

void drawDoto(U8G2& g, DotoFontSize size, int x, int y, const char* text, int scale = 1) {
  if (!text) {
    return;
  }
  const int height = dotoTextHeight(size);
  int cursor = x;
  for (const char* p = text; *p; ++p) {
    const int advance = dotoGlyphAdvance(size, *p);
    for (int row = 0; row < height; ++row) {
      int runStart = -1;
      for (int col = 0; col <= advance; ++col) {
        const bool on = col < advance &&
                        dotoGlyphPixel(size, *p, static_cast<uint8_t>(col), static_cast<uint8_t>(row));
        if (on && runStart < 0) {
          runStart = col;
        } else if (!on && runStart >= 0) {
          g.drawBox(cursor + runStart * scale, y + row * scale, (col - runStart) * scale, scale);
          runStart = -1;
        }
      }
    }
    cursor += advance * scale;
  }
}

int dotoWidth(DotoFontSize size, const char* text, int scale = 1) {
  return text ? dotoTextWidth(size, text) * scale : 0;
}

std::string clipDoto(DotoFontSize size, const std::string& text, int maxWidth) {
  if (dotoTextWidth(size, text.c_str()) <= maxWidth) {
    return text;
  }
  for (size_t length = text.size(); length > 0; --length) {
    const std::string candidate = text.substr(0, length - 1) + "..";
    if (dotoTextWidth(size, candidate.c_str()) <= maxWidth) {
      return candidate;
    }
  }
  return "";
}

int fontWidth(U8G2& g, const uint8_t* font, const char* text) {
  g.setFont(font);
  return g.getStrWidth(text);
}

void fontText(U8G2& g, const uint8_t* font, int x, int y, const char* text) {
  g.setFont(font);
  g.drawStr(x, y, text);
}

std::string clipFont(U8G2& g, const uint8_t* font, const std::string& text, int maxWidth) {
  g.setFont(font);
  if (g.getStrWidth(text.c_str()) <= maxWidth) {
    return text;
  }
  for (size_t length = text.size(); length > 0; --length) {
    const std::string candidate = text.substr(0, length - 1) + "..";
    if (g.getStrWidth(candidate.c_str()) <= maxWidth) {
      return candidate;
    }
  }
  return "";
}

void degreeMark(U8G2& g, int x, int y, int radius) {
  g.drawCircle(x, y, radius);
}

// ---------------------------------------------------------------- shared bits

void formatHourMinute(const ClockView& v, char* out, size_t size) {
  if (!v.timeValid) {
    snprintf(out, size, "--:--");
    return;
  }
  const int hour = displayHour(v.hour, v.hour12);
  if (v.hour12) {
    snprintf(out, size, "%d:%02d", hour, v.minute);
  } else {
    snprintf(out, size, "%02d:%02d", hour, v.minute);
  }
}

void formatSeconds(const ClockView& v, char* out, size_t size) {
  if (!v.timeValid) {
    snprintf(out, size, "--");
    return;
  }
  snprintf(out, size, "%02d", v.second);
}

void formatDate(const ClockView& v, char* out, size_t size) {
  if (!v.timeValid) {
    snprintf(out, size, "SYNCING TIME");
    return;
  }
  snprintf(out, size, "%s %02d %s", weekdayShortName(v.weekday), v.day, monthShortName(v.month));
}

void formatClimate(const ClockView& v, char* out, size_t size) {
  if (!v.climateValid) {
    snprintf(out, size, "--.-C --%%");
    return;
  }
  snprintf(out, size, "%.1fC %.0f%%", static_cast<double>(v.temperatureC), static_cast<double>(v.humidityPct));
}

void batteryIcon(U8G2& g, int x, int y, const ClockView& v) {
  // 20 x 10 body with a nub; fill proportional to the estimate.
  g.drawFrame(x, y, 20, 10);
  g.drawBox(x + 20, y + 3, 2, 4);
  if (v.batteryValid) {
    const int fill = clampInt((v.batteryPercent * 16 + 50) / 100, 0, 16);
    if (fill > 0) {
      g.drawBox(x + 2, y + 2, fill, 6);
    }
    if (v.charging) {
      paper(g);
      g.drawLine(x + 11, y + 1, x + 8, y + 5);
      g.drawLine(x + 8, y + 5, x + 12, y + 5);
      g.drawLine(x + 12, y + 5, x + 9, y + 9);
      ink(g);
    }
  }
}

// Top row: date on the left, climate and battery on the right.
void statusRow(U8G2& g, const ClockView& v, int y) {
  char date[24];
  formatDate(v, date, sizeof(date));
  drawDoto(g, DotoFontSize::Small, 12, y, date);

  char right[32];
  char climate[20];
  formatClimate(v, climate, sizeof(climate));
  if (v.batteryValid) {
    snprintf(right, sizeof(right), "%s  %d%%", climate, v.batteryPercent);
  } else {
    snprintf(right, sizeof(right), "%s", climate);
  }
  const int rightWidth = dotoWidth(DotoFontSize::Small, right);
  const int iconX = kWidth - 12 - 22;
  drawDoto(g, DotoFontSize::Small, iconX - 6 - rightWidth, y, right);
  batteryIcon(g, iconX, y, v);
}

// Bottom row: which agent is doing what; inverted when it needs you.
void agentRow(U8G2& g, const ClockView& v, int y) {
  if (!v.agentConnected) {
    std::string text = v.bridgeLabel + "  " + v.linkLabel;
    drawDoto(g, DotoFontSize::Small, 12, y, clipDoto(DotoFontSize::Small, text, kWidth - 24).c_str());
    return;
  }
  std::string text = v.agentSource + "  " + v.agentDetail;
  if (!v.agentTask.empty()) {
    text += "  " + v.agentTask;
  }
  if (v.agentAttention) {
    g.drawRBox(8, y - 3, kWidth - 16, 16, 4);
    paper(g);
    const std::string label = clipDoto(DotoFontSize::Small, v.agentSource + " NEEDS YOU  " + v.agentTask, kWidth - 32);
    drawDoto(g, DotoFontSize::Small, 16, y, label.c_str());
    ink(g);
    return;
  }
  if (v.agentActive) {
    g.drawDisc(16, y + 4, 3);
  } else {
    g.drawCircle(16, y + 4, 3);
  }
  drawDoto(g, DotoFontSize::Small, 26, y, clipDoto(DotoFontSize::Small, text, kWidth - 40).c_str());
}

void hairline(U8G2& g, int y) {
  g.drawHLine(12, y, kWidth - 24);
}

// ---------------------------------------------------------------- SANS

void drawSansFace(U8G2& g, const ClockView& v) {
  statusRow(g, v, 6);
  hairline(g, 22);

  char hm[8];
  char sec[4];
  formatHourMinute(v, hm, sizeof(hm));
  formatSeconds(v, sec, sizeof(sec));
  const int hmWidth = fontWidth(g, u8g2_font_logisoso92_tn, hm);
  const int secWidth = v.showSeconds ? fontWidth(g, u8g2_font_logisoso42_tn, sec) : 0;
  const int gap = v.showSeconds ? 12 : 0;
  const int x = (kWidth - hmWidth - gap - secWidth) / 2;
  const int top = 38;
  fontText(g, u8g2_font_logisoso92_tn, x, top, hm);
  if (v.showSeconds) {
    fontText(g, u8g2_font_logisoso42_tn, x + hmWidth + gap, top + 92 - 42, sec);
  }
  if (v.hour12 && v.timeValid) {
    drawDoto(g, DotoFontSize::Medium, x + hmWidth + gap, top + 6, isAfternoon(v.hour) ? "PM" : "AM");
  }

  // Seconds rail: one notch per second, tall marks every five.
  const int railX = 20;
  const int railY = 150;
  g.drawHLine(railX, railY + 6, 60 * 6);
  for (int s = 0; s < 60; ++s) {
    const int sx = railX + s * 6;
    if (v.timeValid && s <= v.second) {
      g.drawBox(sx, railY, 5, 5);
    } else if (s % 5 == 0) {
      g.drawVLine(sx + 2, railY + 2, 3);
    }
  }

  char date[32];
  if (v.timeValid) {
    snprintf(date, sizeof(date), "%s %d %s %04d", weekdayShortName(v.weekday), v.day, monthShortName(v.month), v.year);
  } else {
    snprintf(date, sizeof(date), "SYNCING TIME");
  }
  const int dateWidth = fontWidth(g, u8g2_font_helvB18_tr, date);
  fontText(g, u8g2_font_helvB18_tr, (kWidth - dateWidth) / 2, 174, date);

  // Three small pills: temperature, humidity, battery.
  char temp[16];
  char hum[16];
  char bat[16];
  if (v.climateValid) {
    snprintf(temp, sizeof(temp), "%.1f", static_cast<double>(v.temperatureC));
    snprintf(hum, sizeof(hum), "%.0f%% RH", static_cast<double>(v.humidityPct));
  } else {
    snprintf(temp, sizeof(temp), "--.-");
    snprintf(hum, sizeof(hum), "--%% RH");
  }
  snprintf(bat, sizeof(bat), v.batteryValid ? "%d%%" : "--%%", v.batteryPercent);
  const int pillY = 212;
  const int pillW = 112;
  const int pillH = 30;
  const int pillGap = 12;
  const int pillX = (kWidth - pillW * 3 - pillGap * 2) / 2;
  for (int index = 0; index < 3; ++index) {
    const int px = pillX + index * (pillW + pillGap);
    g.drawRFrame(px, pillY, pillW, pillH, 8);
  }
  const int tempWidth = fontWidth(g, u8g2_font_helvB14_tr, temp);
  const int tempX = pillX + (pillW - tempWidth - 16) / 2;
  fontText(g, u8g2_font_helvB14_tr, tempX, pillY + 8, temp);
  degreeMark(g, tempX + tempWidth + 4, pillY + 10, 2);
  fontText(g, u8g2_font_helvB14_tr, tempX + tempWidth + 8, pillY + 8, "C");
  const int humWidth = fontWidth(g, u8g2_font_helvB14_tr, hum);
  fontText(g, u8g2_font_helvB14_tr, pillX + pillW + pillGap + (pillW - humWidth) / 2, pillY + 8, hum);
  const int batX = pillX + (pillW + pillGap) * 2;
  const int batWidth = fontWidth(g, u8g2_font_helvB14_tr, bat);
  batteryIcon(g, batX + (pillW - batWidth - 30) / 2, pillY + 10, v);
  fontText(g, u8g2_font_helvB14_tr, batX + (pillW - batWidth - 30) / 2 + 30, pillY + 8, bat);

  hairline(g, 262);
  agentRow(g, v, 274);
}

// ---------------------------------------------------------------- SEGMENT

struct SegmentBox {
  int x;
  int y;
  int w;
  int h;
  int t;
  int slant;
};

// Shear so the top leans right, like a classic LCD watch.
int shearX(const SegmentBox& box, int localX, int localY) {
  return box.x + localX + (box.slant * (box.h - localY)) / box.h;
}

void segmentPolygon(U8G2& g, const SegmentBox& box, int segment) {
  const int t = box.t;
  const int half = t / 2;
  const int gap = t >= 10 ? 2 : 1;
  const int left = half;
  const int right = box.w - half;
  const int top = half;
  const int mid = box.h / 2;
  const int bottom = box.h - half;
  int lx[6];
  int ly[6];
  auto horizontal = [&](int cy) {
    const int x0 = left + gap;
    const int x1 = right - gap;
    const int px[6] = {x0, x0 + half, x1 - half, x1, x1 - half, x0 + half};
    const int py[6] = {cy, cy - half, cy - half, cy, cy + half, cy + half};
    for (int i = 0; i < 6; ++i) {
      lx[i] = px[i];
      ly[i] = py[i];
    }
  };
  auto vertical = [&](int cx, int y0, int y1) {
    const int px[6] = {cx, cx + half, cx + half, cx, cx - half, cx - half};
    const int py[6] = {y0, y0 + half, y1 - half, y1, y1 - half, y0 + half};
    for (int i = 0; i < 6; ++i) {
      lx[i] = px[i];
      ly[i] = py[i];
    }
  };
  switch (segment) {
    case 0:
      horizontal(top);
      break;
    case 1:
      vertical(right, top + gap, mid - gap);
      break;
    case 2:
      vertical(right, mid + gap, bottom - gap);
      break;
    case 3:
      horizontal(bottom);
      break;
    case 4:
      vertical(left, mid + gap, bottom - gap);
      break;
    case 5:
      vertical(left, top + gap, mid - gap);
      break;
    default:
      horizontal(mid);
      break;
  }
  int xs[6];
  int ys[6];
  for (int i = 0; i < 6; ++i) {
    xs[i] = shearX(box, lx[i], ly[i]);
    ys[i] = box.y + ly[i];
  }
  fillConvex(g, xs, ys, 6);
}

void segmentGlyph(U8G2& g, const SegmentBox& box, char ch, bool ghosts) {
  if (ghosts) {
    for (int segment = 0; segment < 7; ++segment) {
      segmentPolygon(g, box, segment);
    }
    lighten(g, box.x, box.y, box.w + box.slant + 1, box.h + 1, 2);
  }
  const uint8_t mask = sevenSegmentMask(ch);
  for (int segment = 0; segment < 7; ++segment) {
    if (mask & (1 << segment)) {
      segmentPolygon(g, box, segment);
    }
  }
}

void segmentColon(U8G2& g, const SegmentBox& box, bool on) {
  const int size = box.t - 3;
  const int ys[2] = {box.h / 3, (box.h * 2) / 3};
  for (int i = 0; i < 2; ++i) {
    const int cy = ys[i];
    const int x = shearX(box, 0, cy);
    g.drawBox(x, box.y + cy - size / 2, size, size);
    if (!on) {
      lighten(g, x, box.y + cy - size / 2, size, size, 2);
    }
  }
}

void segmentString(U8G2& g, int x, int y, int w, int h, int t, int slant, int spacing, const char* text, bool ghosts) {
  int cursor = x;
  for (const char* p = text; *p; ++p) {
    if (*p == '.') {
      g.drawBox(cursor - spacing + 1, y + h - t, t, t);
      continue;
    }
    SegmentBox box{cursor, y, w, h, t, slant};
    segmentGlyph(g, box, *p, ghosts);
    cursor += w + spacing;
  }
}

void drawSegmentFace(U8G2& g, const ClockView& v) {
  // Weekday strip like a digital watch: today is inverted.
  static const char* kDays[7] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
  const int dayCell = 38;
  const int dayX = 14;
  for (int day = 0; day < 7; ++day) {
    const int cx = dayX + day * dayCell;
    const bool today = v.timeValid && day == v.weekday;
    if (today) {
      g.drawRBox(cx, 7, dayCell - 6, 15, 3);
      paper(g);
    }
    drawDoto(g, DotoFontSize::Small, cx + (dayCell - 6 - dotoWidth(DotoFontSize::Small, kDays[day])) / 2, 10, kDays[day]);
    ink(g);
  }
  const char* meridiem = !v.hour12 ? "24H" : (isAfternoon(v.hour) ? "PM" : "AM");
  drawDoto(g, DotoFontSize::Medium, kWidth - 14 - dotoWidth(DotoFontSize::Medium, meridiem), 7, meridiem);

  g.drawRFrame(8, 30, kWidth - 16, 152, 10);

  char digits[5] = {'-', '-', '-', '-', 0};
  if (v.timeValid) {
    const int hour = displayHour(v.hour, v.hour12);
    digits[0] = (v.hour12 && hour < 10) ? ' ' : static_cast<char>('0' + hour / 10);
    digits[1] = static_cast<char>('0' + hour % 10);
    digits[2] = static_cast<char>('0' + v.minute / 10);
    digits[3] = static_cast<char>('0' + v.minute % 10);
  }
  const int digitW = 54;
  const int digitH = 112;
  const int thick = 15;
  const int slant = 8;
  const int spacing = 10;
  const int colonW = 14;
  const int secW = 26;
  const int secH = 52;
  const int top = 50;
  const int groupWidth = digitW * 4 + spacing * 3 + colonW + 12 + (v.showSeconds ? 8 + secW * 2 + 6 + 4 : slant);
  int cursor = (kWidth - groupWidth) / 2;
  for (int index = 0; index < 4; ++index) {
    if (index == 2) {
      SegmentBox colonBox{cursor, top, colonW, digitH, thick, slant};
      segmentColon(g, colonBox, !v.timeValid || (v.second % 2) == 0);
      cursor += colonW + 12;
    }
    SegmentBox box{cursor, top, digitW, digitH, thick, slant};
    segmentGlyph(g, box, digits[index], true);
    cursor += digitW + spacing;
  }
  if (v.showSeconds) {
    char sec[3] = {'-', '-', 0};
    if (v.timeValid) {
      sec[0] = static_cast<char>('0' + v.second / 10);
      sec[1] = static_cast<char>('0' + v.second % 10);
    }
    segmentString(g, cursor - spacing + 8, top + digitH - secH, secW, secH, 7, 4, 6, sec, true);
  }

  // Second LCD line: month.day on the left, temperature on the right.
  char date[8] = "--.--";
  if (v.timeValid) {
    snprintf(date, sizeof(date), "%02d.%02d", v.month, v.day);
  }
  segmentString(g, 24, 198, 24, 44, 6, 4, 6, date, true);
  char temp[10] = "--.-*C";
  if (v.climateValid) {
    const int tenths = static_cast<int>(lroundf(v.temperatureC * 10.0f));
    const int whole = tenths / 10;
    const int frac = (tenths < 0 ? -tenths : tenths) % 10;
    if (whole >= -9 && whole <= 99) {
      snprintf(temp, sizeof(temp), "%2d.%d*C", whole, frac);
    }
  }
  segmentString(g, 214, 198, 24, 44, 6, 4, 6, temp, true);
  drawDoto(g, DotoFontSize::Small, 24, 248, "DATE");
  drawDoto(g, DotoFontSize::Small, 214, 248, "TEMP");
  char hum[12];
  snprintf(hum, sizeof(hum), v.climateValid ? "RH %.0f%%" : "RH --%%", static_cast<double>(v.humidityPct));
  drawDoto(g, DotoFontSize::Small, 214 + 60, 248, hum);
  if (v.batteryValid) {
    batteryIcon(g, 150, 214, v);
    char bat[8];
    snprintf(bat, sizeof(bat), "%d%%", v.batteryPercent);
    drawDoto(g, DotoFontSize::Small, 150, 230, bat);
  }

  hairline(g, 264);
  agentRow(g, v, 275);
}

// ---------------------------------------------------------------- DOTS

void dotGlyph(U8G2& g, char ch, int x, int y, int pitch, int radius, int columns, int firstColumn) {
  uint8_t bits = 0;
  for (int row = 0; row < 7; ++row) {
    if (!dotMatrixGlyphRow(ch, row, bits)) {
      bits = 0;
    }
    for (int col = 0; col < columns; ++col) {
      const int bit = 4 - (firstColumn + col);
      const int cx = x + col * pitch + pitch / 2;
      const int cy = y + row * pitch + pitch / 2;
      if ((bits >> bit) & 1) {
        g.drawDisc(cx, cy, radius);
      } else {
        g.drawPixel(cx, cy);
      }
    }
  }
}

void drawDotsFace(U8G2& g, const ClockView& v) {
  statusRow(g, v, 6);
  hairline(g, 22);

  char hm[6] = "--:--";
  if (v.timeValid) {
    const int hour = displayHour(v.hour, v.hour12);
    snprintf(hm, sizeof(hm), "%02d:%02d", hour, v.minute);
  }
  const int pitch = 13;
  const int radius = 5;
  // 5 columns per digit, 2 for the colon, 1 column between glyphs.
  const int totalColumns = 5 * 4 + 2 + 4;
  int x = (kWidth - totalColumns * pitch) / 2;
  const int y = 34;
  for (int index = 0; index < 5; ++index) {
    const char ch = hm[index];
    if (ch == ':') {
      const bool on = !v.timeValid || (v.second % 2) == 0;
      if (on) {
        dotGlyph(g, ':', x, y, pitch, radius, 2, 1);
      } else {
        dotGlyph(g, ' ', x, y, pitch, radius, 2, 1);
      }
      x += 3 * pitch;
      continue;
    }
    dotGlyph(g, ch == '-' ? ' ' : ch, x, y, pitch, radius, 5, 0);
    x += 6 * pitch;
  }
  if (v.hour12 && v.timeValid) {
    drawDoto(g, DotoFontSize::Small, kWidth - 30, 34, isAfternoon(v.hour) ? "PM" : "AM");
  }

  // Sixty-dot seconds rail.
  const int railPitch = 6;
  const int railX = (kWidth - 59 * railPitch) / 2;
  const int railY = 146;
  for (int s = 0; s < 60; ++s) {
    const int cx = railX + s * railPitch;
    if (v.timeValid && s <= v.second) {
      g.drawDisc(cx, railY, 2);
    } else if (s % 5 == 0) {
      g.drawBox(cx - 1, railY - 1, 2, 2);
    } else {
      g.drawPixel(cx, railY);
    }
  }
  char sec[8];
  formatSeconds(v, sec, sizeof(sec));
  drawDoto(g, DotoFontSize::Small, (kWidth - dotoWidth(DotoFontSize::Small, sec)) / 2, 156, sec);

  char date[32];
  if (v.timeValid) {
    snprintf(date, sizeof(date), "%s %02d %s %04d", weekdayShortName(v.weekday), v.day, monthShortName(v.month), v.year);
  } else {
    snprintf(date, sizeof(date), "SYNCING TIME");
  }
  drawDoto(g, DotoFontSize::Medium, (kWidth - dotoWidth(DotoFontSize::Medium, date)) / 2, 182, date);
  char climate[24];
  formatClimate(v, climate, sizeof(climate));
  drawDoto(g, DotoFontSize::Medium, (kWidth - dotoWidth(DotoFontSize::Medium, climate)) / 2, 212, climate);

  hairline(g, 262);
  agentRow(g, v, 274);
}

// ---------------------------------------------------------------- ANALOG

void drawAnalogFace(U8G2& g, const ClockView& v) {
  const int cx = 146;
  const int cy = 150;
  const int radius = 136;

  g.drawCircle(cx, cy, radius + 3);
  for (int tick = 0; tick < 60; ++tick) {
    const float turn = static_cast<float>(tick) / 60.0f;
    const bool hourTick = tick % 5 == 0;
    const ClockPoint outer = clockHandPoint(cx, cy, radius - 2, turn);
    const ClockPoint inner = clockHandPoint(cx, cy, hourTick ? radius - 18 : radius - 8, turn);
    if (hourTick) {
      taperedBar(g, static_cast<float>(inner.x), static_cast<float>(inner.y), static_cast<float>(outer.x),
                 static_cast<float>(outer.y), 4.0f, 4.0f);
    } else {
      g.drawLine(inner.x, inner.y, outer.x, outer.y);
    }
  }
  static const char* kNumerals[4] = {"12", "3", "6", "9"};
  for (int index = 0; index < 4; ++index) {
    const ClockPoint p = clockHandPoint(cx, cy, radius - 38, static_cast<float>(index) / 4.0f);
    const int w = fontWidth(g, u8g2_font_helvB18_tr, kNumerals[index]);
    fontText(g, u8g2_font_helvB18_tr, p.x - w / 2, p.y - 9, kNumerals[index]);
  }

  // Date window between the centre and "3".
  if (v.timeValid) {
    char day[4];
    snprintf(day, sizeof(day), "%02d", v.day);
    g.drawFrame(cx + 44, cy - 10, 28, 20);
    drawDoto(g, DotoFontSize::Medium, cx + 47, cy - 7, day);
  }

  if (v.timeValid) {
    const ClockPoint hourEnd = clockHandPoint(cx, cy, 72, hourHandTurn(v.hour, v.minute, v.second));
    const ClockPoint minuteEnd = clockHandPoint(cx, cy, 112, minuteHandTurn(v.minute, v.second));
    taperedBar(g, static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(hourEnd.x),
               static_cast<float>(hourEnd.y), 11.0f, 5.0f);
    taperedBar(g, static_cast<float>(cx), static_cast<float>(cy), static_cast<float>(minuteEnd.x),
               static_cast<float>(minuteEnd.y), 7.0f, 3.0f);
    if (v.showSeconds) {
      const float secondTurn = secondHandTurn(v.second);
      const ClockPoint tip = clockHandPoint(cx, cy, 122, secondTurn);
      const ClockPoint tail = clockHandPoint(cx, cy, 22, secondTurn + 0.5f);
      g.drawLine(tail.x, tail.y, tip.x, tip.y);
      const ClockPoint lolly = clockHandPoint(cx, cy, 96, secondTurn);
      g.drawDisc(lolly.x, lolly.y, 4);
    }
  }
  g.drawDisc(cx, cy, 7);
  paper(g);
  g.drawDisc(cx, cy, 2);
  ink(g);

  // Side panel.
  const int px = 298;
  g.drawVLine(px - 8, 16, kHeight - 32);
  drawDoto(g, DotoFontSize::Medium, px, 18, v.timeValid ? weekdayShortName(v.weekday) : "---");
  char day[4] = "--";
  if (v.timeValid) {
    snprintf(day, sizeof(day), "%02d", v.day);
  }
  drawDoto(g, DotoFontSize::Hero, px, 42, day, 2);
  char monthYear[16] = "--- ----";
  if (v.timeValid) {
    snprintf(monthYear, sizeof(monthYear), "%s %04d", monthShortName(v.month), v.year);
  }
  drawDoto(g, DotoFontSize::Small, px, 100, monthYear);
  g.drawHLine(px, 118, kWidth - px - 10);
  char digital[12];
  if (v.timeValid) {
    snprintf(digital, sizeof(digital), "%02d:%02d:%02d", v.hour, v.minute, v.second);
  } else {
    snprintf(digital, sizeof(digital), "--:--:--");
  }
  drawDoto(g, DotoFontSize::Medium, px, 128, clipDoto(DotoFontSize::Medium, digital, kWidth - px - 8).c_str());
  char climate[20];
  formatClimate(v, climate, sizeof(climate));
  drawDoto(g, DotoFontSize::Small, px, 154, climate);
  batteryIcon(g, px, 172, v);
  if (v.batteryValid) {
    char bat[8];
    snprintf(bat, sizeof(bat), "%d%%", v.batteryPercent);
    drawDoto(g, DotoFontSize::Small, px + 28, 172, bat);
  }
  g.drawHLine(px, 192, kWidth - px - 10);
  const int agentY = 202;
  if (!v.agentConnected) {
    drawDoto(g, DotoFontSize::Small, px, agentY, "BRIDGE");
    drawDoto(g, DotoFontSize::Medium, px, agentY + 14, "OFF");
    drawDoto(g, DotoFontSize::Small, px, agentY + 38, clipDoto(DotoFontSize::Small, v.linkLabel, kWidth - px - 8).c_str());
  } else {
    drawDoto(g, DotoFontSize::Small, px, agentY, clipDoto(DotoFontSize::Small, v.agentSource, kWidth - px - 8).c_str());
    if (v.agentAttention) {
      g.drawRBox(px - 2, agentY + 12, kWidth - px - 6, 20, 4);
      paper(g);
      drawDoto(g, DotoFontSize::Medium, px + 2, agentY + 14, "WAIT");
      ink(g);
    } else {
      drawDoto(g, DotoFontSize::Medium, px, agentY + 14,
               clipDoto(DotoFontSize::Medium, v.agentDetail, kWidth - px - 8).c_str());
    }
    if (!v.agentTask.empty()) {
      drawDoto(g, DotoFontSize::Small, px, agentY + 38, clipDoto(DotoFontSize::Small, v.agentTask, kWidth - px - 8).c_str());
    }
  }
}

// ---------------------------------------------------------------- WORDS

void drawWordsFace(U8G2& g, const ClockView& v) {
  const WordClockFrame frame = v.timeValid ? wordClockFrame(v.hour, v.minute) : WordClockFrame{};
  const int cellW = 32;
  const int cellH = 24;
  const int gridX = (kWidth - cellW * kWordClockCols) / 2;
  const int gridY = 8;
  g.setFont(u8g2_font_helvB14_tr);
  for (int row = 0; row < kWordClockRows; ++row) {
    for (int col = 0; col < kWordClockCols; ++col) {
      const char letter[2] = {wordClockLetter(row, col), 0};
      const int w = g.getStrWidth(letter);
      const int x = gridX + col * cellW + (cellW - w) / 2;
      const int y = gridY + row * cellH + 5;
      g.drawStr(x, y, letter);
      if (!frame.lit[row][col]) {
        lighten(g, gridX + col * cellW, gridY + row * cellH, cellW, cellH, 2);
      }
    }
  }

  hairline(g, 256);
  const int rowY = 266;
  if (v.agentAttention) {
    agentRow(g, v, rowY + 4);
    return;
  }
  char date[24];
  formatDate(v, date, sizeof(date));
  drawDoto(g, DotoFontSize::Medium, 12, rowY, date);
  // Minute dots: 1-4 minutes past the spoken five-minute step.
  const int dotsX = kWidth / 2 - 30;
  for (int index = 0; index < 4; ++index) {
    const int dx = dotsX + index * 20;
    if (index < frame.minuteDots) {
      g.drawDisc(dx, rowY + 8, 5);
    } else {
      g.drawCircle(dx, rowY + 8, 5);
    }
  }
  char right[16];
  if (v.timeValid) {
    snprintf(right, sizeof(right), "%02d:%02d", v.hour, v.minute);
  } else {
    snprintf(right, sizeof(right), "--:--");
  }
  const int rightWidth = dotoWidth(DotoFontSize::Medium, right);
  drawDoto(g, DotoFontSize::Medium, kWidth - 12 - 28 - rightWidth, rowY, right);
  batteryIcon(g, kWidth - 12 - 22, rowY + 3, v);
}

// ---------------------------------------------------------------- TERMINAL

void drawTerminalFace(U8G2& g, const ClockView& v, uint32_t tickMs) {
  const uint8_t* mono = u8g2_font_t0_16b_tr;
  const int lineH = 18;
  int y = 6;
  fontText(g, mono, 10, y, "pet@rlcd:~$ date");
  y += lineH;
  char date[40];
  if (v.timeValid) {
    snprintf(date, sizeof(date), "%s %02d %s %04d", weekdayShortName(v.weekday), v.day, monthShortName(v.month), v.year);
  } else {
    snprintf(date, sizeof(date), "date: clock not synced");
  }
  fontText(g, mono, 10, y, date);

  char hm[8];
  formatHourMinute(v, hm, sizeof(hm));
  const int bigTop = 50;
  fontText(g, u8g2_font_inb63_mn, 10, bigTop, hm);
  int x = 10 + fontWidth(g, u8g2_font_inb63_mn, hm);
  if (v.showSeconds) {
    char sec[4];
    snprintf(sec, sizeof(sec), ":%s", v.timeValid ? "" : "--");
    if (v.timeValid) {
      snprintf(sec, sizeof(sec), ":%02d", v.second);
    }
    fontText(g, u8g2_font_inb33_mn, x + 2, bigTop + 63 - 33, sec);
    x += 2 + fontWidth(g, u8g2_font_inb33_mn, sec);
  }
  const bool cursorOn = ((tickMs / 500) % 2) == 0;
  if (cursorOn) {
    g.drawBox(x + 6, bigTop + 63 - 30, 16, 30);
  }
  if (v.hour12 && v.timeValid) {
    fontText(g, mono, kWidth - 34, bigTop, isAfternoon(v.hour) ? "PM" : "AM");
  }

  y = 128;
  fontText(g, mono, 10, y, "pet@rlcd:~$ agents");
  y += lineH;
  if (!v.agentConnected) {
    fontText(g, mono, 10, y, clipFont(g, mono, "bridge: offline (" + v.linkLabel + ")", kWidth - 20).c_str());
    y += lineH;
  } else {
    for (int index = 0; index < v.agentCount && index < kClockAgentLines; ++index) {
      const ClockAgentLine& line = v.agents[index];
      std::string text = std::string(line.active ? "> " : "  ") + line.source;
      while (text.size() < 12) {
        text += ' ';
      }
      // The agent the board is focused on gets its fine-grained state.
      text += (line.source == v.agentSource && !line.attention) ? v.agentDetail : line.status;
      if (line.attention) {
        g.drawBox(8, y - 1, kWidth - 16, lineH);
        paper(g);
        fontText(g, mono, 10, y, clipFont(g, mono, text + "  <- needs you", kWidth - 20).c_str());
        ink(g);
      } else {
        fontText(g, mono, 10, y, clipFont(g, mono, text, kWidth - 20).c_str());
      }
      y += lineH;
    }
  }
  y = 222;
  fontText(g, mono, 10, y, "pet@rlcd:~$ sensors");
  y += lineH;
  char sensors[64];
  if (v.climateValid) {
    snprintf(sensors, sizeof(sensors), "temp %.1fC  hum %.0f%%  bat %s",
             static_cast<double>(v.temperatureC), static_cast<double>(v.humidityPct),
             v.batteryValid ? (std::to_string(v.batteryPercent) + "%").c_str() : "--");
  } else {
    snprintf(sensors, sizeof(sensors), "temp --  hum --  bat %s",
             v.batteryValid ? (std::to_string(v.batteryPercent) + "%").c_str() : "--");
  }
  fontText(g, mono, 10, y, clipFont(g, mono, sensors, kWidth - 20).c_str());
  y += lineH;
  fontText(g, mono, 10, y, "pet@rlcd:~$");
  if (cursorOn) {
    const int promptWidth = fontWidth(g, mono, "pet@rlcd:~$ ");
    g.drawBox(10 + promptWidth, y + 1, 9, 14);
  }
}

// ---------------------------------------------------------------- PET

void petSpriteScaled(U8G2& g, const PetBitmapFrame& frame, int x, int y, int scale) {
  for (int row = 0; row < frame.height; ++row) {
    int runStart = -1;
    for (int col = 0; col <= frame.width; ++col) {
      const bool on = col < frame.width && petBitmapPixel(frame, static_cast<uint8_t>(col), static_cast<uint8_t>(row));
      if (on && runStart < 0) {
        runStart = col;
      } else if (!on && runStart >= 0) {
        g.drawBox(x + runStart * scale, y + row * scale, (col - runStart) * scale, scale);
        runStart = -1;
      }
    }
  }
}

void drawPetFace(U8G2& g, const ClockView& v, uint32_t tickMs) {
  // Speech bubble with a tail pointing at the pet.
  const int bubbleX = 14;
  const int bubbleY = 18;
  const int bubbleW = 196;
  const int bubbleH = 34;
  g.drawRFrame(bubbleX, bubbleY, bubbleW, bubbleH, 10);
  g.drawRFrame(bubbleX + 1, bubbleY + 1, bubbleW - 2, bubbleH - 2, 9);
  paper(g);
  g.drawBox(62, bubbleY + bubbleH - 2, 18, 3);
  ink(g);
  g.drawLine(62, bubbleY + bubbleH - 2, 70, bubbleY + bubbleH + 12);
  g.drawLine(80, bubbleY + bubbleH - 2, 70, bubbleY + bubbleH + 12);
  g.drawLine(63, bubbleY + bubbleH - 2, 70, bubbleY + bubbleH + 11);
  g.drawLine(79, bubbleY + bubbleH - 2, 70, bubbleY + bubbleH + 11);
  const std::string bubble = clipDoto(DotoFontSize::Medium, v.petBubble, bubbleW - 20);
  drawDoto(g, DotoFontSize::Medium, bubbleX + (bubbleW - dotoWidth(DotoFontSize::Medium, bubble.c_str())) / 2,
           bubbleY + 9, bubble.c_str());

  const PetBitmapFrame& sprite = bitmapForMode(v.petMode, tickMs);
  const int scale = 2;
  const int spriteX = 44;
  const int spriteY = 76;
  petSpriteScaled(g, sprite, spriteX, spriteY, scale);
  g.drawHLine(16, spriteY + sprite.height * scale + 4, 196);
  for (int x = 20; x < 212; x += 12) {
    g.drawPixel(x, spriteY + sprite.height * scale + 8);
  }

  // Time on the right, right-aligned so wide 12/24h strings never clip.
  char hm[8];
  formatHourMinute(v, hm, sizeof(hm));
  const int rightEdge = kWidth - 14;
  const int timeWidth = dotoWidth(DotoFontSize::Hero, hm, 2);
  const int timeX = rightEdge - timeWidth;
  const int columnX = 232;
  drawDoto(g, DotoFontSize::Hero, timeX, 78, hm, 2);
  if (v.showSeconds) {
    char sec[8];
    formatSeconds(v, sec, sizeof(sec));
    const std::string label = std::string(":") + sec +
                              (v.hour12 && v.timeValid ? (isAfternoon(v.hour) ? " PM" : " AM") : "");
    drawDoto(g, DotoFontSize::Medium, rightEdge - dotoWidth(DotoFontSize::Medium, label.c_str()), 134, label.c_str());
  }
  char date[24];
  formatDate(v, date, sizeof(date));
  const std::string dateText = clipDoto(DotoFontSize::Medium, date, rightEdge - columnX);
  drawDoto(g, DotoFontSize::Medium, rightEdge - dotoWidth(DotoFontSize::Medium, dateText.c_str()), 162, dateText.c_str());
  char climate[24];
  formatClimate(v, climate, sizeof(climate));
  drawDoto(g, DotoFontSize::Small, rightEdge - dotoWidth(DotoFontSize::Small, climate), 188, climate);
  if (v.batteryValid) {
    char bat[8];
    snprintf(bat, sizeof(bat), "%d%%", v.batteryPercent);
    const int batWidth = dotoWidth(DotoFontSize::Small, bat);
    drawDoto(g, DotoFontSize::Small, rightEdge - batWidth, 206, bat);
    batteryIcon(g, rightEdge - batWidth - 28, 206, v);
  }

  hairline(g, 262);
  agentRow(g, v, 274);
}

}  // namespace

void drawClockFace(U8G2& g, const ClockView& view, uint32_t tickMs) {
  ink(g);
  switch (view.style) {
    case ClockStyle::Segment:
      drawSegmentFace(g, view);
      break;
    case ClockStyle::Dots:
      drawDotsFace(g, view);
      break;
    case ClockStyle::Analog:
      drawAnalogFace(g, view);
      break;
    case ClockStyle::Words:
      drawWordsFace(g, view);
      break;
    case ClockStyle::Terminal:
      drawTerminalFace(g, view, tickMs);
      break;
    case ClockStyle::Pet:
      drawPetFace(g, view, tickMs);
      break;
    case ClockStyle::Sans:
    default:
      drawSansFace(g, view);
      break;
  }
  ink(g);
}
