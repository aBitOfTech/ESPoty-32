#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LovyanGFX.hpp>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <math.h>
#include <vector>

#include "pins.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define SPOTIFY_CLIENT_ID ""
#define SPOTIFY_CLIENT_SECRET ""
#define SPOTIFY_REFRESH_TOKEN ""
#endif

class Display : public lgfx::LGFX_Device {
  lgfx::Panel_GC9A01 _panel;
  lgfx::Bus_SPI _bus;

 public:
  Display() {
    auto bus = _bus.config();
    bus.spi_host = SPI2_HOST;
    bus.spi_mode = 0;
    bus.freq_write = 80000000;
    bus.freq_read = 16000000;
    bus.pin_sclk = pins::LCD_SCLK;
    bus.pin_mosi = pins::LCD_MOSI;
    bus.pin_miso = pins::LCD_MISO;
    bus.pin_dc = pins::LCD_DC;
    _bus.config(bus);
    _panel.setBus(&_bus);

    auto panel = _panel.config();
    panel.pin_cs = pins::LCD_CS;
    panel.pin_rst = pins::LCD_RST;
    panel.pin_busy = -1;
    panel.memory_width = 240;
    panel.memory_height = 240;
    panel.panel_width = 240;
    panel.panel_height = 240;
    panel.offset_x = 0;
    panel.offset_y = 0;
    panel.offset_rotation = 0;
    panel.dummy_read_pixel = 8;
    panel.dummy_read_bits = 1;
    panel.readable = false;
    panel.invert = true;
    panel.rgb_order = false;
    panel.dlen_16bit = false;
    panel.bus_shared = true;
    _panel.config(panel);
    setPanel(&_panel);
  }
};

// Round 240px panel, Apple Music style: the cover fills the screen, a dark gradient at the bottom
// carries the title (scrolling when it doesn't fit) and artist, three glass buttons sit below,
// and a progress ring runs along the edge.
constexpr int SCREEN = 240;
constexpr int CENTER = SCREEN / 2;
constexpr int SCRIM_TOP = 70;
constexpr int SCRIM_MAX = 215;
constexpr int TITLE_Y = 140;
constexpr int TITLE_X = 26;
constexpr int TITLE_W = 188;
constexpr int TITLE_FADE = 14;
constexpr int ARTIST_Y = 161;
constexpr int ARTIST_W = 176;
constexpr int MARQUEE_GAP = 48;
constexpr int MARQUEE_PX_PER_SEC = 30;
constexpr unsigned long MARQUEE_PAUSE_MS = 2000;
struct GlassButton {
  int x, y, r;
};
constexpr GlassButton BUTTONS[3] = {{72, 198, 19}, {120, 198, 22}, {168, 198, 19}};
constexpr float RING_INNER = 113.5f;
constexpr float RING_OUTER = 118.5f;
constexpr float RING_MID = (RING_INNER + RING_OUTER) / 2;
constexpr unsigned long PROGRESS_REDRAW_MS = 250;
constexpr unsigned long FRAME_MS = 33;
constexpr unsigned long POLL_MS = 3000;
// The glass blur runs at half resolution over the button row plus a margin.
constexpr int BLUR_MARGIN = 8;
constexpr int BLUR_X = BUTTONS[0].x - BUTTONS[0].r - BLUR_MARGIN;
constexpr int BLUR_Y = BUTTONS[1].y - BUTTONS[1].r - BLUR_MARGIN;
constexpr int BLUR_W = (BUTTONS[2].x + BUTTONS[2].r + BLUR_MARGIN - BLUR_X) / 2;
constexpr int BLUR_H = (BUTTONS[1].y + BUTTONS[1].r + BLUR_MARGIN - BLUR_Y) / 2;

enum Command : uint8_t { CMD_PREV, CMD_PLAY, CMD_PAUSE, CMD_NEXT };

Display lcd;
LGFX_Sprite canvas(&lcd);
uint16_t *frame = nullptr;  // canvas buffer; 16bpp sprites store byte-swapped RGB565
uint16_t *under = nullptr;  // everything except the ring and the title, rebuilt only when it changes

// Persistent TLS connections (one per host): a fresh handshake costs ~1 s per request on the ESP32.
// The HTTPClients are global too: ~HTTPClient() calls stop() on its socket.
WiFiClientSecure apiClient;
WiFiClientSecure cdnClient;
HTTPClient apiHttp;
HTTPClient cdnHttp;

// State shared between the network task (core 0) and the render loop (core 1), guarded by stateLock.
struct PlayerState {
  String trackId;  // empty: show statusTitle/statusDetail instead of a track
  String name;
  String artist;
  String notice;
  String statusTitle;
  String statusDetail;
  bool playing = false;
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
  unsigned long progressAt = 0;        // millis() when progressMs was current
  unsigned long playingLockUntil = 0;  // ignore polled is_playing briefly after an optimistic toggle
  const uint16_t *art = nullptr;       // byte-swapped like the canvas, SCREEN x SCREEN
  uint32_t version = 0;                // bumped whenever text or art change
};
SemaphoreHandle_t stateLock;
QueueHandle_t commands;
PlayerState shared;
uint16_t *artBuffers[2] = {nullptr, nullptr};

