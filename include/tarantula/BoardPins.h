#pragma once

#include "UserConfig.h"

// Tarantula single-reader SpoolSense station
// ESP32-WROOM-32 + ILI9341 240x320 + XPT2046 + PN532
//
// Buses are intentionally independent:
//   TFT:     VSPI on 18/23/19
//   XPT2046: software/dedicated pins 16/17/35
//   PN532:   HSPI/custom pins 25/27/26
// Only ground is shared between modules.

// PN5180 compatibility definitions. This station uses PN532, but the stock
// firmware expects the PN5180 symbols to exist at compile time.
#define PIN_PN5180_SCK   25
#define PIN_PN5180_MOSI  27
#define PIN_PN5180_MISO  26
#define PIN_PN5180_NSS   33
#define PIN_PN5180_RST   14
#define PIN_PN5180_BUSY  -1
#define PIN_PN5180_GPIO  -1
#define PIN_PN5180_IRQ   -1
#define PIN_PN5180_AUX   -1

// PN532 — dedicated SPI bus
#define PIN_PN532_SCK    25
#define PIN_PN532_MOSI   27
#define PIN_PN532_MISO   26
#define PIN_PN532_SS     33
#define PIN_PN532_RST    14
#define PIN_PN532_IRQ    -1

// ILI9341 TFT — VSPI
#define PIN_TFT_SCLK     18
#define PIN_TFT_MOSI     23
#define PIN_TFT_MISO     19
#define PIN_TFT_CS       21
#define PIN_TFT_DC       22
#define PIN_TFT_RST      32
#define PIN_TFT_BL       -1  // module LED/backlight is wired directly

// XPT2046 resistive touch — intentionally does not share TFT SPI pins.
// Touch support will be enabled after TFT hardware bring-up.
#define PIN_TOUCH_SCLK   16
#define PIN_TOUCH_MOSI   17
#define PIN_TOUCH_MISO   35
#define PIN_TOUCH_CS     13
#define PIN_TOUCH_IRQ    34

// Unused optional peripherals on this station
#define PIN_LCD_SDA      -1
#define PIN_LCD_SCL      -1
// Stock config defaults the status LED feature on. Keep it assigned to an
// otherwise-unused GPIO so first boot is harmless even with no LED connected.
#define PIN_STATUS_LED   4
#define PIN_KEYPAD_ROW1  -1
#define PIN_KEYPAD_ROW2  -1
#define PIN_KEYPAD_ROW3  -1
#define PIN_KEYPAD_ROW4  -1
#define PIN_KEYPAD_COL1  -1
#define PIN_KEYPAD_COL2  -1
#define PIN_KEYPAD_COL3  -1
