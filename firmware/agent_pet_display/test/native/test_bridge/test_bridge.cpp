#include <unity.h>

#include "bridge_client.h"

void setUp(void) {}

void tearDown(void) {}

void test_parse_running_codex_payload(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":1,\"current_status\":\"working\","
      "\"usage\":{\"today\":\"812k tok\",\"context\":\"189k / 258k\",\"quota\":\"5H 88% WK 92%\"},"
      "\"notification\":{\"id\":\"n1\",\"source\":\"laptop-codex\","
      "\"task\":\"syncing bridge\",\"status\":\"running\","
      "\"priority\":2,\"title\":\"Agent update\","
      "\"message\":\"Codex is working\",\"project\":\"pet-bridge\","
      "\"time\":\"2026-06-07T10:00:00Z\"}}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::Codex), static_cast<int>(state.source));
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("working", state.statusDetail.c_str());
  TEST_ASSERT_TRUE(state.connected);
  TEST_ASSERT_EQUAL_STRING("syncing bridge", state.task.c_str());
  TEST_ASSERT_EQUAL_STRING("2026-06-07T10:00:00Z", state.updatedAt.c_str());
  TEST_ASSERT_EQUAL_STRING("812k tok", state.usageToday.c_str());
  TEST_ASSERT_EQUAL_STRING("189k / 258k", state.usageContext.c_str());
  TEST_ASSERT_EQUAL_STRING("5H 88% WK 92%", state.usageQuota.c_str());
}

void test_unknown_fields_fall_back_safely(void) {
  const char* json = "{\"source\":\"other\",\"status\":\"weird\"}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::Unknown), static_cast<int>(state.source));
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Idle), static_cast<int>(state.status));
}

void test_notification_status_falls_back_when_current_status_missing(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,"
      "\"notification\":{\"source\":\"codex\",\"status\":\"needs-attention\","
      "\"task\":\"review output\"}}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::NeedsAttention), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("needs-attention", state.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("review output", state.task.c_str());
}

void test_current_status_fields_win_over_stale_notification(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":1,\"source\":\"codex\",\"task\":\"sync active file\","
      "\"updated_at\":\"2026-06-08T15:00:00Z\",\"current_status\":\"thinking\","
      "\"notification\":{\"source\":\"codex\",\"status\":\"needs-attention\","
      "\"task\":\"approve shell command\"}}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::Codex), static_cast<int>(state.source));
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("thinking", state.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("sync active file", state.task.c_str());
  TEST_ASSERT_EQUAL_STRING("2026-06-08T15:00:00Z", state.updatedAt.c_str());
}

void test_notification_message_falls_back_when_task_missing(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":3,\"current_status\":\"thinking\","
      "\"notification\":{\"source\":\"claude-code\",\"status\":\"event\","
      "\"message\":\"Planning next patch\",\"time\":\"2026-06-07T11:30:00Z\"}}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::ClaudeCode), static_cast<int>(state.source));
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("thinking", state.statusDetail.c_str());
  TEST_ASSERT_EQUAL_STRING("Planning next patch", state.task.c_str());
  TEST_ASSERT_EQUAL_STRING("2026-06-07T11:30:00Z", state.updatedAt.c_str());
}

void test_near_complete_status_stays_busy(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"current_status\":\"near-complete\","
      "\"notification\":{\"source\":\"codex\",\"task\":\"wrapping up\"}}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
}

void test_compact_status_without_notification_still_maps_status(void) {
  const char* json = "{\"ok\":true,\"unread_count\":0,\"current_status\":\"running\",\"notification\":null}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("", state.task.c_str());
}

void test_searching_status_keeps_detail_label(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"source\":\"codex\",\"task\":\"research docs\","
      "\"updated_at\":\"2026-06-08T15:15:00Z\",\"current_status\":\"searching\"}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("searching", state.statusDetail.c_str());
}

void test_tool_use_status_keeps_detail_label(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"source\":\"codex\",\"task\":\"apply patch\","
      "\"updated_at\":\"2026-06-08T15:16:00Z\",\"current_status\":\"tool-use\"}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("tool-use", state.statusDetail.c_str());
}

void test_blocked_status_stays_attention_with_matching_detail(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"source\":\"hermes\",\"task\":\"review output\","
      "\"updated_at\":\"2026-06-09T15:16:00Z\",\"current_status\":\"blocked\"}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::NeedsAttention), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("needs-attention", state.statusDetail.c_str());
}

void test_tool_calling_status_maps_to_running_working(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"source\":\"codex\",\"task\":\"call tool\","
      "\"updated_at\":\"2026-06-09T15:17:00Z\",\"current_status\":\"tool-calling\"}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("working", state.statusDetail.c_str());
}

void test_awaiting_tool_status_maps_to_running_working(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"source\":\"codex\",\"task\":\"wait tool\","
      "\"updated_at\":\"2026-06-09T15:18:00Z\",\"current_status\":\"awaiting_tool\"}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("working", state.statusDetail.c_str());
}

void test_legacy_direct_event_payload_still_parses(void) {
  const char* json =
      "{\"source\":\"hermes\",\"status\":\"completed\",\"task\":\"final review\","
      "\"updated_at\":\"2026-06-07T10:00:00Z\"}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::Hermes), static_cast<int>(state.source));
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Completed), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("final review", state.task.c_str());
}

