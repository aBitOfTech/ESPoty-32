#pragma once

// Waveshare ESP32-S3-Touch-LCD-1.28 pinout.
// Change only this file for another ESP32-S3 + GC9A01/CST816S board.
namespace pins {
constexpr int LCD_SCLK = 10;
constexpr int LCD_MOSI = 11;
constexpr int LCD_MISO = 12;
constexpr int LCD_CS = 9;
constexpr int LCD_DC = 8;
constexpr int LCD_RST = 14;
constexpr int LCD_BL = 2;
constexpr int TOUCH_SDA = 6;
constexpr int TOUCH_SCL = 7;
constexpr int TOUCH_RST = 13;
constexpr int TOUCH_IRQ = 5;
constexpr uint8_t TOUCH_ADDR = 0x15;
}

