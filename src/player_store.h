#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include <utility>

#include "playback.h"

enum Command : uint8_t { CMD_PREV, CMD_PLAY, CMD_PAUSE, CMD_NEXT };

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
  uint32_t version = 0;                // bumped whenever text or art change
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
