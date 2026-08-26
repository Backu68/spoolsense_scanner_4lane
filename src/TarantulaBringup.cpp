#include <Arduino.h>
#include "TFTConfig.h"

// Tarantula SpoolSense station hardware bring-up.
// First-stage image intentionally tests only the ILI9341 display on the exact
// dedicated TFT pins. Touch and PN532 are added only after this passes.
static LGFX display(TFTDriver::ILI9341);

void setup() {
    delay(500);
    Serial.begin(115200);
    delay(250);
    Serial.println("=== Tarantula ILI9341 bring-up ===");

    display.init();
    display.setRotation(0);

    // Obvious RGB sweep proves SPI traffic and panel initialization before the
    // final static test screen is drawn.
    display.fillScreen(TFT_RED);
    delay(250);
    display.fillScreen(TFT_GREEN);
    delay(250);
    display.fillScreen(TFT_BLUE);
    delay(250);
    display.fillScreen(TFT_BLACK);

    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(18, 72);
    display.print("SpoolSense");
    display.setCursor(18, 104);
    display.print("ILI9341 OK");

    Serial.println("ILI9341 test screen drawn");
}

void loop() {
    delay(1000);
}
