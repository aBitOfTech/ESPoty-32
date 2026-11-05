#include <unity.h>

#include <string>

#include "image_ops.h"
#include "layout.h"
#include "lyrics.h"
#include "playback.h"
#include "rgb565.h"
#include "text_fold.h"
#include "ui_math.h"
#include "url.h"

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

void test_button_at_lyrics_zone() {
  using namespace layout;
  const int reach = LYRICS_BUTTON.r + LYRICS_TOUCH_MARGIN;
  TEST_ASSERT_EQUAL(BUTTON_LYRICS, buttonAt(LYRICS_BUTTON.x, LYRICS_BUTTON.y));
  TEST_ASSERT_EQUAL(BUTTON_LYRICS, buttonAt(LYRICS_BUTTON.x, LYRICS_BUTTON.y + reach));
  TEST_ASSERT_EQUAL(BUTTON_PLAY_PAUSE, buttonAt(LYRICS_BUTTON.x, LYRICS_BUTTON.y + reach + 1));
  TEST_ASSERT_EQUAL(BUTTON_PLAY_PAUSE, buttonAt(CENTER, CENTER));
  // Clear of the playback row and inside the progress ring.
  TEST_ASSERT_TRUE(LYRICS_BUTTON.y + reach < BUTTONS[1].y - BUTTONS[1].r - 8);
  TEST_ASSERT_TRUE(CENTER - (LYRICS_BUTTON.y - LYRICS_BUTTON.r) < 113);
}

void test_band_width_on_round_screen() {
  TEST_ASSERT_EQUAL(200, bandWidth(100, 120, 120));
  TEST_ASSERT_EQUAL(160, bandWidth(100, 60, 90));  // 60 px from the center: 2 * sqrt(100^2 - 60^2)
  TEST_ASSERT_EQUAL(0, bandWidth(100, 10, 30));
  TEST_ASSERT_EQUAL(bandWidth(100, 90, 150), bandWidth(100, 150, 150));  // the far edge decides
  TEST_ASSERT_TRUE(bandWidth(100, 50, 70) < bandWidth(100, 110, 130));
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

// ---------------------------------------------------------------- lyrics

typedef lyrics::Synced<16, 256> SmallLyrics;

static uint32_t tagMs(const char *tag) {
  uint32_t ms = 12345;
  const char *end = tag + strlen(tag);
  return lyrics::parseTimeTag(tag, end, ms) ? ms : UINT32_MAX;
}

void test_parse_time_tag_formats() {
  TEST_ASSERT_EQUAL_UINT32(7130, tagMs("[00:07.13]"));
  TEST_ASSERT_EQUAL_UINT32(83400, tagMs("[01:23.4]"));
  TEST_ASSERT_EQUAL_UINT32(83456, tagMs("[01:23.456]"));
  TEST_ASSERT_EQUAL_UINT32(83000, tagMs("[1:23]"));
  TEST_ASSERT_EQUAL_UINT32(83450, tagMs("[01:23:45]"));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tagMs("[ar:Queen]"));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tagMs("[00:61.00]"));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tagMs("[00:07.13"));
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, tagMs("00:07.13]"));
}

void test_lyrics_parse_skips_metadata_and_sorts() {
  static SmallLyrics l;
  TEST_ASSERT_TRUE(l.parse("[ar:Queen]\n[ti:Bohemian Rhapsody]\n"
                           "[00:07.13] Caught in a landslide\r\n"
                           "garbage without a tag\n"
                           "[00:00.15] Is this the real life?\n"
                           "[xx:yy] broken\n"));
  TEST_ASSERT_EQUAL(2, l.count);
  TEST_ASSERT_EQUAL_UINT32(150, l.lines[0].timeMs);
  TEST_ASSERT_EQUAL_STRING("Is this the real life?", l.textOf(0));
  TEST_ASSERT_EQUAL_STRING("Caught in a landslide", l.textOf(1));  // \r and spaces trimmed
}

