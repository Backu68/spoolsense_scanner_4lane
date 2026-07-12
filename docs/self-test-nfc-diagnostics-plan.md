# Self-Test and NFC Signal-Quality Wizard

## Purpose

SpoolSense failures are often caused by wiring, marginal tag coupling, unstable power, SPI conflicts, incorrect board configuration, or an unavailable network service. These problems can look identical to firmware bugs when the only evidence is a short serial log.

The self-test wizard should guide a user through repeatable checks, explain the result in plain language, and generate a sanitized diagnostic report suitable for a GitHub issue.

The wizard must be safe by default. Normal diagnostics are read-only. Any test that writes to a tag must be clearly separated, require explicit confirmation, and warn the user to use a disposable test tag.

## Goals

- Identify common hardware, configuration, RF, memory, and network failures without requiring a serial console.
- Measure NFC stability using repeatable behavior rather than claiming unsupported RF RSSI data.
- Produce structured results that can be compared across boards and firmware versions.
- Preserve enough low-level detail for maintainers while presenting simple recommendations to users.
- Avoid blocking the web server or main Arduino loop during long tests.
- Never expose Wi-Fi passwords, MQTT passwords, API keys, or complete sensitive configuration values.

## Non-goals

- Laboratory RF field-strength measurement.
- Automatic electrical validation of every wire.
- Certification of antenna tuning or EMC compliance.
- Destructive tag testing without explicit user consent.
- Replacing hardware-in-the-loop release testing.

## User experience

Add a **Run Self-Test** action to the troubleshooting page. The wizard should present tests in stages:

1. Device and firmware
2. Memory and task health
3. NFC reader communication
4. Tag read stability
5. Optional write/read-back test
6. Wi-Fi and local services
7. Optional peripherals
8. Results and support report

Each test reports one of:

- **Pass** — expected behavior observed.
- **Warning** — usable, but degraded or outside the recommended range.
- **Fail** — a required operation failed.
- **Skipped** — feature not enabled, not applicable, or user declined.
- **Waiting** — user action is required, such as placing a tag on the reader.

The interface should always explain what the result means and the next physical action to try.

## Safety model

### Read-only tests

These may run without confirmation:

- Reader firmware/version query
- Register/status diagnostics
- Repeated UID detection
- Repeated page reads
- Memory and stack checks
- Wi-Fi and service reachability
- Display, LED, keypad, and sensor presence checks

### Write test

The write test must require all of the following:

- The user selects **Disposable test tag**.
- A warning explains that existing data may be overwritten.
- The current UID is captured and remains the required UID for every step.
- The user confirms immediately before the write.
- The test writes a small known pattern only to a safe supported area or performs a format/write/verify/restore sequence when restoration is possible.
- Read-back verification must pass before success is reported.

Do not offer a generic write test for locked, read-only, unknown-capacity, Bambu, or unsupported tags.

## Architecture

Create a `DiagnosticsManager` that owns the active diagnostic session and runs long operations from a dedicated low-priority FreeRTOS task.

The HTTP handler should only:

- Start or cancel a session.
- Submit user confirmations.
- Return a snapshot of current progress/results.
- Return the completed support report.

It must not perform repeated NFC scans or live network calls inside the web request.

Suggested types:

```cpp
enum class DiagnosticStatus : uint8_t {
    NOT_RUN,
    RUNNING,
    WAITING_FOR_USER,
    PASS,
    WARNING,
    FAIL,
    SKIPPED
};

enum class DiagnosticTest : uint8_t {
    DEVICE_INFO,
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
    KEYPAD_CHECK
};

struct DiagnosticResult {
    DiagnosticTest test;
    DiagnosticStatus status;
    int32_t code;
    uint32_t duration_ms;
    char summary[96];
    char recommendation[160];
};
```

Use fixed-size storage and bounded messages. Do not allocate a large dynamic result document while the NFC or network stacks are under stress.

## Coordinating with the normal NFC task

Only one task may own the NFC hardware at a time.

Recommended sequence:

