#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include "player_store.h"
#include "spotify_client.h"

// Looks up line-synced lyrics on LRCLIB (https://lrclib.net), a free third-party service without
// API keys. It has its own TLS client on purpose: requests through SpotifyClient carry the Spotify
// access token, which must never reach a third party. Used only by the network task.
class LyricsClient {
 public:
  void begin();
  // Looks up the track by artist, title, album and duration, then without the album if that finds
  // nothing. Fills `out` when the result is LYRICS_READY; otherwise `out` holds garbage.
  LyricsStatus fetch(const PlayerSnapshot &track, LyricsData *out);

 private:
  int get(const String &url);  // HTTP status with the body in body_, or <= 0 on a network error

  WiFiClientSecure client_;
  char *body_ = nullptr;  // LYRICS_MAX_BYTES + 1, in PSRAM
  size_t length_ = 0;
};
