#ifndef MEMORY_DIAGNOSTICS_H
#define MEMORY_DIAGNOSTICS_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Heap + per-task stack instrumentation. Each task reports its own high-water
// mark via reportSelf() from inside its loop — a running task can never be a
// stale handle, and the last report survives task stop (HA reconnect, TFT
// freeForOTA), which is what a stack audit wants.
//
// Each slot is written only by its own task and read from the Arduino loop
// task; 32-bit aligned writes are atomic on ESP32, so no locking is needed.
class MemoryDiagnostics {
public:
    MemoryDiagnostics() = delete;

    enum class Task : uint8_t {
        Loop,
        NFCScan,
        SpoolmanSync,
        HA,
        TFT,
        LCD,
        LED,
        PrinterPoll,
        COUNT
    };

    static constexpr size_t MAX_TRACKED = (size_t)Task::COUNT;

    struct TaskStackStat {
        const char* name;
        uint32_t stackHighWaterBytes;
    };

    // Call from inside the owning task's loop; rate-limited internally so the
    // stack scan runs at most every 5s per task
    static void reportSelf(Task t);

    // Fills out[] with stats for tasks that have reported, returns count
    static size_t collect(TaskStackStat* out, size_t maxOut);

    // Log heap stats + per-task stack high-water marks to serial/LogBuffer
    static void logStats();
};

#endif // MEMORY_DIAGNOSTICS_H
