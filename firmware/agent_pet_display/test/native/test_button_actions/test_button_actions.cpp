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


void test_very_long_press_fires_after_long_press_once(void) {
  ButtonGestureState state{};
  const ButtonUiAction s = ButtonUiAction::Page;
  const ButtonUiAction l = ButtonUiAction::ClockStyle;
  const ButtonUiAction v = ButtonUiAction::Egg;
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGestureEx(state, true, 0, 1500, 5000, s, l, v)));
  TEST_ASSERT_EQUAL(static_cast<int>(l), static_cast<int>(updateButtonGestureEx(state, true, 1500, 1500, 5000, s, l, v)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGestureEx(state, true, 4999, 1500, 5000, s, l, v)));
  TEST_ASSERT_EQUAL(static_cast<int>(v), static_cast<int>(updateButtonGestureEx(state, true, 5000, 1500, 5000, s, l, v)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGestureEx(state, true, 9000, 1500, 5000, s, l, v)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGestureEx(state, false, 9100, 1500, 5000, s, l, v)));
}

void test_chord_swallows_single_actions_and_fires_once(void) {
  ButtonGestureState boot{};
  ButtonGestureState key{};
  ChordGestureState chord{};
  // Boot goes down first, key joins 200 ms later.
  updateButtonGesture(boot, true, 0, 250, 1500, ButtonUiAction::Page, ButtonUiAction::AgentFocus);
  TEST_ASSERT_FALSE(updateChordGesture(chord, boot, key, true, false, 0, 2000));
  updateButtonGesture(boot, true, 200, 250, 1500, ButtonUiAction::Page, ButtonUiAction::AgentFocus);
  updateButtonGesture(key, true, 200, 250, 1500, ButtonUiAction::AgentFocus, ButtonUiAction::AgentFocus);
  TEST_ASSERT_FALSE(updateChordGesture(chord, boot, key, true, true, 200, 2000));
  TEST_ASSERT_TRUE(chord.active);
  // No long actions while the chord is held.
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGesture(boot, true, 1800, 250, 1500, ButtonUiAction::Page,
                                                         ButtonUiAction::AgentFocus)));
  TEST_ASSERT_FALSE(updateChordGesture(chord, boot, key, true, true, 1800, 2000));
  TEST_ASSERT_TRUE(updateChordGesture(chord, boot, key, true, true, 2200, 2000));
  TEST_ASSERT_FALSE(updateChordGesture(chord, boot, key, true, true, 3000, 2000));
  // Releasing does not produce stray page/focus actions.
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGesture(boot, false, 3100, 250, 1500, ButtonUiAction::Page,
                                                         ButtonUiAction::AgentFocus)));
  TEST_ASSERT_FALSE(updateChordGesture(chord, boot, key, false, true, 3100, 2000));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::None),
                    static_cast<int>(updateButtonGesture(key, false, 3200, 250, 1500, ButtonUiAction::AgentFocus,
                                                         ButtonUiAction::AgentFocus)));
  TEST_ASSERT_FALSE(updateChordGesture(chord, boot, key, false, false, 3200, 2000));
  TEST_ASSERT_FALSE(chord.active);
  // A normal tap afterwards works again.
  updateButtonGesture(boot, true, 4000, 250, 1500, ButtonUiAction::Page, ButtonUiAction::AgentFocus);
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::Page),
                    static_cast<int>(updateButtonGesture(boot, false, 4100, 250, 1500, ButtonUiAction::Page,
                                                         ButtonUiAction::AgentFocus)));
}

void test_clock_page_turns_agent_focus_into_style_change(void) {
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::ClockStyle),
                    static_cast<int>(contextualAction(ButtonUiAction::AgentFocus, ScreenPage::Clock)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::AgentFocus),
                    static_cast<int>(contextualAction(ButtonUiAction::AgentFocus, ScreenPage::Overview)));
  TEST_ASSERT_EQUAL(static_cast<int>(ButtonUiAction::Page),
                    static_cast<int>(contextualAction(ButtonUiAction::Page, ScreenPage::Clock)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_short_press_triggers_page_on_release);
  RUN_TEST(test_fast_boot_tap_still_triggers_page_on_release);
  RUN_TEST(test_boot_long_press_triggers_agent_focus_without_page_on_release);
  RUN_TEST(test_key_short_press_triggers_agent_focus);
  RUN_TEST(test_very_long_press_fires_after_long_press_once);
  RUN_TEST(test_chord_swallows_single_actions_and_fires_once);
  RUN_TEST(test_clock_page_turns_agent_focus_into_style_change);
  return UNITY_END();
}
