#include "renderer.h"

#include <math.h>

#include "image_ops.h"
#include "pins.h"
#include "psram.h"
#include "rgb565.h"
#include "text_fold.h"
#include "theme.h"
#include "ui_math.h"

using namespace layout;
using rgb565::mix;
using rgb565::pack;

namespace {
constexpr int SCRIM_TOP = 70;
constexpr int SCRIM_MAX = 215;
constexpr int TITLE_FADE = 14;
constexpr int TITLE_TOP = TITLE_Y - 13;
constexpr int TITLE_H = 26;
constexpr int ARTIST_Y = 161;
constexpr int ARTIST_W = 176;
constexpr int STATUS_W = 200;
constexpr int STATUS_TITLE_Y = 110;
constexpr int STATUS_DETAIL_Y = 136;
constexpr float RING_INNER = 113.5f;
constexpr float RING_OUTER = 118.5f;
constexpr float RING_MID = (RING_INNER + RING_OUTER) / 2;
constexpr int RING_DONE_ALPHA = 235;  // of 255
constexpr int RING_TODO_ALPHA = 120;
constexpr float KNOB_RADIUS = 4.5f;
constexpr size_t FRAME_PIXELS = SCREEN * SCREEN;
}  // namespace

bool Renderer::begin() {
  pinMode(pins::LCD_BL, OUTPUT);
  digitalWrite(pins::LCD_BL, HIGH);
  lcd_.init();
  lcd_.setRotation(0);
  canvas_.setColorDepth(16);
  canvas_.setPsram(true);
  frame_ = static_cast<uint16_t *>(canvas_.createSprite(SCREEN, SCREEN));
  under_ = psramAlloc<uint16_t>(FRAME_PIXELS);
  if (!frame_ || !under_) return false;
  buildGeometry();
  return true;
}

void Renderer::showError(const char *text) {
  lcd_.fillScreen(TFT_BLACK);
  lcd_.setTextDatum(middle_center);
  lcd_.setFont(&fonts::DejaVu18);
  lcd_.setTextColor(theme::TEXT);
  lcd_.drawString(text, CENTER, CENTER);
}

// Precomputes the gradient and the ring pixels once.
void Renderer::buildGeometry() {
  for (int y = 0; y < SCREEN; ++y) {
    const float t = y < SCRIM_TOP ? 0.0f : static_cast<float>(y - SCRIM_TOP) / (SCREEN - SCRIM_TOP);
    scrimAlpha_[y] = static_cast<uint8_t>(SCRIM_MAX * t * t * (3 - 2 * t));
  }
  for (int y = 0; y < SCREEN; ++y) {
    for (int x = 0; x < SCREEN; ++x) {
      const float dx = x + 0.5f - CENTER;
      const float dy = y + 0.5f - CENTER;
      const float d = hypotf(dx, dy);
      const float coverage = constrain(fminf(RING_OUTER - d, d - RING_INNER) + 0.5f, 0.0f, 1.0f);
      if (coverage <= 0) continue;
      float turn = atan2f(dx, -dy) / (2 * PI);
      if (turn < 0) turn += 1;
      ring_.push_back({static_cast<uint16_t>(y * SCREEN + x), static_cast<uint16_t>(turn * 65535),
                       static_cast<uint8_t>(coverage * 255)});
    }
  }
}

void Renderer::blendPx(int i, uint16_t color, int alpha) {
  if (alpha <= 0) return;
  frame_[i] = __builtin_bswap16(alpha >= 256 ? color : mix(__builtin_bswap16(frame_[i]), color, alpha));
}

// Uses the canvas' current font.
String Renderer::fitText(const String &text, int maxWidth) {
  if (canvas_.textWidth(text) <= maxWidth) return text;
  String out = text;
  while (out.length() && canvas_.textWidth(out + "...") > maxWidth) out.remove(out.length() - 1);
  return out + "...";
}

// ---------------------------------------------------------------- layers

void Renderer::drawVignette() {
  for (int y = 0; y < SCREEN; ++y) {
    for (int x = 0; x < SCREEN; ++x) {
      const float dx = (x - CENTER) / static_cast<float>(CENTER);
      const float dy = (y - CENTER) / static_cast<float>(CENTER);
      const float shade = 1.0f - 0.5f * (dx * dx + dy * dy);
      frame_[y * SCREEN + x] = __builtin_bswap16(pack(theme::BACKGROUND_RGB[0] * shade,
                                                      theme::BACKGROUND_RGB[1] * shade,
                                                      theme::BACKGROUND_RGB[2] * shade));
    }
  }
}

void Renderer::drawScrim() {
  for (int y = SCRIM_TOP; y < SCREEN; ++y) {
    uint16_t *row = frame_ + y * SCREEN;
    const int alpha = scrimAlpha_[y];
    for (int x = 0; x < SCREEN; ++x) row[x] = __builtin_bswap16(mix(__builtin_bswap16(row[x]), theme::SCRIM, alpha));
  }
}

