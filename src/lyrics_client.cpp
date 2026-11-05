#include "lyrics_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "app_config.h"
#include "psram.h"
#include "url.h"

namespace {
constexpr const char *LYRICS_URL = "https://lrclib.net/api/get";
// LRCLIB asks clients to identify themselves.
constexpr const char *USER_AGENT = "ESPoty-32 (https://github.com/aBitOfTech/ESPoty-32)";
}  // namespace

void LyricsClient::begin() {
  client_.setInsecure();  // ponytail: same trade-off as SpotifyClient::begin().
  client_.setHandshakeTimeout(LYRICS_TIMEOUT_MS / 1000);  // the default is 120 s
  body_ = psramAlloc<char>(LYRICS_MAX_BYTES + 1);
}

LyricsStatus LyricsClient::fetch(const PlayerSnapshot &track, LyricsData *out) {
  if (!body_ || !out) return LYRICS_FAILED;
  if (!track.name.length() || !track.artist.length()) return LYRICS_NOT_FOUND;
  const String query = String(LYRICS_URL) + "?artist_name=" + urlEncode(track.artist) +
                       "&track_name=" + urlEncode(track.name) +
                       "&duration=" + String((track.durationMs + 500) / 1000);  // LRCLIB allows +-2 s
  int status = HTTP_CODE_NOT_FOUND;
  if (track.album.length()) status = get(query + "&album_name=" + urlEncode(track.album));
  if (status == HTTP_CODE_NOT_FOUND) status = get(query);  // Spotify and LRCLIB often name albums differently
  if (status == HTTP_CODE_NOT_FOUND) return LYRICS_NOT_FOUND;
  if (status != HTTP_CODE_OK) return LYRICS_FAILED;

  // Keep only what the view uses; plainLyrics and lyricsfile are skipped while parsing.
  JsonDocument filter;
  filter["syncedLyrics"] = true;
  filter["instrumental"] = true;
  JsonDocument json;
  if (deserializeJson(json, static_cast<const char *>(body_), length_, DeserializationOption::Filter(filter)) !=
      DeserializationError::Ok) {
    return LYRICS_FAILED;
  }
  if (json["instrumental"] | false) return LYRICS_INSTRUMENTAL;
  const char *lrc = json["syncedLyrics"];  // null when the entry only has plain lyrics
  return lrc && out->parse(lrc) ? LYRICS_READY : LYRICS_NOT_FOUND;
}

// HTTP/1.0 so the body comes unchunked and ends when the server closes the connection: LRCLIB
// answers HTTP/1.1 requests with Transfer-Encoding: chunked and no Content-Length, and the raw stream
// would then carry the chunk markers. One request per connection is fine: lookups are rare.
int LyricsClient::get(const String &url) {
  length_ = 0;
  HTTPClient http;
  http.useHTTP10(true);
  http.setUserAgent(USER_AGENT);
  http.setConnectTimeout(LYRICS_TIMEOUT_MS);
  http.setTimeout(LYRICS_TIMEOUT_MS);
  const char *headers[] = {"Transfer-Encoding"};
  http.collectHeaders(headers, 1);
  if (!http.begin(client_, url)) return 0;
  const int status = http.GET();
  const int size = http.getSize();  // -1 without Content-Length
  bool ok = status == HTTP_CODE_OK && http.header("Transfer-Encoding").length() == 0 &&
            size <= static_cast<int>(LYRICS_MAX_BYTES);
  if (ok) {
    WiFiClient &stream = http.getStream();
    const unsigned long deadline = millis() + LYRICS_TIMEOUT_MS;
    bool closed = false;
    while ((size < 0 || length_ < static_cast<size_t>(size)) && static_cast<long>(millis() - deadline) < 0) {
      const int available = stream.available();
      if (available <= 0) {
        if (!stream.connected()) {
          closed = true;
          break;
        }
        delay(2);
        continue;
      }
      if (length_ >= LYRICS_MAX_BYTES) break;  // more data than fits: too big
      const size_t room = LYRICS_MAX_BYTES - length_;
      const int read = stream.read(reinterpret_cast<uint8_t *>(body_) + length_,
                                   static_cast<size_t>(available) < room ? available : room);
      if (read > 0) length_ += read;
    }
    ok = size >= 0 ? length_ == static_cast<size_t>(size) : closed;  // complete, not cut by the deadline or cap
  }
  http.end();
  client_.stop();
  body_[length_] = '\0';
  return ok ? status : status == HTTP_CODE_OK ? -1 : status;
}
