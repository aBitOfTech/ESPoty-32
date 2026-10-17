#pragma once

#include <stddef.h>
#include <stdint.h>

// The bundled fonts are ASCII-only: fold Latin-1 accents (UTF-8 0xC3 xx) to plain letters, map the
// Spanish inverted marks to their plain form and skip symbols/emoji the font can't draw.
// Works with Arduino String and std::string.
template <typename Str>
Str asciiFold(const Str &s) {
  static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUY?saaaaaaaceeeeiiiidnooooo/ouuuuy?y";
  Str out;
  for (size_t i = 0; i < s.length(); ++i) {
    const uint8_t c = s[i];
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if ((c == 0xC3 || c == 0xC2) && i + 1 < s.length()) {
      const uint8_t next = s[++i];
      if (c == 0xC3 && next >= 0x80 && next <= 0xBF) out += latin1[next - 0x80];
      else if (next == 0xA1) out += '!';
      else if (next == 0xBF) out += '?';
    } else if (c >= 0xE0) {
      i += c >= 0xF0 ? 3 : 2;
    } else {
      ++i;
    }
  }
  return out;
}
