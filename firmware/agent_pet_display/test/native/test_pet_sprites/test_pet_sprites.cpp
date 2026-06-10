#include <unity.h>

#include "pet_sprites.h"

void setUp(void) {}

void tearDown(void) {}

void test_active_pet_species_is_gugugaga(void) {
  TEST_ASSERT_EQUAL_STRING("GUGUGAGA", activePetSpecies());
}

void test_idle_bitmap_fits_rlcd_pet_panel(void) {
  const PetBitmapFrame& frame = bitmapForMode(PetMode::Idle, 0);

  TEST_ASSERT_TRUE(frame.width >= 44);
  TEST_ASSERT_TRUE(frame.width <= 64);
  TEST_ASSERT_TRUE(frame.height >= 52);
  TEST_ASSERT_TRUE(frame.height <= 64);
  TEST_ASSERT_NOT_NULL(frame.bits);
}

void test_bitmap_pixels_preserve_penguin_suit_silhouette(void) {
  const PetBitmapFrame& frame = bitmapForMode(PetMode::Idle, 0);

  TEST_ASSERT_TRUE(petBitmapPixel(frame, frame.width / 2, 4));
  TEST_ASSERT_TRUE(petBitmapPixel(frame, frame.width / 2, frame.height - 5));
  TEST_ASSERT_FALSE(petBitmapPixel(frame, 0, 0));
}

void test_attention_and_sleep_modes_use_distinct_frames(void) {
  const PetBitmapFrame& attention = bitmapForMode(PetMode::Attention, 0);
  const PetBitmapFrame& sleep = bitmapForMode(PetMode::Sleep, 0);

  TEST_ASSERT_NOT_EQUAL(attention.bits, sleep.bits);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_active_pet_species_is_gugugaga);
  RUN_TEST(test_idle_bitmap_fits_rlcd_pet_panel);
  RUN_TEST(test_bitmap_pixels_preserve_penguin_suit_silhouette);
  RUN_TEST(test_attention_and_sleep_modes_use_distinct_frames);
  return UNITY_END();
}
