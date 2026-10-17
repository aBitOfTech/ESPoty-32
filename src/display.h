#pragma once

#include <LovyanGFX.hpp>

#include "pins.h"

// GC9A01 240x240 round panel on SPI.
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