1. Ask `NFCManager` to enter diagnostic mode through a controlled request.
2. Stop accepting new tag-write jobs or wait for the write queue to drain.
3. Suspend normal scanning only after the scan task reaches a safe boundary.
4. Give `DiagnosticsManager` temporary exclusive access to structured reader operations.
5. Restore RF configuration and resume the normal scan task when diagnostics finish or are canceled.

Avoid calling `vTaskSuspend()` at an arbitrary point during an SPI transaction. Add a cooperative pause/acknowledgement mechanism to `NFCManager` instead.

## Stage 1 — Device and firmware checks

Collect:

- Firmware version
- Board target and detected chip model/revision
- Flash size and partition layout
- PSRAM presence and size where applicable
- Reset reason
- Uptime
- Configured NFC reader
- Enabled peripherals
- Device ID and sanitized hostname

Checks:

- Compiled flash size matches the board configuration.
- OTA slots exist and have sufficient capacity.
- PSRAM expectation matches detected hardware.
- Reset reason indicates a recent watchdog, panic, or brownout.
- Mutually incompatible features are not enabled on conflicting pins.

Recommendations should distinguish likely power problems, configuration problems, and software crashes.

## Stage 2 — Memory and task health

Use the existing `MemoryDiagnostics` data and add snapshots at diagnostic start and end.

Report:

- Current free internal heap
- Minimum-ever free heap
- Largest free internal block
- Heap fragmentation indicator: largest block divided by total free internal heap
- Per-task stack high-water marks
- Queue usage/high-water marks where available
- Task handles that were expected but never started

Suggested initial thresholds should be conservative and configurable in code:

- Fail when a required task is absent.
- Warn when a stack has less than a documented safety reserve.
- Warn when the largest internal allocation block is too small for known TLS/OTA operations.
- Warn when minimum-ever heap shows a near-exhaustion event.

Thresholds must be validated from real device telemetry before being treated as authoritative.

## Stage 3 — NFC reader communication

### Common checks

- Reader object initialized successfully.
- Reader responds consistently to multiple version/status queries.
- Reset completes within its timeout.
- RF setup succeeds repeatedly.
- SPI operations do not report a wedged bus.
- Configured reader matches the connected hardware.

### PN5180 checks

Expose structured values rather than parsing serial output:

- Firmware and EEPROM version
- BUSY state before and after reset
- Reset duration
- IDLE IRQ arrival and duration
- `IRQ_STATUS`, `RF_STATUS`, and `SYSTEM_STATUS`
- RF configuration load result
- RF on/off transition result
- Bus-wedged latch state
- ISO15693 and ISO14443A setup result

Potential recommendations:

- BUSY stuck high: check power, ground, BUSY wiring, and module voltage.
- No IDLE IRQ: check reset/power integrity and reader module health.
- Repeated SPI failures: check NSS/SCK/MISO/MOSI and TFT bus conflicts.
- Reader resets under RF load: check 5 V supply and decoupling.

### PN532 checks

Expose:

- Firmware/IC version query result
- `SAMConfig()` result
- SPI reinitialization result
- Passive-target command timing
- Reactivation success count
- MIFARE/NTAG command failure count

Potential recommendations:

- Firmware query failure: verify SPI mode and wiring.
- `SAMConfig()` failure: check reset timing and supply stability.
- High reactivation failures: improve tag placement or antenna spacing.

## Stage 4 — Tag detection stability

Ask the user to place one tag flat and keep it stationary. Capture its UID, protocol, kind, and variant.

Run a bounded test, initially 100 detection attempts over approximately 5–10 seconds.

Measure:

- Successful detections
- Missed detections
- UID mismatches or length changes
- Partial/spurious UID events
- Average, minimum, maximum, and percentile detection latency
- Reader resets or RF recovery attempts
- Protocol switching failures

Abort and report **tag changed** if a genuinely different UID appears.

### Stability score

Call this an **NFC stability score**, not signal strength.

An initial score can combine:

- Detection success rate
- UID consistency
- Read success rate
- Retry rate
- Recovery/reset count
- Latency consistency

Example initial grading:

- 95–100: Excellent
- 85–94: Good
- 70–84: Marginal
- Below 70: Poor

