// Native unit tests for PrinterManager state machine.
// Tests the IDLE→TRACKING state machine, filament deduction, job lifecycle,
// grace periods, and deferred filament lookup.
//
// Build: make test_printer_manager
// Run:   ./test_printer_manager

#define NATIVE_TEST 1

#include "test_helpers.h"
#include "platform/NativePlatform.h"
#include "MockPrinterStrategy.h"

// Define header guards BEFORE including production code — stubs replace real headers.
// When PrinterManager.cpp includes ConfigurationManager.h, the guard is already set,
// so the real header is skipped and the stub below is used instead.
//
// KNOWN LIMITATION: the stubbed contracts (AppMessage, AppMessageType, the manager
// APIs below) are local copies. If the production versions drift, this test still
// compiles against the copies and can false-pass. When changing AppMessage or the
// ApplicationManager/NFCManager surfaces PrinterManager uses, update these stubs
// in the same commit. (Real types with no heavy deps — NFCTypes.h — are included
// directly instead of copied.)
#define CONFIGURATION_MANAGER_H
#define APPLICATION_MANAGER_H
#define NFC_MANAGER_H

// Real type definitions PrinterManager.cpp pulls in directly (data-only header)
#include "NFCTypes.h"

// Minimal stubs for headers PrinterManager includes
namespace ConfigStub {
    static uint32_t pollIntervalMs = 10000;
}

// --- Stub ConfigurationManager ---
class ConfigurationManager {
public:
    static ConfigurationManager& getInstance() { static ConfigurationManager inst; return inst; }
    uint32_t getPollIntervalMs() const { return ConfigStub::pollIntervalMs; }
    bool isPrusaLinkEnabled() const { return true; }
    const char* getPrusaLinkURL() const { return "http://test"; }
    const char* getPrusaLinkAPIKey() const { return "key"; }
};

// --- Capture messages ---
#include <vector>

enum class AppMessageType {
    PRINT_STARTED,
    PRINT_ENDED,
    SPOOL_DETECTED,
    SPOOL_UPDATED,
    BLANK_TAG_DETECTED,
    GENERIC_TAG_DETECTED,
    SPOOLMAN_SYNCED,
    TAG_REMOVED,
    HA_WRITE_TAG,
    HA_UPDATE_REMAINING,
    PRINTER_WARNING,
};

struct PrinterWarningPayload {
    char warning_type[24];
    char expected[32];
    char actual[32];
    float gcode_temp;
    int16_t tag_max_temp;
};

struct AppMessage {
    AppMessageType type;
    union {
        struct { int job_id; } printStarted;
        struct {
            int job_id;
            float filament_used_grams;
            bool canceled;
            int tool_count;
            float filament_per_tool[IPrinterStrategy::MAX_TOOLS];
        } printEnded;
        PrinterWarningPayload printerWarning;
    } payload;
};

static std::vector<AppMessageType> capturedTypes;
static std::vector<float> capturedFilament;
static std::vector<bool> capturedCanceled;
static std::vector<int> capturedJobIds;
static std::vector<int> capturedToolCounts;
static std::vector<std::vector<float>> capturedPerTool;

// --- Stub NFCManager (CurrentSpoolState comes from the real NFCTypes.h) ---
class NFCManager {
public:
    static NFCManager& getInstance() { static NFCManager inst; return inst; }
    bool getCurrentSpoolState(CurrentSpoolState& out) {
        out.present = false;
        out.tag_data_valid = false;
        return false;
    }
};

class ApplicationManager {
public:
    static ApplicationManager& getInstance() { static ApplicationManager inst; return inst; }
    bool sendMessage(const AppMessage& msg, uint32_t = 0) {
        capturedTypes.push_back(msg.type);
        if (msg.type == AppMessageType::PRINT_STARTED) {
            capturedJobIds.push_back(msg.payload.printStarted.job_id);
        }
        if (msg.type == AppMessageType::PRINT_ENDED) {
            capturedFilament.push_back(msg.payload.printEnded.filament_used_grams);
            capturedCanceled.push_back(msg.payload.printEnded.canceled);
            capturedJobIds.push_back(msg.payload.printEnded.job_id);
            capturedToolCounts.push_back(msg.payload.printEnded.tool_count);
            std::vector<float> tools;
            for (int i = 0; i < msg.payload.printEnded.tool_count; i++) {
                tools.push_back(msg.payload.printEnded.filament_per_tool[i]);
            }
            capturedPerTool.push_back(tools);
        }
        return true;
    }
    void publishToHA(const char*, const char*, bool) {}
};

