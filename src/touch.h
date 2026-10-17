#pragma once

#include <stdint.h>

// CST816S touch controller.
class Touch {
 public:
  void begin();  // after i2c::begin()
  // Returns true once per press, with panel coordinates: the finger must lift before the next tap.
  bool readTap(uint16_t &x, uint16_t &y);

 private:
  bool fingerDown_ = false;
};
