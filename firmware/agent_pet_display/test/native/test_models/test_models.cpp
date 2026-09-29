#include <unity.h>

#include "app_config.h"
#include "models.h"

void setUp(void) {}

void tearDown(void) {}

void test_default_agent_state_is_unknown_idle_and_disconnected(void) {
  const AgentState state;

  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::Unknown), static_cast<int>(state.source));
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Idle), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL(static_cast<int>(FocusMode::Auto), static_cast<int>(state.focusMode));
  TEST_ASSERT_EQUAL_STRING("idle", state.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("", state.focusId.c_str());
  TEST_ASSERT_EQUAL(-1, state.focusIndex);
  TEST_ASSERT_EQUAL(0, state.focusCount);
  TEST_ASSERT_FALSE(state.agentSlots[0].present);
  TEST_ASSERT_FALSE(state.connected);
}

void test_normal_power_state_does_not_show_battery_detail(void) {
  const PowerState powerState{4200, 55, false, false, true};

  TEST_ASSERT_FALSE(shouldShowBatteryDetail(powerState));
}

void test_low_battery_power_state_does_show_battery_detail(void) {
  const PowerState powerState{3600, 12, false, true, true};

  TEST_ASSERT_TRUE(shouldShowBatteryDetail(powerState));
}

void test_charging_power_state_does_show_battery_detail(void) {
  const PowerState powerState{4180, 71, true, false, true};

  TEST_ASSERT_TRUE(shouldShowBatteryDetail(powerState));
}

void test_display_state_defaults_include_layout_placeholders(void) {
  const DisplayState view{};

  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Overview), static_cast<int>(view.page));
  TEST_ASSERT_EQUAL(static_cast<int>(PetMode::Sleep), static_cast<int>(view.petMode));
  TEST_ASSERT_EQUAL_STRING("IDLE", view.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("--:--:--", view.sidebarTime.c_str());
  TEST_ASSERT_EQUAL_STRING("----/--/--", view.sidebarDate.c_str());
  TEST_ASSERT_EQUAL_STRING("--", view.sidebarClimate.c_str());
  TEST_ASSERT_EQUAL_STRING("--", view.sidebarAgent.c_str());
  TEST_ASSERT_EQUAL_STRING("--", view.sidebarTokens.c_str());
  TEST_ASSERT_EQUAL_STRING("TODAY", view.sidebarTokensLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("--", view.sidebarContext.c_str());
  TEST_ASSERT_EQUAL_STRING("CONTEXT", view.sidebarContextLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("--", view.sidebarQuota.c_str());
  TEST_ASSERT_EQUAL_STRING("QUOTA", view.sidebarQuotaLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("quota", view.sidebarQuotaStyle.c_str());
  TEST_ASSERT_EQUAL_STRING("AUTO", view.focusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("BOOT page  HOLD/KEY agent", view.focusHint.c_str());
  TEST_ASSERT_EQUAL_STRING("...", view.buddyBubble.c_str());
  TEST_ASSERT_EQUAL_STRING("--", view.footerMessage.c_str());
  TEST_ASSERT_EQUAL_STRING("WIFI --", view.linkLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("WIFI --", view.networkLine.c_str());
  TEST_ASSERT_EQUAL_STRING("BRIDGE --", view.bridgeLine.c_str());
}

void test_next_screen_page_cycles_through_overview_usage_and_clock(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Usage),
                    static_cast<int>(nextScreenPage(ScreenPage::Overview)));
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Clock),
                    static_cast<int>(nextScreenPage(ScreenPage::Usage)));
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Overview),
                    static_cast<int>(nextScreenPage(ScreenPage::Clock)));
  TEST_ASSERT_EQUAL(3, screenPageNumber(ScreenPage::Clock));
  TEST_ASSERT_EQUAL(kScreenPageCount, screenPageNumber(ScreenPage::Clock));
}

void test_environment_refresh_interval_supports_live_seconds(void) {
  TEST_ASSERT_TRUE(kEnvironmentSampleMs <= 1000);
}

int runUnityTests(void) {
  UNITY_BEGIN();
  RUN_TEST(test_default_agent_state_is_unknown_idle_and_disconnected);
  RUN_TEST(test_normal_power_state_does_not_show_battery_detail);
  RUN_TEST(test_low_battery_power_state_does_show_battery_detail);
  RUN_TEST(test_charging_power_state_does_show_battery_detail);
  RUN_TEST(test_display_state_defaults_include_layout_placeholders);
  RUN_TEST(test_next_screen_page_cycles_through_overview_usage_and_clock);
  RUN_TEST(test_environment_refresh_interval_supports_live_seconds);
  return UNITY_END();
}

int main(void) {
  return runUnityTests();
}