// Include the REAL production PrinterManager — stubs above satisfy its dependencies.
// This ensures tests exercise actual production logic, not a drifting copy.
#include "../../src/PrinterManager.h"
#include "../../src/PrinterManager.cpp"

// =========================================================================
// Test helpers
// =========================================================================

static MockPrinterStrategy mock;
static PrinterManager& pm = PrinterManager::getInstance();

static void reset() {
    capturedTypes.clear();
    capturedFilament.clear();
    capturedCanceled.clear();
    capturedJobIds.clear();
    capturedToolCounts.clear();
    capturedPerTool.clear();
    mock = MockPrinterStrategy();
    pm.begin();
    pm.setStrategy(&mock);
}

// =========================================================================
// Tests
// =========================================================================

bool test_idle_no_job() {
    reset();
    mock.setIdle();
    pm.poll();
    TEST_ASSERT_EQ(pm.getState(), PrinterState::IDLE);
    TEST_ASSERT_EQ(capturedTypes.size(), 0u);
    return true;
}

bool test_detect_print_start() {
    reset();
    mock.setPrinting(42, 9.18f);
    pm.poll();
    TEST_ASSERT_EQ(pm.getState(), PrinterState::TRACKING);
    TEST_ASSERT_EQ(capturedTypes.size(), 1u);
    TEST_ASSERT_EQ(capturedTypes[0], AppMessageType::PRINT_STARTED);
    TEST_ASSERT_EQ(capturedJobIds[0], 42);
    return true;
}

bool test_print_finished_100_percent() {
    reset();
    mock.setPrinting(42, 9.18f);
    pm.poll();  // detect start

    mock.setFinished(100.0f);
    pm.poll();  // detect finish

    TEST_ASSERT_EQ(pm.getState(), PrinterState::IDLE);
    TEST_ASSERT_EQ(capturedTypes.size(), 2u);
    TEST_ASSERT_EQ(capturedTypes[1], AppMessageType::PRINT_ENDED);
    TEST_ASSERT_FLOAT_NEAR(capturedFilament[0], 9.18f, 0.01f);
    TEST_ASSERT_EQ(capturedCanceled[0], false);
    return true;
}

bool test_print_canceled_at_30_percent() {
    reset();
    mock.setPrinting(42, 9.18f, 30.0f);
    pm.poll();  // detect start

    mock.setStopped(30.0f);
    pm.poll();  // detect stop

    TEST_ASSERT_EQ(pm.getState(), PrinterState::IDLE);
    TEST_ASSERT_FLOAT_NEAR(capturedFilament[0], 9.18f * 0.30f, 0.01f);
    TEST_ASSERT_EQ(capturedCanceled[0], true);
    return true;
}

bool test_job_disappeared_under_95_is_canceled() {
    reset();
    mock.setPrinting(42, 9.18f, 0.0f);
    pm.poll();  // start → TRACKING, lastProgress=0

    mock.setPrinting(42, 9.18f, 50.0f);
    pm.poll();  // update progress to 50%

    mock.clearJob();
    pm.poll();  // grace 1
    TEST_ASSERT_EQ(pm.getState(), PrinterState::TRACKING);

    pm.poll();  // grace 2 → disappeared
    TEST_ASSERT_EQ(pm.getState(), PrinterState::IDLE);
    TEST_ASSERT_EQ(capturedCanceled[0], true);
    TEST_ASSERT_FLOAT_NEAR(capturedFilament[0], 9.18f * 0.50f, 0.01f);
    return true;
}

