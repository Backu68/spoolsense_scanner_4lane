#pragma once

// Shared types for the self-test / NFC signal-quality wizard (#253).
// Kept dependency-free (no Arduino/FreeRTOS/hardware) so the session state
// machine, stability score, and report redaction are unit-testable natively.

#include <stdint.h>
#include <stddef.h>

// Per-test outcome. Ordering matters: worst-case rollup takes the max severity
// among non-skipped results (FAIL > WARNING > PASS), see diagWorseStatus().
enum class DiagnosticStatus : uint8_t {
    NOT_RUN = 0,
    RUNNING,
    WAITING_FOR_USER,
    SKIPPED,
    PASS,
    WARNING,
    FAIL
};

// The catalogue of checks. Stored in results as a stable uint8_t id so a
// report from an older firmware stays parseable.
enum class DiagnosticTest : uint8_t {
    DEVICE_INFO = 0,
    RESET_REASON,
    HEAP_HEALTH,
    TASK_STACKS,
    NFC_READER_INIT,
    NFC_READER_VERSION,
    NFC_REGISTER_HEALTH,
    TAG_DETECTION_STABILITY,
    TAG_READ_STABILITY,
    TAG_WRITE_VERIFY,
    WIFI_HEALTH,
    MQTT_REACHABILITY,
    SPOOLMAN_REACHABILITY,
    PRINTER_REACHABILITY,
    DISPLAY_CHECK,
    LED_CHECK,
    KEYPAD_CHECK,
    TEST_COUNT
};

// One fixed-size result row. No String/heap — bounded messages only.
struct DiagnosticResult {
    DiagnosticTest  test;
    DiagnosticStatus status;
    int32_t         code;          // test-specific numeric detail (0 = none)
    uint32_t        duration_ms;
    char            summary[96];
    char            recommendation[160];
};

// Raw counters gathered by the NFC stability tests (stage 4/5). Fed to
// diagComputeStabilityScore(). Pure data — no methods.
struct NfcStabilityMetrics {
    uint16_t detect_attempts;
    uint16_t detect_success;
    uint16_t uid_mismatches;   // partial/length-changed UID events (NOT a real tag swap)
    uint16_t read_attempts;
    uint16_t read_success;
    uint16_t retries;
    uint16_t recoveries;       // RF resets / bus-wedge recoveries during the run
    uint32_t latency_min_ms;
    uint32_t latency_max_ms;
    uint32_t latency_avg_ms;
};

// Coarse human grade for a 0-100 stability score.
enum class NfcStabilityGrade : uint8_t {
    POOR = 0,     // < 70
    MARGINAL,     // 70-84
    GOOD,         // 85-94
    EXCELLENT     // 95-100
};

// Structured reader status captured by NFCConnectionI::getDiagnosticSnapshot().
// Cross-reader: PN5180 fills the register block, PN532 fills sam_config_ok;
// fields not applicable to a reader stay zero/false.
struct ReaderDiagnostics {
    char     reader_name[24];   // e.g. "PN5180 v4.0" / "PN532 v1.6"
    bool     initialized;
    uint8_t  fw_major;
    uint8_t  fw_minor;
    bool     has_registers;     // true = the register block below is meaningful (PN5180)
    uint32_t rf_status;         // RF_STATUS (0x1D)
    uint32_t irq_status;        // IRQ_STATUS (0x02)
    uint32_t system_status;     // SYSTEM_STATUS (0x24)
    bool     bus_wedged;        // PN5180 fail-fast latch state
    bool     sam_config_ok;     // PN532 SAMConfig() result
};
