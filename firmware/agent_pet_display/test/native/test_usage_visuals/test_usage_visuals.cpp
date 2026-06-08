#include <unity.h>

#include "usage_visuals.h"

void setUp(void) {}

void tearDown(void) {}

void test_parse_quota_usage_extracts_five_hour_and_week_values(void) {
  const QuotaUsage usage = parseQuotaUsage("5H 68% WK 88%");

  TEST_ASSERT_EQUAL(68, usage.fiveHourPercent);
  TEST_ASSERT_EQUAL(88, usage.weekPercent);
}

void test_parse_quota_usage_handles_missing_values(void) {
  const QuotaUsage usage = parseQuotaUsage("--");

  TEST_ASSERT_EQUAL(-1, usage.fiveHourPercent);
  TEST_ASSERT_EQUAL(-1, usage.weekPercent);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_quota_usage_extracts_five_hour_and_week_values);
  RUN_TEST(test_parse_quota_usage_handles_missing_values);
  return UNITY_END();
}