// Network-task-only state.
std::vector<uint8_t> cover;
uint16_t *decoded = nullptr;
int decodedW = 0;
int decodedH = 0;
String accessToken;
String coverTrack;

// Render-loop-only state.
PlayerState view;
uint32_t seenVersion = UINT32_MAX;
String title;
int titleWidth = 0;
String artistLine;
unsigned long titleShownAt = 0;
int pressedButton = -1;
unsigned long pressedUntil = 0;
uint8_t scrimAlpha[SCREEN];
uint8_t blurPlanes[4][BLUR_W * BLUR_H];
struct RingPixel {
  uint16_t index;
  uint16_t position;  // 0..65535 clockwise from 12 o'clock
  uint8_t coverage;
};
std::vector<RingPixel> ring;

template <typename T>
T *psramAlloc(size_t count) {
  void *p = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return static_cast<T *>(p ? p : malloc(count * sizeof(T)));
}

inline void unpack(uint16_t c, int &r, int &g, int &b) {
  r = (c >> 8) & 0xF8;
  g = (c >> 3) & 0xFC;
  b = (c << 3) & 0xF8;
  r |= r >> 5;
  g |= g >> 6;
  b |= b >> 5;
}

inline uint16_t pack(int r, int g, int b) {
  r = constrain(r, 0, 255);
  g = constrain(g, 0, 255);
  b = constrain(b, 0, 255);
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// alpha 0..256 is the weight of `top`.
inline uint16_t mix(uint16_t base, uint16_t top, int alpha) {
  int r0, g0, b0, r1, g1, b1;
  unpack(base, r0, g0, b0);
  unpack(top, r1, g1, b1);
  return pack(r0 + (((r1 - r0) * alpha) >> 8), g0 + (((g1 - g0) * alpha) >> 8), b0 + (((b1 - b0) * alpha) >> 8));
}

inline void blendPx(int i, uint16_t c, int alpha) {
  if (alpha <= 0) return;
  frame[i] = __builtin_bswap16(alpha >= 256 ? c : mix(__builtin_bswap16(frame[i]), c, alpha));
}

// ---------------------------------------------------------------- text

// The bundled fonts are ASCII-only; fold Latin-1 accents (UTF-8 0xC3 xx) to plain letters.
String asciiFold(const String &s) {
  static const char latin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUY?saaaaaaaceeeeiiiidnooooo/ouuuuy?y";
  String out;
  for (size_t i = 0; i < s.length(); ++i) {
    const uint8_t c = s[i];
    if (c < 0x80) {
      out += static_cast<char>(c);
    } else if ((c == 0xC3 || c == 0xC2) && i + 1 < s.length()) {
      const uint8_t next = s[++i];
      if (c == 0xC3 && next >= 0x80 && next <= 0xBF) out += latin1[next - 0x80];
      else if (next == 0xA1) out += '!';
      else if (next == 0xBF) out += '?';
    } else if (c >= 0xE0) {
      i += c >= 0xF0 ? 3 : 2;  // skip symbols/emoji the font can't draw
    } else {
      ++i;
    }
  }
  return out;
}

// Uses the canvas' current font.
String fitText(const String &text, int maxWidth) {
  if (canvas.textWidth(text) <= maxWidth) return text;
  String out = text;
  while (out.length() && canvas.textWidth(out + "...") > maxWidth) out.remove(out.length() - 1);
  return out + "...";
}

void layoutText() {
  canvas.setFont(&fonts::DejaVu18);
  title = asciiFold(view.name);
  titleWidth = canvas.textWidth(title);
  canvas.setFont(&fonts::DejaVu12);
  artistLine = fitText(asciiFold(view.notice.length() ? view.notice : view.artist), ARTIST_W);
}

// Horizontal scroll of a title wider than TITLE_W: hold at the start, scroll one full loop, repeat.
int marqueeOffset(unsigned long now) {
  if (titleWidth <= TITLE_W) return 0;
  const unsigned long loopPx = titleWidth + MARQUEE_GAP;
  const unsigned long scrollMs = loopPx * 1000 / MARQUEE_PX_PER_SEC;
  const unsigned long phase = (now - titleShownAt) % (MARQUEE_PAUSE_MS + scrollMs);
  return phase < MARQUEE_PAUSE_MS ? 0 : (phase - MARQUEE_PAUSE_MS) * MARQUEE_PX_PER_SEC / 1000;
}

// ---------------------------------------------------------------- drawing

void drawVignette() {
  for (int y = 0; y < SCREEN; ++y) {
    for (int x = 0; x < SCREEN; ++x) {
      const float dx = (x - CENTER) / 120.0f;
      const float dy = (y - CENTER) / 120.0f;
      const float shade = 1.0f - 0.5f * (dx * dx + dy * dy);
      frame[y * SCREEN + x] = __builtin_bswap16(pack(0x1C * shade, 0x1C * shade, 0x24 * shade));
    }
  }
}

void drawScrim() {
  for (int y = SCRIM_TOP; y < SCREEN; ++y) {
    uint16_t *row = frame + y * SCREEN;
    const int alpha = scrimAlpha[y];
    for (int x = 0; x < SCREEN; ++x) row[x] = __builtin_bswap16(mix(__builtin_bswap16(row[x]), TFT_BLACK, alpha));
  }
}

void boxBlur(uint8_t *plane, uint8_t *tmp, int w, int h, int r) {
  const int scale = 65536 / (2 * r + 1);  // multiply instead of dividing per pixel
  for (int y = 0; y < h; ++y) {
    const uint8_t *row = plane + y * w;
    int sum = 0;
    for (int i = -r; i <= r; ++i) sum += row[constrain(i, 0, w - 1)];
    for (int x = 0; x < w; ++x) {
      tmp[y * w + x] = (sum * scale) >> 16;
      sum += row[min(x + r + 1, w - 1)] - row[max(x - r, 0)];
    }
  }
  for (int x = 0; x < w; ++x) {
    int sum = 0;
    for (int i = -r; i <= r; ++i) sum += tmp[constrain(i, 0, h - 1) * w + x];
    for (int y = 0; y < h; ++y) {
      plane[y * w + x] = (sum * scale) >> 16;
      sum += tmp[min(y + r + 1, h - 1) * w + x] - tmp[max(y - r, 0) * w + x];
    }
  }
}

// Liquid-glass buttons: blur what's behind, lift it toward white, add a specular rim
// (bright on top, faint below) and a glow on the pressed one.
void drawGlassButtons() {
  for (int y = 0; y < BLUR_H; ++y) {
    const uint16_t *row0 = frame + (BLUR_Y + 2 * y) * SCREEN + BLUR_X;
    const uint16_t *row1 = row0 + SCREEN;
    for (int x = 0; x < BLUR_W; ++x) {
      const uint16_t quad[4] = {row0[2 * x], row0[2 * x + 1], row1[2 * x], row1[2 * x + 1]};
      int r = 0, g = 0, b = 0;
      for (uint16_t v : quad) {
        v = __builtin_bswap16(v);
        r += (v >> 8) & 0xF8;
        g += (v >> 3) & 0xFC;
        b += (v << 3) & 0xF8;
      }
      blurPlanes[0][y * BLUR_W + x] = r >> 2;
      blurPlanes[1][y * BLUR_W + x] = g >> 2;
      blurPlanes[2][y * BLUR_W + x] = b >> 2;
    }
  }
  for (int c = 0; c < 3; ++c) {
    boxBlur(blurPlanes[c], blurPlanes[3], BLUR_W, BLUR_H, 3);
    boxBlur(blurPlanes[c], blurPlanes[3], BLUR_W, BLUR_H, 3);
  }
  for (int i = 0; i < 3; ++i) {
    const GlassButton &button = BUTTONS[i];
    const float base = (i == 1 ? 0.24f : 0.16f) + (i == pressedButton ? 0.3f : 0.0f);
    for (int y = button.y - button.r - 1; y <= button.y + button.r + 1; ++y) {
      for (int x = button.x - button.r - 1; x <= button.x + button.r + 1; ++x) {
        const float dx = x + 0.5f - button.x;
        const float dy = y + 0.5f - button.y;
        const float d = sqrtf(dx * dx + dy * dy);
        const float coverage = constrain(button.r + 0.5f - d, 0.0f, 1.0f);
        if (coverage <= 0) continue;
        const float top = constrain(0.5f - dy / (2 * button.r), 0.0f, 1.0f);  // 1 at the top edge, 0 at the bottom
        const float rim = constrain(1.0f - fabsf(d - (button.r - 0.8f)), 0.0f, 1.0f) * (0.2f + 0.55f * top);
        const float light = constrain(base + 0.1f * top + rim, 0.0f, 1.0f);
        const int s = ((y - BLUR_Y) >> 1) * BLUR_W + ((x - BLUR_X) >> 1);
        const int r = blurPlanes[0][s] + (255 - blurPlanes[0][s]) * light;
        const int g = blurPlanes[1][s] + (255 - blurPlanes[1][s]) * light;
        const int b = blurPlanes[2][s] + (255 - blurPlanes[2][s]) * light;
        blendPx(y * SCREEN + x, pack(r, g, b), static_cast<int>(coverage * 256));
      }
    }
  }
}

void drawIcon(int button, bool playing) {
  const int cx = BUTTONS[button].x;
  const int cy = BUTTONS[button].y;
  const uint16_t color = TFT_WHITE;
  if (button == 0) {
    canvas.fillRect(cx - 9, cy - 8, 3, 16, color);
    canvas.fillTriangle(cx + 8, cy - 8, cx + 8, cy + 8, cx - 6, cy, color);
  } else if (button == 2) {
    canvas.fillRect(cx + 7, cy - 8, 3, 16, color);
    canvas.fillTriangle(cx - 8, cy - 8, cx - 8, cy + 8, cx + 6, cy, color);
  } else if (playing) {
    canvas.fillRoundRect(cx - 8, cy - 9, 5, 18, 2, color);
    canvas.fillRoundRect(cx + 3, cy - 9, 5, 18, 2, color);
  } else {
    canvas.fillTriangle(cx - 6, cy - 10, cx - 6, cy + 10, cx + 10, cy, color);
  }
}

// Cover, gradient, glass buttons and artist: only changes with the track, play state or a press.
void buildUnder() {
  if (view.art) memcpy(frame, view.art, SCREEN * SCREEN * sizeof(uint16_t));
  else drawVignette();
  drawScrim();
  drawGlassButtons();
  for (int button = 0; button < 3; ++button) drawIcon(button, view.playing);
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::DejaVu12);
  canvas.setTextColor(view.notice.length() ? 0xFD20 : 0xC618);
  canvas.drawString(artistLine, CENTER, ARTIST_Y);
  memcpy(under, frame, SCREEN * SCREEN * sizeof(uint16_t));
}

