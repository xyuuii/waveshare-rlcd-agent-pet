#include <unity.h>

#include "device_settings.h"

void setUp(void) {}

void tearDown(void) {}

void test_new_revision_applies_only_present_fields(void) {
  DeviceSettingsState state{};
  state.display.hour12 = true;
  BridgeDisplayCommand command{};
  command.present = true;
  command.rev = 4;
  command.hasStyle = true;
  command.style = ClockStyle::Analog;
  TEST_ASSERT_TRUE(applyDisplayCommand(state, command));
  TEST_ASSERT_EQUAL(static_cast<int>(ClockStyle::Analog), static_cast<int>(state.display.clockStyle));
  TEST_ASSERT_TRUE(state.display.hour12);  // untouched
  TEST_ASSERT_EQUAL_UINT32(4, state.displayRev);
}

void test_same_revision_does_not_override_local_button_choice(void) {
  DeviceSettingsState state{};
  BridgeDisplayCommand command{};
  command.present = true;
  command.rev = 9;
  command.hasStyle = true;
  command.style = ClockStyle::Words;
  command.hasPage = true;
  command.page = ScreenPage::Clock;
  TEST_ASSERT_TRUE(applyDisplayCommand(state, command));
  // User presses KEY on the board and picks another face.
  state.display.clockStyle = ClockStyle::Pet;
  state.page = ScreenPage::Overview;
  // Every later poll repeats rev 9; the local choice must stay.
  TEST_ASSERT_FALSE(applyDisplayCommand(state, command));
  TEST_ASSERT_EQUAL(static_cast<int>(ClockStyle::Pet), static_cast<int>(state.display.clockStyle));
  TEST_ASSERT_EQUAL(static_cast<int>(ScreenPage::Overview), static_cast<int>(state.page));
  // A bridge restart that resets its counter still counts as "different".
  command.rev = 1;
  TEST_ASSERT_TRUE(applyDisplayCommand(state, command));
  TEST_ASSERT_EQUAL(static_cast<int>(ClockStyle::Words), static_cast<int>(state.display.clockStyle));
}

void test_missing_or_zero_revision_is_ignored(void) {
  DeviceSettingsState state{};
  BridgeDisplayCommand command{};
  TEST_ASSERT_FALSE(applyDisplayCommand(state, command));
  command.present = true;
  command.rev = 0;
  command.hasStyle = true;
  command.style = ClockStyle::Dots;
  TEST_ASSERT_FALSE(applyDisplayCommand(state, command));
  TEST_ASSERT_EQUAL(static_cast<int>(ClockStyle::Sans), static_cast<int>(state.display.clockStyle));
}

void test_tz_updates_are_validated(void) {
  DeviceSettingsState state{};
  TEST_ASSERT_TRUE(applyTzCommand(state, "GMT0BST,M3.5.0/1,M10.5.0"));
  TEST_ASSERT_FALSE(applyTzCommand(state, "GMT0BST,M3.5.0/1,M10.5.0"));
  TEST_ASSERT_FALSE(applyTzCommand(state, "bad tz"));
  TEST_ASSERT_EQUAL_STRING("GMT0BST,M3.5.0/1,M10.5.0", state.tz.c_str());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_new_revision_applies_only_present_fields);
  RUN_TEST(test_same_revision_does_not_override_local_button_choice);
  RUN_TEST(test_missing_or_zero_revision_is_ignored);
  RUN_TEST(test_tz_updates_are_validated);
  return UNITY_END();
}
