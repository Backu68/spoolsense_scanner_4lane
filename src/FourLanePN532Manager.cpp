#if defined(SPOOLSENSE_4LANE_PN532)

#include "FourLanePN532Manager.h"
#include <cstring>

constexpr uint8_t FourLanePN532Manager::CS_PINS[FourLanePN532Manager::LANE_COUNT];

bool FourLanePN532Manager::begin() {
    Serial.println("FourLanePN532: initializing shared SPI readers");
    Serial.printf("  SPI SCK=%u MISO=%u MOSI=%u shared RST=%u\n",
                  PIN_SCK, PIN_MISO, PIN_MOSI, PIN_RST);

    // Keep every reader deselected before the SPI bus is initialized. This is
    // especially important when some lanes are physically absent during bench testing.
    for (uint8_t i = 0; i < LANE_COUNT; ++i) {
        pinMode(CS_PINS[i], OUTPUT);
        digitalWrite(CS_PINS[i], HIGH);
    }

    bool anyReady = false;
    for (uint8_t i = 0; i < LANE_COUNT; ++i) {
        Serial.printf("FourLanePN532: lane %u CS=%u init...\n", i + 1, CS_PINS[i]);
        lanes_[i].reader = new HardwareNFCConnectionPN532(
            PIN_RST, CS_PINS[i], PIN_SCK, PIN_MOSI, PIN_MISO);

        if (lanes_[i].reader == nullptr) {
            Serial.printf("FourLanePN532: lane %u allocation FAILED\n", i + 1);
            continue;
        }

        lanes_[i].ready = lanes_[i].reader->begin();
        if (!lanes_[i].ready) {
            Serial.printf("FourLanePN532: lane %u reader NOT FOUND\n", i + 1);
            continue;
        }

        char readerInfo[48] = {0};
        lanes_[i].reader->getReaderInfo(readerInfo, sizeof(readerInfo));
        Serial.printf("FourLanePN532: lane %u READY - %s\n", i + 1, readerInfo);
        anyReady = true;
    }

    Serial.printf("FourLanePN532: init complete (%s)\n",
                  anyReady ? "at least one reader ready" : "NO READERS FOUND");
    return anyReady;
}

void FourLanePN532Manager::poll() {
    // Exactly one PN532 transaction chain per call. This keeps access strictly
    // serialized because Adafruit_PN532 uses a file-scope packet buffer shared
    // by every PN532 object.
    pollLane(nextLane_);
    nextLane_ = (nextLane_ + 1) % LANE_COUNT;
}

bool FourLanePN532Manager::isLaneReady(uint8_t lane) const {
    if (lane < 1 || lane > LANE_COUNT) return false;
    return lanes_[lane - 1].ready;
}

void FourLanePN532Manager::pollLane(uint8_t laneIndex) {
    LaneState& lane = lanes_[laneIndex];
    if (!lane.ready || lane.reader == nullptr) return;

    uint8_t uid[10] = {0};
    uint8_t uidLength = 0;
    const bool found = lane.reader->detectTag(uid, &uidLength);

    if (found && uidLength > 0) {
        lane.absentMisses = 0;

        const bool changed = !lane.present ||
                             lane.uidLength != uidLength ||
                             memcmp(lane.uid, uid, uidLength) != 0;

        if (changed) {
            lane.present = true;
            lane.uidLength = uidLength;
            memcpy(lane.uid, uid, uidLength);

            Serial.printf("LANE %u TAG UID=", laneIndex + 1);
            printUid(uid, uidLength);
            Serial.println();
        }
        return;
    }

    if (!lane.present) return;

    if (++lane.absentMisses >= REMOVE_MISS_THRESHOLD) {
        lane.present = false;
        lane.absentMisses = 0;
        lane.uidLength = 0;
        memset(lane.uid, 0, sizeof(lane.uid));
        Serial.printf("LANE %u TAG REMOVED\n", laneIndex + 1);
    }
}

void FourLanePN532Manager::printUid(const uint8_t* uid, uint8_t uidLength) {
    for (uint8_t i = 0; i < uidLength; ++i) {
        if (uid[i] < 0x10) Serial.print('0');
        Serial.print(uid[i], HEX);
    }
}

#endif // SPOOLSENSE_4LANE_PN532
