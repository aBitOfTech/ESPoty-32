// Spotify player for the Waveshare ESP32-S3-Touch-LCD-1.28.
//
// Core 1 (this file): touch, accelerometer and drawing. Core 0 (network_task): Wi-Fi, Spotify and
// covers. They share state through PlayerStore.

#include <Arduino.h>

#include "app_config.h"
#include "i2c_bus.h"
#include "network_task.h"
#include "orientation.h"
#include "player_store.h"
#include "renderer.h"
#include "theme.h"
#include "touch.h"
#include "ui_math.h"

namespace {

PlayerStore store;
Renderer renderer;
Touch touch;
Orientation orientation;

// Render-loop-only state.
PlayerState view;  // last copy of the shared state
uint32_t seenVersion = UINT32_MAX;
unsigned long titleShownAt = 0;
int pressedButton = -1;
unsigned long pressedUntil = 0;

void handleTap(int x, int y) {
  if (!view.trackId.length()) return;
  unrotate(orientation.angle(), x, y);
  const layout::Button button = buttonAt(x, y);
  pressedButton = button;
  pressedUntil = millis() + PRESS_FLASH_MS;
  Command command = button == layout::BUTTON_PREV ? CMD_PREV : CMD_NEXT;
  if (button == layout::BUTTON_PLAY_PAUSE) {
    // Optimistic: flip play/pause (and freeze/resume the progress) before the network round trip.
    command = store.with([](PlayerState &s) -> Command {
      const unsigned long now = millis();
      s.playback.setPlaying(!s.playback.playing, now);
      s.playingLockUntil = now + OPTIMISTIC_LOCK_MS;
      return s.playback.playing ? CMD_PLAY : CMD_PAUSE;
    });
  }
  store.post(command);
}

// Copies the shared state when it changed; otherwise just the playback progress. Returns true when
// text or art changed.
bool syncView(unsigned long now) {
  return store.with([&](PlayerState &s) -> bool {
    if (s.version == seenVersion) {
      view.playback = s.playback;
      return false;
    }
    if (s.trackId != view.trackId || s.name != view.name) titleShownAt = now;
    view = s;
    seenVersion = s.version;
    return true;
  });
}

}  // namespace

void setup() {
  i2c::begin();
  touch.begin();
  orientation.begin();
  if (!renderer.begin()) {
    renderer.showError(texts::OUT_OF_MEMORY);
    for (;;) delay(1000);
  }
  store.begin();
  store.with([](PlayerState &s) { s.statusTitle = texts::CONNECTING; });
  startNetworkTask(store);
}

void loop() {
  uint16_t x = 0;
  uint16_t y = 0;
  if (touch.readTap(x, y)) handleTap(x, y);

  static unsigned long lastFrame = 0;
  static bool lastPressed = false;
  static bool lastPlaying = false;
  const unsigned long now = millis();
  const bool turned = orientation.update(now);
  if (turned) renderer.setAngle(orientation.angle());
  bool changed = syncView(now);

  if (!view.trackId.length()) {
    if (changed || turned) renderer.drawStatus(view);
    delay(10);
    return;
  }

  if (pressedButton >= 0 && static_cast<long>(now - pressedUntil) >= 0) pressedButton = -1;
  if ((pressedButton >= 0) != lastPressed || view.playback.playing != lastPlaying) changed = true;
  lastPressed = pressedButton >= 0;
  lastPlaying = view.playback.playing;
  if (changed) renderer.setTrack(view, pressedButton);

  // Redraw for a rotation or a scrolling title (every frame) or the progress ring (a few times per second).
  const unsigned long interval = renderer.titleScrolls() ? FRAME_MS : view.playback.playing ? PROGRESS_REDRAW_MS : ULONG_MAX;
  if (!changed && !turned && now - lastFrame < interval) {
    delay(5);
    return;
  }
  lastFrame = now;
  renderer.drawFrame(view.playback.fractionAt(now), now - titleShownAt);
}
