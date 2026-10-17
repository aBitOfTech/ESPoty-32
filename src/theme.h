#pragma once

#include <stdint.h>

// Colors (RGB565) and on-screen texts. The bundled fonts are ASCII-only; asciiFold() strips accents.
namespace theme {
constexpr uint16_t TEXT = 0xFFFF;
constexpr uint16_t TEXT_DIM = 0xC618;  // artist and status detail
constexpr uint16_t NOTICE = 0xFD20;    // command errors, shown in place of the artist
constexpr uint16_t ICON = 0xFFFF;
constexpr uint16_t RING_DONE = 0xFFFF;
constexpr uint16_t RING_TODO = 0x0000;
constexpr uint16_t SCRIM = 0x0000;
constexpr uint8_t BACKGROUND_RGB[3] = {0x1C, 0x1C, 0x24};  // center of the vignette without a cover
}  // namespace theme

namespace texts {
constexpr const char *CONNECTING = "Conectando WiFi";
constexpr const char *SPOTIFY_MISSING = "Falta Spotify";
constexpr const char *SPOTIFY_MISSING_DETAIL = "configura secrets.h";
constexpr const char *NOTHING_PLAYING = "Nada reproduciéndose";
constexpr const char *NO_PERMISSION = "Sin permiso: renueva el token";
constexpr const char *SPOTIFY_ERROR = "Spotify error ";
constexpr const char *OUT_OF_MEMORY = "Sin memoria";
}  // namespace texts
