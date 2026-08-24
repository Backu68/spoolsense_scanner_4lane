#include "FourLanePN532Manager.h"

#ifdef SPOOLSENSE_4LANE_PN532

#include "BoardPins.h"
#include "HomeAssistantManager.h"
#include "TagStateJson.h"

#include <Arduino.h>
#include <cstring>

FourLanePN532Manager& FourLanePN532Manager::getInstance() {
    static FourLanePN532Manager instance;
    return instance;
}

bool FourLanePN532Manager::begin() {
    static const uint8_t csPins[LANE_COUNT] = {
        PIN_PN532_LANE1_SS,
        PIN_PN532_LANE2_SS,
        PIN_PN532_LANE3_SS,
        PIN_PN532_LANE4_SS,
    };

    // Deselect every PN532 before SPI starts. With four powered readers on one
    // bus, one floating CS is enough to corrupt every transaction.
    for (uint8_t i = 0; i < LANE_COUNT; ++i) {
        pinMode(csPins[i], OUTPUT);
        digitalWrite(csPins[i], HIGH);
    }

    bool anyReady = false;
    for (uint8_t i = 0; i < LANE_COUNT; ++i) {
        lanes_[i].reader = new HardwareNFCConnectionPN532(
            PIN_PN532_RST,
            csPins[i],
            PIN_PN532_SCK,
            PIN_PN532_MOSI,
            PIN_PN532_MISO);

        if (!lanes_[i].reader) {
            Serial.printf("4Lane PN532: lane %u allocation failed\n", i + 1);
            continue;
        }

        lanes_[i].ready = lanes_[i].reader->begin();
        Serial.printf("4Lane PN532: lane %u CS=%u %s\n",
                      i + 1, csPins[i], lanes_[i].ready ? "READY" : "FAILED");
        anyReady |= lanes_[i].ready;
    }

    if (!anyReady) {
        Serial.println("4Lane PN532: no readers initialized");
        return false;
    }

    Serial.println("4Lane PN532: initialized; sequential UID polling enabled");
    return true;
}

void FourLanePN532Manager::startTask() {
    if (taskHandle_) return;
    BaseType_t rc = xTaskCreatePinnedToCore(
        taskFunc,
        "NFC4Lane",
        6144,
        this,
        1,
        &taskHandle_,
        1);
    if (rc != pdPASS) {
        taskHandle_ = nullptr;
        Serial.println("4Lane PN532: failed to start scan task");
        return;
    }
    Serial.println("4Lane PN532: scan task started");
}

bool FourLanePN532Manager::isLaneReady(uint8_t lane) const {
    return lane < LANE_COUNT && lanes_[lane].ready;
}

bool FourLanePN532Manager::isLanePresent(uint8_t lane) const {
    return lane < LANE_COUNT && lanes_[lane].present;
}

const char* FourLanePN532Manager::getLaneUid(uint8_t lane) const {
    if (lane >= LANE_COUNT) return "";
    return lanes_[lane].uidHex;
}

void FourLanePN532Manager::taskFunc(void* param) {
    static_cast<FourLanePN532Manager*>(param)->taskLoop();
}

void FourLanePN532Manager::taskLoop() {
    while (true) {
        for (uint8_t lane = 0; lane < LANE_COUNT; ++lane) {
            scanLane(lane);
            // Yield between readers. PN532 detectTag itself can wait up to
            // 100 ms, so this keeps WiFi/MQTT responsive without parallel SPI.
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        const bool mqttConnected = HomeAssistantManager::getInstance().isConnected();
        if (mqttConnected && !lastMqttConnected_) {
            // The HA/MQTT task intentionally drops queued publishes while
            // disconnected. Republish every lane on reconnect so retained
            // virtual-scanner state is always repaired.
            publishAllLanes();
        }
        lastMqttConnected_ = mqttConnected;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void FourLanePN532Manager::scanLane(uint8_t lane) {
    if (lane >= LANE_COUNT) return;
    LaneState& state = lanes_[lane];
    if (!state.ready || !state.reader) return;

    uint8_t uid[10] = {0};
    uint8_t uidLength = 0;
    const bool found = state.reader->detectTag(uid, &uidLength);

    if (found && uidLength > 0) {
        state.absentMisses = 0;
        const bool changed = !state.present ||
                             state.uidLength != uidLength ||
                             memcmp(state.uid, uid, uidLength) != 0;

        if (changed) {
            state.present = true;
            state.uidLength = uidLength > sizeof(state.uid) ? sizeof(state.uid) : uidLength;
            memcpy(state.uid, uid, state.uidLength);
            uidToHex(state.uid, state.uidLength, state.uidHex, sizeof(state.uidHex));
            state.reader->setCurrentUid(state.uid, state.uidLength);

            Serial.printf("4Lane PN532: lane %u tag %s\n", lane + 1, state.uidHex);
            publishLane(lane, true);
        }
        return;
    }

    if (!state.present) {
        if (!state.initialPublished) {
            publishLane(lane, true);
        }
        return;
    }

    if (++state.absentMisses >= REMOVE_MISS_THRESHOLD) {
        Serial.printf("4Lane PN532: lane %u tag removed (%s)\n", lane + 1, state.uidHex);
        state.present = false;
        state.uidLength = 0;
        state.uid[0] = 0;
        state.uidHex[0] = '\0';
        state.absentMisses = 0;
        publishLane(lane, true);
    }
}

void FourLanePN532Manager::publishLane(uint8_t lane, bool force) {
    if (lane >= LANE_COUNT) return;
    LaneState& state = lanes_[lane];

    // Always mark the initial state as observed. If MQTT is not connected yet,
    // publishAllLanes() will repair retained state on the connection edge.
    state.initialPublished = true;

    auto& ha = HomeAssistantManager::getInstance();
    if (!ha.isConfigured() || (!force && !ha.isConnected())) return;

    HAPublishRequest req{};
    char baseId[7];
    HomeAssistantManager::getDeviceId(baseId, sizeof(baseId));
    snprintf(req.topic, sizeof(req.topic),
             "spoolsense/%s-L%u/tag/state", baseId, lane + 1);

    if (state.present) {
        TagStateFields fields{};
        strncpy(fields.uid, state.uidHex, sizeof(fields.uid) - 1);
        fields.present = true;
        fields.tag_data_valid = false;
        fields.tag_format = "uid_only";
        fields.material_type[0] = '\0';
        fields.material_name[0] = '\0';
        fields.color[0] = '\0';
        fields.manufacturer[0] = '\0';
        fields.remaining_g = 0.0f;
        fields.initial_weight_g = 0.0f;
        fields.spoolman_id = -1;
        fields.blank = false;
        buildTagStateJson(req.payload, sizeof(req.payload), fields);
    } else {
        buildEmptyTagStateJson(req.payload, sizeof(req.payload));
    }

    req.retained = true;
    if (!ha.enqueuePublish(req)) {
        Serial.printf("4Lane PN532: lane %u MQTT publish queue full\n", lane + 1);
    }
}

void FourLanePN532Manager::publishAllLanes() {
    for (uint8_t lane = 0; lane < LANE_COUNT; ++lane) {
        if (lanes_[lane].ready) publishLane(lane, true);
    }
}

void FourLanePN532Manager::uidToHex(const uint8_t* uid, uint8_t uidLength,
                                    char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!uid) return;

    size_t pos = 0;
    for (uint8_t i = 0; i < uidLength && pos + 2 < outSize; ++i) {
        int written = snprintf(out + pos, outSize - pos, "%02X", uid[i]);
        if (written != 2) break;
        pos += 2;
    }
}

#endif // SPOOLSENSE_4LANE_PN532
