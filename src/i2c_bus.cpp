#include "i2c_bus.h"

#include <Wire.h>

#include "pins.h"

namespace i2c {

void begin() { Wire.begin(pins::TOUCH_SDA, pins::TOUCH_SCL); }

bool write(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool read(uint8_t addr, uint8_t reg, uint8_t *buffer, size_t count) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom(addr, count) != count) return false;
  for (size_t i = 0; i < count; ++i) buffer[i] = Wire.read();
  return true;
}

}  // namespace i2c
