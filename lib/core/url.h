#pragma once

#include <stddef.h>
#include <stdint.h>

// Percent-encodes a UTF-8 string for a URL query value: everything except letters, digits and -_.~
// (so accents, spaces, '&' and '/' are escaped). Works with Arduino String and std::string.
template <typename Str>
Str urlEncode(const Str &s) {
  static const char hex[] = "0123456789ABCDEF";
  Str out;
  for (size_t i = 0; i < s.length(); ++i) {
    const uint8_t c = s[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
        c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 0x0F];
    }
  }
  return out;
}
