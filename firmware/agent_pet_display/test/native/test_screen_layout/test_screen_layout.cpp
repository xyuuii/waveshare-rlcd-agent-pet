#include <unity.h>

#include "screen_layout.h"

ScreenRect overviewRightInfoCardRect(int index);

void setUp(void) {}

void tearDown(void) {}

void test_overview_time_card_and_status_card_have_poster_scale_height(void) {
  const ScreenRect timeCard = overviewTimeCardRect();
  const ScreenRect statusCard = overviewStatusCardRect();
  const ScreenRect metricCard = overviewMetricCardRect(0);

  TEST_ASSERT_TRUE(timeCard.h >= 52);
  TEST_ASSERT_TRUE(statusCard.h >= 68);
  TEST_ASSERT_TRUE(timeCard.h > metricCard.h);
  TEST_ASSERT_TRUE(statusCard.h > metricCard.h);
}

void test_overview_metric_cards_stack_without_overlap(void) {
  const ScreenRect first = overviewMetricCardRect(0);
  const ScreenRect second = overviewMetricCardRect(1);
  const ScreenRect third = overviewMetricCardRect(2);

  TEST_ASSERT_EQUAL(first.x, second.x);
  TEST_ASSERT_EQUAL(first.x, third.x);
  TEST_ASSERT_TRUE(first.y + first.h < second.y);
  TEST_ASSERT_TRUE(second.y + second.h < third.y);
  TEST_ASSERT_TRUE(third.h >= 28);
}

void test_overview_uses_companion_badge_instead_of_large_pet_panel(void) {
  const ScreenRect petPanel = overviewPetPanelRect();
  const ScreenRect timeCard = overviewTimeCardRect();
  const ScreenRect statusCard = overviewStatusCardRect();
  const ScreenRect metricCard = overviewMetricCardRect(0);

  TEST_ASSERT_TRUE(petPanel.w <= 120);
  TEST_ASSERT_TRUE(petPanel.h <= 92);
  TEST_ASSERT_TRUE(timeCard.w >= 220);
  TEST_ASSERT_TRUE(timeCard.w > petPanel.w);
  TEST_ASSERT_EQUAL(timeCard.x, metricCard.x);
  TEST_ASSERT_EQUAL(timeCard.w, metricCard.w);
  TEST_ASSERT_TRUE(metricCard.h >= 24);
  TEST_ASSERT_TRUE(petPanel.y > statusCard.y);
}

void test_overview_right_column_is_filled_above_companion_badge(void) {
  const ScreenRect topInfo = overviewRightInfoCardRect(0);
  const ScreenRect lowerInfo = overviewRightInfoCardRect(1);
  const ScreenRect petPanel = overviewPetPanelRect();

  TEST_ASSERT_EQUAL(petPanel.x, topInfo.x);
  TEST_ASSERT_EQUAL(petPanel.w, topInfo.w);
  TEST_ASSERT_EQUAL(petPanel.x, lowerInfo.x);
  TEST_ASSERT_EQUAL(petPanel.w, lowerInfo.w);
  TEST_ASSERT_TRUE(topInfo.y < lowerInfo.y);
  TEST_ASSERT_TRUE(topInfo.h >= 54);
  TEST_ASSERT_TRUE(lowerInfo.h >= 56);
  TEST_ASSERT_TRUE(lowerInfo.y + lowerInfo.h <= petPanel.y - 4);
  TEST_ASSERT_TRUE(petPanel.y + petPanel.h <= 268);
}

void test_overview_quota_bars_fit_within_quota_card(void) {
  const ScreenRect quotaCard = overviewMetricCardRect(2);
  const ScreenRect petPanel = overviewPetPanelRect();
  const QuotaBarLayout bars = overviewQuotaBarLayout();

  TEST_ASSERT_TRUE(bars.fiveHourBar.x >= quotaCard.x + 92);
  TEST_ASSERT_TRUE(bars.fiveHourBar.x + bars.fiveHourBar.w <= quotaCard.x + quotaCard.w - 8);
  TEST_ASSERT_TRUE(bars.weekBar.x >= quotaCard.x + 92);
  TEST_ASSERT_TRUE(bars.weekBar.x + bars.weekBar.w <= quotaCard.x + quotaCard.w - 8);
  TEST_ASSERT_TRUE(bars.fiveHourBar.y < bars.weekBar.y);
  TEST_ASSERT_TRUE(quotaCard.x + quotaCard.w < petPanel.x);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_overview_time_card_and_status_card_have_poster_scale_height);
  RUN_TEST(test_overview_metric_cards_stack_without_overlap);
  RUN_TEST(test_overview_uses_companion_badge_instead_of_large_pet_panel);
  RUN_TEST(test_overview_right_column_is_filled_above_companion_badge);
  RUN_TEST(test_overview_quota_bars_fit_within_quota_card);
  return UNITY_END();
}