bool test_job_disappeared_at_95_treated_as_finished() {
    reset();
    mock.setPrinting(42, 9.18f, 0.0f);
    pm.poll();  // start

    mock.setPrinting(42, 9.18f, 96.0f);
    pm.poll();  // update progress to 96%

    mock.clearJob();
    pm.poll();  // grace 1
    pm.poll();  // grace 2 → disappeared, 96% >= 95% → treated as finished

    TEST_ASSERT_EQ(capturedCanceled[0], false);  // treated as finished
    TEST_ASSERT_FLOAT_NEAR(capturedFilament[0], 9.18f, 0.01f);  // 100%
    return true;
}

bool test_disconnect_during_tracking() {
    reset();
    mock.setPrinting(42, 9.18f, 60.0f);
    pm.poll();

    mock.setDisconnected();
    pm.poll();  // grace 1
    TEST_ASSERT_EQ(pm.getState(), PrinterState::TRACKING);

    pm.poll();  // grace 2 → disappeared
    TEST_ASSERT_EQ(pm.getState(), PrinterState::IDLE);
    TEST_ASSERT_EQ(capturedTypes.size(), 2u);  // START + END
    return true;
}

bool test_job_replaced() {
    reset();
    mock.setPrinting(42, 9.18f, 50.0f);
    pm.poll();  // start job 42

    mock.setPrinting(99, 15.0f, 0.0f);  // new job
    pm.poll();  // should end 42, start 99

    TEST_ASSERT_EQ(pm.getState(), PrinterState::TRACKING);
    TEST_ASSERT_EQ(capturedTypes.size(), 3u);  // START(42) + END(42) + START(99)
    TEST_ASSERT_EQ(capturedTypes[0], AppMessageType::PRINT_STARTED);
    TEST_ASSERT_EQ(capturedTypes[1], AppMessageType::PRINT_ENDED);
    TEST_ASSERT_EQ(capturedTypes[2], AppMessageType::PRINT_STARTED);
    TEST_ASSERT_EQ(capturedJobIds[0], 42);  // first start
    TEST_ASSERT_EQ(capturedJobIds[1], 42);  // end of 42
    TEST_ASSERT_EQ(capturedJobIds[2], 99);  // start of 99
    return true;
}

bool test_deferred_filament_on_finish() {
    reset();
    mock.setPrinting(42, 0.0f);  // no filament metadata initially
    pm.poll();

    mock.setDeferredFilament(42, 9.18f);
    mock.setFinished(100.0f);
    pm.poll();

    TEST_ASSERT_EQ(pm.getState(), PrinterState::IDLE);
    TEST_ASSERT_FLOAT_NEAR(capturedFilament[0], 9.18f, 0.01f);
    TEST_ASSERT_EQ(mock.deferredCalled_, 1);
    return true;
}

bool test_deferred_not_called_on_replace() {
    reset();
    mock.setPrinting(42, 0.0f);
    pm.poll();

    mock.setDeferredFilament(42, 9.18f);
    mock.setPrinting(99, 15.0f);  // replace, not finish
    pm.poll();

    // Deferred should NOT be called for replaced jobs
    TEST_ASSERT_EQ(mock.deferredCalled_, 0);
    return true;
}

bool test_update_called_each_poll() {
    reset();
    mock.setIdle();
    pm.poll();
    pm.poll();
    pm.poll();
    TEST_ASSERT_EQ(mock.updateCalled_, 3);
    return true;
}

bool test_filament_updates_during_tracking() {
    reset();
    mock.setPrinting(42, 0.0f);  // no metadata yet
    pm.poll();

    mock.setPrinting(42, 9.18f, 50.0f);  // metadata arrives
    pm.poll();

    mock.setFinished(100.0f);
    pm.poll();

    TEST_ASSERT_FLOAT_NEAR(capturedFilament[0], 9.18f, 0.01f);
    return true;
}

bool test_zero_filament_no_deferred() {
    reset();
    mock.setPrinting(42, 0.0f);
    pm.poll();

    // No deferred data set
    mock.setFinished(100.0f);
    pm.poll();

    TEST_ASSERT_FLOAT_NEAR(capturedFilament[0], 0.0f, 0.01f);
    return true;
}

bool test_single_tool_has_zero_tool_count() {
    reset();
    mock.setPrinting(42, 9.18f);
    pm.poll();

    mock.setFinished(100.0f);
    pm.poll();

    TEST_ASSERT_EQ(capturedToolCounts[0], 0);
    TEST_ASSERT_EQ(capturedPerTool[0].size(), 0u);
    return true;
}

