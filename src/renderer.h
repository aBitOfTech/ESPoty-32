#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

#include <vector>

#include "display.h"
#include "layout.h"
#include "player_store.h"

// Round panel, Apple Music style: the cover fills the screen, a dark gradient at the bottom carries
// the title (scrolling when it doesn't fit) and artist, three glass buttons sit below, and a progress
// ring runs along the edge. A smaller glass button at the top switches to the lyrics view: the cover
// darkened, the line being sung in the middle and the previous and next lines dimmed around it.
// Everything is composed in a PSRAM sprite and pushed rotated by angle.
class Renderer {
 public:
  Renderer() : canvas_(&lcd_) {}

  bool begin();                      // false when the frame buffers can't be allocated
  void showError(const char *text);  // straight to the panel; works even if begin() failed
  void setAngle(float degrees) { angle_ = degrees; }

  void drawStatus(const PlayerState &state);
  // Re-lays out the text and rebuilds the layer under the ring and title. Call when the track, play
  // state, notice, cover, lyrics, pressed button, view or lyric line (index, -1 before the first)
  // change.
  void setTrack(const PlayerState &state, int pressedButton, bool lyricsView, int lyricLine);
  void drawFrame(float progress, unsigned long marqueeElapsedMs);
  bool titleScrolls() const { return !lyricsView_ && titleWidth_ > layout::TITLE_W; }

 private:
  struct RingPixel {
    uint16_t index;
    uint16_t position;  // 0..65535 clockwise from 12 o'clock
    uint8_t coverage;
  };

  // The glass blur runs at half resolution over the button row plus a margin.
  static constexpr int BLUR_MARGIN = 8;
  static constexpr int BLUR_X = layout::BUTTONS[0].x - layout::BUTTONS[0].r - BLUR_MARGIN;
  static constexpr int BLUR_Y = layout::BUTTONS[1].y - layout::BUTTONS[1].r - BLUR_MARGIN;
  static constexpr int BLUR_W = (layout::BUTTONS[2].x + layout::BUTTONS[2].r + BLUR_MARGIN - BLUR_X) / 2;
  static constexpr int BLUR_H = (layout::BUTTONS[1].y + layout::BUTTONS[1].r + BLUR_MARGIN - BLUR_Y) / 2;

  void buildGeometry();
  void blendPx(int i, uint16_t color, int alpha);
  String fitText(const String &text, int maxWidth);
  void drawVignette();
  void drawScrim();
  void drawShade();
  void drawGlassButtons();
  void drawIcon(int button, bool playing);
  void drawLyricsButton();
  void drawLyrics(const PlayerState &state, int line);
  int drawLyric(const char *text, const lgfx::IFont *font, int rowHeight, int maxRows, int y, int anchor,
                uint16_t color);
  void drawRing(float progress);
  void drawTitle(int offset);
  void present();

  Display lcd_;
  LGFX_Sprite canvas_;
  uint16_t *frame_ = nullptr;  // canvas buffer; 16bpp sprites store byte-swapped RGB565
  uint16_t *under_ = nullptr;  // everything except the ring and the title
  float angle_ = 0;            // degrees clockwise the UI is rotated on the panel
  int pressedButton_ = -1;
  bool lyricsView_ = false;
  String title_;
  int titleWidth_ = 0;
  uint8_t scrimAlpha_[layout::SCREEN];
  uint8_t blurPlanes_[4][BLUR_W * BLUR_H];
  std::vector<RingPixel> ring_;
};
