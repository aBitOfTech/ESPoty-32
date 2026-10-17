#pragma once

#include <stddef.h>
#include <stdint.h>

// The I2C bus shared by the touch controller and the accelerometer. Only the render loop uses it.
namespace i2c {
void begin();
bool write(uint8_t addr, uint8_t reg, uint8_t value);
bool read(uint8_t addr, uint8_t reg, uint8_t *buffer, size_t count);
}  // namespace i2c