Do not freeze the formula until results have been collected from known-good and deliberately marginal PN5180 and PN532 setups.

## Stage 5 — Tag read stability

After stable detection, repeatedly read the appropriate data region.

### OpenPrintTag / ISO15693

- Read all expected pages in the same batches used by production.
- Parse NDEF/CBOR every iteration.
- Compare stable pages and decoded identity fields.
- Count block failures, parse failures, retries, and RF resets.

### NTAG / ISO14443A

- Detect the NTAG variant.
- Read within user-memory boundaries.
- Parse TigerTag, OpenTag3D, or OpenSpool where applicable.
- Count reactivations, partial reads, NDEF failures, and UID mismatches.

### Bambu / MIFARE Classic

- Authenticate required sectors.
- Track success per sector/block.
- Reject incomplete data instead of parsing uninitialized blocks.
- Validate decoded weight, diameter, and temperature ranges.

The report should clearly separate reader communication failures from tag-format parse failures.

## Stage 6 — Optional write/read-back test

Use the same production write functions and verification logic rather than creating an unrelated test implementation.

Minimum checks:

1. Capture and lock expected UID.
2. Confirm tag variant and capacity.
3. Reject locked or unsupported tags.
4. Write a known bounded payload.
5. Read it back and compare every written byte.
6. Repeat once only if the production recovery policy allows it.
7. Report pages written, retries, duration, and verification result.

For a restoration test, save the original bytes in RAM and restore them only if the original read was complete and the tag capacity is known. Warn that power loss between test write and restoration can still leave the disposable tag altered.

## Stage 7 — Network and service health

Run network checks through the component that owns outbound network work, serialized by the global HTTP policy or a future network worker.

### Wi-Fi

- Connection state
- RSSI
- IP, gateway, DNS, and subnet validity
- Default gateway reachability where supported
- DNS resolution
- mDNS registration state
- Reconnect count and last disconnect reason if available

### MQTT

- Configuration present
- TCP/broker connection result
- Authentication result code
- Publish and optional loopback acknowledgement on a temporary diagnostic topic

Do not include MQTT username or password in the report.

### Spoolman

- URL parses and uses an allowed scheme
- `/api/v1/info` response and latency
- Version parse
- Required extra-field status
- Optional read-only lookup check

Do not create vendors, filaments, or spools during diagnostics.

### Printer integrations

- Moonraker/U1 endpoint reachability
- PrusaLink status endpoint and authentication
- API version and response latency
- Required object/field presence

Do not send G-code or change a filament assignment during the default test.

## Stage 8 — Peripheral tests

### Display

Show a test pattern, colors, text alignment, and driver name. Ask the user to confirm that it appears correctly.

### LED

Cycle red, green, blue, white, and off. Ask the user to confirm correct colors; this can identify RGB/GRB/RGBW ordering mistakes.

### Keypad or encoder

Show the expected key/control and record user input with a timeout. Highlight stuck keys or impossible pin configuration.

### Optional sensors

For future load cells or humidity sensors, report bus detection, address, plausible reading ranges, and calibration status.

## API design

Suggested endpoints:

- `POST /api/diagnostics/session` — start a session with selected optional tests.
- `GET /api/diagnostics/session` — return progress and structured results.
- `POST /api/diagnostics/session/input` — provide confirmation or user-observed result.
- `POST /api/diagnostics/session/cancel` — cancel safely and restore normal operation.
- `GET /api/diagnostics/report` — return the sanitized completed report.

Only one session may run at a time. Mutating diagnostic endpoints must follow the web authentication and CSRF protections proposed in the security review.

## Sanitized support report

Export both readable text and JSON.

Include:

- Firmware, board, reader, and enabled-feature summary
- Reset reason and uptime
- Heap and stack measurements
- NFC register/status results
- Tag protocol, kind, and variant
- Redacted UID by default, such as `E004...A91F`
- Detection/read success rates and timing
- Write verification result when explicitly run
- Network service result codes and latency
- Peripheral confirmations
- Last relevant errors and recommendations

Exclude or redact:

