#include <unity.h>

#include "button_actions.h"

void setUp(void) {}

void tearDown(void) {}

void test_boot_short_press_triggers_page_on_release(void) {
  ButtonGestureState state{};

  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGesture(state,
                                                         true,
                                                         1000,
                                                         250,
                                                         800,
                                                         ButtonUiAction::Page,
                                                         ButtonUiAction::AgentFocus)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::Page),
                    static_cast<int>(updateButtonGesture(state,
                                                         false,
                                                         1320,
                                                         250,
                                                         800,
                                                         ButtonUiAction::Page,
                                                         ButtonUiAction::AgentFocus)));
}

void test_fast_boot_tap_still_triggers_page_on_release(void) {
  ButtonGestureState state{};
  updateButtonGesture(state, true, 1000, 250, 1500, ButtonUiAction::Page, ButtonUiAction::AgentFocus);

  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::Page),
                    static_cast<int>(updateButtonGesture(state,
                                                         false,
                                                         1080,
                                                         250,
                                                         1500,
                                                         ButtonUiAction::Page,
                                                         ButtonUiAction::AgentFocus)));
}

void test_boot_long_press_triggers_agent_focus_without_page_on_release(void) {
  ButtonGestureState state{};
  updateButtonGesture(state, true, 1000, 250, 1500, ButtonUiAction::Page, ButtonUiAction::AgentFocus);

  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::AgentFocus),
                    static_cast<int>(updateButtonGesture(state,
                                                         true,
                                                         2510,
                                                         250,
                                                         1500,
                                                         ButtonUiAction::Page,
                                                         ButtonUiAction::AgentFocus)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGesture(state,
                                                         false,
                                                         2600,
                                                         250,
                                                         1500,
                                                         ButtonUiAction::Page,
                                                         ButtonUiAction::AgentFocus)));
}

void test_key_short_press_triggers_agent_focus(void) {
  ButtonGestureState state{};
  updateButtonGesture(state, true, 2000, 250, 1500, ButtonUiAction::AgentFocus, ButtonUiAction::AgentFocus);

  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::AgentFocus),
                    static_cast<int>(updateButtonGesture(state,
                                                         false,
                                                         2080,
                                                         250,
                                                         1500,
                                                         ButtonUiAction::AgentFocus,
                                                         ButtonUiAction::AgentFocus)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_short_press_triggers_page_on_release);
  RUN_TEST(test_fast_boot_tap_still_triggers_page_on_release);
  RUN_TEST(test_boot_long_press_triggers_agent_focus_without_page_on_release);
  RUN_TEST(test_key_short_press_triggers_agent_focus);
  return UNITY_END();
}
