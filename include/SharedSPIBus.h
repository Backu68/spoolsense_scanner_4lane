#pragma once

#include <Arduino.h>
#include <SPI.h>

// Shared GP-SPI ownership for boards that explicitly opt in with
// BOARD_SHARED_SPI. Separate-bus WROOM/S3 builds compile to no-op guards.
namespace SharedSPIBus {

static constexpr uint32_t LOCK_TIMEOUT_MS = 3500;

#if defined(BOARD_SHARED_SPI)

// Prepare both chip selects HIGH and initialize the global Arduino SPI bus if
// no owner has done so yet. The TFT path instead calls adoptInitializedBus()
// after LovyanGFX initializes that same global bus.
bool prepareChipSelects(int16_t nfcCs, int16_t tftCs);
bool begin(int8_t sck, int8_t miso, int8_t mosi, int16_t nfcCs, int16_t tftCs);
bool adoptInitializedBus(int16_t nfcCs, int16_t tftCs);
SPIClass& bus();
bool take(TickType_t timeout = pdMS_TO_TICKS(LOCK_TIMEOUT_MS));
void give();

class Guard {
public:
    explicit Guard(TickType_t timeout = pdMS_TO_TICKS(LOCK_TIMEOUT_MS))
        : locked_(take(timeout)) {}
    ~Guard() { if (locked_) give(); }

    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    explicit operator bool() const { return locked_; }

private:
    bool locked_;
};

#else

inline SPIClass& bus() { return SPI; }

class Guard {
public:
    explicit Guard(TickType_t = 0) {}
    explicit operator bool() const { return true; }
};

#endif

} // namespace SharedSPIBus