void test_openclaw_payload_maps_to_openclaw_source(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"source\":\"openclaw\",\"task\":\"watch hooks\","
      "\"updated_at\":\"2026-06-09T09:30:00Z\",\"current_status\":\"tool-use\"}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::OpenClaw), static_cast<int>(state.source));
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::Running), static_cast<int>(state.status));
  TEST_ASSERT_EQUAL_STRING("tool-use", state.statusDetail.c_str());
}

void test_non_ascii_task_falls_back_to_source_session_label(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"source\":\"hermes\",\"task\":\"你好\","
      "\"updated_at\":\"2026-06-10T09:56:16Z\",\"current_status\":\"completed\","
      "\"agents\":["
      "{\"id\":\"slot_a\",\"source\":\"codex\",\"task\":\"mac-codex-runtime\",\"status\":\"running\"},"
      "{\"id\":\"slot_b\",\"source\":\"hermes\",\"task\":\"你好\",\"status\":\"completed\"}"
      "]}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("HERMES SESSION", state.task.c_str());
  TEST_ASSERT_EQUAL_STRING("mac-codex-runtime", state.agentSlots[0].task.c_str());
  TEST_ASSERT_EQUAL_STRING("HERMES SESSION", state.agentSlots[1].task.c_str());
}

void test_generic_usage_metadata_is_parsed_for_non_codex_agents(void) {
  const char* json =
      "{\"ok\":true,\"unread_count\":0,\"source\":\"hermes\",\"task\":\"pet review\","
      "\"updated_at\":\"2026-06-09T09:30:00Z\",\"current_status\":\"thinking\","
      "\"usage\":{\"today\":\"1m 05s\",\"context\":\"1 call\",\"quota\":\"CLEAR\","
      "\"today_label\":\"SESS\",\"today_hint\":\"Elapsed active time\","
      "\"context_label\":\"TOOLS\",\"context_hint\":\"Tool starts in this session\","
      "\"quota_label\":\"ATTN\",\"quota_hint\":\"User approval / attention state\","
      "\"quota_style\":\"text\"}}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("SESS", state.usageTodayLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("1m 05s", state.usageToday.c_str());
  TEST_ASSERT_EQUAL_STRING("Elapsed active time", state.usageTodayHint.c_str());
  TEST_ASSERT_EQUAL_STRING("TOOLS", state.usageContextLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("1 call", state.usageContext.c_str());
  TEST_ASSERT_EQUAL_STRING("ATTN", state.usageQuotaLabel.c_str());
  TEST_ASSERT_EQUAL_STRING("CLEAR", state.usageQuota.c_str());
  TEST_ASSERT_EQUAL_STRING("text", state.usageQuotaStyle.c_str());
}

void test_parse_focus_metadata_and_agent_slots(void) {
  const char* json =
      "{\"ok\":true,\"source\":\"codex\",\"current_status\":\"thinking\","
      "\"focus_mode\":\"pinned\",\"focus_id\":\"slot_codex_b\",\"focus_index\":1,\"focus_count\":8,"
      "\"agents\":["
      "{\"id\":\"slot_codex_a\",\"source\":\"codex\",\"task\":\"task a\",\"status\":\"thinking\",\"updated_at\":\"2026-06-09T10:00:00Z\"},"
      "{\"id\":\"slot_codex_b\",\"source\":\"hermes\",\"task\":\"task b\",\"status\":\"needs-attention\",\"updated_at\":\"2026-06-09T10:00:05Z\"},"
      "{\"id\":\"slot_codex_c\",\"source\":\"openclaw\",\"task\":\"task c\",\"status\":\"completed\",\"updated_at\":\"2026-06-09T10:00:10Z\"},"
      "{\"id\":\"slot_codex_d\",\"source\":\"claude-code\",\"task\":\"task d\",\"status\":\"error\",\"updated_at\":\"2026-06-09T10:00:15Z\"},"
      "{\"id\":\"slot_codex_e\",\"source\":\"codex\",\"task\":\"task e\",\"status\":\"running\",\"updated_at\":\"2026-06-09T10:00:20Z\"},"
      "{\"id\":\"slot_codex_f\",\"source\":\"hermes\",\"task\":\"task f\",\"status\":\"started\",\"updated_at\":\"2026-06-09T10:00:25Z\"},"
      "{\"id\":\"slot_codex_g\",\"source\":\"codex\",\"task\":\"task g\",\"status\":\"searching\",\"updated_at\":\"2026-06-09T10:00:30Z\"},"
      "{\"id\":\"slot_codex_h\",\"source\":\"hermes\",\"task\":\"task h\",\"status\":\"tool-use\",\"updated_at\":\"2026-06-09T10:00:35Z\"}"
      "]}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(FocusMode::Pinned), static_cast<int>(state.focusMode));
  TEST_ASSERT_EQUAL_STRING("slot_codex_b", state.focusId.c_str());
  TEST_ASSERT_EQUAL(1, state.focusIndex);
  TEST_ASSERT_EQUAL(kMaxAgentSlots, state.focusCount);
  TEST_ASSERT_TRUE(state.agentSlots[0].present);
  TEST_ASSERT_EQUAL_STRING("slot_codex_a", state.agentSlots[0].id.c_str());
  TEST_ASSERT_EQUAL_STRING("task b", state.agentSlots[1].task.c_str());
  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::Hermes), static_cast<int>(state.agentSlots[1].source));
  TEST_ASSERT_EQUAL(static_cast<int>(AgentStatus::NeedsAttention),
                    static_cast<int>(state.agentSlots[1].status));
  TEST_ASSERT_TRUE(state.agentSlots[kMaxAgentSlots - 1].present);
  TEST_ASSERT_EQUAL_STRING("slot_codex_f", state.agentSlots[kMaxAgentSlots - 1].id.c_str());
}

