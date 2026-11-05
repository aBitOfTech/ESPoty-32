#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <utility>

#include "app_config.h"
#include "lyrics.h"
#include "playback.h"

// Not a Command: Command indexes SpotifyClient::send's paths[].
enum Command : uint8_t { CMD_PREV, CMD_PLAY, CMD_PAUSE, CMD_NEXT };

using LyricsData = lyrics::Synced<LYRICS_MAX_LINES, LYRICS_MAX_TEXT>;

enum LyricsStatus : uint8_t {
  LYRICS_PENDING,  // not looked up yet for this track (or the lookup is running)
  LYRICS_READY,
  LYRICS_NOT_FOUND,  // no entry, or an entry without synced lyrics
  LYRICS_INSTRUMENTAL,
  LYRICS_FAILED,  // network or parse error; not retried for this track
};

// What the UI shows. Written by the network task, copied by the render loop.
struct PlayerState {
  String trackId;  // empty: show statusTitle/statusDetail instead of a track
  String name;
  String artist;
  String notice;  // command error, shown in place of the artist
  String statusTitle;
  String statusDetail;
  Playback playback;
  unsigned long playingLockUntil = 0;  // ignore polled is_playing briefly after an optimistic toggle
  const uint16_t *art = nullptr;       // byte-swapped like the canvas, SCREEN x SCREEN
  bool lyricsWanted = false;           // written by the render loop: the lyrics view is open
  LyricsStatus lyricsStatus = LYRICS_PENDING;
  const LyricsData *lyrics = nullptr;  // for trackId when lyricsStatus is LYRICS_READY
  uint32_t version = 0;                // bumped whenever text, art or lyrics change
};

// The state shared between the network task (core 0) and the render loop (core 1), plus the queue of
// commands from the touch screen. The state is only reachable through with(), which holds the lock
// for the duration of the callback.
class PlayerStore {
 public:
  void begin();

  template <typename F>
  auto with(F f) -> decltype(f(std::declval<PlayerState &>())) {
    Lock lock(mutex_);
    return f(state_);
  }

  void post(Command command);  // never blocks; drops the command if the queue is full
  bool take(Command &command, TickType_t wait);

 private:
  class Lock {
   public:
    explicit Lock(SemaphoreHandle_t mutex) : mutex_(mutex) { xSemaphoreTake(mutex_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(mutex_); }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

   private:
    SemaphoreHandle_t mutex_;
  };

  PlayerState state_;
  SemaphoreHandle_t mutex_ = nullptr;
  QueueHandle_t commands_ = nullptr;
};