void drawRing(float progress) {
  const uint16_t limit = static_cast<uint16_t>(constrain(progress, 0.0f, 1.0f) * 65535);
  for (const RingPixel &p : ring) {
    if (p.position <= limit) blendPx(p.index, TFT_WHITE, p.coverage * 235 / 255);
    else blendPx(p.index, TFT_BLACK, p.coverage * 120 / 255);
  }
  // Round knob at the playhead.
  const float a = progress * 2 * PI;
  const float kx = CENTER + sinf(a) * RING_MID;
  const float ky = CENTER - cosf(a) * RING_MID;
  for (int y = static_cast<int>(ky) - 5; y <= static_cast<int>(ky) + 5; ++y) {
    for (int x = static_cast<int>(kx) - 5; x <= static_cast<int>(kx) + 5; ++x) {
      if (x < 0 || y < 0 || x >= SCREEN || y >= SCREEN) continue;
      const float coverage = constrain(4.5f - hypotf(x + 0.5f - kx, y + 0.5f - ky), 0.0f, 1.0f);
      blendPx(y * SCREEN + x, TFT_WHITE, static_cast<int>(coverage * 256));
    }
  }
}

void drawTitle(int offset) {
  canvas.setFont(&fonts::DejaVu18);
  canvas.setTextColor(TFT_WHITE);
  if (titleWidth <= TITLE_W) {
    canvas.setTextDatum(middle_center);
    canvas.drawString(title, CENTER, TITLE_Y);
    return;
  }
  constexpr int top = TITLE_Y - 13;
  constexpr int height = 26;
  canvas.setTextDatum(middle_left);
  canvas.setClipRect(TITLE_X, top, TITLE_W, height);
  canvas.drawString(title, TITLE_X - offset, TITLE_Y);
  canvas.drawString(title, TITLE_X - offset + titleWidth + MARQUEE_GAP, TITLE_Y);
  canvas.clearClipRect();
  // Soft edges: blend the text back into the background near the clip borders.
  for (int y = top; y < top + height; ++y) {
    for (int k = 0; k < TITLE_FADE; ++k) {
      const int fade = 256 - (k + 1) * 256 / (TITLE_FADE + 1);  // weight of the background
      const int right = y * SCREEN + TITLE_X + TITLE_W - 1 - k;
      frame[right] = __builtin_bswap16(mix(__builtin_bswap16(frame[right]), __builtin_bswap16(under[right]), fade));
      if (offset > 0) {
        const int left = y * SCREEN + TITLE_X + k;
        frame[left] = __builtin_bswap16(mix(__builtin_bswap16(frame[left]), __builtin_bswap16(under[left]), fade));
      }
    }
  }
}

