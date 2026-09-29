#include <unity.h>

#include "pet_state_machine.h"

void setUp(void) {}

void tearDown(void) {}

void test_thinking_agent_gets_dedicated_pet_mode(void) {
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

  TEST_ASSERT_NOT_EQUAL(static_cast<int>(PetMode::Busy), static_cast<int>(view.petMode));
  TEST_ASSERT_EQUAL_STRING("THINKING", view.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("10:24:30", view.sidebarTime.c_str());
  TEST_ASSERT_EQUAL_STRING("2026/06/08", view.sidebarDate.c_str());
  TEST_ASSERT_EQUAL_STRING("23.5C 58%", view.sidebarClimate.c_str());
  TEST_ASSERT_EQUAL_STRING("CODEX THNK", view.sidebarAgent.c_str());
  TEST_ASSERT_EQUAL_STRING("thinking...", view.buddyBubble.c_str());
  TEST_ASSERT_EQUAL_STRING("THINKING  Sync bridge", view.footerMessage.c_str());
}

void test_running_details_drive_distinct_pet_modes(void) {
  AgentState agent{};
  agent.source = SourceKind::Codex;
  agent.status = AgentStatus::Running;
  agent.task = "Trace live status";
  agent.connected = true;
  const PowerState power{3800, 60, false, false, true};
  const EnvironmentState environment{};

  agent.statusDetail = "thinking";
  const DisplayState thinking = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  agent.statusDetail = "searching";
  const DisplayState searching = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  agent.statusDetail = "tool-use";
  const DisplayState toolUse = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  agent.statusDetail = "working";
  const DisplayState working = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  agent.statusDetail = "almost-done";
  const DisplayState almostDone = deriveDisplayState(agent, power, environment, ScreenPage::Overview);

  TEST_ASSERT_NOT_EQUAL(static_cast<int>(thinking.petMode), static_cast<int>(searching.petMode));
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(searching.petMode), static_cast<int>(toolUse.petMode));
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(toolUse.petMode), static_cast<int>(working.petMode));
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(working.petMode), static_cast<int>(almostDone.petMode));
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


void test_clock_view_carries_time_settings_and_agents(void) {
  AgentState agent{};
  agent.source = SourceKind::Codex;
  agent.status = AgentStatus::Running;
  agent.statusDetail = "tool-use";
  agent.task = "build firmware";
  agent.connected = true;
  agent.focusCount = 3;
  agent.agentSlots[0] = AgentSlotSummary{"a", SourceKind::Hermes, AgentStatus::NeedsAttention, "", "", true};
  agent.agentSlots[1] = AgentSlotSummary{"b", SourceKind::Codex, AgentStatus::Running, "", "", true};
  agent.agentSlots[2] = AgentSlotSummary{"c", SourceKind::OpenClaw, AgentStatus::Idle, "", "", false};
  const PowerState power{3900, 77, false, false, true};
  const EnvironmentState environment{true, 2026, 9, 28, 14, 5, 9, 1, 21.0f, 40.0f, true};
  DisplaySettings settings{};
  settings.clockStyle = ClockStyle::Words;
  settings.hour12 = true;
  settings.showSeconds = false;

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Clock, NetworkState{}, settings);

  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Clock), static_cast<int>(view.page));
  TEST_ASSERT_EQUAL(static_cast<int>(ClockStyle::Words), static_cast<int>(view.clock.style));
  TEST_ASSERT_TRUE(view.clock.hour12);
  TEST_ASSERT_FALSE(view.clock.showSeconds);
  TEST_ASSERT_TRUE(view.clock.timeValid);
  TEST_ASSERT_EQUAL(14, view.clock.hour);
  TEST_ASSERT_EQUAL(1, view.clock.weekday);
  TEST_ASSERT_EQUAL(77, view.clock.batteryPercent);
  TEST_ASSERT_TRUE(view.clock.agentActive);
  TEST_ASSERT_FALSE(view.clock.agentAttention);
  TEST_ASSERT_EQUAL_STRING("TOOL USE", view.clock.agentDetail.c_str());
  // Slot c is not present, so only two lines; attention is carried per line.
  TEST_ASSERT_EQUAL(2, view.clock.agentCount);
  TEST_ASSERT_EQUAL_STRING("HERMES", view.clock.agents[0].source.c_str());
  TEST_ASSERT_TRUE(view.clock.agents[0].attention);
  TEST_ASSERT_EQUAL_STRING("CODEX", view.clock.agents[1].source.c_str());
  TEST_ASSERT_TRUE(view.clock.agents[1].active);
}