void test_out_of_window_pinned_focus_falls_back_to_auto_metadata(void) {
  const char* json =
      "{\"ok\":true,\"source\":\"codex\",\"current_status\":\"thinking\","
      "\"focus_mode\":\"pinned\",\"focus_id\":\"slot_codex_h\",\"focus_index\":7,\"focus_count\":8,"
      "\"agents\":["
      "{\"id\":\"slot_codex_a\",\"source\":\"codex\",\"task\":\"task a\",\"status\":\"thinking\",\"updated_at\":\"2026-06-09T10:00:00Z\"},"
      "{\"id\":\"slot_codex_b\",\"source\":\"hermes\",\"task\":\"task b\",\"status\":\"needs-attention\",\"updated_at\":\"2026-06-09T10:00:05Z\"},"
      "{\"id\":\"slot_codex_c\",\"source\":\"openclaw\",\"task\":\"task c\",\"status\":\"completed\",\"updated_at\":\"2026-06-09T10:00:10Z\"},"
      "{\"id\":\"slot_codex_d\",\"source\":\"claude-code\",\"task\":\"task d\",\"status\":\"error\",\"updated_at\":\"2026-06-09T10:00:15Z\"},"
      "{\"id\":\"slot_codex_e\",\"source\":\"codex\",\"task\":\"task e\",\"status\":\"running\",\"updated_at\":\"2026-06-09T10:00:20Z\"},"
      "{\"id\":\"slot_codex_f\",\"source\":\"hermes\",\"task\":\"task f\",\"status\":\"started\",\"updated_at\":\"2026-06-09T10:00:25Z\"},"
      "{\"id\":\"slot_codex_g\",\"source\":\"codex\",\"task\":\"task g\",\"status\":\"searching\",\"updated_at\":\"2026-06-09T10:00:30Z\"},"
      "{\"id\":\"slot_codex_h\",\"source\":\"hermes\",\"task\":\"task h\",\"status\":\"tool-use\",\"updated_at\":\"2026-06-09T10:00:35Z\"}"
      "]}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(FocusMode::Auto), static_cast<int>(state.focusMode));
  TEST_ASSERT_EQUAL_STRING("", state.focusId.c_str());
  TEST_ASSERT_EQUAL(-1, state.focusIndex);
  TEST_ASSERT_EQUAL(kMaxAgentSlots, state.focusCount);
}

