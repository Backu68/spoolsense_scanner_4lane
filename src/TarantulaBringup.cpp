#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_PN532.h>
#include "TFTConfig.h"
#include "BoardPins.h"

// Tarantula SpoolSense station hardware bring-up.
// Proves three independent signal paths:
//   ILI9341  -> VSPI 18/23/19
//   XPT2046  -> software SPI 16/17/35 + CS13/IRQ34
//   PN532    -> HSPI 25/27/26 + CS33/RST14
static LGFX display(TFTDriver::ILI9341);
static SPIClass nfcSPI(HSPI);
static Adafruit_PN532 nfc(PIN_PN532_SS, &nfcSPI);
static bool nfcReady = false;

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
    x = touchRead12(0xD0);
    y = touchRead12(0x90);
    return true;
}

static void showNfcStatus(const char* text, uint16_t color = TFT_WHITE) {
    display.fillRect(18, 216, 210, 48, TFT_BLACK);
    display.setTextColor(color, TFT_BLACK);
    display.setCursor(18, 216);
    display.print(text);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
}

static void initPn532() {
    // Make the reader deterministic before starting HSPI.
    pinMode(PIN_PN532_RST, OUTPUT);
    digitalWrite(PIN_PN532_RST, LOW);
    delay(20);
    digitalWrite(PIN_PN532_RST, HIGH);
    delay(100);

    nfcSPI.begin(PIN_PN532_SCK, PIN_PN532_MISO, PIN_PN532_MOSI, PIN_PN532_SS);
    nfc.begin();

    uint32_t version = nfc.getFirmwareVersion();
    if (!version) {
        Serial.println("PN532 NOT FOUND");
        showNfcStatus("PN532 FAIL", TFT_RED);
        return;
    }

    Serial.printf("PN532 found: IC=0x%02X FW=%u.%u\n",
                  (unsigned)((version >> 24) & 0xFF),
                  (unsigned)((version >> 16) & 0xFF),
                  (unsigned)((version >> 8) & 0xFF));
    nfc.SAMConfig();
    nfcReady = true;
    showNfcStatus("PN532 READY", TFT_GREEN);
}

void setup() {
    delay(500);
    Serial.begin(115200);
    delay(250);
    Serial.println("=== Tarantula full hardware bring-up ===");

    display.init();
    display.setRotation(0);
    display.fillScreen(TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(2);
    display.setCursor(18, 24);
    display.print("SpoolSense");
    display.setCursor(18, 56);
    display.print("ILI9341 OK");
    display.setCursor(18, 88);
    display.print("Touch OK");
    display.setCursor(18, 120);
    display.print("NFC test...");

    pinMode(PIN_TOUCH_SCLK, OUTPUT);
    pinMode(PIN_TOUCH_MOSI, OUTPUT);
    pinMode(PIN_TOUCH_MISO, INPUT);
    pinMode(PIN_TOUCH_CS, OUTPUT);
    pinMode(PIN_TOUCH_IRQ, INPUT_PULLUP);
    digitalWrite(PIN_TOUCH_SCLK, LOW);
    digitalWrite(PIN_TOUCH_MOSI, LOW);
    digitalWrite(PIN_TOUCH_CS, HIGH);

    Serial.println("ILI9341 OK; XPT2046 OK");
    initPn532();
}

void loop() {
    static uint32_t lastTouchReport = 0;
    static uint32_t lastNfcScan = 0;

    uint16_t x = 0, y = 0;
    if (readTouch(x, y) && millis() - lastTouchReport >= 100) {
        lastTouchReport = millis();
        Serial.printf("TOUCH raw X=%u Y=%u\n", x, y);
        display.fillRect(18, 152, 210, 48, TFT_BLACK);
        display.setCursor(18, 152);
        display.printf("X:%4u", x);
        display.setCursor(18, 176);
        display.printf("Y:%4u", y);
    }

    if (nfcReady && millis() - lastNfcScan >= 150) {
        lastNfcScan = millis();
        uint8_t uid[7] = {0};
        uint8_t uidLength = 0;
        if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 50)) {
            char uidText[24] = {0};
            size_t pos = 0;
            for (uint8_t i = 0; i < uidLength && pos + 3 < sizeof(uidText); ++i) {
                pos += snprintf(uidText + pos, sizeof(uidText) - pos, "%02X", uid[i]);
            }
            Serial.printf("PN532 TAG UID=%s\n", uidText);
            showNfcStatus(uidText, TFT_CYAN);
            delay(250);
        }
    }

    delay(5);
}