- Wi-Fi and MQTT passwords
- PrusaLink/API keys
- Full credentials embedded in URLs
- MQTT password and sensitive topics
- Full MAC address unless the user explicitly opts in
- Full tag UID by default
- Raw decrypted Bambu blocks

Add a report-format version so tooling can parse future reports safely.

## Implementation phases

### Phase 1 — Structured diagnostics foundation

1. Add `DiagnosticsManager` with session state and fixed result storage.
2. Add cooperative NFC diagnostic ownership/pause.
3. Convert PN5180 and PN532 diagnostic logging into structured snapshots.
4. Add session start/status/cancel APIs.
5. Add device, memory, task, and reader communication tests.

### Phase 2 — NFC stability tests

1. Add repeated detection test with UID consistency checks.
2. Add protocol-specific repeated read tests.
3. Record latency, retries, recoveries, and parse failures.
4. Implement an experimental stability score.
5. Validate scoring on known-good and intentionally marginal hardware.

### Phase 3 — Troubleshooting UI and support report

1. Build the staged wizard on the existing troubleshooting page.
2. Add plain-language recommendations mapped from structured error codes.
3. Add sanitized JSON/text report export.
4. Add copy-to-clipboard support summary.

### Phase 4 — Optional destructive and peripheral tests

1. Add disposable-tag write/read-back test.
2. Add display, LED, keypad/encoder confirmation steps.
3. Add read-only network service tests through the network owner.
4. Add explicit cancellation and recovery tests.

## Testing plan

### Native tests

- Session state transitions and cancellation
- Only one active session permitted
- Timeout and user-input handling
- Score calculation boundaries
- UID mismatch abort
- Result buffer capacity
- Report redaction of credentials, URLs, MAC, and UID
- Unknown reader/test types remain forward compatible
- NFC pause acknowledgement and resume after every exit path

### Hardware matrix

- ESP32-WROOM + PN5180
- ESP32-S3-Zero + PN5180
- ESP32-S3-Zero + PN532
- ESP32-S3-DevKitC + PN5180 and TFT
- ESP32-C3 + both supported reader choices
- LCD, TFT, LED, and keypad enabled/disabled combinations

### Fault injection

- Disconnect each SPI signal where safe
- Hold or float BUSY/IRQ inputs
- Brownout or weak USB supply
- Wrong board pin profile
- SPI conflict with TFT
- Tag at increasing antenna distance and misalignment
- Tag removal during every NFC test phase
- Malformed NDEF and partially readable tags
- Locked/read-only tag during write-test selection
- Wi-Fi disconnect and service timeouts
- Cancel or close the browser during a session

## Acceptance criteria

- The default wizard never modifies a tag, printer, or remote database.
- The normal NFC task always resumes after completion, cancellation, browser loss, or test failure.
- Known wiring/RF faults produce a specific useful recommendation instead of only “NFC failed.”
- Stability results are repeatable within an agreed tolerance on a stationary known-good tag.
- The report contains no credentials or raw decrypted tag data.
- Long tests do not block the web server, Wi-Fi watchdog, or application message loop.
- The disposable-tag write test never writes after a UID change.
- A generated report contains enough structured evidence to triage the majority of NFC support issues without requesting a serial log first.

## Expected files

- New `src/DiagnosticsManager.h/.cpp`
- `src/NFCManager.h/.cpp`
- `src/NFCConnectionI.h`
- `src/HardwareNFCConnection.h/.cpp`
- `src/HardwareNFCConnectionPN532.h/.cpp`
- `src/MemoryDiagnostics.h/.cpp`
- `src/WebServerManager.h/.cpp`
- `src/TroubleshootingHTML.h`
- Optional shared diagnostic result/error type header
- Native diagnostics tests and hardware test checklist

## Recommended first deliverable

The best first release is read-only and contains:

1. Device/reset/memory/task checks.
2. Structured PN5180/PN532 communication checks.
3. A 100-cycle detection and read stability test.
4. Plain-language wiring and tag-placement recommendations.
5. A sanitized report export.

The write test and network/peripheral extensions should follow after the read-only foundation has been proven on real hardware.
