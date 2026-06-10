#include <unity.h>

#include "focus_controller.h"

void setUp(void) {}

void tearDown(void) {}

void test_single_press_moves_from_auto_to_first_slot(void) {
  FocusRequestState state{};
  AgentState agent{};
  agent.focusCount = 2;
  agent.agentSlots[0].id = "slot_a";
  agent.agentSlots[0].present = true;
  agent.agentSlots[1].id = "slot_b";
  agent.agentSlots[1].present = true;

  advanceFocusSelection(state, agent, 1000, 45000);

  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Pinned), static_cast<int>(state.mode));
  TEST_ASSERT_EQUAL_STRING("slot_a", state.focusId.c_str());
  TEST_ASSERT_EQUAL(0, state.visibleIndex);
  TEST_ASSERT_EQUAL(2, state.visibleCount);
  TEST_ASSERT_EQUAL(46000, state.expiresAtMs);
}

void test_expired_pinned_focus_returns_to_auto(void) {
  FocusRequestState state{};
  state.mode = LocalFocusMode::Pinned;
  state.focusId = "slot_a";
  state.visibleIndex = 0;
  state.visibleCount = 2;
  state.expiresAtMs = 1000;

  expireFocusIfNeeded(state, 1001);

  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Auto), static_cast<int>(state.mode));
  TEST_ASSERT_EQUAL_STRING("", state.focusId.c_str());
  TEST_ASSERT_EQUAL(-1, state.visibleIndex);
  TEST_ASSERT_EQUAL(2, state.visibleCount);
}

void test_expiry_handles_millis_wrap_without_immediate_reset(void) {
  FocusRequestState state{};
  AgentState agent{};
  agent.focusCount = 1;
  agent.agentSlots[0].id = "slot_a";
  agent.agentSlots[0].present = true;
  const uint32_t nearWrap = 0xFFFFFFF0u;

  advanceFocusSelection(state, agent, nearWrap, 45000);
  expireFocusIfNeeded(state, nearWrap + 10u);

  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Pinned), static_cast<int>(state.mode));
  TEST_ASSERT_EQUAL_STRING("slot_a", state.focusId.c_str());

  expireFocusIfNeeded(state, nearWrap + 45001u);

  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Auto), static_cast<int>(state.mode));
  TEST_ASSERT_EQUAL_STRING("", state.focusId.c_str());
  TEST_ASSERT_EQUAL(-1, state.visibleIndex);
}

void test_expiry_allows_zero_deadline_after_wrap(void) {
  FocusRequestState state{};
  AgentState agent{};
  agent.focusCount = 1;
  agent.agentSlots[0].id = "slot_a";
  agent.agentSlots[0].present = true;
  const uint32_t nearZeroDeadline = 0u - 45000u;

  advanceFocusSelection(state, agent, nearZeroDeadline, 45000);
  TEST_ASSERT_EQUAL(0u, state.expiresAtMs);

  expireFocusIfNeeded(state, nearZeroDeadline + 10u);
  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Pinned), static_cast<int>(state.mode));

  expireFocusIfNeeded(state, nearZeroDeadline + 45001u);
  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Auto), static_cast<int>(state.mode));
  TEST_ASSERT_EQUAL_STRING("", state.focusId.c_str());
  TEST_ASSERT_EQUAL(-1, state.visibleIndex);
}

void test_pressing_past_last_slot_cycles_back_to_auto(void) {
  FocusRequestState state{};
  AgentState agent{};
  agent.focusCount = 2;
  agent.agentSlots[0].id = "slot_a";
  agent.agentSlots[0].present = true;
  agent.agentSlots[1].id = "slot_b";
  agent.agentSlots[1].present = true;

  advanceFocusSelection(state, agent, 1000, 45000);
  advanceFocusSelection(state, agent, 2000, 45000);
  advanceFocusSelection(state, agent, 3000, 45000);

  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Auto), static_cast<int>(state.mode));
  TEST_ASSERT_EQUAL_STRING("", state.focusId.c_str());
  TEST_ASSERT_EQUAL(-1, state.visibleIndex);
  TEST_ASSERT_EQUAL(2, state.visibleCount);
}

