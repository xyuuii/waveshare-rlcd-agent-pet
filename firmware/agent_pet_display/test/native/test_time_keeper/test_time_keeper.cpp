#include <unity.h>

#include "time_keeper.h"

void setUp(void) {}

void tearDown(void) {}

void test_epoch_round_trip_and_weekday(void) {
  const CivilTime utc{2026, 9, 28, 9, 42, 17, 0};
  const int64_t epoch = epochFromCivilUtc(utc);
  TEST_ASSERT_EQUAL_INT64(1790588537LL, epoch);
  const CivilTime back = civilUtcFromEpoch(epoch);
  TEST_ASSERT_EQUAL(2026, back.year);
  TEST_ASSERT_EQUAL(9, back.month);
  TEST_ASSERT_EQUAL(28, back.day);
  TEST_ASSERT_EQUAL(9, back.hour);
  TEST_ASSERT_EQUAL(42, back.minute);
  TEST_ASSERT_EQUAL(17, back.second);
  TEST_ASSERT_EQUAL(1, back.weekday);  // Monday

  const CivilTime leap{2000, 2, 29, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_INT64(951782400LL, epochFromCivilUtc(leap));
  TEST_ASSERT_EQUAL(2, civilUtcFromEpoch(951782400LL).weekday);  // Tuesday
}

void test_plausibility_window(void) {
  TEST_ASSERT_FALSE(isPlausibleEpoch(0));
  TEST_ASSERT_FALSE(isPlausibleEpoch(946684800LL));  // RTC reset to 2000
  TEST_ASSERT_TRUE(isPlausibleEpoch(1790588537LL));
  TEST_ASSERT_FALSE(isPlausibleEpoch(4102444800LL));
  CivilTime out{};
  TEST_ASSERT_FALSE(localTimeFromEpoch(946684800LL, "UTC0", out));
}

void test_london_summer_winter_and_changeover(void) {
  const char* london = "GMT0BST,M3.5.0/1,M10.5.0";
  CivilTime local{};
  TEST_ASSERT_TRUE(localTimeFromEpoch(1790588537LL, london, local));
  TEST_ASSERT_EQUAL(10, local.hour);  // BST = UTC+1
  TEST_ASSERT_EQUAL(42, local.minute);
  TEST_ASSERT_EQUAL(1, local.weekday);

  TEST_ASSERT_TRUE(localTimeFromEpoch(1792889999LL, london, local));
  TEST_ASSERT_EQUAL(1, local.hour);
  TEST_ASSERT_EQUAL(59, local.minute);
  TEST_ASSERT_TRUE(localTimeFromEpoch(1792890000LL, london, local));
  TEST_ASSERT_EQUAL(1, local.hour);  // clocks go back: 02:00 BST -> 01:00 GMT
  TEST_ASSERT_EQUAL(0, local.minute);

  TEST_ASSERT_TRUE(localTimeFromEpoch(1796126400LL, london, local));
  TEST_ASSERT_EQUAL(12, local.hour);
  TEST_ASSERT_EQUAL(12, local.month);
}

void test_china_default_and_legacy_rtc_migration(void) {
  CivilTime local{};
  TEST_ASSERT_TRUE(localTimeFromEpoch(1790563337LL, "", local));  // falls back to CST-8
  TEST_ASSERT_EQUAL(10, local.hour);
  TEST_ASSERT_EQUAL(42, local.minute);
  TEST_ASSERT_TRUE(localTimeFromEpoch(1790563337LL, "not a tz!", local));
  TEST_ASSERT_EQUAL(10, local.hour);

  // Old firmware stored China local time in the RTC.
  const CivilTime rtc{2026, 9, 28, 10, 42, 17, 1};
  TEST_ASSERT_EQUAL_INT64(1790563337LL, legacyRtcToUtc(rtc));
}

void test_tz_string_validation(void) {
  TEST_ASSERT_TRUE(isAcceptablePosixTz("GMT0BST,M3.5.0/1,M10.5.0"));
  TEST_ASSERT_TRUE(isAcceptablePosixTz("<+0530>-5:30"));
  TEST_ASSERT_TRUE(isAcceptablePosixTz("UTC0"));
  TEST_ASSERT_FALSE(isAcceptablePosixTz(nullptr));
  TEST_ASSERT_FALSE(isAcceptablePosixTz(""));
  TEST_ASSERT_FALSE(isAcceptablePosixTz("0UTC"));
  TEST_ASSERT_FALSE(isAcceptablePosixTz("UTC0; rm -rf"));
  TEST_ASSERT_FALSE(isAcceptablePosixTz("ABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDEFGHIJABCDE"));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_epoch_round_trip_and_weekday);
  RUN_TEST(test_plausibility_window);
  RUN_TEST(test_london_summer_winter_and_changeover);
  RUN_TEST(test_china_default_and_legacy_rtc_migration);
  RUN_TEST(test_tz_string_validation);
  return UNITY_END();
}
