#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <vector>

// Downloads album covers from Spotify's CDN over a kept-alive connection and turns them into
// SCREEN x SCREEN images ready to copy into the canvas. Used only by the network task.
class CoverLoader {
 public:
  void begin();
  // Writes the cover center-cropped and byte-swapped (like the canvas) into `art`.
  bool load(const String &url, uint16_t *art);

 private:
  bool download(const String &url);
  bool decode(uint16_t *art);

  WiFiClientSecure client_;
  HTTPClient http_;
  std::vector<uint8_t> jpeg_;
};