void test_lyrics_parse_repeats_multi_tag_lines() {
  static SmallLyrics l;
  TEST_ASSERT_TRUE(l.parse("[00:20.00][00:10.00] Chorus\n[00:15.00] Verse"));
  TEST_ASSERT_EQUAL(3, l.count);
  TEST_ASSERT_EQUAL_STRING("Chorus", l.textOf(0));
  TEST_ASSERT_EQUAL_STRING("Verse", l.textOf(1));
  TEST_ASSERT_EQUAL_STRING("Chorus", l.textOf(2));
  TEST_ASSERT_EQUAL_UINT32(20000, l.lines[2].timeMs);
}

void test_lyrics_parse_folds_text_and_marks_pauses() {
  static SmallLyrics l;
  // Spanish accents fold; an empty line and an Arabic-only line (U+0628 U+0650...) become pauses.
  TEST_ASSERT_TRUE(l.parse("[00:01.00] Canci\xC3\xB3n   de  amor\n"
                           "[00:02.00] \n"
                           "[00:03.00] \xD8\xA8\xD9\x90 \xD8\xB3\n"
                           "[00:04.00] Fin"));
  TEST_ASSERT_EQUAL(4, l.count);
  TEST_ASSERT_EQUAL_STRING("Cancion de amor", l.textOf(0));
  TEST_ASSERT_TRUE(l.isPause(1));
  TEST_ASSERT_TRUE(l.isPause(2));
  TEST_ASSERT_EQUAL_STRING("", l.textOf(2));
  TEST_ASSERT_FALSE(l.isPause(3));
  TEST_ASSERT_EQUAL(3, l.neighbor(0, 1));
  TEST_ASSERT_EQUAL(0, l.neighbor(3, -1));
  TEST_ASSERT_EQUAL(-1, l.neighbor(0, -1));
  TEST_ASSERT_EQUAL(-1, l.neighbor(3, 1));
}

void test_lyrics_parse_without_text_fails() {
  static SmallLyrics l;
  TEST_ASSERT_FALSE(l.parse(""));
  TEST_ASSERT_EQUAL(0, l.count);
  TEST_ASSERT_FALSE(l.parse("[ar:Nobody]\n[00:01.00]\n[00:02.00]   "));
  TEST_ASSERT_EQUAL(2, l.count);  // only pauses
}

void test_lyrics_parse_caps_lines_and_text() {
  static lyrics::Synced<2, 256> fewLines;
  fewLines.parse("[00:01.00] a\n[00:02.00] b\n[00:03.00] c");
  TEST_ASSERT_EQUAL(2, fewLines.count);
  TEST_ASSERT_EQUAL_STRING("b", fewLines.textOf(1));

  static lyrics::Synced<8, 8> littleText;  // "abc" + NUL fits after the shared "", "toolong" doesn't
  TEST_ASSERT_TRUE(littleText.parse("[00:01.00] abc\n[00:02.00] toolong\n[00:03.00] de"));
  TEST_ASSERT_EQUAL(2, littleText.count);
  TEST_ASSERT_EQUAL_STRING("abc", littleText.textOf(0));
  TEST_ASSERT_EQUAL_STRING("de", littleText.textOf(1));
}

void test_lyrics_line_at() {
  static SmallLyrics l;
  l.parse("[00:01.00] one\n[00:02.00] two\n[00:04.00] three");
  TEST_ASSERT_EQUAL(-1, l.lineAt(-500));
  TEST_ASSERT_EQUAL(-1, l.lineAt(999));
  TEST_ASSERT_EQUAL(0, l.lineAt(1000));
  TEST_ASSERT_EQUAL(0, l.lineAt(1999));
  TEST_ASSERT_EQUAL(1, l.lineAt(2000));
  TEST_ASSERT_EQUAL(2, l.lineAt(600000));
  l.parse("");
  TEST_ASSERT_EQUAL(-1, l.lineAt(1000));
}

static int fixedWidth(int, int) { return 50; }
static int tenPerChar(const char *, int length) { return length * 10; }

static std::string spanText(const char *text, const lyrics::Span &span) {
  return std::string(text + span.start, text + span.end);
}

