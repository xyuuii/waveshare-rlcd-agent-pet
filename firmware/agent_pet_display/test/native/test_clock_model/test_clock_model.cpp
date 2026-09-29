#include <unity.h>

#include <string.h>

#include <string>

#include "clock_model.h"

void setUp(void) {}

void tearDown(void) {}

void test_style_names_round_trip_and_cycle(void) {
  for (int index = 0; index < kClockStyleCount; ++index) {
    const ClockStyle style = static_cast<ClockStyle>(index);
    ClockStyle parsed = ClockStyle::Sans;
    TEST_ASSERT_TRUE(clockStyleFromName(clockStyleName(style), parsed));
    TEST_ASSERT_EQUAL(index, static_cast<int>(parsed));
    TEST_ASSERT_EQUAL(index, static_cast<int>(previousClockStyle(nextClockStyle(style))));
  }
  ClockStyle untouched = ClockStyle::Words;
  TEST_ASSERT_FALSE(clockStyleFromName("comic-sans", untouched));
  TEST_ASSERT_FALSE(clockStyleFromName(nullptr, untouched));
  TEST_ASSERT_EQUAL(static_cast<int>(ClockStyle::Words), static_cast<int>(untouched));
  TEST_ASSERT_EQUAL(static_cast<int>(ClockStyle::Sans), static_cast<int>(nextClockStyle(ClockStyle::Pet)));
}

void test_twelve_hour_mapping(void) {
  TEST_ASSERT_EQUAL(0, displayHour(0, false));
  TEST_ASSERT_EQUAL(12, displayHour(0, true));
  TEST_ASSERT_EQUAL(12, displayHour(12, true));
  TEST_ASSERT_EQUAL(1, displayHour(13, true));
  TEST_ASSERT_EQUAL(23, displayHour(23, false));
  TEST_ASSERT_FALSE(isAfternoon(11));
  TEST_ASSERT_TRUE(isAfternoon(12));
}

void test_names_are_bounded(void) {
  TEST_ASSERT_EQUAL_STRING("SUN", weekdayShortName(0));
  TEST_ASSERT_EQUAL_STRING("SAT", weekdayShortName(6));
  TEST_ASSERT_EQUAL_STRING("---", weekdayShortName(7));
  TEST_ASSERT_EQUAL_STRING("SEP", monthShortName(9));
  TEST_ASSERT_EQUAL_STRING("---", monthShortName(0));
}

void test_seven_segment_masks(void) {
  TEST_ASSERT_EQUAL_HEX8(0x3F, sevenSegmentMask('0'));
  TEST_ASSERT_EQUAL_HEX8(0x06, sevenSegmentMask('1'));
  TEST_ASSERT_EQUAL_HEX8(0x7F, sevenSegmentMask('8'));
  TEST_ASSERT_EQUAL_HEX8(0x00, sevenSegmentMask('x'));
  TEST_ASSERT_EQUAL_HEX8(0x39, sevenSegmentMask('C'));
  TEST_ASSERT_EQUAL_HEX8(0x63, sevenSegmentMask('*'));
  TEST_ASSERT_EQUAL_HEX8(0x73, sevenSegmentMask('P'));
  int segmentCounts[10] = {6, 2, 5, 5, 4, 5, 6, 3, 7, 6};
  for (int digit = 0; digit < 10; ++digit) {
    const uint8_t mask = sevenSegmentMask(static_cast<char>('0' + digit));
    int count = 0;
    for (int bit = 0; bit < 7; ++bit) {
      count += (mask >> bit) & 1;
    }
    TEST_ASSERT_EQUAL(segmentCounts[digit], count);
  }
}

void test_dot_matrix_glyphs(void) {
  uint8_t bits = 0xFF;
  TEST_ASSERT_TRUE(dotMatrixGlyphRow('8', 3, bits));
  TEST_ASSERT_EQUAL_HEX8(0x0E, bits);
  TEST_ASSERT_TRUE(dotMatrixGlyphRow(':', 0, bits));
  TEST_ASSERT_EQUAL_HEX8(0x00, bits);
  TEST_ASSERT_FALSE(dotMatrixGlyphRow('A', 0, bits));
  TEST_ASSERT_FALSE(dotMatrixGlyphRow('1', 7, bits));
  for (int digit = 0; digit < 10; ++digit) {
    for (int row = 0; row < 7; ++row) {
      TEST_ASSERT_TRUE(dotMatrixGlyphRow(static_cast<char>('0' + digit), row, bits));
      TEST_ASSERT_EQUAL_HEX8(0x00, bits & 0xE0);
    }
  }
}

