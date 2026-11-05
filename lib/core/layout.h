#pragma once

// Geometry of the round 240 px UI shared by drawing and touch handling. Constants used only by the
// renderer live in renderer.h/.cpp.
namespace layout {

constexpr int SCREEN = 240;
constexpr int CENTER = SCREEN / 2;

constexpr int TITLE_Y = 140;
constexpr int TITLE_X = 26;
constexpr int TITLE_W = 188;
constexpr int MARQUEE_GAP = 48;
constexpr int MARQUEE_PX_PER_SEC = 30;
constexpr unsigned long MARQUEE_PAUSE_MS = 2000;

struct GlassButton {
  int x, y, r;
};
enum Button { BUTTON_PREV, BUTTON_PLAY_PAUSE, BUTTON_NEXT, BUTTON_LYRICS };
constexpr GlassButton BUTTONS[3] = {{72, 198, 19}, {120, 198, 22}, {168, 198, 19}};  // the playback row

// Toggles the lyrics view. Not part of BUTTONS: the row's blur and drawing assume three buttons.
// Its edge stays 49 px from the center, well inside the progress ring (113.5 px).
constexpr GlassButton LYRICS_BUTTON = {120, 34, 15};
constexpr int LYRICS_TOUCH_MARGIN = 10;  // the touch zone is a circle this much wider than the button

}  // namespace layout
