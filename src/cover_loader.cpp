#include "cover_loader.h"

#include <TJpg_Decoder.h>

#include "app_config.h"
#include "image_ops.h"
#include "layout.h"
#include "psram.h"

using layout::SCREEN;

namespace {
// TJpgDec's callback takes no context pointer, so the decode target lives here.
uint16_t *decoded = nullptr;
int decodedW = 0;
int decodedH = 0;

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
}  // namespace

void CoverLoader::begin() {
  client_.setInsecure();  // ponytail: same trade-off as SpotifyClient::begin().
  TJpgDec.setCallback(jpegToBuffer);
}

bool CoverLoader::load(const String &url, uint16_t *art) { return download(url) && decode(art); }

bool CoverLoader::download(const String &url) {
  if (!http_.begin(client_, url)) return false;
  http_.setTimeout(COVER_HTTP_TIMEOUT_MS);
  if (http_.GET() != HTTP_CODE_OK) {
    http_.end();
    return false;
  }
  const int length = http_.getSize();
  if (length > static_cast<int>(COVER_MAX_BYTES)) {
    http_.end();
    return false;
  }
  jpeg_.clear();
  if (length > 0) jpeg_.reserve(length);
  WiFiClient &stream = http_.getStream();
  uint8_t buffer[2048];
  const unsigned long deadline = millis() + COVER_DOWNLOAD_MS;
  // With keep-alive the socket stays open after the body, so stop at Content-Length.
  while ((length > 0 ? jpeg_.size() < static_cast<size_t>(length) : stream.connected() || stream.available()) &&
         millis() < deadline) {
    const int available = stream.available();
    if (available <= 0) {
      delay(1);
      continue;
    }
    const size_t want = available > static_cast<int>(sizeof(buffer)) ? sizeof(buffer) : available;
    const size_t read = stream.readBytes(buffer, want);
    jpeg_.insert(jpeg_.end(), buffer, buffer + read);
    if (jpeg_.size() > COVER_MAX_BYTES) break;
  }
  http_.end();
  if (length <= 0) client_.stop();
  return !jpeg_.empty() && jpeg_.size() <= COVER_MAX_BYTES;
}

// Decodes at the smallest JPEG scale that still covers the screen, then center-crops and resamples.
bool CoverLoader::decode(uint16_t *art) {
  uint16_t w = 0;
  uint16_t h = 0;
  if (TJpgDec.getJpgSize(&w, &h, jpeg_.data(), jpeg_.size()) != JDR_OK) return false;
  uint8_t scale = 1;
  while (scale < 8 && w / (scale * 2) >= SCREEN && h / (scale * 2) >= SCREEN) scale *= 2;
  decodedW = (w + scale - 1) / scale;
  decodedH = (h + scale - 1) / scale;
  decoded = psramAlloc<uint16_t>(decodedW * decodedH);
  if (!decoded) return false;
  TJpgDec.setJpgScale(scale);
  const bool ok = TJpgDec.drawJpg(0, 0, jpeg_.data(), jpeg_.size()) == JDR_OK;
  if (ok) {
    const int side = min(decodedW, decodedH);
    const float step = static_cast<float>(side) / SCREEN;
    const float ox = (decodedW - side) / 2.0f;
    const float oy = (decodedH - side) / 2.0f;
    for (int y = 0; y < SCREEN; ++y) {
      for (int x = 0; x < SCREEN; ++x) {
        art[y * SCREEN + x] = __builtin_bswap16(image::sampleBilinear(
            decoded, decodedW, decodedH, ox + (x + 0.5f) * step - 0.5f, oy + (y + 0.5f) * step - 0.5f));
      }
    }
  }
  free(decoded);
  decoded = nullptr;
  return ok;
}
