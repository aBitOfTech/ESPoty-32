# Spotify Cover for ESP32-S3 Touch LCD 1.28

**English** | [Español](README.es.md)

Spotify player for the round 240x240 GC9A01 display: it shows the album cover full screen, the title and artist of the current track, a progress ring along the edge and three touch buttons to control playback. It targets the **Waveshare ESP32-S3-Touch-LCD-1.28** (CST816S + GC9A01); no external wiring is needed.

## Features

- Album cover center-cropped and scaled to 240x240, with a dark gradient at the bottom so the text stays readable.
- Large title (scrolling horizontally when it doesn't fit) with the artist below.
- Progress ring around the edge that advances in real time between polls.
- "Glass" style buttons for previous, play/pause and next.
- The interface rotates to any angle following the board's position (QMI8658 accelerometer), so it always stays upright.
- Status screens: `Conectando WiFi` (connecting to Wi-Fi), `Falta Spotify` (missing credentials or invalid token) and `Nada reproduciéndose` (nothing playing).

## Requirements

- PlatformIO
- ESP32-S3 with PSRAM (the Waveshare ESP32-S3-Touch-LCD-1.28 has 16 MB flash and PSRAM)
- Python 3 (standard library only) to obtain the refresh token
- An app created in the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
- Spotify Premium account for the playback controls (the Spotify API rejects them on free accounts)
- USB-C data cable to power the board and upload the firmware

## Setup

1. In the Spotify Dashboard, add `http://127.0.0.1:8888/callback` as a Redirect URI.
2. Get the refresh token:

```sh
python tools/get_refresh_token.py CLIENT_ID CLIENT_SECRET
```

   The script opens the browser, waits for the authorization on `127.0.0.1:8888` and prints a `#define SPOTIFY_REFRESH_TOKEN "..."` line ready to paste.

3. Copy `include/secrets.example.h` to `include/secrets.h` and fill in Wi-Fi, credentials and the printed token. `secrets.h` is listed in `.gitignore`.
4. Connect the board and run:

```sh
pio run -t upload
```

If the automatic upload doesn't enter programming mode: hold **BOOT**, press and release **RESET**, release **BOOT** and run `pio run -t upload` again. **RESET** alone only restarts the board.

The refresh token is obtained once through the OAuth Authorization Code flow with the `user-read-currently-playing`, `user-read-playback-state` and `user-modify-playback-state` scopes. The access token is never stored in flash: it is renewed in RAM at boot and whenever Spotify answers 401.

If the buttons show `Sin permiso: renueva el token` (no permission: renew the token), the refresh token was generated without `user-modify-playback-state` (for example, with an older version of the script): run step 2 again and update `secrets.h`.

## Usage

- The bottom strip has three touch zones: previous, play/pause and next. The zones are wider than the drawn buttons.
- Tapping anywhere above the buttons also toggles play/pause.
- Turn the board and the interface turns with it to stay upright; the touch zones follow the rotation. Lying flat (within ~30° of horizontal), it keeps the last orientation.
- Play/pause is reflected on screen immediately; if Spotify rejects the command, the state is reverted and the error is shown in orange in place of the artist.

## How it works

- The player state (`GET /v1/me/player`) is polled every 3 seconds and right after each previous/next command.
- Networking (Wi-Fi, HTTPS and JPEG decoding) runs in a task on core 0; touch and drawing run in `loop()` on core 1, so the UI never freezes during requests.
- TLS connections to the API and to the cover CDN are kept alive to avoid a ~1 s handshake per request.
- Of the images Spotify offers, the smallest one that still covers 240 px is downloaded, and it is decoded into a double buffer in PSRAM.
- The accelerometer is read about 30 times per second; the gravity vector is filtered and the sprite is pushed rotated (`pushRotateZoomWithAA`) only when the angle changes by more than 1°. If the UI turns the wrong way or settles tilted, adjust `IMU_ANGLE_SIGN` / `IMU_ANGLE_OFFSET` in `src/app_config.h`.
- Everything is composed in a PSRAM sprite and pushed to the display in one go; the title is redrawn at ~30 fps only while it scrolls, and the ring about 4 times per second.

## Project structure

| Path | Contents |
|---|---|
| `src/main.cpp` | `setup()` and the render loop (core 1): touch, rotation and redraw scheduling |
| `src/network_task.*` | Network task (core 0): Wi-Fi, polling, commands and cover swaps |
| `src/spotify_client.*` | Spotify Web API: token refresh, player state, playback commands |
| `src/cover_loader.*` | Cover download, JPEG decoding and center-crop to 240x240 |
| `src/renderer.*`, `src/display.h` | Drawing: cover, gradient, title, glass buttons, progress ring, rotation |
| `src/player_store.*` | State shared between both cores (only accessible under its lock) and the command queue |
| `src/touch.*`, `src/orientation.*`, `src/i2c_bus.*` | CST816S touch, QMI8658 accelerometer and their shared I²C bus |
| `src/app_config.h`, `src/theme.h` | Timings, limits and calibration; colors and on-screen texts |
| `lib/core/` | Hardware-independent logic (color math, blur, text folding, layout, playback), covered by the tests |
| `test/test_core/` | Unit tests for `lib/core` |

### Tests

The `lib/core` logic is tested on the PC with Unity:

```sh
pio test -e native
```

This needs a host C++ compiler (gcc, e.g. MinGW-w64 on Windows) in `PATH`. `pio run` and `pio run -t upload` only build the firmware.

## Pins

The pins live in `include/pins.h`, the only file to change for another ESP32-S3 + GC9A01/CST816S board. They are the internal connections between the ESP32-S3 and the display/touch controller: SPI SCLK/MOSI/MISO/CS/DC/RST/BL = 10/11/12/9/8/14/2; I²C SDA/SCL and touch RST/IRQ = 6/7/13/5 (I²C addresses: CST816S `0x15`, QMI8658 accelerometer `0x6B` on the same bus). You don't need to wire them yourself. The onboard USB-C uses the board's USB-serial converter to upload the firmware.

## Known limitations

- The bundled fonts are ASCII-only: Latin accents are shown without diacritics (`é` → `e`), and emoji and other symbols are skipped.
- HTTPS uses `setInsecure()` to avoid storing a CA bundle in flash; for a device exposed to untrusted networks, certificate validation should be added.
- The cover is limited to 300 KB to avoid exhausting PSRAM; if the download or decoding fails, a dark background is shown.
- Only the first artist of each track is shown.- On-screen messages are in Spanish.