void drawStatus() {
  drawVignette();
  canvas.setTextDatum(middle_center);
  canvas.setFont(&fonts::DejaVu18);
  canvas.setTextColor(TFT_WHITE);
  canvas.drawString(fitText(asciiFold(view.statusTitle), 200), CENTER, 110);
  if (view.statusDetail.length()) {
    canvas.setFont(&fonts::DejaVu12);
    canvas.setTextColor(0xC618);
    canvas.drawString(fitText(asciiFold(view.statusDetail), 200), CENTER, 136);
  }
  canvas.pushSprite(0, 0);
}

void drawFrame(float progress, int titleOffset) {
  memcpy(frame, under, SCREEN * SCREEN * sizeof(uint16_t));
  drawRing(progress);
  drawTitle(titleOffset);
  canvas.pushSprite(0, 0);
}

// Precomputes the gradient and the ring pixels once.
void buildGeometry() {
  for (int y = 0; y < SCREEN; ++y) {
    const float t = y < SCRIM_TOP ? 0.0f : static_cast<float>(y - SCRIM_TOP) / (SCREEN - SCRIM_TOP);
    scrimAlpha[y] = static_cast<uint8_t>(SCRIM_MAX * t * t * (3 - 2 * t));
  }
  for (int y = 0; y < SCREEN; ++y) {
    for (int x = 0; x < SCREEN; ++x) {
      const float dx = x + 0.5f - CENTER;
      const float dy = y + 0.5f - CENTER;
      const float d = hypotf(dx, dy);
      const float coverage = constrain(fminf(RING_OUTER - d, d - RING_INNER) + 0.5f, 0.0f, 1.0f);
      if (coverage <= 0) continue;
      float turn = atan2f(dx, -dy) / (2 * PI);
      if (turn < 0) turn += 1;
      ring.push_back({static_cast<uint16_t>(y * SCREEN + x), static_cast<uint16_t>(turn * 65535),
                      static_cast<uint8_t>(coverage * 255)});
    }
  }
}