void test_build_focused_poll_url_appends_focus_query_with_existing_token(void) {
  FocusRequestState state{};

  TEST_ASSERT_EQUAL_STRING(
      "http://bridge.local/esp32/poll?token=abc&focus=auto",
      buildFocusedPollUrl("http://bridge.local/esp32/poll?token=abc", state).c_str());

  state.mode = LocalFocusMode::Pinned;
  state.focusId = "slot_b";
  TEST_ASSERT_EQUAL_STRING(
      "http://bridge.local/esp32/poll?token=abc&focus=slot_b",
      buildFocusedPollUrl("http://bridge.local/esp32/poll?token=abc", state).c_str());

  state.focusId = "slot&other=1";
  TEST_ASSERT_EQUAL_STRING(
      "http://bridge.local/esp32/poll?token=abc&focus=slot%26other%3D1",
      buildFocusedPollUrl("http://bridge.local/esp32/poll?token=abc", state).c_str());
}

void test_sync_falls_back_to_auto_when_bridge_drops_requested_focus(void) {
  FocusRequestState state{};
  state.mode = LocalFocusMode::Pinned;
  state.focusId = "slot_b";
  state.visibleIndex = 1;
  state.visibleCount = 2;
  state.expiresAtMs = 50000;

  AgentState agent{};
  agent.focusMode = FocusMode::Auto;
  agent.focusCount = 1;
  agent.agentSlots[0].id = "slot_a";
  agent.agentSlots[0].present = true;

  syncFocusSelectionFromBridge(state, agent, 1000, 45000);

  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Auto), static_cast<int>(state.mode));
  TEST_ASSERT_EQUAL_STRING("", state.focusId.c_str());
  TEST_ASSERT_EQUAL(-1, state.visibleIndex);
  TEST_ASSERT_EQUAL(1, state.visibleCount);
}

void test_sync_updates_to_bridge_selected_slot_when_focus_stays_pinned(void) {
  FocusRequestState state{};
  state.mode = LocalFocusMode::Pinned;
  state.focusId = "stale";
  state.visibleIndex = 3;
  state.visibleCount = 4;
  state.expiresAtMs = 90000;

  AgentState agent{};
  agent.focusMode = FocusMode::Pinned;
  agent.focusId = "slot_b";
  agent.focusIndex = 1;
  agent.focusCount = 2;
  agent.agentSlots[0].id = "slot_a";
  agent.agentSlots[0].present = true;
  agent.agentSlots[1].id = "slot_b";
  agent.agentSlots[1].present = true;

  syncFocusSelectionFromBridge(state, agent, 1000, 45000);

  TEST_ASSERT_EQUAL(static_cast<int>(LocalFocusMode::Pinned), static_cast<int>(state.mode));
  TEST_ASSERT_EQUAL_STRING("slot_b", state.focusId.c_str());
  TEST_ASSERT_EQUAL(1, state.visibleIndex);
  TEST_ASSERT_EQUAL(2, state.visibleCount);
  TEST_ASSERT_EQUAL(90000, state.expiresAtMs);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_single_press_moves_from_auto_to_first_slot);
  RUN_TEST(test_expired_pinned_focus_returns_to_auto);
  RUN_TEST(test_expiry_handles_millis_wrap_without_immediate_reset);
  RUN_TEST(test_expiry_allows_zero_deadline_after_wrap);
  RUN_TEST(test_pressing_past_last_slot_cycles_back_to_auto);
  RUN_TEST(test_build_focused_poll_url_appends_focus_query_with_existing_token);
  RUN_TEST(test_sync_falls_back_to_auto_when_bridge_drops_requested_focus);
  RUN_TEST(test_sync_updates_to_bridge_selected_slot_when_focus_stays_pinned);
  return UNITY_END();
}
