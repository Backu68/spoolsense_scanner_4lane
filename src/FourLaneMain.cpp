#if defined(SPOOLSENSE_4LANE_PN532)

#include <Arduino.h>
#include "FourLanePN532Manager.h"

// Dedicated bring-up entry point for the 4-lane PN532 prototype.
static FourLanePN532Manager fourLaneNfc;

void setup() {
    delay(500);
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("=== SpoolSense 4-Lane PN532 hardware bring-up ===");
    Serial.println("This build is intentionally serial-only; stock SpoolSense remains unchanged.");
    Serial.println("Lane CS: L1=14 L2=18 L3=32 L4=33");

    if (!fourLaneNfc.begin()) {
        Serial.println("FourLanePN532: no readers initialized; continuing for diagnostics");
    }

    Serial.println("FourLanePN532: polling started");
}

void loop() {
    fourLaneNfc.poll();
    delay(2);
}

#endif // SPOOLSENSE_4LANE_PN532