void test_clock_pet_sleeps_at_night_when_nothing_runs(void) {
  AgentState agent{};
  agent.source = SourceKind::Codex;
  agent.status = AgentStatus::Idle;
  agent.connected = true;
  const PowerState power{3900, 77, false, false, true};
  EnvironmentState environment{true, 2026, 9, 28, 2, 30, 0, 1, 21.0f, 40.0f, true};

  DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Clock);
  TEST_ASSERT_EQUAL(static_cast<int>(PetMode::Sleep), static_cast<int>(view.clock.petMode));
  TEST_ASSERT_EQUAL_STRING("zzz...", view.clock.petBubble.c_str());

  environment.hour = 9;
  view = deriveDisplayState(agent, power, environment, ScreenPage::Clock);
  TEST_ASSERT_EQUAL_STRING("good morning!", view.clock.petBubble.c_str());

  agent.status = AgentStatus::Running;
  agent.statusDetail = "thinking";
  environment.hour = 2;
  view = deriveDisplayState(agent, power, environment, ScreenPage::Clock);
  TEST_ASSERT_EQUAL(static_cast<int>(PetMode::Thinking), static_cast<int>(view.clock.petMode));
  TEST_ASSERT_EQUAL_STRING("thinking...", view.clock.petBubble.c_str());
}

void test_clock_view_without_bridge_or_time(void) {
  AgentState agent{};
  const PowerState power{3900, 77, false, false, true};
  const EnvironmentState environment{};
  NetworkState network{};
  network.wifiKnown = true;
  network.wifiConnected = true;
  network.rssi = -58;

  const DisplayState view = deriveDisplayState(agent, power, environment, ScreenPage::Clock, network);
  TEST_ASSERT_FALSE(view.clock.timeValid);
  TEST_ASSERT_FALSE(view.clock.agentConnected);
  TEST_ASSERT_EQUAL(0, view.clock.agentCount);
  TEST_ASSERT_EQUAL_STRING("BRIDGE OFF", view.clock.bridgeLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("WIFI -58", view.clock.linkLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("zzz", view.clock.petBubble.c_str());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_thinking_agent_gets_dedicated_pet_mode);
  RUN_TEST(test_running_details_drive_distinct_pet_modes);
  RUN_TEST(test_low_battery_biases_pet_to_tired);
  RUN_TEST(test_usage_page_keeps_usage_values_and_marks_page);
  RUN_TEST(test_searching_detail_gets_own_screen_label);
  RUN_TEST(test_openclaw_source_gets_own_label);
  RUN_TEST(test_dynamic_usage_labels_flow_into_display_state);
  RUN_TEST(test_pinned_focus_is_rendered_in_display_state);
  RUN_TEST(test_invalid_pinned_focus_falls_back_to_auto_label);
  RUN_TEST(test_wifi_connected_bridge_offline_gets_clear_footer);
  RUN_TEST(test_wifi_reconnecting_gets_distinct_status_from_bridge_offline);
  RUN_TEST(test_clock_view_carries_time_settings_and_agents);
  RUN_TEST(test_clock_pet_sleeps_at_night_when_nothing_runs);
  RUN_TEST(test_clock_view_without_bridge_or_time);
  return UNITY_END();
}
