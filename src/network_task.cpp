#include "network_task.h"

#include <WiFi.h>

#include "app_config.h"
#include "cover_loader.h"
#include "credentials.h"
#include "layout.h"
#include "psram.h"
#include "spotify_client.h"
#include "theme.h"

namespace {

class NetworkWorker {
 public:
  explicit NetworkWorker(PlayerStore &store) : store_(store) {}

  void allocate() {
    for (uint16_t *&buffer : artBuffers_) buffer = psramAlloc<uint16_t>(layout::SCREEN * layout::SCREEN);
  }

  void run() {
    spotify_.begin();
    covers_.begin();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(250));
    unsigned long nextPoll = 0;
    for (;;) {
      const long wait = static_cast<long>(nextPoll - millis());
      Command command;
      if (store_.take(command, wait > 0 ? pdMS_TO_TICKS(wait) : 0)) {
        runCommand(command);
        continue;
      }
      poll();
      nextPoll = millis() + POLL_MS;
    }
  }

 private:
  void poll() {
    PlayerSnapshot snapshot;
    switch (spotify_.fetchPlayer(snapshot)) {
      case PollResult::NoCredentials:
        return showStatus(texts::SPOTIFY_MISSING, texts::SPOTIFY_MISSING_DETAIL);
      case PollResult::NothingPlaying:
        return showStatus(texts::NOTHING_PLAYING);
      case PollResult::Failed:
        return;  // keep showing the last state
      case PollResult::Ok:
        break;
    }
    applySnapshot(snapshot);
    updateCover(snapshot);
  }

  void applySnapshot(const PlayerSnapshot &snapshot) {
    store_.with([&](PlayerState &s) {
      if (s.trackId != snapshot.trackId) {
        s.trackId = snapshot.trackId;
        s.name = snapshot.name;
        s.artist = snapshot.artist;
        s.notice = "";
        s.version++;
      }
      const unsigned long now = millis();
      s.playback.durationMs = snapshot.durationMs;
      s.playback.progressMs = snapshot.progressMs;
      s.playback.progressAt = now;
      if (static_cast<long>(now - s.playingLockUntil) >= 0) s.playback.playing = snapshot.playing;
    });
  }

  void updateCover(const PlayerSnapshot &snapshot) {
    if (snapshot.trackId == coverTrack_) return;
    coverTrack_ = snapshot.trackId;
    // Decode into whichever buffer isn't on screen, then swap.
    const uint16_t *shown = store_.with([](PlayerState &s) -> const uint16_t * { return s.art; });
    uint16_t *target = shown == artBuffers_[0] ? artBuffers_[1] : artBuffers_[0];
    const bool ok = target && covers_.load(snapshot.imageUrl, target);
    store_.with([&](PlayerState &s) {
      s.art = ok ? target : nullptr;
      s.version++;
    });
  }

  void showStatus(const char *title, const char *detail = "") {
    store_.with([&](PlayerState &s) {
      if (s.trackId.length() || s.statusTitle != title || s.statusDetail != detail) {
        s.trackId = "";
        s.statusTitle = title;
        s.statusDetail = detail;
        s.version++;
      }
    });
    coverTrack_ = "";
  }

  void runCommand(Command command) {
    const bool skip = command == CMD_PREV || command == CMD_NEXT;
    const int status = spotify_.send(command);
    const bool ok = status >= 200 && status < 300;
    const String notice = ok ? String() : commandError(status);
    store_.with([&](PlayerState &s) {
      if (notice != s.notice) {
        s.notice = notice;
        s.version++;
      }
      if (!ok && !skip) {  // undo the optimistic toggle
        s.playback.playing = command == CMD_PAUSE;
        s.playingLockUntil = 0;
      }
    });
    if (ok && skip) waitForTrackChange();
  }

  static String commandError(int status) {
    if (status == HTTP_CODE_UNAUTHORIZED || status == HTTP_CODE_FORBIDDEN) return texts::NO_PERMISSION;
    return texts::SPOTIFY_ERROR + String(status);
  }

  // Spotify switches tracks asynchronously; poll briefly until the new track shows up.
  void waitForTrackChange() {
    const String before = coverTrack_;
    for (int i = 0; i < SKIP_POLL_ATTEMPTS && coverTrack_ == before; ++i) {
      if (i) vTaskDelay(pdMS_TO_TICKS(SKIP_POLL_DELAY_MS));
      poll();
    }
  }

  PlayerStore &store_;
  SpotifyClient spotify_;
  CoverLoader covers_;
  String coverTrack_;  // track whose cover is in (or failed to load into) the shown buffer
  uint16_t *artBuffers_[2] = {nullptr, nullptr};
};

void taskEntry(void *worker) { static_cast<NetworkWorker *>(worker)->run(); }

}  // namespace

void startNetworkTask(PlayerStore &store) {
  static NetworkWorker worker(store);
  worker.allocate();
  xTaskCreatePinnedToCore(taskEntry, "spotify", NETWORK_TASK_STACK, &worker, 1, nullptr, 0);
}
