#include <unity.h>

#include "doto_font.h"

void setUp(void) {}

void tearDown(void) {}

bool glyphHasInk(DotoFontSize size, char ch) {
  const int advance = dotoGlyphAdvance(size, ch);
  const int height = dotoTextHeight(size);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < advance; ++x) {
      if (dotoGlyphPixel(size, ch, static_cast<uint8_t>(x), static_cast<uint8_t>(y))) {
        return true;
      }
    }
  }
  return false;
}

int textInkPixels(DotoFontSize size, const char* text) {
  int ink = 0;
  for (const char* cursor = text; *cursor != 0; ++cursor) {
    const int advance = dotoGlyphAdvance(size, *cursor);
    const int height = dotoTextHeight(size);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < advance; ++x) {
        if (dotoGlyphPixel(size, *cursor, static_cast<uint8_t>(x), static_cast<uint8_t>(y))) {
          ++ink;
        }
      }
    }
  }
  return ink;
}

void test_doto_font_has_metrics_for_clock_and_agent_text(void) {
  TEST_ASSERT_TRUE(dotoTextHeight(DotoFontSize::Hero) >= 22);
  TEST_ASSERT_TRUE(dotoTextWidth(DotoFontSize::Hero, "09:52:15") > 80);
  TEST_ASSERT_TRUE(dotoTextWidth(DotoFontSize::Medium, "THINKING") > 40);
}

void test_doto_font_keeps_ascii_punctuation_needed_by_rlcd_ui(void) {
  TEST_ASSERT_TRUE(glyphHasInk(DotoFontSize::Medium, ':'));
  TEST_ASSERT_TRUE(dotoTextWidth(DotoFontSize::Medium, "35.0k / 258k") > 70);
  TEST_ASSERT_EQUAL(0, dotoTextWidth(DotoFontSize::Medium, ""));
}

void test_doto_hero_font_stays_airier_for_rlcd(void) {
  const char* sample = "THINKING";
  const int totalPixels = dotoTextWidth(DotoFontSize::Hero, sample) * dotoTextHeight(DotoFontSize::Hero);
  const int inkPixels = textInkPixels(DotoFontSize::Hero, sample);

  TEST_ASSERT_TRUE(inkPixels * 100 < totalPixels * 22);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_doto_font_has_metrics_for_clock_and_agent_text);
  RUN_TEST(test_doto_font_keeps_ascii_punctuation_needed_by_rlcd_ui);
  RUN_TEST(test_doto_hero_font_stays_airier_for_rlcd);
  return UNITY_END();
}