// ---------------------------------------------------------------- touch

// CST816S auto-sleeps and NACKs I2C when idle; only read it after its IRQ fires.
volatile bool touchIrq = false;
void IRAM_ATTR onTouchIrq() { touchIrq = true; }

// Returns true once per press: the finger must lift (a release IRQ) before the next tap counts.
bool readTap(uint16_t &x, uint16_t &y) {
  static bool fingerDown = false;
  if (!touchIrq) return false;
  touchIrq = false;
  Wire.beginTransmission(pins::TOUCH_ADDR);
  Wire.write(0x02);  // finger count; coordinates follow at 0x03..0x06
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(pins::TOUCH_ADDR, 5) != 5) return false;
  const bool down = Wire.read() > 0;
  const uint8_t xHigh = Wire.read();
  const uint8_t xLow = Wire.read();
  const uint8_t yHigh = Wire.read();
  const uint8_t yLow = Wire.read();
  const bool tap = down && !fingerDown;
  fingerDown = down;
  if (!tap) return false;
  x = ((xHigh & 0x0F) << 8) | xLow;
  y = ((yHigh & 0x0F) << 8) | yLow;
  return x < SCREEN && y < SCREEN;
}

// 0 = previous, 1 = play/pause, 2 = next. The touch zones are wider than the drawn buttons;
// anywhere above the button row toggles play/pause.
int buttonAt(int x, int y) {
  if (y < BUTTONS[1].y - BUTTONS[1].r - 8) return 1;
  return x < (BUTTONS[0].x + BUTTONS[1].x) / 2 ? 0 : x < (BUTTONS[1].x + BUTTONS[2].x) / 2 ? 1 : 2;
}

uint32_t positionMs(const PlayerState &s, unsigned long now) {
  const uint32_t pos = s.progressMs + (s.playing ? now - s.progressAt : 0);
  return s.durationMs ? min(pos, s.durationMs) : pos;
}

void handleTap(int x, int y) {
  if (!view.trackId.length()) return;
  const int button = buttonAt(x, y);
  pressedButton = button;
  pressedUntil = millis() + 180;
  uint8_t command = button == 0 ? CMD_PREV : CMD_NEXT;
  if (button == 1) {
    // Optimistic: flip play/pause (and freeze/resume the progress) before the network round trip.
    xSemaphoreTake(stateLock, portMAX_DELAY);
    const unsigned long now = millis();
    shared.progressMs = positionMs(shared, now);
    shared.progressAt = now;
    shared.playing = !shared.playing;
    shared.playingLockUntil = now + 2500;
    command = shared.playing ? CMD_PLAY : CMD_PAUSE;
    xSemaphoreGive(stateLock);
  }
  xQueueSend(commands, &command, 0);
}

// ---------------------------------------------------------------- Spotify (network task)

void setStatus(const char *title, const char *detail = "") {
  xSemaphoreTake(stateLock, portMAX_DELAY);
  if (shared.trackId.length() || shared.statusTitle != title || shared.statusDetail != detail) {
    shared.trackId = "";
    shared.statusTitle = title;
    shared.statusDetail = detail;
    shared.version++;
  }
  xSemaphoreGive(stateLock);
  coverTrack = "";
}