void test_hand_points_follow_the_dial(void) {
  const ClockPoint twelve = clockHandPoint(100, 100, 50, 0.0f);
  TEST_ASSERT_EQUAL(100, twelve.x);
  TEST_ASSERT_EQUAL(50, twelve.y);
  const ClockPoint three = clockHandPoint(100, 100, 50, 0.25f);
  TEST_ASSERT_EQUAL(150, three.x);
  TEST_ASSERT_EQUAL(100, three.y);
  const ClockPoint six = clockHandPoint(100, 100, 50, 0.5f);
  TEST_ASSERT_EQUAL(100, six.x);
  TEST_ASSERT_EQUAL(150, six.y);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, hourHandTurn(15, 0, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f + 0.5f / 12.0f, hourHandTurn(3, 30, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, minuteHandTurn(30, 0));
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.75f, secondHandTurn(45));
}

void test_word_spans_match_the_grid(void) {
  // Every lit letter, across a full day, must spell the phrase in reading order.
  for (int hour = 0; hour < 24; ++hour) {
    for (int minute = 0; minute < 60; ++minute) {
      const WordClockFrame frame = wordClockFrame(hour, minute);
      std::string letters;
      for (int row = 0; row < kWordClockRows; ++row) {
        for (int col = 0; col < kWordClockCols; ++col) {
          if (frame.lit[row][col]) {
            letters += wordClockLetter(row, col);
          }
        }
      }
      std::string phrase = wordClockPhrase(hour, minute);
      std::string compact;
      for (char ch : phrase) {
        if (ch != ' ') {
          compact += ch;
        }
      }
      TEST_ASSERT_EQUAL_STRING(compact.c_str(), letters.c_str());
      TEST_ASSERT_EQUAL(minute % 5, frame.minuteDots);
    }
  }
}

void test_word_phrases(void) {
  TEST_ASSERT_EQUAL_STRING("IT IS TEN OCLOCK", wordClockPhrase(10, 0).c_str());
  TEST_ASSERT_EQUAL_STRING("IT IS TWENTY PAST TEN", wordClockPhrase(10, 22).c_str());
  TEST_ASSERT_EQUAL_STRING("IT IS A QUARTER PAST TWELVE", wordClockPhrase(0, 15).c_str());
  TEST_ASSERT_EQUAL_STRING("IT IS HALF PAST FIVE", wordClockPhrase(17, 34).c_str());
  TEST_ASSERT_EQUAL_STRING("IT IS TWENTY FIVE TO ELEVEN", wordClockPhrase(10, 35).c_str());
  TEST_ASSERT_EQUAL_STRING("IT IS A QUARTER TO TWELVE", wordClockPhrase(23, 45).c_str());
  TEST_ASSERT_EQUAL_STRING("IT IS FIVE TO ONE", wordClockPhrase(12, 59).c_str());
}

void test_hidden_word_grid_is_well_formed(void) {
  for (int row = 0; row < kWordClockRows; ++row) {
    for (int col = 0; col < kWordClockCols; ++col) {
      const char letter = wordClockLetter(row, col);
      TEST_ASSERT_TRUE(letter >= 'A' && letter <= 'Z');
    }
  }
  TEST_ASSERT_EQUAL(' ', wordClockLetter(10, 0));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_style_names_round_trip_and_cycle);
  RUN_TEST(test_twelve_hour_mapping);
  RUN_TEST(test_names_are_bounded);
  RUN_TEST(test_seven_segment_masks);
  RUN_TEST(test_dot_matrix_glyphs);
  RUN_TEST(test_hand_points_follow_the_dial);
  RUN_TEST(test_word_spans_match_the_grid);
  RUN_TEST(test_word_phrases);
  RUN_TEST(test_hidden_word_grid_is_well_formed);
  return UNITY_END();
}
