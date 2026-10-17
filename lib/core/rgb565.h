#pragma once

#include <stdint.h>

// RGB565 color math. These work on native RGB565; the canvas stores pixels byte-swapped, so callers
// swap with __builtin_bswap16 when reading or writing it.
namespace rgb565 {

inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// Expands to 8 bits per channel, replicating the high bits so white stays 255.
inline void unpack(uint16_t c, int &r, int &g, int &b) {
  r = (c >> 8) & 0xF8;
  g = (c >> 3) & 0xFC;
  b = (c << 3) & 0xF8;
  r |= r >> 5;
  g |= g >> 6;
  b |= b >> 5;
}

inline uint16_t pack(int r, int g, int b) {
  r = clamp255(r);
  g = clamp255(g);
  b = clamp255(b);
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// alpha 0..256 is the weight of `top`.
inline uint16_t mix(uint16_t base, uint16_t top, int alpha) {
  int r0, g0, b0, r1, g1, b1;
  unpack(base, r0, g0, b0);
  unpack(top, r1, g1, b1);
  return pack(r0 + (((r1 - r0) * alpha) >> 8), g0 + (((g1 - g0) * alpha) >> 8), b0 + (((b1 - b0) * alpha) >> 8));
}

}  // namespace rgb565
