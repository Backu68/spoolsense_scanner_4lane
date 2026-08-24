#pragma once

#if defined(SPOOLSENSE_4LANE_PN532)

#include <Arduino.h>
#include <Adafruit_PN532.h>

class FourLanePN532Manager {
public:
    static constexpr uint8_t LANE_COUNT = 4;

    // ESP32-WROOM wiring for the BoxTurtle 4-lane prototype.
    static constexpr uint8_t PIN_RST  = 13;
    static constexpr uint8_t PIN_SCK  = 25;
    static constexpr uint8_t PIN_MISO = 26;
    static constexpr uint8_t PIN_MOSI = 27;

    bool begin();
    void poll();

    bool isLaneReady(uint8_t lane) const;

private:
    struct LaneState {
        Adafruit_PN532* reader = nullptr;
        bool ready = false;
        bool present = false;
        uint8_t uid[10] = {0};
        uint8_t uidLength = 0;
        uint8_t absentMisses = 0;
    };

    static constexpr uint8_t CS_PINS[LANE_COUNT] = {14, 16, 17, 18};
    static constexpr uint8_t REMOVE_MISS_THRESHOLD = 3;

    LaneState lanes_[LANE_COUNT];
    uint8_t nextLane_ = 0;

    void pollLane(uint8_t laneIndex);
    static void printUid(const uint8_t* uid, uint8_t uidLength);
};

#endif // SPOOLSENSE_4LANE_PN532
