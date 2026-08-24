#pragma once

#ifdef SPOOLSENSE_4LANE_PN532

#include <cstdint>
#include "HardwareNFCConnectionPN532.h"

#ifndef NATIVE_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

// Four-lane PN532 monitor for Box Turtle / AFC.
//
// One ESP32 owns four PN532 readers on a shared SPI bus. SCK/MOSI/MISO and RST
// are shared, while each reader has its own chip-select. Access is strictly
// serialized because Adafruit_PN532 uses a file-scope packet buffer.
//
// Phase 1 intentionally publishes UID-only tag/state payloads. That is enough
// for SpoolSense middleware to resolve each tag through Spoolman and activate a
// dedicated AFC lane. Rich-tag read/write is layered on after the four-reader
// hardware path is proven stable.
class FourLanePN532Manager {
public:
    static constexpr uint8_t LANE_COUNT = 4;

    static FourLanePN532Manager& getInstance();

    bool begin();
    void startTask();
    bool isLaneReady(uint8_t lane) const;
    bool isLanePresent(uint8_t lane) const;
    const char* getLaneUid(uint8_t lane) const;

private:
    FourLanePN532Manager() = default;
    FourLanePN532Manager(const FourLanePN532Manager&) = delete;
    FourLanePN532Manager& operator=(const FourLanePN532Manager&) = delete;

    struct LaneState {
        HardwareNFCConnectionPN532* reader = nullptr;
        bool ready = false;
        bool present = false;
        bool initialPublished = false;
        uint8_t uid[10] = {0};
        uint8_t uidLength = 0;
        char uidHex[21] = {0};
        uint8_t absentMisses = 0;
    };

    LaneState lanes_[LANE_COUNT];
    TaskHandle_t taskHandle_ = nullptr;
    bool lastMqttConnected_ = false;

    static constexpr uint8_t REMOVE_MISS_THRESHOLD = 3;

    static void taskFunc(void* param);
    void taskLoop();
    void scanLane(uint8_t lane);
    void publishLane(uint8_t lane, bool force = false);
    void publishAllLanes();
    static void uidToHex(const uint8_t* uid, uint8_t uidLength, char* out, size_t outSize);
};

#endif // SPOOLSENSE_4LANE_PN532
