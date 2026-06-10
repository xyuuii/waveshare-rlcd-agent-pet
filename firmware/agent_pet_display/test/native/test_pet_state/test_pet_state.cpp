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
  TEST_ASSERT_EQUAL_STRING("BOOT page  HOLD/KEY agent", view.focusHint.c_str());
  TEST_ASSERT_EQUAL_STRING("BOOT page  HOLD/KEY agent", view.footerMessage.c_str());
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

void test_openclaw_source_gets_own_label(void) {
  AgentState agent{};
  agent.source = SourceKind::OpenClaw;
  agent.status = AgentStatus::Running;
  agent.statusDetail = "tool-use";
  agent.task = "Run bridge plugin";
  agent.connected = true;
  const PowerState power{3990, 74, false, false, true};
  const EnvironmentState environment{};

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  TEST_ASSERT_EQUAL_STRING("OPENCLAW", view.sourceLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("OPENCLAW TOOL", view.sidebarAgent.c_str());
}

void test_dynamic_usage_labels_flow_into_display_state(void) {
  AgentState agent{};
  agent.source = SourceKind::Hermes;
  agent.status = AgentStatus::Running;
  agent.statusDetail = "thinking";
  agent.connected = true;
  agent.usageToday = "12m 04s";
  agent.usageTodayLabel = "SESS";
  agent.usageTodayHint = "Elapsed active time";
  agent.usageContext = "3 calls";
  agent.usageContextLabel = "TOOLS";
  agent.usageContextHint = "Tool starts in this session";
  agent.usageQuota = "WAITING";
  agent.usageQuotaLabel = "ATTN";
  agent.usageQuotaHint = "User approval / attention state";
  agent.usageQuotaStyle = "text";
  const PowerState power{4010, 82, false, false, true};
  const EnvironmentState environment{};

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Usage);

  TEST_ASSERT_EQUAL_STRING("SESS", view.sidebarTokensLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("TOOLS", view.sidebarContextLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("ATTN", view.sidebarQuotaLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("Elapsed active time", view.sidebarTokensHint.c_str());
  TEST_ASSERT_EQUAL_STRING("User approval / attention state", view.sidebarQuotaHint.c_str());
  TEST_ASSERT_EQUAL_STRING("text", view.sidebarQuotaStyle.c_str());
}

void test_pinned_focus_is_rendered_in_display_state(void) {
  AgentState agent{};
  agent.source = SourceKind::Hermes;
  agent.status = AgentStatus::Running;
  agent.statusDetail = "thinking";
  agent.connected = true;
  agent.focusMode = FocusMode::Pinned;
  agent.focusIndex = 1;
  agent.focusCount = 3;
  const PowerState power{4010, 82, false, false, true};
  const EnvironmentState environment{};

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  TEST_ASSERT_EQUAL_STRING("PIN 2/3", view.focusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("BOOT page  HOLD/KEY agent", view.focusHint.c_str());
}

void test_invalid_pinned_focus_falls_back_to_auto_label(void) {
  AgentState agent{};
  agent.source = SourceKind::Codex;
  agent.status = AgentStatus::Running;
  agent.statusDetail = "thinking";
  agent.connected = true;
  agent.focusMode = FocusMode::Pinned;
  agent.focusIndex = -1;
  agent.focusCount = 3;
  const PowerState power{4010, 82, false, false, true};
  const EnvironmentState environment{};

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  TEST_ASSERT_EQUAL_STRING("AUTO", view.focusLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("BOOT page  HOLD/KEY agent", view.focusHint.c_str());
}

void test_wifi_connected_bridge_offline_gets_clear_footer(void) {
  AgentState agent{};
  agent.connected = false;
  const PowerState power{4010, 82, false, false, true};
  const EnvironmentState environment{};
  NetworkState network{};
  network.wifiKnown = true;
  network.wifiConnected = true;
  network.rssi = -61;
  network.ip = "192.168.1.88";

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Overview, network);

  TEST_ASSERT_EQUAL_STRING("WIFI -61", view.linkLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("WIFI -61dBm", view.networkLine.c_str());
  TEST_ASSERT_EQUAL_STRING("BRIDGE OFF", view.bridgeLine.c_str());
  TEST_ASSERT_EQUAL_STRING("Bridge offline; WiFi OK", view.footerMessage.c_str());
}

void test_wifi_reconnecting_gets_distinct_status_from_bridge_offline(void) {
  AgentState agent{};
  agent.connected = false;
  const PowerState power{4010, 82, false, false, true};
  const EnvironmentState environment{};
  NetworkState network{};
  network.wifiKnown = true;
  network.wifiConnected = false;
  network.wifiConnecting = true;
  network.reconnectAttempts = 3;

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Overview, network);

  TEST_ASSERT_EQUAL_STRING("WIFI TRY", view.linkLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("WIFI TRY 3", view.networkLine.c_str());
  TEST_ASSERT_EQUAL_STRING("BRIDGE WAIT", view.bridgeLine.c_str());
  TEST_ASSERT_EQUAL_STRING("WiFi reconnecting", view.footerMessage.c_str());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_running_agent_maps_to_busy_pet);
  RUN_TEST(test_low_battery_biases_pet_to_tired);
  RUN_TEST(test_usage_page_keeps_usage_values_and_marks_page);
  RUN_TEST(test_searching_detail_gets_own_screen_label);
  RUN_TEST(test_openclaw_source_gets_own_label);
  RUN_TEST(test_dynamic_usage_labels_flow_into_display_state);
  RUN_TEST(test_pinned_focus_is_rendered_in_display_state);
  RUN_TEST(test_invalid_pinned_focus_falls_back_to_auto_label);
  RUN_TEST(test_wifi_connected_bridge_offline_gets_clear_footer);
  RUN_TEST(test_wifi_reconnecting_gets_distinct_status_from_bridge_offline);
  return UNITY_END();
}
