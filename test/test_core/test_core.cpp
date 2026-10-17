#include <unity.h>

#include <string>

#include "image_ops.h"
#include "layout.h"
#include "playback.h"
#include "rgb565.h"
#include "text_fold.h"
#include "ui_math.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- rgb565

void test_pack_unpack_keeps_white_and_black() {
  int r, g, b;
  rgb565::unpack(rgb565::pack(255, 255, 255), r, g, b);
  TEST_ASSERT_EQUAL(255, r);
  TEST_ASSERT_EQUAL(255, g);
  TEST_ASSERT_EQUAL(255, b);
  TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565::pack(0, 0, 0));
}

void test_pack_clamps_out_of_range() {
  TEST_ASSERT_EQUAL_HEX16(rgb565::pack(255, 0, 0), rgb565::pack(400, -20, -1));
}

void test_mix_weights() {
  const uint16_t black = 0x0000;
  const uint16_t white = 0xFFFF;
  TEST_ASSERT_EQUAL_HEX16(black, rgb565::mix(black, white, 0));
  TEST_ASSERT_EQUAL_HEX16(white, rgb565::mix(black, white, 256));
  int r, g, b;
  rgb565::unpack(rgb565::mix(black, white, 128), r, g, b);
  TEST_ASSERT_INT_WITHIN(8, 127, r);
  TEST_ASSERT_INT_WITHIN(8, 127, g);
  TEST_ASSERT_INT_WITHIN(8, 127, b);
}

// ---------------------------------------------------------------- image_ops

void test_box_blur_keeps_flat_plane() {
  uint8_t plane[6 * 4];
  uint8_t tmp[6 * 4];
  for (uint8_t &p : plane) p = 90;
  image::boxBlur(plane, tmp, 6, 4, 2);
  for (uint8_t p : plane) TEST_ASSERT_INT_WITHIN(2, 90, p);  // each pass may round down by 1
}

void test_sample_bilinear_interpolates_and_clamps() {
  const uint16_t src[2] = {0x0000, 0xFFFF};
  int r, g, b;
  rgb565::unpack(image::sampleBilinear(src, 2, 1, 0.5f, 0), r, g, b);
  TEST_ASSERT_INT_WITHIN(8, 127, r);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, image::sampleBilinear(src, 2, 1, 5.0f, 3.0f));
  TEST_ASSERT_EQUAL_HEX16(0x0000, image::sampleBilinear(src, 2, 1, -1.0f, -1.0f));
}

// ---------------------------------------------------------------- text_fold

void test_ascii_fold_strips_accents() {
  TEST_ASSERT_EQUAL_STRING("Cancion", asciiFold(std::string("Canci\xC3\xB3n")).c_str());
  TEST_ASSERT_EQUAL_STRING("Nandu", asciiFold(std::string("\xC3\x91" "and\xC3\xBA")).c_str());
}

void test_ascii_fold_maps_inverted_marks() {
  TEST_ASSERT_EQUAL_STRING("?Que? !Si!", asciiFold(std::string("\xC2\xBFQu\xC3\xA9? \xC2\xA1S\xC3\xAD!")).c_str());
}

void test_ascii_fold_skips_emoji_and_symbols() {
  TEST_ASSERT_EQUAL_STRING("Hi  there", asciiFold(std::string("Hi \xF0\x9F\x98\x80 there")).c_str());
  TEST_ASSERT_EQUAL_STRING("A  B", asciiFold(std::string("A \xE2\x80\x94 B")).c_str());
}

// ---------------------------------------------------------------- ui_math

void test_marquee_short_title_does_not_scroll() {
  TEST_ASSERT_EQUAL(0, marqueeOffset(layout::TITLE_W, 5000));
}

