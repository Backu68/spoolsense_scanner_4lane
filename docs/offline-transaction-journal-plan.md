# Reliable Offline Transaction Journal

## Purpose

SpoolSense currently coordinates NFC tags, Spoolman, MQTT, printers, and Home Assistant across separate tasks and network connections. An operation can be accepted by one subsystem and fail in another. For example, a filament deduction can be removed from NVS after it is queued even if the later NFC write fails.

The transaction journal makes these multi-step operations durable. Work is retained until every required step is verified, and interrupted operations resume safely after a reboot, network outage, tag removal, or RF failure.

## User experience

The normal successful workflow should remain quiet. The journal becomes visible only when useful.

The web UI should show pending operations with states such as:

- Waiting for the correct tag
- Writing tag
- Waiting for Spoolman
- Waiting for MQTT
- Retrying after a failure
- Completed
- Needs attention

Users should be able to retry or dismiss an operation that requires attention. Destructive dismissal should require confirmation and explain what data may remain inconsistent.

## Initial scope

The first release should journal the operations where data loss or duplication matters most:

1. Filament deductions written to OpenPrintTag or OpenTag3D.
2. Filament deductions sent directly to Spoolman.
3. Tag-to-Spoolman linking.
4. Tag writes followed by Spoolman synchronization.

Later releases can add configuration changes, tray assignments, tag conversions, and other multi-system workflows.

MQTT telemetry does not need to block completion in the first version. A retained state publication can be regenerated from current state, while a lost weight deduction cannot.

## Core design rule: every retry must be idempotent

A transaction may be repeated after power loss. Repeating it must produce the same final result instead of applying the change twice.

Do not journal an instruction such as:

> Add 20 grams to consumed weight.

Instead, snapshot the starting value and journal an absolute target:

> For tag UID `E004...`, set consumed weight to 342.5 grams.

Writing the absolute value again is safe if the ESP32 resets after the physical tag write but before recording success. The same principle applies to Spoolman: PATCH an absolute target weight instead of submitting another relative deduction.

## Proposed transaction model

Use a fixed-size record without `String`, pointers, or heap-owned data.

```cpp
enum class TransactionType : uint8_t {
    SET_TAG_CONSUMED_WEIGHT,
    SET_SPOOLMAN_REMAINING,
    LINK_TAG_TO_SPOOL,
    WRITE_TAG_AND_SYNC
};

enum class TransactionState : uint8_t {
    PENDING,
    WAITING_FOR_TAG,
    TAG_WRITE_QUEUED,
    TAG_WRITE_VERIFIED,
    REMOTE_SYNC_PENDING,
    REMOTE_SYNC_VERIFIED,
    COMPLETED,
    NEEDS_ATTENTION
};

struct TransactionRecord {
    uint16_t schema_version;
    uint16_t record_size;
    uint64_t transaction_id;
    TransactionType type;
    TransactionState state;
    uint8_t required_steps;
    uint8_t completed_steps;

    char uid[17];
    int32_t spoolman_id;
    float starting_consumed_g;
    float target_consumed_g;
    float target_remaining_g;

    uint8_t retry_count;
    int16_t last_error;
    uint32_t next_attempt_ms;
    uint32_t created_unix;
    uint32_t updated_unix;
    uint32_t crc32;
};
```

Format-specific payloads should use a tagged union when more transaction types are introduced. Every record must include the expected UID and a persistent transaction ID.

## Storage design

Store a small fixed number of records in the existing `spoolsense` NVS namespace or a dedicated `transactions` namespace.

Recommended first implementation:

- 16 fixed slots named `tx00` through `tx15`.
- One complete `TransactionRecord` blob per slot.
- CRC32 and schema version in every record.
- A separate persistent generation/counter used to create transaction IDs.
- Reuse completed slots only after completion has been acknowledged and retained for a short diagnostic period.

Writing a complete slot as one NVS blob avoids partially updating several keys. NVS already provides flash wear leveling, but state should still be persisted only on meaningful transitions—not on every scan-loop pass.