bool test_multi_tool_per_tool_data() {
    reset();
    float perTool[] = {15.0f, 20.0f, 10.0f};
    mock.setPrinting(200, 45.0f);
    mock.setPerToolData(3, perTool);
    pm.poll();  // start → caches per-tool data

    mock.setFinished(100.0f);
    pm.poll();  // finish → emits cached per-tool in PRINT_ENDED

    TEST_ASSERT_EQ(capturedToolCounts[0], 3);
    TEST_ASSERT_FLOAT_NEAR(capturedFilament[0], 45.0f, 0.01f);
    TEST_ASSERT_FLOAT_NEAR(capturedPerTool[0][0], 15.0f, 0.01f);
    TEST_ASSERT_FLOAT_NEAR(capturedPerTool[0][1], 20.0f, 0.01f);
    TEST_ASSERT_FLOAT_NEAR(capturedPerTool[0][2], 10.0f, 0.01f);
    return true;
}

bool test_multi_tool_cached_on_job_replaced() {
    reset();
    float perTool1[] = {10.0f, 5.0f};
    mock.setPrinting(100, 15.0f);
    mock.setPerToolData(2, perTool1);
    pm.poll();  // start job 100, cache [10, 5]

    // Replace with new job that has different per-tool data
    float perTool2[] = {30.0f};
    mock.setPrinting(200, 30.0f);
    mock.setPerToolData(1, perTool2);
    pm.poll();  // should end 100 with cached [10, 5], start 200

    // PRINT_ENDED for job 100 should use cached data, not new job's
    TEST_ASSERT_EQ(capturedToolCounts[0], 2);
    TEST_ASSERT_FLOAT_NEAR(capturedPerTool[0][0], 0.0f, 0.01f);  // 0% progress on job 100
    TEST_ASSERT_FLOAT_NEAR(capturedPerTool[0][1], 0.0f, 0.01f);
    return true;
}

bool test_no_strategy_does_nothing() {
    reset();
    pm.setStrategy(nullptr);
    pm.poll();
    TEST_ASSERT_EQ(capturedTypes.size(), 0u);
    TEST_ASSERT_EQ(pm.getState(), PrinterState::IDLE);
    return true;
}

bool test_paused_starts_tracking() {
    reset();
    mock.connected_ = true;
    mock.hasJob_ = true;
    mock.jobId_ = 55;
    mock.progress_ = 10.0f;
    mock.totalFilamentG_ = 5.0f;
    strncpy(mock.jobState_, "PAUSED", sizeof(mock.jobState_));
    mock.updateCalled_ = 0;

    pm.poll();
    TEST_ASSERT_EQ(pm.getState(), PrinterState::TRACKING);
    TEST_ASSERT_EQ(capturedTypes[0], AppMessageType::PRINT_STARTED);
    return true;
}

// =========================================================================

int main() {
    printf("PrinterManager tests\n");
    printf("====================\n\n");

    RUN_TEST(test_idle_no_job);
    RUN_TEST(test_detect_print_start);
    RUN_TEST(test_print_finished_100_percent);
    RUN_TEST(test_print_canceled_at_30_percent);
    RUN_TEST(test_job_disappeared_under_95_is_canceled);
    RUN_TEST(test_job_disappeared_at_95_treated_as_finished);
    RUN_TEST(test_disconnect_during_tracking);
    RUN_TEST(test_job_replaced);
    RUN_TEST(test_deferred_filament_on_finish);
    RUN_TEST(test_deferred_not_called_on_replace);
    RUN_TEST(test_update_called_each_poll);
    RUN_TEST(test_filament_updates_during_tracking);
    RUN_TEST(test_zero_filament_no_deferred);
    RUN_TEST(test_single_tool_has_zero_tool_count);
    RUN_TEST(test_multi_tool_per_tool_data);
    RUN_TEST(test_multi_tool_cached_on_job_replaced);
    RUN_TEST(test_no_strategy_does_nothing);
    RUN_TEST(test_paused_starts_tracking);

    print_test_summary();
    return _tests_failed > 0 ? 1 : 0;
}
