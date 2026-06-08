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

void test_invalid_json_returns_false(void) {
  AgentState state{};

  const bool ok = parseAgentStatePayload("{\"ok\":true", state);

  TEST_ASSERT_FALSE(ok);
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
  RUN_TEST(test_legacy_direct_event_payload_still_parses);
  RUN_TEST(test_invalid_json_returns_false);
  return UNITY_END();
}
