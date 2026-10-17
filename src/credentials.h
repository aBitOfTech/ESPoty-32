#pragma once

// Wi-Fi and Spotify credentials from include/secrets.h (see secrets.example.h). Without it the
// firmware still builds and shows "Falta Spotify".
#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define SPOTIFY_CLIENT_ID ""
#define SPOTIFY_CLIENT_SECRET ""
#define SPOTIFY_REFRESH_TOKEN ""
#endif
