#include "orientation.h"

#include <Arduino.h>

#include "app_config.h"
#include "i2c_bus.h"
#include "pins.h"
#include "ui_math.h"

void Orientation::begin() {
  uint8_t id = 0;
  if (!i2c::read(pins::IMU_ADDR, 0x00, &id, 1) || id != 0x05) return;  // WHO_AM_I
  ready_ = i2c::write(pins::IMU_ADDR, 0x02, 0x40) &&  // CTRL1: register auto-increment, little endian
           i2c::write(pins::IMU_ADDR, 0x03, 0x06) &&  // CTRL2: accelerometer +-2 g, 125 Hz
           i2c::write(pins::IMU_ADDR, 0x08, 0x01);    // CTRL7: enable the accelerometer only
}

bool Orientation::update(unsigned long now) {
  if (!ready_ || now - lastSample_ < FRAME_MS) return false;
  lastSample_ = now;
  uint8_t data[6];
  if (!i2c::read(pins::IMU_ADDR, 0x35, data, sizeof(data))) return false;  // AX_L .. AZ_H
  const float ax = static_cast<int16_t>(data[0] | data[1] << 8);
  const float ay = static_cast<int16_t>(data[2] | data[3] << 8);
  const float az = static_cast<int16_t>(data[4] | data[5] << 8);
  if (hypotf(ax, ay) < IMU_FLAT_RATIO * sqrtf(ax * ax + ay * ay + az * az)) return false;
  // Filter the vector, not the angle, so there is no jump when it wraps around 180 degrees.
  gx_ = seeded_ ? gx_ + (ax - gx_) * IMU_SMOOTHING : ax;
  gy_ = seeded_ ? gy_ + (ay - gy_) * IMU_SMOOTHING : ay;
  seeded_ = true;
  const float target = IMU_ANGLE_SIGN * atan2f(gx_, gy_) * RAD_TO_DEG + IMU_ANGLE_OFFSET;
  const float delta = wrapDegrees(target - angle_);
  if (fabsf(delta) < IMU_DEADBAND_DEG) return false;
  angle_ = wrapDegrees(angle_ + delta);
  return true;
}
