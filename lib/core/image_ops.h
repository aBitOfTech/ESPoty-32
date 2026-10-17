#pragma once

#include <stdint.h>

#include "rgb565.h"

namespace image {

inline int clampIndex(int v, int size) { return v < 0 ? 0 : v >= size ? size - 1 : v; }

// Two-pass box blur of one 8-bit plane, in place; `tmp` must hold w * h bytes.
inline void boxBlur(uint8_t *plane, uint8_t *tmp, int w, int h, int r) {
  const int scale = 65536 / (2 * r + 1);  // multiply instead of dividing per pixel
  for (int y = 0; y < h; ++y) {
    const uint8_t *row = plane + y * w;
    int sum = 0;
    for (int i = -r; i <= r; ++i) sum += row[clampIndex(i, w)];
    for (int x = 0; x < w; ++x) {
      tmp[y * w + x] = (sum * scale) >> 16;
      sum += row[clampIndex(x + r + 1, w)] - row[clampIndex(x - r, w)];
    }
  }
  for (int x = 0; x < w; ++x) {
    int sum = 0;
    for (int i = -r; i <= r; ++i) sum += tmp[clampIndex(i, h) * w + x];
    for (int y = 0; y < h; ++y) {
      plane[y * w + x] = (sum * scale) >> 16;
      sum += tmp[clampIndex(y + r + 1, h) * w + x] - tmp[clampIndex(y - r, h) * w + x];
    }
  }
}

// Samples a native RGB565 image at fractional coordinates, clamped to its edges.
inline uint16_t sampleBilinear(const uint16_t *src, int w, int h, float sx, float sy) {
  sx = sx < 0 ? 0 : sx > w - 1.0f ? w - 1.0f : sx;
  sy = sy < 0 ? 0 : sy > h - 1.0f ? h - 1.0f : sy;
  const int x0 = static_cast<int>(sx);
  const int y0 = static_cast<int>(sy);
  const int x1 = x0 + 1 < w ? x0 + 1 : w - 1;
  const int y1 = y0 + 1 < h ? y0 + 1 : h - 1;
  const int fx = static_cast<int>((sx - x0) * 256);
  const int fy = static_cast<int>((sy - y0) * 256);
  return rgb565::mix(rgb565::mix(src[y0 * w + x0], src[y0 * w + x1], fx),
                     rgb565::mix(src[y1 * w + x0], src[y1 * w + x1], fx), fy);
}

}  // namespace image