bool refreshToken() {
  if (!strlen(SPOTIFY_REFRESH_TOKEN)) return false;
  WiFiClientSecure client;
  client.setInsecure(); // ponytail: CA bundle omitted; add Spotify CA pinning for hostile networks.
  HTTPClient http;
  if (!http.begin(client, "https://accounts.spotify.com/api/token")) return false;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = "grant_type=refresh_token&refresh_token=" + String(SPOTIFY_REFRESH_TOKEN) +
                "&client_id=" + String(SPOTIFY_CLIENT_ID) + "&client_secret=" + String(SPOTIFY_CLIENT_SECRET);
  const int status = http.POST(body);
  if (status != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  JsonDocument json;
  const bool ok = deserializeJson(json, http.getString()) == DeserializationError::Ok;
  if (ok) accessToken = json["access_token"].as<String>();
  http.end();
  return ok && accessToken.length();
}

// Sends a Spotify Web API request over the kept-alive connection; the caller reads the body and calls end().
int apiCall(const char *method, const String &url) {
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!apiHttp.begin(apiClient, url)) return 0;
    apiHttp.addHeader("Authorization", "Bearer " + accessToken);
    // HTTPClient omits Content-Length for empty bodies; Spotify answers 411 without it.
    if (strcmp(method, "GET") != 0) apiHttp.addHeader("Content-Length", "0");
    const int status = apiHttp.sendRequest(method);
    if (status > 0) return status;
    apiHttp.end();
    apiClient.stop();  // the server closed the idle keep-alive connection; reconnect once
  }
  return 0;
}

// Reads the body so the connection can be reused. 204/304 have none and no Content-Length:
// getString() would then wait for the server to close the kept-alive socket, i.e. forever.
String apiBody(int status) {
  return status > 0 && status != HTTP_CODE_NO_CONTENT && status != HTTP_CODE_NOT_MODIFIED ? apiHttp.getString() : String();
}

bool downloadCover(const String &url) {
  if (!cdnHttp.begin(cdnClient, url)) return false;
  cdnHttp.setTimeout(10000);
  const int status = cdnHttp.GET();
  if (status != HTTP_CODE_OK) {
    cdnHttp.end();
    return false;
  }
  const int length = cdnHttp.getSize();
  if (length > 300000) {
    cdnHttp.end();
    return false;
  }
  cover.clear();
  if (length > 0) cover.reserve(length);
  WiFiClient &stream = cdnHttp.getStream();
  uint8_t buffer[2048];
  const unsigned long deadline = millis() + 15000;
  // With keep-alive the socket stays open after the body, so stop at Content-Length.
  while ((length > 0 ? cover.size() < static_cast<size_t>(length) : stream.connected() || stream.available()) &&
         millis() < deadline) {
    const int available = stream.available();
    if (available <= 0) {
      delay(1);
      continue;
    }
    const size_t want = available > static_cast<int>(sizeof(buffer)) ? sizeof(buffer) : available;
    const size_t read = stream.readBytes(buffer, want);
    cover.insert(cover.end(), buffer, buffer + read);
    if (cover.size() > 300000) break;
  }
  cdnHttp.end();
  if (length <= 0) cdnClient.stop();
  return !cover.empty() && cover.size() <= 300000;
}

bool jpegToBuffer(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  for (int row = 0; row < h; ++row) {
    const int dy = y + row;
    if (dy < 0 || dy >= decodedH) continue;
    for (int col = 0; col < w; ++col) {
      const int dx = x + col;
      if (dx >= 0 && dx < decodedW) decoded[dy * decodedW + dx] = bitmap[row * w + col];
    }
  }
  return true;
}

uint16_t sampleBilinear(const uint16_t *src, int w, int h, float sx, float sy) {
  sx = constrain(sx, 0.0f, w - 1.0f);
  sy = constrain(sy, 0.0f, h - 1.0f);
  const int x0 = static_cast<int>(sx);
  const int y0 = static_cast<int>(sy);
  const int x1 = min(x0 + 1, w - 1);
  const int y1 = min(y0 + 1, h - 1);
  const int fx = static_cast<int>((sx - x0) * 256);
  const int fy = static_cast<int>((sy - y0) * 256);
  return mix(mix(src[y0 * w + x0], src[y0 * w + x1], fx), mix(src[y1 * w + x0], src[y1 * w + x1], fx), fy);
}

