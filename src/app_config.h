#pragma once

#include <stddef.h>

// Timing, limits and calibration. Pins are in include/pins.h, layout in lib/core/layout.h, colors and
// on-screen texts in theme.h.

constexpr unsigned long POLL_MS = 3000;             // player state poll interval
constexpr unsigned long FRAME_MS = 33;              // ~30 fps for the marquee and rotation
constexpr unsigned long PROGRESS_REDRAW_MS = 250;   // progress ring refresh while playing
constexpr unsigned long PRESS_FLASH_MS = 180;       // how long a tapped button stays lit
constexpr unsigned long OPTIMISTIC_LOCK_MS = 2500;  // ignore polled is_playing after a local toggle
constexpr int SKIP_POLL_ATTEMPTS = 6;               // polls after previous/next until the track changes
constexpr unsigned long SKIP_POLL_DELAY_MS = 150;

constexpr size_t COVER_MAX_BYTES = 300000;  // keeps the JPEG within PSRAM
constexpr unsigned long COVER_HTTP_TIMEOUT_MS = 10000;
constexpr unsigned long COVER_DOWNLOAD_MS = 15000;

// Synced lyrics from LRCLIB. The response also carries plainLyrics and a YAML copy of everything:
// ~4x the synced text (41 KB for a 148-line song). A lookup blocks the network task, and with it the
// buttons, while it runs: usually 1-2 s, at worst LYRICS_TIMEOUT_MS per step of up to two requests.
constexpr size_t LYRICS_MAX_BYTES = 96 * 1024;     // response body, in PSRAM
constexpr int LYRICS_MAX_LINES = 300;              // LRC lines kept per song
constexpr int LYRICS_MAX_TEXT = 16 * 1024;         // their folded text, in PSRAM (x2 buffers)
constexpr unsigned long LYRICS_TIMEOUT_MS = 5000;  // connect, TLS handshake and download, each
constexpr long LYRICS_OFFSET_MS = 0;               // added to the playback position; > 0 shows lines earlier

constexpr int NETWORK_TASK_STACK = 16384;

// Gravity-based rotation: the UI turns to stay upright while the board is held up, and keeps its
// last angle when the board lies within ~30 degrees of flat (gravity then says nothing about "up").
// If the UI turns the wrong way, negate IMU_ANGLE_SIGN; if it settles tilted, adjust IMU_ANGLE_OFFSET.
constexpr float IMU_ANGLE_SIGN = -1.0f;
constexpr float IMU_ANGLE_OFFSET = -90.0f;
constexpr float IMU_FLAT_RATIO = 0.5f;    // sin(30 deg)
constexpr float IMU_SMOOTHING = 0.3f;     // low-pass weight of each new sample
constexpr float IMU_DEADBAND_DEG = 1.0f;  // ignore smaller changes so a still board doesn't redraw