If 16 active transactions are insufficient, reject new work clearly instead of silently dropping the oldest operation.

## Transaction ID generation

Do not use `millis()` as a request ID. It repeats after reboot and can collide between rapid requests.

Use a persistent 64-bit value composed from:

- A boot-independent device identifier or random installation identifier.
- A monotonically increasing NVS counter.

The NFC and Spoolman request/response payloads must carry this ID end to end.

## Ownership and task boundaries

Create a `TransactionManager` as the single owner of transaction records and state transitions.

Other components should communicate with it through messages:

- `ApplicationManager` requests creation of a transaction.
- `TransactionManager` requests an NFC write.
- `NFCManager` returns `{transaction_id, success, verified, error}`.
- `TransactionManager` requests a Spoolman update.
- `SpoolmanManager` returns `{transaction_id, success, confirmed_value, error}`.
- `TransactionManager` advances or retries the transaction.

The actual write payload must travel with the queued job or through a generation-safe payload slot. It must not use the current single `rawWriteBuffer_` or `atomicWriteFields_` sidecars.

## Filament deduction workflow

### Creating the transaction

1. Receive a deduction for a specific UID.
2. Read the current tag or Spoolman weight.
3. Calculate an absolute target consumed/remaining weight.
4. Clamp the target to valid physical limits.
5. Persist the complete transaction in `PENDING` state.
6. Only after persistence succeeds, begin external work.

### Tag write path

1. Confirm that the currently presented tag UID matches the transaction UID.
2. If the tag is absent, set `WAITING_FOR_TAG` and leave the record durable.
3. Queue `SET_CONSUMED_WEIGHT` using the absolute target and transaction ID.
4. Set `TAG_WRITE_QUEUED`.
5. NFC writes the value and performs read-back verification.
6. NFC returns a completion message containing the transaction ID.
7. Only verified success advances to `TAG_WRITE_VERIFIED`.
8. If Spoolman must also be updated, advance to `REMOTE_SYNC_PENDING`; otherwise complete.

### Spoolman path

1. Resolve the spool using the existing identity resolver.
2. PATCH the absolute remaining/used weight.
3. Read back or validate the returned representation.
4. Advance to `REMOTE_SYNC_VERIFIED` only when the intended value is confirmed.
5. Mark the transaction `COMPLETED`.

### Reboot recovery

At boot, scan all valid journal slots:

- `PENDING` or `WAITING_FOR_TAG`: resume when the expected tag appears.
- `TAG_WRITE_QUEUED`: read the tag first. If it already contains the target, mark the tag step verified; otherwise write the same absolute target again.
- `TAG_WRITE_VERIFIED` or `REMOTE_SYNC_PENDING`: resume the Spoolman step.
- `COMPLETED`: retain briefly for diagnostics, then recycle the slot.
- Invalid CRC or unknown schema: do not execute; expose it as requiring attention.

## Retry policy

Classify failures rather than treating them equally:

- **Tag absent or wrong UID:** wait indefinitely for the correct tag; do not count aggressive retries.
- **Temporary RF failure:** retry with bounded exponential backoff while the tag remains present.
- **Network unavailable or HTTP timeout:** retry with exponential backoff and jitter.
- **Spoolman record not found:** re-run identity resolution, then require attention if the mapping remains ambiguous.
- **Validation or capacity failure:** stop automatic retries and set `NEEDS_ATTENTION`.
- **Queue full:** keep the durable transaction pending and retry later.

Cap automatic retry frequency, not transaction lifetime. A recoverable operation should survive for days if necessary.

## Web API and interface

Add endpoints such as:

- `GET /api/transactions` — list active and recent transactions.
- `POST /api/transactions/{id}/retry` — retry a recoverable transaction now.
- `POST /api/transactions/{id}/dismiss` — dismiss with explicit confirmation.

Add a small status indicator to the landing and reader pages:

