#include "clock_model.h"

#include <math.h>
#include <string.h>

namespace {

struct StyleInfo {
  const char* name;
  const char* label;
};

constexpr StyleInfo kStyles[kClockStyleCount] = {
    {"sans", "SANS"},
    {"segment", "LCD"},
    {"dots", "DOTS"},
    {"analog", "DIAL"},
    {"words", "WORDS"},
    {"terminal", "TERM"},
    {"pet", "PET"},
};

constexpr const char* kWeekdays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
constexpr const char* kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                     "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Each row is 5 bits wide, bit 4 is the leftmost dot.
constexpr uint8_t kDotDigits[10][7] = {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},  // 0
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},  // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},  // 2
    {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},  // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},  // 4
    {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},  // 5
    {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},  // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},  // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},  // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},  // 9
};
constexpr uint8_t kDotColon[7] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};

// Our own letter grid. The filler letters hide "GUGUGAGA RLCD XYUUII".
constexpr const char* kWordGrid[kWordClockRows] = {
    "ITGISUGUGAG",  // IT IS
    "AAQUARTERRL",  // A QUARTER
    "TWENTYFIVEC",  // TWENTY FIVE
    "HALFDTENXTO",  // HALF TEN TO
    "PASTYUUNINE",  // PAST NINE
    "ONESIXTHREE",  // ONE SIX THREE
    "FOURFIVETWO",  // FOUR FIVE TWO
    "EIGHTELEVEN",  // EIGHT ELEVEN
    "SEVENTWELVE",  // SEVEN TWELVE
    "TENIIOCLOCK",  // TEN OCLOCK
};

struct WordSpan {
  uint8_t row;
  uint8_t col;
  uint8_t len;
  const char* text;
};

constexpr WordSpan kIt{0, 0, 2, "IT"};
constexpr WordSpan kIs{0, 3, 2, "IS"};
constexpr WordSpan kA{1, 0, 1, "A"};
constexpr WordSpan kQuarter{1, 2, 7, "QUARTER"};
constexpr WordSpan kTwenty{2, 0, 6, "TWENTY"};
constexpr WordSpan kFiveMin{2, 6, 4, "FIVE"};
constexpr WordSpan kHalf{3, 0, 4, "HALF"};
constexpr WordSpan kTenMin{3, 5, 3, "TEN"};
constexpr WordSpan kTo{3, 9, 2, "TO"};
constexpr WordSpan kPast{4, 0, 4, "PAST"};
constexpr WordSpan kOclock{9, 5, 6, "OCLOCK"};

constexpr WordSpan kHours[12] = {
    {8, 5, 6, "TWELVE"},  // 0 / 12
    {5, 0, 3, "ONE"},
    {6, 8, 3, "TWO"},
    {5, 6, 5, "THREE"},
    {6, 0, 4, "FOUR"},
    {6, 4, 4, "FIVE"},
    {5, 3, 3, "SIX"},
    {8, 0, 5, "SEVEN"},
    {7, 0, 5, "EIGHT"},
    {4, 7, 4, "NINE"},
    {9, 0, 3, "TEN"},
    {7, 5, 6, "ELEVEN"},
};

struct WordPlan {
  const WordSpan* words[8];
  int count;
};

WordPlan planWords(int hour24, int minute) {
  WordPlan plan{};
  auto add = [&plan](const WordSpan& span) {
    if (plan.count < 8) {
      plan.words[plan.count++] = &span;
    }
  };
  const int safeMinute = minute < 0 ? 0 : (minute > 59 ? 59 : minute);
  const int step = safeMinute / 5;
  int hour = ((hour24 % 24) + 24) % 24;
  if (step >= 7) {
    hour += 1;
  }
  const WordSpan& hourWord = kHours[hour % 12];

  add(kIt);
  add(kIs);
  switch (step) {
    case 0:
      add(hourWord);
      add(kOclock);
      return plan;
    case 1:
      add(kFiveMin);
      add(kPast);
      break;
    case 2:
      add(kTenMin);
      add(kPast);
      break;
    case 3:
      add(kA);
      add(kQuarter);
      add(kPast);
      break;
    case 4:
      add(kTwenty);
      add(kPast);
      break;
    case 5:
      add(kTwenty);
      add(kFiveMin);
      add(kPast);
      break;
    case 6:
      add(kHalf);
      add(kPast);
      break;
    case 7:
      add(kTwenty);
      add(kFiveMin);
      add(kTo);
      break;
    case 8:
      add(kTwenty);
      add(kTo);
      break;
    case 9:
      add(kA);
      add(kQuarter);
      add(kTo);
      break;
    case 10:
      add(kTenMin);
      add(kTo);
      break;
    default:
      add(kFiveMin);
      add(kTo);
      break;
  }
  add(hourWord);
  return plan;
}

}  // namespace

const char* clockStyleName(ClockStyle style) {
  const int index = static_cast<int>(style);
  if (index < 0 || index >= kClockStyleCount) {
    return kStyles[0].name;
  }
  return kStyles[index].name;
}