// Liquid-glass buttons: blur what's behind, lift it toward white, add a specular rim
// (bright on top, faint below) and a glow on the pressed one.
void Renderer::drawGlassButtons() {
  for (int y = 0; y < BLUR_H; ++y) {
    const uint16_t *row0 = frame_ + (BLUR_Y + 2 * y) * SCREEN + BLUR_X;
    const uint16_t *row1 = row0 + SCREEN;
    for (int x = 0; x < BLUR_W; ++x) {
      const uint16_t quad[4] = {row0[2 * x], row0[2 * x + 1], row1[2 * x], row1[2 * x + 1]};
      int r = 0, g = 0, b = 0;
      for (uint16_t v : quad) {
        v = __builtin_bswap16(v);
        r += (v >> 8) & 0xF8;
        g += (v >> 3) & 0xFC;
        b += (v << 3) & 0xF8;
      }
      blurPlanes_[0][y * BLUR_W + x] = r >> 2;
      blurPlanes_[1][y * BLUR_W + x] = g >> 2;
      blurPlanes_[2][y * BLUR_W + x] = b >> 2;
    }
  }
  for (int c = 0; c < 3; ++c) {
    image::boxBlur(blurPlanes_[c], blurPlanes_[3], BLUR_W, BLUR_H, 3);
    image::boxBlur(blurPlanes_[c], blurPlanes_[3], BLUR_W, BLUR_H, 3);
  }
  for (int i = 0; i < 3; ++i) {
    const GlassButton &button = BUTTONS[i];
    const float base = (i == BUTTON_PLAY_PAUSE ? 0.24f : 0.16f) + (i == pressedButton_ ? 0.3f : 0.0f);
    for (int y = button.y - button.r - 1; y <= button.y + button.r + 1; ++y) {
      for (int x = button.x - button.r - 1; x <= button.x + button.r + 1; ++x) {
        const float dx = x + 0.5f - button.x;
        const float dy = y + 0.5f - button.y;
        const float d = sqrtf(dx * dx + dy * dy);
        const float coverage = constrain(button.r + 0.5f - d, 0.0f, 1.0f);
        if (coverage <= 0) continue;
        const float top = constrain(0.5f - dy / (2 * button.r), 0.0f, 1.0f);  // 1 at the top edge, 0 at the bottom
        const float rim = constrain(1.0f - fabsf(d - (button.r - 0.8f)), 0.0f, 1.0f) * (0.2f + 0.55f * top);
        const float light = constrain(base + 0.1f * top + rim, 0.0f, 1.0f);
        const int s = ((y - BLUR_Y) >> 1) * BLUR_W + ((x - BLUR_X) >> 1);
        const int r = blurPlanes_[0][s] + (255 - blurPlanes_[0][s]) * light;
        const int g = blurPlanes_[1][s] + (255 - blurPlanes_[1][s]) * light;
        const int b = blurPlanes_[2][s] + (255 - blurPlanes_[2][s]) * light;
        blendPx(y * SCREEN + x, pack(r, g, b), static_cast<int>(coverage * 256));
      }
    }
  }
}

void Renderer::drawIcon(int button, bool playing) {
  const int cx = BUTTONS[button].x;
  const int cy = BUTTONS[button].y;
  const uint16_t color = theme::ICON;
  if (button == BUTTON_PREV) {
    canvas_.fillRect(cx - 9, cy - 8, 3, 16, color);
    canvas_.fillTriangle(cx + 8, cy - 8, cx + 8, cy + 8, cx - 6, cy, color);
  } else if (button == BUTTON_NEXT) {
    canvas_.fillRect(cx + 7, cy - 8, 3, 16, color);
    canvas_.fillTriangle(cx - 8, cy - 8, cx - 8, cy + 8, cx + 6, cy, color);
  } else if (playing) {
    canvas_.fillRoundRect(cx - 8, cy - 9, 5, 18, 2, color);
    canvas_.fillRoundRect(cx + 3, cy - 9, 5, 18, 2, color);
  } else {
    canvas_.fillTriangle(cx - 6, cy - 10, cx - 6, cy + 10, cx + 10, cy, color);
  }
}

