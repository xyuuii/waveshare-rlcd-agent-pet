#include <unity.h>

#include "battery_monitor.h"

void setUp(void) {}

void tearDown(void) {}

void test_full_voltage_maps_to_high_percent(void) {
  TEST_ASSERT_GREATER_OR_EQUAL(95, mapBatteryPercent(4200));
}

void test_low_voltage_sets_low_battery(void) {
  const PowerState power = derivePowerState(3450, false);

  TEST_ASSERT_TRUE(power.lowBattery);
}

void test_charging_shows_detail(void) {
  const PowerState power = derivePowerState(3900, true);

  TEST_ASSERT_TRUE(power.charging);
  TEST_ASSERT_TRUE(shouldShowBatteryDetail(power));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_full_voltage_maps_to_high_percent);
  RUN_TEST(test_low_voltage_sets_low_battery);
  RUN_TEST(test_charging_shows_detail);
  return UNITY_END();
}