void test_negative_pinned_focus_index_falls_back_to_auto_metadata(void) {
  const char* json =
      "{\"ok\":true,\"source\":\"codex\",\"current_status\":\"thinking\","
      "\"focus_mode\":\"pinned\",\"focus_id\":\"slot_codex_a\",\"focus_index\":-2,\"focus_count\":1,"
      "\"agents\":["
      "{\"id\":\"slot_codex_a\",\"source\":\"codex\",\"task\":\"task a\",\"status\":\"thinking\",\"updated_at\":\"2026-06-09T10:00:00Z\"}"
      "]}";
  AgentState state{};

  const bool ok = parseAgentStatePayload(json, state);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL(static_cast<int>(FocusMode::Auto), static_cast<int>(state.focusMode));
  TEST_ASSERT_EQUAL_STRING("", state.focusId.c_str());
  TEST_ASSERT_EQUAL(-1, state.focusIndex);
  TEST_ASSERT_EQUAL(1, state.focusCount);
}

void test_invalid_json_returns_false(void) {
  AgentState state{};

  const bool ok = parseAgentStatePayload("{\"ok\":true", state);

  TEST_ASSERT_FALSE(ok);
}


void test_poll_payload_carries_display_tz_and_egg_commands(void) {
  const char* json =
      "{\"ok\":true,\"source\":\"mac-codex\",\"current_status\":\"thinking\",\"task\":\"t\","
      "\"updated_at\":\"2026-09-28T09:00:00Z\",\"server_time_ms\":1790588537123,"
      "\"display\":{\"rev\":7,\"clock_style\":\"segment\",\"hour12\":true,\"show_seconds\":false,\"page\":\"clock\"},"
      "\"time\":{\"tz\":\"GMT0BST,M3.5.0/1,M10.5.0\",\"zone\":\"Europe/London\"},"
      "\"egg\":{\"rev\":3,\"id\":\"demo\",\"frames\":120,\"fps_x100\":2000,\"width\":400,\"height\":300,"
      "\"start_at_ms\":1790588541000}}";
  AgentState state{};
  BridgeCommands commands{};
  TEST_ASSERT_TRUE(parsePollPayload(json, state, commands));
  TEST_ASSERT_EQUAL(static_cast<int>(SourceKind::Codex), static_cast<int>(state.source));
  TEST_ASSERT_TRUE(commands.serverTimeMs == 1790588537123LL);
  TEST_ASSERT_TRUE(commands.display.present);
  TEST_ASSERT_EQUAL_UINT32(7, commands.display.rev);
  TEST_ASSERT_TRUE(commands.display.hasStyle);
  TEST_ASSERT_EQUAL(static_cast<int>(ClockStyle::Segment), static_cast<int>(commands.display.style));
  TEST_ASSERT_TRUE(commands.display.hasHour12);
  TEST_ASSERT_TRUE(commands.display.hour12);
  TEST_ASSERT_TRUE(commands.display.hasShowSeconds);
  TEST_ASSERT_FALSE(commands.display.showSeconds);
  TEST_ASSERT_TRUE(commands.display.hasPage);
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Clock), static_cast<int>(commands.display.page));
  TEST_ASSERT_TRUE(commands.hasTz);
  TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", commands.tz.c_str());
  TEST_ASSERT_TRUE(commands.egg.present);
  TEST_ASSERT_EQUAL_STRING("demo", commands.egg.id.c_str());
  TEST_ASSERT_EQUAL_UINT32(120, commands.egg.frames);
  TEST_ASSERT_EQUAL_UINT16(2000, commands.egg.fpsX100);
  TEST_ASSERT_TRUE(commands.egg.startAtMs == 1790588541000LL);
}

