#include "MemoryDiagnostics.h"
#include "LogBuffer.h"
#include <esp_heap_caps.h>

static constexpr uint32_t kReportIntervalMs = 5000;

static const char* const kTaskNames[MemoryDiagnostics::MAX_TRACKED] = {
    "loopTask",
    "NFCScanTask",
    "SpoolmanSync",
    "HATask",
    "TFTTask",
    "LCDTask",
    "LEDTask",
    "PrinterPoll",
};

namespace {
struct Slot {
    volatile uint32_t hwmBytes;
    volatile uint32_t lastReportMs;  // 0 = never reported
};
}
static Slot slots_[MemoryDiagnostics::MAX_TRACKED];

void MemoryDiagnostics::reportSelf(Task t) {
    size_t i = (size_t)t;
    if (i >= MAX_TRACKED) {
        return;
    }
    uint32_t now = millis();
    if (slots_[i].lastReportMs != 0 && now - slots_[i].lastReportMs < kReportIntervalMs) {
        return;
    }
    // ESP-IDF StackType_t is uint8_t, so the high-water mark is already in bytes
    slots_[i].hwmBytes = (uint32_t)uxTaskGetStackHighWaterMark(nullptr);
    slots_[i].lastReportMs = (now != 0) ? now : 1;
}

size_t MemoryDiagnostics::collect(TaskStackStat* out, size_t maxOut) {
    size_t count = 0;
    for (size_t i = 0; i < MAX_TRACKED && count < maxOut; i++) {
        if (slots_[i].lastReportMs == 0) {
            continue;
        }
        out[count].name = kTaskNames[i];
        out[count].stackHighWaterBytes = slots_[i].hwmBytes;
        count++;
    }
    return count;
}

void MemoryDiagnostics::logStats() {
    // ESP.getFreeHeap() blends PSRAM into the totals on -S3 boards; the internal-only
    // numbers are where task stacks, WiFi buffers, and TLS actually live
    LogBuffer::getInstance().logPrintf("MEM: heap free=%u min=%u largest=%u | internal free=%u min=%u largest=%u\n",
                                       (unsigned)ESP.getFreeHeap(),
                                       (unsigned)ESP.getMinFreeHeap(),
                                       (unsigned)ESP.getMaxAllocHeap(),
                                       (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                                       (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));

    TaskStackStat stats[MAX_TRACKED];
    size_t count = collect(stats, MAX_TRACKED);

    char line[192];
    line[0] = '\0';
    size_t pos = 0;
    for (size_t i = 0; i < count; i++) {
        int written = snprintf(line + pos, sizeof(line) - pos, "%s=%u ",
                               stats[i].name, (unsigned)stats[i].stackHighWaterBytes);
        if (written < 0 || pos + (size_t)written >= sizeof(line)) {
            break;
        }
        pos += (size_t)written;
    }
    LogBuffer::getInstance().logPrintf("MEM: stack-hwm %s\n", line);
}