void test_marquee_pauses_then_scrolls() {
  const int width = 300;
  TEST_ASSERT_EQUAL(0, marqueeOffset(width, layout::MARQUEE_PAUSE_MS - 1));
  TEST_ASSERT_EQUAL(layout::MARQUEE_PX_PER_SEC, marqueeOffset(width, layout::MARQUEE_PAUSE_MS + 1000));
  const unsigned long loopMs = layout::MARQUEE_PAUSE_MS + (width + layout::MARQUEE_GAP) * 1000UL / layout::MARQUEE_PX_PER_SEC;
  TEST_ASSERT_EQUAL(0, marqueeOffset(width, loopMs));  // back to the start, pausing again
}

void test_button_at_maps_zones() {
  using namespace layout;
  TEST_ASSERT_EQUAL(BUTTON_PREV, buttonAt(BUTTONS[0].x, BUTTONS[0].y));
  TEST_ASSERT_EQUAL(BUTTON_PLAY_PAUSE, buttonAt(BUTTONS[1].x, BUTTONS[1].y));
  TEST_ASSERT_EQUAL(BUTTON_NEXT, buttonAt(BUTTONS[2].x, BUTTONS[2].y));
  TEST_ASSERT_EQUAL(BUTTON_PLAY_PAUSE, buttonAt(BUTTONS[0].x, 50));  // above the row
}

void test_unrotate_identity_at_zero() {
  int x = 37, y = 201;
  unrotate(0, x, y);
  TEST_ASSERT_EQUAL(37, x);
  TEST_ASSERT_EQUAL(201, y);
}

void test_unrotate_quarter_turn() {
  // With the UI turned 90 degrees clockwise, its top-center pixel (120, 10) lands at (229, 120).
  int x = 229, y = 120;
  unrotate(90, x, y);
  TEST_ASSERT_EQUAL(120, x);
  TEST_ASSERT_EQUAL(10, y);
}

void test_wrap_degrees() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -170.0f, wrapDegrees(190.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 170.0f, wrapDegrees(-190.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, wrapDegrees(360.0f));
}

// ---------------------------------------------------------------- playback

void test_playback_extrapolates_while_playing() {
  Playback p;
  p.playing = true;
  p.progressMs = 1000;
  p.durationMs = 10000;
  p.progressAt = 500;
  TEST_ASSERT_EQUAL_UINT32(3000, p.positionAt(2500));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.3f, p.fractionAt(2500));
}

void test_playback_holds_when_paused_and_clamps_to_duration() {
  Playback p;
  p.progressMs = 1000;
  p.durationMs = 2000;
  TEST_ASSERT_EQUAL_UINT32(1000, p.positionAt(99999));
  p.playing = true;
  TEST_ASSERT_EQUAL_UINT32(2000, p.positionAt(99999));
}

void test_playback_set_playing_keeps_position() {
  Playback p;
  p.playing = true;
  p.durationMs = 10000;
  p.setPlaying(false, 4000);
  TEST_ASSERT_FALSE(p.playing);
  TEST_ASSERT_EQUAL_UINT32(4000, p.positionAt(9000));
  p.setPlaying(true, 9000);
  TEST_ASSERT_EQUAL_UINT32(5000, p.positionAt(10000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_pack_unpack_keeps_white_and_black);
  RUN_TEST(test_pack_clamps_out_of_range);
  RUN_TEST(test_mix_weights);
  RUN_TEST(test_box_blur_keeps_flat_plane);
  RUN_TEST(test_sample_bilinear_interpolates_and_clamps);
  RUN_TEST(test_ascii_fold_strips_accents);
  RUN_TEST(test_ascii_fold_maps_inverted_marks);
  RUN_TEST(test_ascii_fold_skips_emoji_and_symbols);
  RUN_TEST(test_marquee_short_title_does_not_scroll);
  RUN_TEST(test_marquee_pauses_then_scrolls);
  RUN_TEST(test_button_at_maps_zones);
  RUN_TEST(test_unrotate_identity_at_zero);
  RUN_TEST(test_unrotate_quarter_turn);
  RUN_TEST(test_wrap_degrees);
  RUN_TEST(test_playback_extrapolates_while_playing);
  RUN_TEST(test_playback_holds_when_paused_and_clamps_to_duration);
  RUN_TEST(test_playback_set_playing_keeps_position);
  return UNITY_END();
}