// Decodes the cover at the smallest JPEG scale that still covers the screen, center-crops and
// resamples it into `art`, stored byte-swapped so it can be copied straight into the canvas.
bool decodeArt(uint16_t *art) {
  uint16_t w = 0;
  uint16_t h = 0;
  if (TJpgDec.getJpgSize(&w, &h, cover.data(), cover.size()) != JDR_OK) return false;
  uint8_t scale = 1;
  while (scale < 8 && w / (scale * 2) >= SCREEN && h / (scale * 2) >= SCREEN) scale *= 2;
  decodedW = (w + scale - 1) / scale;
  decodedH = (h + scale - 1) / scale;
  decoded = psramAlloc<uint16_t>(decodedW * decodedH);
  if (!decoded) return false;
  TJpgDec.setJpgScale(scale);
  const bool ok = TJpgDec.drawJpg(0, 0, cover.data(), cover.size()) == JDR_OK;
  if (ok) {
    const int side = min(decodedW, decodedH);
    const float step = static_cast<float>(side) / SCREEN;
    const float ox = (decodedW - side) / 2.0f;
    const float oy = (decodedH - side) / 2.0f;
    for (int y = 0; y < SCREEN; ++y) {
      for (int x = 0; x < SCREEN; ++x) {
        art[y * SCREEN + x] = __builtin_bswap16(
            sampleBilinear(decoded, decodedW, decodedH, ox + (x + 0.5f) * step - 0.5f, oy + (y + 0.5f) * step - 0.5f));
      }
    }
  }
  free(decoded);
  decoded = nullptr;
  return ok;
}

// Returns the HTTP status (2xx on success). A 401 refreshes the token and retries once:
// a token without user-modify-playback-state keeps answering 401 "Permissions missing".
int playerCommand(const char *path, bool post, bool retry = true) {
  if (!accessToken.length() && !refreshToken()) return HTTP_CODE_UNAUTHORIZED;
  const int status = apiCall(post ? "POST" : "PUT", String("https://api.spotify.com/v1/me/player/") + path);
  apiBody(status);
  apiHttp.end();
  if (status == HTTP_CODE_UNAUTHORIZED && retry && refreshToken()) return playerCommand(path, post, false);
  return status;
}

void pollState(bool retry = true) {
  if (!accessToken.length() && !refreshToken()) {
    setStatus("Falta Spotify", "configura secrets.h");
    return;
  }
  const int status = apiCall("GET", "https://api.spotify.com/v1/me/player");
  const String body = apiBody(status);
  apiHttp.end();
  if (status == HTTP_CODE_UNAUTHORIZED && retry && refreshToken()) return pollState(false);
  if (status == HTTP_CODE_NO_CONTENT) return setStatus("Nada reproduciéndose");
  if (status != HTTP_CODE_OK) return;
  // The response is several KB of available_markets etc.; keep only what the UI uses.
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
  if (deserializeJson(json, body, DeserializationOption::Filter(filter)) != DeserializationError::Ok) return;
  JsonObject item = json["item"];
  if (item.isNull()) return setStatus("Nada reproduciéndose");
  const String id = item["id"].as<String>();

  xSemaphoreTake(stateLock, portMAX_DELAY);
  if (shared.trackId != id) {
    shared.trackId = id;
    shared.name = item["name"].as<String>();
    shared.artist = item["artists"][0]["name"].as<String>();
    shared.notice = "";
    shared.version++;
  }
  const unsigned long now = millis();
  shared.durationMs = item["duration_ms"] | 0;
  shared.progressMs = json["progress_ms"] | 0;
  shared.progressAt = now;
  if (static_cast<long>(now - shared.playingLockUntil) >= 0) shared.playing = json["is_playing"] | false;
  xSemaphoreGive(stateLock);

  if (id == coverTrack) return;
  coverTrack = id;
  // Smallest image that still covers the panel (Spotify sends 640/300/64): far less to download and decode.
  String image;
  int bestWidth = INT_MAX;
  for (JsonObject img : item["album"]["images"].as<JsonArray>()) {
    const int width = img["width"] | 0;
    if (width >= SCREEN && width < bestWidth) {
      bestWidth = width;
      image = img["url"].as<String>();
    }
  }
  if (!image.length()) image = item["album"]["images"][0]["url"].as<String>();
  // Decode into whichever buffer isn't on screen, then swap.
  uint16_t *target = shared.art == artBuffers[0] ? artBuffers[1] : artBuffers[0];
  const bool ok = target && downloadCover(image) && decodeArt(target);
  xSemaphoreTake(stateLock, portMAX_DELAY);
  shared.art = ok ? target : nullptr;
  shared.version++;
  xSemaphoreGive(stateLock);
}

void runCommand(uint8_t command) {
  static const char *paths[] = {"previous", "play", "pause", "next"};
  const bool skip = command == CMD_PREV || command == CMD_NEXT;
  const int status = playerCommand(paths[command], skip);
  const bool ok = status >= 200 && status < 300;
  xSemaphoreTake(stateLock, portMAX_DELAY);
  const String notice =
      ok ? "" : status == 401 || status == 403 ? "Sin permiso: renueva el token" : "Spotify error " + String(status);
  if (notice != shared.notice) {
    shared.notice = notice;
    shared.version++;
  }
  if (!ok && !skip) {  // undo the optimistic toggle
    shared.playing = command == CMD_PAUSE;
    shared.playingLockUntil = 0;
  }
  xSemaphoreGive(stateLock);
  if (ok && skip) {
    // Spotify switches tracks asynchronously; poll briefly until the new track shows up.
    const String before = coverTrack;
    for (int i = 0; i < 6 && coverTrack == before; ++i) {
      if (i) vTaskDelay(pdMS_TO_TICKS(150));
      pollState();
    }
  }
}