void Renderer::drawRing(float progress) {
  const uint16_t limit = static_cast<uint16_t>(constrain(progress, 0.0f, 1.0f) * 65535);
  for (const RingPixel &p : ring_) {
    if (p.position <= limit) blendPx(p.index, theme::RING_DONE, p.coverage * RING_DONE_ALPHA / 255);
    else blendPx(p.index, theme::RING_TODO, p.coverage * RING_TODO_ALPHA / 255);
  }
  // Round knob at the playhead.
  const float a = progress * 2 * PI;
  const float kx = CENTER + sinf(a) * RING_MID;
  const float ky = CENTER - cosf(a) * RING_MID;
  const int reach = static_cast<int>(ceilf(KNOB_RADIUS));
  for (int y = static_cast<int>(ky) - reach; y <= static_cast<int>(ky) + reach; ++y) {
    for (int x = static_cast<int>(kx) - reach; x <= static_cast<int>(kx) + reach; ++x) {
      if (x < 0 || y < 0 || x >= SCREEN || y >= SCREEN) continue;
      const float coverage = constrain(KNOB_RADIUS - hypotf(x + 0.5f - kx, y + 0.5f - ky), 0.0f, 1.0f);
      blendPx(y * SCREEN + x, theme::RING_DONE, static_cast<int>(coverage * 256));
    }
  }
}

void Renderer::drawTitle(int offset) {
  canvas_.setFont(&fonts::DejaVu18);
  canvas_.setTextColor(theme::TEXT);
  if (!titleScrolls()) {
    canvas_.setTextDatum(middle_center);
    canvas_.drawString(title_, CENTER, TITLE_Y);
    return;
  }
  canvas_.setTextDatum(middle_left);
  canvas_.setClipRect(TITLE_X, TITLE_TOP, TITLE_W, TITLE_H);
  canvas_.drawString(title_, TITLE_X - offset, TITLE_Y);
  canvas_.drawString(title_, TITLE_X - offset + titleWidth_ + MARQUEE_GAP, TITLE_Y);
  canvas_.clearClipRect();
  // Soft edges: blend the text back into the background near the clip borders.
  for (int y = TITLE_TOP; y < TITLE_TOP + TITLE_H; ++y) {
    for (int k = 0; k < TITLE_FADE; ++k) {
      const int fade = 256 - (k + 1) * 256 / (TITLE_FADE + 1);  // weight of the background
      const int right = y * SCREEN + TITLE_X + TITLE_W - 1 - k;
      frame_[right] = __builtin_bswap16(mix(__builtin_bswap16(frame_[right]), __builtin_bswap16(under_[right]), fade));
      if (offset > 0) {
        const int left = y * SCREEN + TITLE_X + k;
        frame_[left] = __builtin_bswap16(mix(__builtin_bswap16(frame_[left]), __builtin_bswap16(under_[left]), fade));
      }
    }
  }
}

// Pushes the canvas rotated around the center. The rotated square always covers the round panel.
void Renderer::present() {
  if (angle_ == 0) canvas_.pushSprite(0, 0);
  else canvas_.pushRotateZoomWithAA(&lcd_, angle_, 1.0f, 1.0f);
}

// ---------------------------------------------------------------- screens

void Renderer::drawStatus(const PlayerState &state) {
  drawVignette();
  canvas_.setTextDatum(middle_center);
  canvas_.setFont(&fonts::DejaVu18);
  canvas_.setTextColor(theme::TEXT);
  canvas_.drawString(fitText(asciiFold(state.statusTitle), STATUS_W), CENTER, STATUS_TITLE_Y);
  if (state.statusDetail.length()) {
    canvas_.setFont(&fonts::DejaVu12);
    canvas_.setTextColor(theme::TEXT_DIM);
    canvas_.drawString(fitText(asciiFold(state.statusDetail), STATUS_W), CENTER, STATUS_DETAIL_Y);
  }
  present();
}

void Renderer::setTrack(const PlayerState &state, int pressedButton) {
  pressedButton_ = pressedButton;
  canvas_.setFont(&fonts::DejaVu18);
  title_ = asciiFold(state.name);
  titleWidth_ = canvas_.textWidth(title_);

  if (state.art) memcpy(frame_, state.art, FRAME_PIXELS * sizeof(uint16_t));
  else drawVignette();
  drawScrim();
  drawGlassButtons();
  for (int button = 0; button < 3; ++button) drawIcon(button, state.playback.playing);
  const bool hasNotice = state.notice.length() > 0;
  canvas_.setTextDatum(middle_center);
  canvas_.setFont(&fonts::DejaVu12);
  canvas_.setTextColor(hasNotice ? theme::NOTICE : theme::TEXT_DIM);
  canvas_.drawString(fitText(asciiFold(hasNotice ? state.notice : state.artist), ARTIST_W), CENTER, ARTIST_Y);
  memcpy(under_, frame_, FRAME_PIXELS * sizeof(uint16_t));
}

void Renderer::drawFrame(float progress, unsigned long marqueeElapsedMs) {
  memcpy(frame_, under_, FRAME_PIXELS * sizeof(uint16_t));
  drawRing(progress);
  drawTitle(marqueeOffset(titleWidth_, marqueeElapsedMs));
  present();
}
