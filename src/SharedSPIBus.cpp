#include "SharedSPIBus.h"

#if defined(BOARD_SHARED_SPI)

namespace SharedSPIBus {
namespace {

int16_t s_nfcCs = -1;
int16_t s_tftCs = -1;

SemaphoreHandle_t mutexHandle() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
    return mutex;
}

void setChipSelectHigh(int16_t pin) {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
}

bool configureChipSelectsUnlocked(int16_t nfcCs, int16_t tftCs) {
    if (nfcCs < 0 || tftCs < 0 || nfcCs == tftCs) {
        Serial.printf("SharedSPI: invalid chip selects NFC=%d TFT=%d\n", nfcCs, tftCs);
        return false;
    }
    s_nfcCs = nfcCs;
    s_tftCs = tftCs;
    setChipSelectHigh(s_nfcCs);
    setChipSelectHigh(s_tftCs);
    return true;
}

void deselectAll() {
    if (s_nfcCs >= 0) digitalWrite(s_nfcCs, HIGH);
    if (s_tftCs >= 0) digitalWrite(s_tftCs, HIGH);
}

} // namespace

SPIClass& bus() {
    return SPI;
}

bool prepareChipSelects(int16_t nfcCs, int16_t tftCs) {
    SemaphoreHandle_t mutex = mutexHandle();
    if (!mutex || xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        Serial.println("SharedSPI: failed to acquire chip-select lock");
        return false;
    }
    bool ok = configureChipSelectsUnlocked(nfcCs, tftCs);
    deselectAll();
    xSemaphoreGiveRecursive(mutex);
    return ok;
}

bool begin(int8_t sck, int8_t miso, int8_t mosi, int16_t nfcCs, int16_t tftCs) {
    SemaphoreHandle_t mutex = mutexHandle();
    if (!mutex || xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        Serial.println("SharedSPI: failed to acquire initialization lock");
        return false;
    }

    bool ok = configureChipSelectsUnlocked(nfcCs, tftCs);
    if (ok && SPI.bus() == nullptr) {
        ok = SPI.begin(sck, miso, mosi, -1);
        if (!ok) Serial.println("SharedSPI: Arduino SPI initialization failed");
    }
    deselectAll();
    xSemaphoreGiveRecursive(mutex);
    return ok && SPI.bus() != nullptr;
}

bool adoptInitializedBus(int16_t nfcCs, int16_t tftCs) {
    SemaphoreHandle_t mutex = mutexHandle();
    if (!mutex || xSemaphoreTakeRecursive(mutex, pdMS_TO_TICKS(LOCK_TIMEOUT_MS)) != pdTRUE) {
        Serial.println("SharedSPI: failed to acquire adoption lock");
        return false;
    }

    bool ok = configureChipSelectsUnlocked(nfcCs, tftCs) && SPI.bus() != nullptr;
    if (!ok) Serial.println("SharedSPI: LovyanGFX did not initialize the Arduino SPI bus");
    deselectAll();
    xSemaphoreGiveRecursive(mutex);
    return ok;
}

bool take(TickType_t timeout) {
    SemaphoreHandle_t mutex = mutexHandle();
    if (!mutex || xSemaphoreTakeRecursive(mutex, timeout) != pdTRUE) return false;
    deselectAll();
    return true;
}

void give() {
    deselectAll();
    SemaphoreHandle_t mutex = mutexHandle();
    if (mutex) xSemaphoreGiveRecursive(mutex);
}

} // namespace SharedSPIBus

#endif
