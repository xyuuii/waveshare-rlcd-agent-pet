#pragma once

#include <stdint.h>

#include <string>

// Pure clock logic shared by the firmware, the host renderer and native tests.
// Nothing here touches U8g2 or Arduino APIs.

enum class ClockStyle : uint8_t {
  Sans,
  Segment,
  Dots,
  Analog,
  Words,
  Terminal,
  Pet,
  Count,
};

static constexpr int kClockStyleCount = static_cast<int>(ClockStyle::Count);

const char* clockStyleName(ClockStyle style);    // stable id, e.g. "segment"
const char* clockStyleLabel(ClockStyle style);   // short label for the panel, e.g. "LCD"
bool clockStyleFromName(const char* name, ClockStyle& out);
ClockStyle nextClockStyle(ClockStyle style);
ClockStyle previousClockStyle(ClockStyle style);

// Hours as shown on the face; 12-hour mode maps 0 -> 12 and 13 -> 1.
int displayHour(int hour24, bool hour12);
bool isAfternoon(int hour24);

const char* weekdayShortName(int weekday);  // 0 = Sunday ... 6 = Saturday, "SUN"
const char* monthShortName(int month);      // 1..12, "JAN"

// Seven segment bit mask: bit0=a (top), b, c, d, e, f, bit6=g (middle).
// Digits, '-', and a few letters (A C H P); '*' draws a degree mark.
uint8_t sevenSegmentMask(char digit);

// 5x7 dot matrix glyph rows for digits and ':' (bit 4 = leftmost column).
bool dotMatrixGlyphRow(char ch, int row, uint8_t& bits);

// Analog hands: endpoint for a fraction of a full turn (0 = 12 o'clock, clockwise).
struct ClockPoint {
  int x;
  int y;
};
ClockPoint clockHandPoint(int cx, int cy, int radius, float turnFraction);
float hourHandTurn(int hour, int minute, int second);
float minuteHandTurn(int minute, int second);
float secondHandTurn(int second);

// Word clock: an 11 x 10 letter grid, each lit letter is one bit.
static constexpr int kWordClockCols = 11;
static constexpr int kWordClockRows = 10;
struct WordClockFrame {
  uint8_t lit[kWordClockRows][kWordClockCols];
  int minuteDots;  // 0..4 extra minutes past the five-minute step
};
char wordClockLetter(int row, int col);
WordClockFrame wordClockFrame(int hour24, int minute);
std::string wordClockPhrase(int hour24, int minute);  // "IT IS TWENTY PAST TEN"
