#include "spotify_client.h"

#include <ArduinoJson.h>
#include <limits.h>

#include "credentials.h"
#include "layout.h"

namespace {
constexpr const char *TOKEN_URL = "https://accounts.spotify.com/api/token";
constexpr const char *PLAYER_URL = "https://api.spotify.com/v1/me/player";

// The response is several KB of available_markets etc.; keep only what the UI uses.
PollResult parsePlayer(const String &body, PlayerSnapshot &out) {
  JsonDocument filter;
  filter["is_playing"] = true;
  filter["progress_ms"] = true;
  filter["item"]["id"] = true;
  filter["item"]["name"] = true;
  filter["item"]["duration_ms"] = true;
  filter["item"]["artists"][0]["name"] = true;
  filter["item"]["album"]["images"][0]["url"] = true;
  filter["item"]["album"]["images"][0]["width"] = true;
  JsonDocument json;
  if (deserializeJson(json, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) {
    return PollResult::Failed;
  }
  JsonObject item = json["item"];
  if (item.isNull()) return PollResult::NothingPlaying;

  out.trackId = item["id"].as<String>();
  out.name = item["name"].as<String>();
  out.artist = item["artists"][0]["name"].as<String>();
  out.durationMs = item["duration_ms"] | 0;
  out.progressMs = json["progress_ms"] | 0;
  out.playing = json["is_playing"] | false;
  // Smallest image that still covers the panel (Spotify sends 640/300/64): far less to download and decode.
  int bestWidth = INT_MAX;
  for (JsonObject image : item["album"]["images"].as<JsonArray>()) {
    const int width = image["width"] | 0;
    if (width >= layout::SCREEN && width < bestWidth) {
      bestWidth = width;
      out.imageUrl = image["url"].as<String>();
    }
  }
  if (!out.imageUrl.length()) out.imageUrl = item["album"]["images"][0]["url"].as<String>();
  return PollResult::Ok;
}
}  // namespace

void SpotifyClient::begin() {
  client_.setInsecure();  // ponytail: CA bundle omitted; pin Spotify's CA for hostile networks.
}

PollResult SpotifyClient::fetchPlayer(PlayerSnapshot &out) {
  if (!ensureToken()) return PollResult::NoCredentials;
  String body;
  const int status = call("GET", PLAYER_URL, body);
  if (status == HTTP_CODE_NO_CONTENT) return PollResult::NothingPlaying;
  if (status != HTTP_CODE_OK) return PollResult::Failed;
  return parsePlayer(body, out);
}

int SpotifyClient::send(Command command) {
  static const char *const paths[] = {"/previous", "/play", "/pause", "/next"};
  const bool skip = command == CMD_PREV || command == CMD_NEXT;
  String body;
  return call(skip ? "POST" : "PUT", String(PLAYER_URL) + paths[command], body);
}

bool SpotifyClient::ensureToken() { return accessToken_.length() || refreshToken(); }

bool SpotifyClient::refreshToken() {
  if (!strlen(SPOTIFY_REFRESH_TOKEN)) return false;
  WiFiClientSecure client;
  client.setInsecure();  // ponytail: same trade-off as begin().
  HTTPClient http;
  if (!http.begin(client, TOKEN_URL)) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const String form = "grant_type=refresh_token&refresh_token=" + String(SPOTIFY_REFRESH_TOKEN) +
                      "&client_id=" + String(SPOTIFY_CLIENT_ID) + "&client_secret=" + String(SPOTIFY_CLIENT_SECRET);
  if (http.POST(form) != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  JsonDocument json;
  const bool ok = deserializeJson(json, http.getString()) == DeserializationError::Ok;
  if (ok) accessToken_ = json["access_token"].as<String>();
  http.end();
  return ok && accessToken_.length();
}

// Sends a request and reads its body. A 401 refreshes the token and retries once: a token without
// user-modify-playback-state keeps answering 401 "Permissions missing".
int SpotifyClient::call(const char *method, const String &url, String &body) {
  if (!ensureToken()) return HTTP_CODE_UNAUTHORIZED;
  int status = 0;
  for (int attempt = 0; attempt < 2; ++attempt) {
    status = request(method, url);
    body = readBody(status);
    http_.end();
    if (status != HTTP_CODE_UNAUTHORIZED || attempt > 0 || !refreshToken()) break;
  }
  return status;
}

// Sends over the kept-alive connection, reconnecting once if the server closed it while idle.
int SpotifyClient::request(const char *method, const String &url) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!http_.begin(client_, url)) return 0;
    http_.addHeader("Authorization", "Bearer " + accessToken_);
    // HTTPClient omits Content-Length for empty bodies; Spotify answers 411 without it.
    if (strcmp(method, "GET") != 0) http_.addHeader("Content-Length", "0");
    const int status = http_.sendRequest(method);
    if (status > 0) return status;
    http_.end();
    client_.stop();
  }
  return 0;
}

// Reads the body so the connection can be reused. 204/304 have none and no Content-Length:
// getString() would then wait for the server to close the kept-alive socket, i.e. forever.
String SpotifyClient::readBody(int status) {
  return status > 0 && status != HTTP_CODE_NO_CONTENT && status != HTTP_CODE_NOT_MODIFIED ? http_.getString() : String();
}
