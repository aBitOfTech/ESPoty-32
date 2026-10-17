#include "touch.h"

#include <Arduino.h>

#include "i2c_bus.h"
#include "layout.h"
#include "pins.h"

namespace {
// CST816S auto-sleeps and NACKs I2C when idle; only read it after its IRQ fires.
volatile bool touchIrq = false;
void IRAM_ATTR onTouchIrq() { touchIrq = true; }
}  // namespace

void Touch::begin() {
  pinMode(pins::TOUCH_RST, OUTPUT);
  digitalWrite(pins::TOUCH_RST, LOW);
  delay(5);
  digitalWrite(pins::TOUCH_RST, HIGH);
  delay(50);  // CST816S boot time after reset
  pinMode(pins::TOUCH_IRQ, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pins::TOUCH_IRQ), onTouchIrq, FALLING);
  // Factory IrqCtl only fires on gestures; enable IRQ on touch/release and keep the chip awake.
  i2c::write(pins::TOUCH_ADDR, 0xFA, 0x60);  // IrqCtl = EnTouch | EnChange
  i2c::write(pins::TOUCH_ADDR, 0xFE, 0x01);  // DisAutoSleep
}

bool Touch::readTap(uint16_t &x, uint16_t &y) {
  if (!touchIrq) return false;
  touchIrq = false;
  uint8_t data[5];  // finger count, then X high/low and Y high/low
  if (!i2c::read(pins::TOUCH_ADDR, 0x02, data, sizeof(data))) return false;
  const bool down = data[0] > 0;
  const bool tap = down && !fingerDown_;
  fingerDown_ = down;
  if (!tap) return false;
  x = ((data[1] & 0x0F) << 8) | data[2];
  y = ((data[3] & 0x0F) << 8) | data[4];
  return x < layout::SCREEN && y < layout::SCREEN;
}
