#pragma once

#include <LovyanGFX.hpp>
#include "BoardPins.h"

// ---------------------------------------------------------------------------
// LovyanGFX display config — runtime panel selection (ST7789 or GC9A01).
//
// Both panels are 240x240 SPI. Same pins, same bus, different driver IC.
// The panel type is selected at construction via a string parameter
// read from NVS ("st7789" or "gc9a01").
//
// SPI bus:
//   WROOM  — VSPI (bus 3): pins 22/23 freed from LCD when TFT replaces LCD
//   S3-Zero — SPI2 (FSPI): side header pins
// ---------------------------------------------------------------------------

enum class TFTDriver : uint8_t {
    ST7789 = 0,
    GC9A01 = 1,
    ILI9341 = 2,   // 240x320 — dashboard centered with 40px letterbox
    ILI9488 = 3    // 320x480 — dashboard centered
};

#if defined(BOARD_ESP32_S3)

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_st7789;
    lgfx::Panel_GC9A01  _panel_gc9a01;
    lgfx::Panel_ILI9341 _panel_ili9341;
    lgfx::Panel_ILI9488 _panel_ili9488;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX(TFTDriver driver = TFTDriver::ST7789) {
        // SPI bus — S3-DevKitC uses SPI3 (PN5180 on SPI2), S3-Zero uses SPI2
        {
            auto cfg = _bus_instance.config();
#if defined(BOARD_S3_DEVKITC)
            cfg.spi_host   = SPI3_HOST;  // SPI3 — PN5180 uses SPI2/FSPI
#else
            cfg.spi_host   = SPI2_HOST;  // FSPI on S3-Zero
#endif
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = PIN_TFT_SCLK;
            cfg.pin_mosi   = PIN_TFT_MOSI;
            cfg.pin_miso   = PIN_TFT_MISO;
            cfg.pin_dc     = PIN_TFT_DC;
            _bus_instance.config(cfg);
        }

        // Select panel and configure
        lgfx::Panel_Device* panel = nullptr;
        if (driver == TFTDriver::GC9A01) {
            panel = &_panel_gc9a01;
        } else if (driver == TFTDriver::ILI9341) {
            panel = &_panel_ili9341;
        } else if (driver == TFTDriver::ILI9488) {
            panel = &_panel_ili9488;
        } else {
            panel = &_panel_st7789;
        }

        panel->setBus(&_bus_instance);

        {
            auto cfg = panel->config();
            cfg.pin_cs   = PIN_TFT_CS;
            cfg.pin_rst  = PIN_TFT_RST;
            cfg.pin_busy = -1;
            // The dashboard renders a 240x240 sprite; larger ILI94xx panels
            // center it by offsetting the addressing window (#180)
            if (driver == TFTDriver::ILI9341) {
                cfg.memory_width  = 240;
                cfg.memory_height = 320;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 0;
                cfg.offset_y      = 40;
            } else if (driver == TFTDriver::ILI9488) {
                cfg.memory_width  = 320;
                cfg.memory_height = 480;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 40;
                cfg.offset_y      = 120;
            } else {
                cfg.memory_width  = 240;
                cfg.memory_height = 240;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 0;
                cfg.offset_y      = 0;
            }
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable     = false;
            cfg.invert       = (driver == TFTDriver::ILI9341 || driver == TFTDriver::ILI9488)
                                   ? false : true;
            cfg.rgb_order    = false;
            cfg.dlen_16bit   = false;
            cfg.bus_shared   = false;
            panel->config(cfg);
        }

        // Backlight (skip if pin not assigned)
        if (PIN_TFT_BL >= 0) {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = PIN_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            panel->setLight(&_light_instance);
        }

        setPanel(panel);
    }
};

#else // WROOM or single-SPI C3/C5/C6

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_st7789;
    lgfx::Panel_GC9A01  _panel_gc9a01;
    lgfx::Panel_ILI9341 _panel_ili9341;
    lgfx::Panel_ILI9488 _panel_ili9488;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;

public:
    LGFX(TFTDriver driver = TFTDriver::ST7789) {
        // SPI bus — WROOM uses VSPI (PN5180 on HSPI). C3 has no VSPI; TFT is
        // out of scope for C3 builds but LGFX must still link, so fall back to SPI2.
        {
            auto cfg = _bus_instance.config();
#if defined(BOARD_ESP32_C3) || defined(BOARD_ESP32_C5) || defined(BOARD_ESP32_C6)
            cfg.spi_host   = SPI2_HOST;  // Single-SPI targets: TFT is unsupported at runtime
#else
            cfg.spi_host   = VSPI_HOST;
#endif
            cfg.spi_mode   = 0;
            cfg.freq_write = 40000000;
            cfg.freq_read  = 16000000;
            cfg.pin_sclk   = PIN_TFT_SCLK;
            cfg.pin_mosi   = PIN_TFT_MOSI;
            cfg.pin_miso   = PIN_TFT_MISO;
            cfg.pin_dc     = PIN_TFT_DC;
            _bus_instance.config(cfg);
        }

        lgfx::Panel_Device* panel = nullptr;
        if (driver == TFTDriver::GC9A01) {
            panel = &_panel_gc9a01;
        } else if (driver == TFTDriver::ILI9341) {
            panel = &_panel_ili9341;
        } else if (driver == TFTDriver::ILI9488) {
            panel = &_panel_ili9488;
        } else {
            panel = &_panel_st7789;
        }

        panel->setBus(&_bus_instance);

        {
            auto cfg = panel->config();
            cfg.pin_cs   = PIN_TFT_CS;
            cfg.pin_rst  = PIN_TFT_RST;
            cfg.pin_busy = -1;
            // The dashboard renders a 240x240 sprite; larger ILI94xx panels
            // center it by offsetting the addressing window (#180)
            if (driver == TFTDriver::ILI9341) {
                cfg.memory_width  = 240;
                cfg.memory_height = 320;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 0;
                cfg.offset_y      = 40;
            } else if (driver == TFTDriver::ILI9488) {
                cfg.memory_width  = 320;
                cfg.memory_height = 480;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 40;
                cfg.offset_y      = 120;
            } else {
                cfg.memory_width  = 240;
                cfg.memory_height = 240;
                cfg.panel_width   = 240;
                cfg.panel_height  = 240;
                cfg.offset_x      = 0;
                cfg.offset_y      = 0;
            }
            cfg.offset_rotation = 0;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits  = 1;
            cfg.readable     = false;
            cfg.invert       = (driver == TFTDriver::ILI9341 || driver == TFTDriver::ILI9488)
                                   ? false : true;
            cfg.rgb_order    = false;
            cfg.dlen_16bit   = false;
            cfg.bus_shared   = false;
            panel->config(cfg);
        }

        // Backlight (skip if pin not assigned)
        if (PIN_TFT_BL >= 0) {
            auto cfg = _light_instance.config();
            cfg.pin_bl      = PIN_TFT_BL;
            cfg.invert      = false;
            cfg.freq        = 44100;
            cfg.pwm_channel = 7;
            _light_instance.config(cfg);
            panel->setLight(&_light_instance);
        }

        setPanel(panel);
    }
};

#endif // BOARD_ESP32_S3
