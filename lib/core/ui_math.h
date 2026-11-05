#pragma once

#include <math.h>

#include "layout.h"

// Horizontal scroll of a title wider than TITLE_W: hold at the start, scroll one full loop, repeat.
inline int marqueeOffset(int textWidth, unsigned long elapsedMs) {
  using namespace layout;
  if (textWidth <= TITLE_W) return 0;
  const unsigned long loopPx = textWidth + MARQUEE_GAP;
  const unsigned long scrollMs = loopPx * 1000 / MARQUEE_PX_PER_SEC;
  const unsigned long phase = elapsedMs % (MARQUEE_PAUSE_MS + scrollMs);
  return phase < MARQUEE_PAUSE_MS ? 0 : (phase - MARQUEE_PAUSE_MS) * MARQUEE_PX_PER_SEC / 1000;
}

// The touch zones are wider than the drawn buttons. Above the button row, the lyrics button has a
// round zone of its own and anywhere else toggles play/pause.
inline layout::Button buttonAt(int x, int y) {
  using namespace layout;
  const int dx = x - LYRICS_BUTTON.x;
  const int dy = y - LYRICS_BUTTON.y;
  const int reach = LYRICS_BUTTON.r + LYRICS_TOUCH_MARGIN;
  if (dx * dx + dy * dy <= reach * reach) return BUTTON_LYRICS;
  if (y < BUTTONS[1].y - BUTTONS[1].r - 8) return BUTTON_PLAY_PAUSE;
  if (x < (BUTTONS[0].x + BUTTONS[1].x) / 2) return BUTTON_PREV;
  return x < (BUTTONS[1].x + BUTTONS[2].x) / 2 ? BUTTON_PLAY_PAUSE : BUTTON_NEXT;
}

// Panel coordinates -> UI coordinates, for a UI drawn rotated `angleDeg` clockwise around the center.
inline void unrotate(float angleDeg, int &x, int &y) {
  using namespace layout;
  const float a = angleDeg * 0.0174532925f;
  const float dx = x + 0.5f - CENTER;
  const float dy = y + 0.5f - CENTER;
  x = static_cast<int>(floorf(CENTER + cosf(a) * dx + sinf(a) * dy));
  y = static_cast<int>(floorf(CENTER - sinf(a) * dx + cosf(a) * dy));
}

// Width of the horizontal band [top, bottom] that fits inside a circle of `radius` around the
// screen center: the band is as narrow as its edge farthest from the center.
inline int bandWidth(float radius, int top, int bottom) {
  using namespace layout;
  const float dy = fmaxf(fabsf(static_cast<float>(top - CENTER)), fabsf(static_cast<float>(bottom - CENTER)));
  return dy >= radius ? 0 : static_cast<int>(2 * sqrtf(radius * radius - dy * dy));
}

// Wraps an angle to [-180, 180] degrees.
inline float wrapDegrees(float deg) { return remainderf(deg, 360.0f); }
