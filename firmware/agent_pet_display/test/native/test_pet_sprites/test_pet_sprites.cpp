#include <unity.h>

#include <string.h>

#include "pet_sprites.h"

void setUp(void) {}

void tearDown(void) {}

void test_active_pet_species_is_capybara(void) {
  TEST_ASSERT_EQUAL_STRING("CAPYBARA", activePetSpecies());
}

void test_attention_mode_uses_compact_alert_pose(void) {
  const PetSpriteFrame& frame = spriteForMode(PetMode::Attention, 0);

  TEST_ASSERT_EQUAL_STRING("  ^    ^  ", frame.lines[0]);
  TEST_ASSERT_EQUAL_STRING(" /O____O\\ ", frame.lines[1]);
}

void test_celebrate_mode_uses_compact_cheer_pose_on_second_phase(void) {
  const PetSpriteFrame& frame = spriteForMode(PetMode::Celebrate, 500);

  TEST_ASSERT_EQUAL_STRING("   \\__/   ", frame.lines[0]);
  TEST_ASSERT_EQUAL_STRING("(   WW   )", frame.lines[3]);
}

void test_idle_sprite_is_tightly_cropped_for_rlcd_badge_layout(void) {
  const PetSpriteFrame& frame = spriteForMode(PetMode::Idle, 0);

  TEST_ASSERT_TRUE(strlen(frame.lines[1]) <= 10);
  TEST_ASSERT_TRUE(strlen(frame.lines[2]) <= 10);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_active_pet_species_is_capybara);
  RUN_TEST(test_attention_mode_uses_compact_alert_pose);
  RUN_TEST(test_celebrate_mode_uses_compact_cheer_pose_on_second_phase);
  RUN_TEST(test_idle_sprite_is_tightly_cropped_for_rlcd_badge_layout);
  return UNITY_END();
}
