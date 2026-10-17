#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "player_store.h"

// The parts of GET /v1/me/player the UI uses.
struct PlayerSnapshot {
  String trackId;
  String name;
  String artist;
  String imageUrl;  // smallest cover that still fills the panel
  bool playing = false;
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
};

enum class PollResult { Ok, NothingPlaying, NoCredentials, Failed };

// Spotify Web API over a kept-alive TLS connection: a fresh handshake costs ~1 s per request on the
// ESP32. Used only by the network task.
class SpotifyClient {
 public:
  void begin();
  PollResult fetchPlayer(PlayerSnapshot &out);
  int send(Command command);  // HTTP status, 2xx on success

 private:
  bool ensureToken();
  bool refreshToken();
  int call(const char *method, const String &url, String &body);
  int request(const char *method, const String &url);
  String readBody(int status);

  WiFiClientSecure client_;
  HTTPClient http_;  // kept alive with the client: ~HTTPClient() calls stop() on its socket
  String accessToken_;
};
