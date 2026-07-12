#pragma once

// Self-test / NFC signal-quality wizard (#253). Owns a single diagnostic
// session and runs it on a dedicated low-priority FreeRTOS task so long checks
// never block the web server or the app loop. Read-only by default. The NFC
// stability stage cooperatively pauses the scan task (never a mid-SPI suspend)
// to take exclusive ownership of the reader.

#include "DiagnosticsTypes.h"

#ifndef NATIVE_TEST
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#endif

class NFCManager;

class DiagnosticsManager {
public:
    static DiagnosticsManager& getInstance();

    struct Options {
        bool network = true;    // wifi/mqtt/spoolman/printer reachability
        bool stability = true;  // 100-cycle detection + read stability (needs a tag)
    };

    // Start a session on the worker task. Returns false if one is already active.
    bool startSession(const Options& opts);
    // Cancel the active session and restore normal scanning.
    void cancelSession();
    // Advance a WAITING_FOR_USER stage (user placed the tag / acknowledged).
    void submitUserContinue();

    // Immutable snapshot for the GET endpoint — copied out under the lock.
    struct Snapshot {
        DiagnosticStatus overall;
        bool     active;
        bool     waiting_for_user;
        char     stage_prompt[96];
        uint8_t  result_count;
        DiagnosticResult results[(size_t)DiagnosticTest::TEST_COUNT];
        uint8_t  stability_score;      // 0 if the stability stage did not run
        bool     stability_ran;
    };
    void getSnapshot(Snapshot& out);

    // Render the sanitized text support report. Returns bytes written (excl NUL).
    size_t buildReport(char* buf, size_t buflen);

    // Short display names for the JSON/report — shared by the endpoint.
    static const char* testName(DiagnosticTest t);
    static const char* statusName(DiagnosticStatus s);

private:
    DiagnosticsManager() = default;
    DiagnosticsManager(const DiagnosticsManager&) = delete;
    DiagnosticsManager& operator=(const DiagnosticsManager&) = delete;

    void ensureLock();
    void lock();
    void unlock();

    void addResult(DiagnosticTest t, DiagnosticStatus s, int32_t code,
                   uint32_t duration_ms, const char* summary, const char* recommendation);
    void setPrompt(const char* p);
    bool waitForUser(uint32_t timeout_ms);   // returns false on cancel/timeout

    static void sessionTaskFunc(void* param);
    void runSession();

    // Individual checks (each appends one DiagnosticResult).
    void checkDeviceInfo();
    void checkResetReason();
    void checkHeapHealth();
    void checkTaskStacks();
    void resumeScanAndWait(NFCManager& nfc);
    void checkReaderInit();
    void checkReaderVersion();
    void checkReaderRegisters();
    void checkWifi();
    void checkMqtt();
    void checkSpoolman();
    void checkPrinter();
    void runStabilityStage();   // pauses scan, drives detection/read, scores

#ifndef NATIVE_TEST
    SemaphoreHandle_t lock_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
#endif
    volatile bool active_ = false;
    volatile bool cancelRequested_ = false;
    volatile bool userContinue_ = false;
    volatile bool waitingForUser_ = false;
    char stagePrompt_[96] = {0};
    Options opts_;
    DiagnosticResult results_[(size_t)DiagnosticTest::TEST_COUNT];
    uint8_t resultCount_ = 0;
    uint8_t stabilityScore_ = 0;
    bool stabilityRan_ = false;
    ReaderDiagnostics readerSnap_ = {};   // captured during reader checks, reused in report
    bool readerSnapValid_ = false;
};