void test_wrap_text_greedy_rows() {
  const char *text = "aaa bbb ccc";
  lyrics::Span rows[3];
  TEST_ASSERT_EQUAL(3, lyrics::wrapText(text, 3, fixedWidth, tenPerChar, rows));
  TEST_ASSERT_EQUAL_STRING("aaa", spanText(text, rows[0]).c_str());
  TEST_ASSERT_EQUAL_STRING("bbb", spanText(text, rows[1]).c_str());
  TEST_ASSERT_EQUAL_STRING("ccc", spanText(text, rows[2]).c_str());
  TEST_ASSERT_EQUAL(1, lyrics::wrapText("aa bb", 3, fixedWidth, tenPerChar, rows));
  TEST_ASSERT_EQUAL_STRING("aa bb", spanText("aa bb", rows[0]).c_str());
}

void test_wrap_text_overflow_and_edge_cases() {
  const char *text = "aaa bbb ccc ";
  lyrics::Span rows[2];
  TEST_ASSERT_EQUAL(2, lyrics::wrapText(text, 2, fixedWidth, tenPerChar, rows));
  TEST_ASSERT_EQUAL_STRING("bbb ccc", spanText(text, rows[1]).c_str());  // the rest, to truncate
  TEST_ASSERT_EQUAL(1, lyrics::wrapText("abcdefghij", 2, fixedWidth, tenPerChar, rows));  // a long word
  TEST_ASSERT_EQUAL(0, lyrics::wrapText("", 2, fixedWidth, tenPerChar, rows));
  TEST_ASSERT_EQUAL(0, lyrics::wrapText("   ", 2, fixedWidth, tenPerChar, rows));
}

static int narrowEdges(int row, int rows) { return rows == 2 ? 70 : row == 1 ? 100 : 40; }

void test_wrap_text_uses_width_per_row() {
  lyrics::Span rows[3];
  // "aaaa bbbb cc" fits two rows of 70 px.
  const char *two = "aaaa bbbb cc";
  TEST_ASSERT_EQUAL(2, lyrics::wrapText(two, 3, narrowEdges, tenPerChar, rows));
  TEST_ASSERT_EQUAL_STRING("bbbb cc", spanText(two, rows[1]).c_str());
  // "aaaa bbbb ccc" doesn't; it needs the 3-row block (40, 100, 40) but fills only two of its rows.
  const char *three = "aaaa bbbb ccc";
  TEST_ASSERT_EQUAL(3, lyrics::wrapText(three, 3, narrowEdges, tenPerChar, rows));
  TEST_ASSERT_EQUAL_STRING("aaaa", spanText(three, rows[0]).c_str());
  TEST_ASSERT_EQUAL_STRING("bbbb ccc", spanText(three, rows[1]).c_str());
  TEST_ASSERT_EQUAL(rows[2].start, rows[2].end);
}

// ---------------------------------------------------------------- url

void test_url_encode() {
  TEST_ASSERT_EQUAL_STRING("AC%2FDC%20%26%20T%C3%BA", urlEncode(std::string("AC/DC & T\xC3\xBA")).c_str());
  TEST_ASSERT_EQUAL_STRING("Az09-_.~", urlEncode(std::string("Az09-_.~")).c_str());
  TEST_ASSERT_EQUAL_STRING("%3D%3F%2B%23", urlEncode(std::string("=?+#")).c_str());
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
  RUN_TEST(test_button_at_lyrics_zone);
  RUN_TEST(test_band_width_on_round_screen);
  RUN_TEST(test_unrotate_identity_at_zero);
  RUN_TEST(test_unrotate_quarter_turn);
  RUN_TEST(test_wrap_degrees);
  RUN_TEST(test_playback_extrapolates_while_playing);
  RUN_TEST(test_playback_holds_when_paused_and_clamps_to_duration);
  RUN_TEST(test_playback_set_playing_keeps_position);
  RUN_TEST(test_parse_time_tag_formats);
  RUN_TEST(test_lyrics_parse_skips_metadata_and_sorts);
  RUN_TEST(test_lyrics_parse_repeats_multi_tag_lines);
  RUN_TEST(test_lyrics_parse_folds_text_and_marks_pauses);
  RUN_TEST(test_lyrics_parse_without_text_fails);
  RUN_TEST(test_lyrics_parse_caps_lines_and_text);
  RUN_TEST(test_lyrics_line_at);
  RUN_TEST(test_wrap_text_greedy_rows);
  RUN_TEST(test_wrap_text_overflow_and_edge_cases);
  RUN_TEST(test_wrap_text_uses_width_per_row);
  RUN_TEST(test_url_encode);
  return UNITY_END();
}