void test_poll_payload_rejects_unsafe_egg_and_unknown_style(void) {
  const char* json =
      "{\"ok\":true,\"current_status\":\"idle\","
      "\"display\":{\"rev\":1,\"clock_style\":\"comic\",\"page\":\"settings\"},"
      "\"egg\":{\"rev\":2,\"id\":\"big\",\"frames\":10,\"fps_x100\":2000,\"width\":1920,\"height\":1080}}";
  AgentState state{};
  BridgeCommands commands{};
  TEST_ASSERT_TRUE(parsePollPayload(json, state, commands));
  TEST_ASSERT_TRUE(commands.display.present);
  TEST_ASSERT_FALSE(commands.display.hasStyle);
  TEST_ASSERT_FALSE(commands.display.hasPage);
  TEST_ASSERT_FALSE(commands.display.hasHour12);
  TEST_ASSERT_FALSE(commands.egg.present);
  TEST_ASSERT_FALSE(commands.hasTz);
  TEST_ASSERT_TRUE(commands.serverTimeMs == 0);
}

void test_legacy_payload_has_no_commands(void) {
  const char* json = "{\"ok\":true,\"current_status\":\"completed\",\"source\":\"hermes\"}";
  AgentState state{};
  BridgeCommands commands{};
  commands.hasTz = true;
  TEST_ASSERT_TRUE(parsePollPayload(json, state, commands));
  TEST_ASSERT_FALSE(commands.display.present);
  TEST_ASSERT_FALSE(commands.egg.present);
  TEST_ASSERT_FALSE(commands.hasTz);
}

void test_telemetry_query_is_compact_and_url_safe(void) {
  DeviceTelemetry t{};
  t.firmware = "1.3.0 beta";
  t.batteryValid = true;
  t.batteryPercent = 84;
  t.batteryMv = 3980;
  t.climateValid = true;
  t.temperatureC = 22.64f;
  t.humidityPct = 48.2f;
  t.rssi = -52;
  t.uptimeS = 3600;
  t.page = ScreenPage::Clock;
  t.clockStyle = ClockStyle::Words;
  t.settingsRev = 7;
  t.eggRev = 3;
  t.timeValid = true;
  const std::string query = buildTelemetryQuery(t);
  TEST_ASSERT_EQUAL_STRING(
      "&fw=1.3.0%20beta&bat=84&mv=3980&temp=22.6&hum=48&rssi=-52&up=3600&page=clock&style=words&srev=7&erev=3&clk=1",
      query.c_str());

  DeviceTelemetry empty{};
  TEST_ASSERT_EQUAL_STRING("&fw=&up=0&page=overview&style=sans&srev=0&erev=0&clk=0", buildTelemetryQuery(empty).c_str());
}

void test_page_names_round_trip(void) {
  ScreenPage page = ScreenPage::Overview;
  TEST_ASSERT_TRUE(screenPageFromName(screenPageName(ScreenPage::Usage), page));
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Usage), static_cast<int>(page));
  TEST_ASSERT_TRUE(screenPageFromName("clock", page));
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Clock), static_cast<int>(page));
  TEST_ASSERT_FALSE(screenPageFromName("secret", page));
  TEST_ASSERT_FALSE(screenPageFromName(nullptr, page));
}