- No badge when nothing is pending.
- Yellow badge for waiting/retrying.
- Red badge for needs-attention records.

The transaction detail should show UID, operation, intended result, completed steps, last error, retry count, and next retry time. It must not expose credentials.

## Implementation phases

### Phase 1 — Completion correlation

1. Add `transaction_id` or `request_id` to `SpoolUpdatedPayload`.
2. Preserve it through `NFCManager::processWriteQueue()`.
3. Add correlated result messages from `SpoolmanManager`.
4. Replace `millis()` request IDs with the existing monotonic generator as an interim improvement.

### Phase 2 — Durable transaction store

1. Implement `TransactionRecord`, CRC validation, and fixed NVS slots.
2. Implement create, load, update-state, list, complete, and recycle operations.
3. Add boot-time recovery without yet changing all callers.
4. Add NVS corruption and capacity tests.

### Phase 3 — Migrate filament deductions

1. Change relative `REMOVE_WEIGHT` flows to absolute target writes.
2. Stop clearing pending deductions when a request is merely queued.
3. Clear/complete only after verified NFC or Spoolman success.
4. Migrate existing `deductions` NVS entries into transactions on first boot after upgrade.

### Phase 4 — Migrate linking and write/sync workflows

1. Journal pending tag-to-spool links.
2. Journal tag writes that require a subsequent Spoolman update.
3. Remove shared write sidecars in favor of queue-owned payloads.

### Phase 5 — User interface and diagnostics

1. Add transaction API endpoints.
2. Add pending/failed indicators and detail views.
3. Include journal health in `/api/diagnostics` and support bundles.
4. Add Home Assistant diagnostic entities only if they prove useful; avoid noisy per-retry notifications.

## Testing plan

### Native tests

- Transaction creation survives manager re-instantiation.
- CRC corruption is detected and never executed.
- Full journal rejects new work without overwriting active records.
- Queue acceptance does not complete a transaction.
- Verified success completes the correct transaction ID.
- Failure for one transaction cannot complete another.
- Replaying an absolute deduction does not deduct twice.
- Old NVS deduction entries migrate once.
- Retry backoff handles `millis()` wraparound.

### Hardware fault-injection tests

- Remove the tag before, during, and immediately after a write.
- Reset power after the physical write but before the state update.
- Disable Wi-Fi between tag verification and Spoolman synchronization.
- Reboot during every transaction state.
- Present the wrong tag while a transaction is waiting.
- Fill the NFC and Spoolman queues.
- Test weak RF coupling on PN5180 and PN532.
- Return HTTP 404, 409, 422, 500, malformed JSON, and timeouts from a mock Spoolman server.

### Acceptance criteria

- No verified deduction is lost after any single reset, network failure, tag removal, or queue failure.
- No deduction is applied twice after retry or reboot.
- The wrong UID is never modified.
- Every incomplete operation is visible and actionable.
- Flash writes remain bounded to state transitions and show no unexpected NVS growth during endurance testing.
- Normal scan latency is not materially increased when no transaction is pending.

## Files expected to change

- New `src/TransactionManager.h/.cpp`
- `src/ApplicationManager.h/.cpp`
- `src/NFCManager.h/.cpp`
- `src/NFCWriteTypes.h`
- `src/DeductionManager.h/.cpp`
- `src/SpoolmanManager.h/.cpp`
- `src/WebServerManager.h/.cpp`
- Reader, landing, and troubleshooting web assets
- Native test stubs, Makefile, and new transaction tests

## Main risks

- A relative operation accidentally surviving in a retryable workflow and double-applying.
- NVS wear from persisting retry counters or timers too frequently.
- Completing the wrong transaction because request IDs are not carried through every queue.
- Ambiguous Spoolman identity resolution after a tag is re-linked.
- Schema upgrades leaving old records executable with changed semantics.

These risks are manageable if absolute target values, persistent IDs, single ownership, and read-back confirmation are treated as non-negotiable design rules.
