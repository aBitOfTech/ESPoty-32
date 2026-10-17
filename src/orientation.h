#pragma once

// Turns the QMI8658 accelerometer reading into the angle the UI must be rotated to stay upright.
class Orientation {
 public:
  void begin();  // after i2c::begin(); without a QMI8658 the angle stays at 0
  // Samples once per frame; returns true when angle() moved enough to redraw.
  bool update(unsigned long now);
  float angle() const { return angle_; }  // degrees clockwise

 private:
  bool ready_ = false;
  bool seeded_ = false;
  unsigned long lastSample_ = 0;
  float gx_ = 0;
  float gy_ = 0;
  float angle_ = 0;
};
