#include <unity.h>

#include "pet_state_machine.h"

void setUp(void) {}

void tearDown(void) {}

void test_running_agent_maps_to_busy_pet(void) {
  AgentState agent{};
  agent.source = SourceKind::Codex;
  agent.status = AgentStatus::Running;
  agent.statusDetail = "thinking";
  agent.task = "Sync bridge";
  agent.updatedAt = "2026-06-08T10:00:00Z";
  agent.connected = true;
  const PowerState power{3800, 60, false, false, true};
  const EnvironmentState environment{true, 2026, 6, 8, 10, 24, 30, 1, 23.5f, 58.0f, true};

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  TEST_ASSERT_EQUAL(static_cast<int>(PetMode::Busy), static_cast<int>(view.petMode));
  TEST_ASSERT_EQUAL_STRING("THINKING", view.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("10:24:30", view.sidebarTime.c_str());
  TEST_ASSERT_EQUAL_STRING("2026/06/08", view.sidebarDate.c_str());
  TEST_ASSERT_EQUAL_STRING("23.5C 58%", view.sidebarClimate.c_str());
  TEST_ASSERT_EQUAL_STRING("CODEX THNK", view.sidebarAgent.c_str());
  TEST_ASSERT_EQUAL_STRING("thinking...", view.buddyBubble.c_str());
  TEST_ASSERT_EQUAL_STRING("THINKING  Sync bridge", view.footerMessage.c_str());
}

void test_low_battery_biases_pet_to_tired(void) {
  AgentState agent{};
  agent.status = AgentStatus::Idle;
  agent.connected = true;
  const PowerState power{3400, 10, false, true, true};
  const EnvironmentState environment{};

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  TEST_ASSERT_EQUAL(static_cast<int>(PetMode::Tired), static_cast<int>(view.petMode));
  TEST_ASSERT_TRUE(view.showBatteryDetail);
}

void test_usage_page_keeps_usage_values_and_marks_page(void) {
  AgentState agent{};
  agent.source = SourceKind::Codex;
  agent.status = AgentStatus::NeedsAttention;
  agent.statusDetail = "needs-attention";
  agent.task = "Approve shell command";
  agent.updatedAt = "2026-06-08T12:00:00Z";
  agent.usageToday = "49M tok";
  agent.usageContext = "66.9k / 258k";
  agent.usageQuota = "5H 88% WK 92%";
  agent.connected = true;
  const PowerState power{4065, 85, false, false, true};
  const EnvironmentState environment{true, 2026, 6, 8, 12, 39, 0, 1, 23.7f, 53.0f, true};

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Usage);

  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Usage), static_cast<int>(view.page));
  TEST_ASSERT_EQUAL(static_cast<int>(PetMode::Attention), static_cast<int>(view.petMode));
  TEST_ASSERT_EQUAL_STRING("WAITING", view.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("49M tok", view.sidebarTokens.c_str());
  TEST_ASSERT_EQUAL_STRING("66.9k / 258k", view.sidebarContext.c_str());
  TEST_ASSERT_EQUAL_STRING("5H 88% WK 92%", view.sidebarQuota.c_str());
  TEST_ASSERT_EQUAL_STRING("PAGE 2/2 BOOT toggle", view.footerMessage.c_str());
}

void test_searching_detail_gets_own_screen_label(void) {
  AgentState agent{};
  agent.source = SourceKind::Codex;
  agent.status = AgentStatus::Running;
  agent.statusDetail = "searching";
  agent.task = "Search open source refs";
  agent.connected = true;
  const PowerState power{4000, 80, false, false, true};
  const EnvironmentState environment{};

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  TEST_ASSERT_EQUAL_STRING("SEARCHING", view.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("CODEX SRCH", view.sidebarAgent.c_str());
  TEST_ASSERT_EQUAL_STRING("searching...", view.buddyBubble.c_str());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_running_agent_maps_to_busy_pet);
  RUN_TEST(test_low_battery_biases_pet_to_tired);
  RUN_TEST(test_usage_page_keeps_usage_values_and_marks_page);
  RUN_TEST(test_searching_detail_gets_own_screen_label);
  return UNITY_END();
}