void networkTask(void *) {
  apiClient.setInsecure();  // ponytail: same trade-off as refreshToken(); pin Spotify's CA for hostile networks.
  cdnClient.setInsecure();
  TJpgDec.setCallback(jpegToBuffer);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(250));
  unsigned long nextPoll = 0;
  for (;;) {
    const long wait = static_cast<long>(nextPoll - millis());
    uint8_t command;
    if (xQueueReceive(commands, &command, wait > 0 ? pdMS_TO_TICKS(wait) : 0) == pdTRUE) {
      runCommand(command);
      continue;
    }
    pollState();
    nextPoll = millis() + POLL_MS;
  }
}

// ---------------------------------------------------------------- setup / render loop

void setup() {
  pinMode(pins::LCD_BL, OUTPUT);
  digitalWrite(pins::LCD_BL, HIGH);
  pinMode(pins::TOUCH_RST, OUTPUT);
  digitalWrite(pins::TOUCH_RST, LOW);
  delay(5);
  digitalWrite(pins::TOUCH_RST, HIGH);
  delay(50);  // CST816S boot time after reset
  Wire.begin(pins::TOUCH_SDA, pins::TOUCH_SCL);
  pinMode(pins::TOUCH_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pins::TOUCH_IRQ), onTouchIrq, FALLING);
  // Factory IrqCtl only fires on gestures; enable IRQ on touch/release and keep the chip awake.
  const uint8_t touchConfig[][2] = {{0xFA, 0x60}, {0xFE, 0x01}};  // IrqCtl=EnTouch|EnChange, DisAutoSleep
  for (const auto &reg : touchConfig) {
    Wire.beginTransmission(pins::TOUCH_ADDR);
    Wire.write(reg[0]);
    Wire.write(reg[1]);
    Wire.endTransmission();
  }
  lcd.init();
  lcd.setRotation(0);
  canvas.setColorDepth(16);
  canvas.setPsram(true);
  canvas.createSprite(SCREEN, SCREEN);
  frame = static_cast<uint16_t *>(canvas.getBuffer());
  under = psramAlloc<uint16_t>(SCREEN * SCREEN);
  artBuffers[0] = psramAlloc<uint16_t>(SCREEN * SCREEN);
  artBuffers[1] = psramAlloc<uint16_t>(SCREEN * SCREEN);
  buildGeometry();
  stateLock = xSemaphoreCreateMutex();
  commands = xQueueCreate(4, sizeof(uint8_t));
  shared.statusTitle = "Conectando WiFi";
  // Network on core 0 so HTTP/TLS and JPEG decoding never stall touch and drawing on core 1.
  xTaskCreatePinnedToCore(networkTask, "spotify", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  uint16_t x = 0;
  uint16_t y = 0;
  if (readTap(x, y)) handleTap(x, y);

  static unsigned long lastFrame = 0;
  static bool lastPressed = false;
  static bool lastPlaying = false;
  const unsigned long now = millis();
  bool changed = false;

  xSemaphoreTake(stateLock, portMAX_DELAY);
  if (shared.version != seenVersion) {
    if (shared.trackId != view.trackId || shared.name != view.name) titleShownAt = now;
    view = shared;
    seenVersion = shared.version;
    changed = true;
  } else {
    view.playing = shared.playing;
    view.progressMs = shared.progressMs;
    view.progressAt = shared.progressAt;
    view.durationMs = shared.durationMs;
  }
  xSemaphoreGive(stateLock);

  if (!view.trackId.length()) {
    if (changed) drawStatus();
    delay(10);
    return;
  }

  if (pressedButton >= 0 && static_cast<long>(now - pressedUntil) >= 0) pressedButton = -1;
  if ((pressedButton >= 0) != lastPressed || view.playing != lastPlaying) changed = true;
  lastPressed = pressedButton >= 0;
  lastPlaying = view.playing;
  if (changed) {
    layoutText();
    buildUnder();
  }

  // Redraw for a scrolling title (every frame) or the progress ring (a few times per second).
  const bool scrolling = titleWidth > TITLE_W;
  const unsigned long interval = scrolling ? FRAME_MS : view.playing ? PROGRESS_REDRAW_MS : ULONG_MAX;
  if (!changed && now - lastFrame < interval) {
    delay(5);
    return;
  }
  lastFrame = now;
  const float progress = view.durationMs ? static_cast<float>(positionMs(view, now)) / view.durationMs : 0.0f;
  drawFrame(progress, marqueeOffset(now));
}
