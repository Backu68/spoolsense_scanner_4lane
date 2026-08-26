#include <Arduino.h>
#include "TFTConfig.h"
#include "BoardPins.h"

// Tarantula SpoolSense station hardware bring-up.
// Stage 2 proves the ILI9341 plus XPT2046 touch. Touch is intentionally
// bit-banged so it uses its own pins and shares no SPI signal pins with
// either the TFT or PN532.
static LGFX display(TFTDriver::ILI9341);

static uint8_t touchTransfer(uint8_t value) {
    uint8_t result = 0;
    for (int bit = 7; bit >= 0; --bit) {
        digitalWrite(PIN_TOUCH_MOSI, (value >> bit) & 1);
        digitalWrite(PIN_TOUCH_SCLK, HIGH);
        delayMicroseconds(1);
        result = (uint8_t)((result << 1) | (digitalRead(PIN_TOUCH_MISO) ? 1 : 0));
        digitalWrite(PIN_TOUCH_SCLK, LOW);
        delayMicroseconds(1);
    }
    return result;
}

static uint16_t touchRead12(uint8_t command) {
    digitalWrite(PIN_TOUCH_CS, LOW);
    touchTransfer(command);
    uint16_t value = (uint16_t)touchTransfer(0x00) << 8;
    value |= touchTransfer(0x00);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    return (value >> 3) & 0x0FFF;
}

static bool readTouch(uint16_t& x, uint16_t& y) {
    if (digitalRead(PIN_TOUCH_IRQ) != LOW) return false;

    // XPT2046: 0xD0 = X position, 0x90 = Y position, 12-bit differential.
    x = touchRead12(0xD0);
    y = touchRead12(0x90);
    return true;
}

void setup() {
    delay(500);
    Serial.begin(115200);
    delay(250);
    Serial.println("=== Tarantula ILI9341 + XPT2046 bring-up ===");

    display.init();
    display.setRotation(0);

    display.fillScreen(TFT_RED);
    delay(200);
    display.fillScreen(TFT_GREEN);
    delay(200);
    display.fillScreen(TFT_BLUE);
    delay(200);
    display.fillScreen(TFT_BLACK);

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(18, 56);
    display.print("SpoolSense");
    display.setCursor(18, 88);
    display.print("ILI9341 OK");
    display.setCursor(18, 120);
    display.print("Touch screen");

    pinMode(PIN_TOUCH_SCLK, OUTPUT);
    pinMode(PIN_TOUCH_MOSI, OUTPUT);
    pinMode(PIN_TOUCH_MISO, INPUT);
    pinMode(PIN_TOUCH_CS, OUTPUT);
    pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);
    digitalWrite(PIN_TOUCH_SCLK, LOW);
    digitalWrite(PIN_TOUCH_MOSI, LOW);
    digitalWrite(PIN_TOUCH_CS, HIGH);

    Serial.println("ILI9341 OK; XPT2046 test ready");
    Serial.printf("Touch pins: CLK=%d DIN=%d DO=%d CS=%d IRQ=%d\n",
                  PIN_TOUCH_SCLK, PIN_TOUCH_MOSI, PIN_TOUCH_MISO,
                  PIN_TOUCH_CS, PIN_TOUCH_IRQ);
}

void loop() {
    static bool wasPressed = false;
    static uint32_t lastReport = 0;

    uint16_t x = 0, y = 0;
    bool pressed = readTouch(x, y);

    if (pressed && (millis() - lastReport >= 75)) {
        lastReport = millis();
        Serial.printf("TOUCH raw X=%u Y=%u\n", x, y);

        display.fillRect(18, 156, 210, 48, TFT_BLACK);
        display.setCursor(18, 156);
        display.printf("X:%4u", x);
        display.setCursor(18, 180);
        display.printf("Y:%4u", y);
        wasPressed = true;
    } else if (!pressed && wasPressed) {
        Serial.println("TOUCH released");
        wasPressed = false;
    }

    delay(5);
}