const char* clockStyleLabel(ClockStyle style) {
  const int index = static_cast<int>(style);
  if (index < 0 || index >= kClockStyleCount) {
    return kStyles[0].label;
  }
  return kStyles[index].label;
}

bool clockStyleFromName(const char* name, ClockStyle& out) {
  if (!name || !name[0]) {
    return false;
  }
  for (int index = 0; index < kClockStyleCount; ++index) {
    if (strcmp(name, kStyles[index].name) == 0) {
      out = static_cast<ClockStyle>(index);
      return true;
    }
  }
  return false;
}

ClockStyle nextClockStyle(ClockStyle style) {
  const int index = static_cast<int>(style);
  return static_cast<ClockStyle>((index + 1 + kClockStyleCount) % kClockStyleCount);
}

ClockStyle previousClockStyle(ClockStyle style) {
  const int index = static_cast<int>(style);
  return static_cast<ClockStyle>((index - 1 + kClockStyleCount) % kClockStyleCount);
}

int displayHour(int hour24, bool hour12) {
  const int hour = ((hour24 % 24) + 24) % 24;
  if (!hour12) {
    return hour;
  }
  const int value = hour % 12;
  return value == 0 ? 12 : value;
}

bool isAfternoon(int hour24) {
  const int hour = ((hour24 % 24) + 24) % 24;
  return hour >= 12;
}

const char* weekdayShortName(int weekday) {
  if (weekday < 0 || weekday > 6) {
    return "---";
  }
  return kWeekdays[weekday];
}

const char* monthShortName(int month) {
  if (month < 1 || month > 12) {
    return "---";
  }
  return kMonths[month - 1];
}

uint8_t sevenSegmentMask(char digit) {
  switch (digit) {
    case '0':
      return 0x3F;
    case '1':
      return 0x06;
    case '2':
      return 0x5B;
    case '3':
      return 0x4F;
    case '4':
      return 0x66;
    case '5':
      return 0x6D;
    case '6':
      return 0x7D;
    case '7':
      return 0x07;
    case '8':
      return 0x7F;
    case '9':
      return 0x6F;
    case '-':
      return 0x40;
    case 'A':
      return 0x77;
    case 'C':
      return 0x39;
    case 'H':
      return 0x76;
    case 'P':
      return 0x73;
    case '*':  // degree mark
      return 0x63;
    default:
      return 0x00;
  }
}

bool dotMatrixGlyphRow(char ch, int row, uint8_t& bits) {
  if (row < 0 || row >= 7) {
    return false;
  }
  if (ch >= '0' && ch <= '9') {
    bits = kDotDigits[ch - '0'][row];
    return true;
  }
  if (ch == ':') {
    bits = kDotColon[row];
    return true;
  }
  if (ch == ' ') {
    bits = 0;
    return true;
  }
  return false;
}

ClockPoint clockHandPoint(int cx, int cy, int radius, float turnFraction) {
  const float angle = turnFraction * 6.28318530718f;
  const float x = static_cast<float>(cx) + static_cast<float>(radius) * sinf(angle);
  const float y = static_cast<float>(cy) - static_cast<float>(radius) * cosf(angle);
  return ClockPoint{static_cast<int>(lroundf(x)), static_cast<int>(lroundf(y))};
}

float hourHandTurn(int hour, int minute, int second) {
  const float hours = static_cast<float>(((hour % 12) + 12) % 12) + static_cast<float>(minute) / 60.0f +
                      static_cast<float>(second) / 3600.0f;
  return hours / 12.0f;
}

float minuteHandTurn(int minute, int second) {
  return (static_cast<float>(minute) + static_cast<float>(second) / 60.0f) / 60.0f;
}

float secondHandTurn(int second) {
  return static_cast<float>(second) / 60.0f;
}

char wordClockLetter(int row, int col) {
  if (row < 0 || row >= kWordClockRows || col < 0 || col >= kWordClockCols) {
    return ' ';
  }
  return kWordGrid[row][col];
}

WordClockFrame wordClockFrame(int hour24, int minute) {
  WordClockFrame frame{};
  const WordPlan plan = planWords(hour24, minute);
  for (int index = 0; index < plan.count; ++index) {
    const WordSpan& span = *plan.words[index];
    for (int offset = 0; offset < span.len; ++offset) {
      frame.lit[span.row][span.col + offset] = 1;
    }
  }
  const int safeMinute = minute < 0 ? 0 : (minute > 59 ? 59 : minute);
  frame.minuteDots = safeMinute % 5;
  return frame;
}

std::string wordClockPhrase(int hour24, int minute) {
  const WordPlan plan = planWords(hour24, minute);
  std::string phrase;
  for (int index = 0; index < plan.count; ++index) {
    if (!phrase.empty()) {
      phrase += ' ';
    }
    phrase += plan.words[index]->text;
  }
  return phrase;
}