void test_anim_chunk_url_keeps_token_and_encodes_id(void) {
  const std::string url = buildAnimChunkUrl("http://192.168.1.23:17366/esp32/poll?token=abc123", "bad apple", 120, 60,
                                            16384);
  TEST_ASSERT_EQUAL_STRING(
      "http://192.168.1.23:17366/esp32/anim/bad%20apple/frames?token=abc123&start=120&count=60&max_bytes=16384",
      url.c_str());
  TEST_ASSERT_EQUAL_STRING("http://h:1/esp32/anim/demo/frames?start=0&count=1&max_bytes=10",
                           buildAnimChunkUrl("http://h:1/esp32/poll", "demo", 0, 1, 10).c_str());
  TEST_ASSERT_EQUAL_STRING("", buildAnimChunkUrl("http://h:1/other", "demo", 0, 1, 10).c_str());
}

void test_redact_url_token(void) {
  TEST_ASSERT_EQUAL_STRING("http://h/esp32/poll?token=***",
                           redactUrlToken("http://h/esp32/poll?token=s3cr3t").c_str());
  TEST_ASSERT_EQUAL_STRING("http://h/p?focus=auto&token=***&x=1",
                           redactUrlToken("http://h/p?focus=auto&token=abc&x=1").c_str());
  TEST_ASSERT_EQUAL_STRING("http://h/p", redactUrlToken("http://h/p").c_str());
}

void test_egg_request_is_reported_only_when_set(void) {
  DeviceTelemetry t{};
  TEST_ASSERT_TRUE(buildTelemetryQuery(t).find("eggreq") == std::string::npos);
  t.eggRequest = 3;
  TEST_ASSERT_TRUE(buildTelemetryQuery(t).find("&eggreq=3") != std::string::npos);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_running_codex_payload);
  RUN_TEST(test_unknown_fields_fall_back_safely);
  RUN_TEST(test_notification_status_falls_back_when_current_status_missing);
  RUN_TEST(test_current_status_fields_win_over_stale_notification);
  RUN_TEST(test_notification_message_falls_back_when_task_missing);
  RUN_TEST(test_near_complete_status_stays_busy);
  RUN_TEST(test_compact_status_without_notification_still_maps_status);
  RUN_TEST(test_searching_status_keeps_detail_label);
  RUN_TEST(test_tool_use_status_keeps_detail_label);
  RUN_TEST(test_blocked_status_stays_attention_with_matching_detail);
  RUN_TEST(test_tool_calling_status_maps_to_running_working);
  RUN_TEST(test_awaiting_tool_status_maps_to_running_working);
  RUN_TEST(test_legacy_direct_event_payload_still_parses);
  RUN_TEST(test_openclaw_payload_maps_to_openclaw_source);
  RUN_TEST(test_non_ascii_task_falls_back_to_source_session_label);
  RUN_TEST(test_generic_usage_metadata_is_parsed_for_non_codex_agents);
  RUN_TEST(test_parse_focus_metadata_and_agent_slots);
  RUN_TEST(test_out_of_window_pinned_focus_falls_back_to_auto_metadata);
  RUN_TEST(test_negative_pinned_focus_index_falls_back_to_auto_metadata);
  RUN_TEST(test_invalid_json_returns_false);
  RUN_TEST(test_poll_payload_carries_display_tz_and_egg_commands);
  RUN_TEST(test_poll_payload_rejects_unsafe_egg_and_unknown_style);
  RUN_TEST(test_legacy_payload_has_no_commands);
  RUN_TEST(test_telemetry_query_is_compact_and_url_safe);
  RUN_TEST(test_page_names_round_trip);
  RUN_TEST(test_anim_chunk_url_keeps_token_and_encodes_id);
  RUN_TEST(test_redact_url_token);
  RUN_TEST(test_egg_request_is_reported_only_when_set);
  return UNITY_END();
}
