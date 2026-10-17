#pragma once

#include <stdint.h>

// Track progress as last reported by Spotify, extrapolated locally between polls.
struct Playback {
  bool playing = false;
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
  unsigned long progressAt = 0;  // millis() when progressMs was current

  uint32_t positionAt(unsigned long now) const {
    const uint32_t pos = progressMs + (playing ? now - progressAt : 0);
    return durationMs && pos > durationMs ? durationMs : pos;
  }

  float fractionAt(unsigned long now) const {
    return durationMs ? static_cast<float>(positionAt(now)) / durationMs : 0.0f;
  }

  // Pauses or resumes at `now` without a jump in the position.
  void setPlaying(bool value, unsigned long now) {
    progressMs = positionAt(now);
    progressAt = now;
    playing = value;
  }
};
