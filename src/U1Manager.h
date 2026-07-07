#ifndef U1_MANAGER_H
#define U1_MANAGER_H

#include <cstdint>
#ifndef NATIVE_TEST
#include <freertos/FreeRTOS.h>
#endif

// Forward declarations to keep this header lean — full definitions live in
// ApplicationManager.h (payload types) and NFCTypes.h (CurrentSpoolState).
struct SpoolDetectedPayload;
struct SpoolmanSyncedPayload;
struct CurrentSpoolState;

// U1's external filament-detection wire format. Built from on-tag data, then
// possibly augmented from a Spoolman sync result; serialized into the JSON body
// of POST /printer/filament_detect/set.
struct U1FilamentInfo {
    char vendor[64] = {};
    char main_type[24] = {};   // uppercase, e.g. "PLA", "PETG"
    char sub_type[24] = {};    // e.g. "Matte", "CF", may be empty
    int  rgb_1 = -1;            // -1 = no color sent
    int  alpha = 255;
    int  hotend_min_temp = 0;   // 0 = not sent
    int  hotend_max_temp = 0;
    int  bed_temp = 0;
    uint8_t card_uid[8] = {};
    uint8_t card_uid_len = 0;
};

// U1Manager — Snapmaker U1 direct-mode bridge.
//
// Pushes scanner results to the U1's external filament-detection endpoint
// (paxx12 Extended Firmware with "Filament Detection: External" enabled).
// Reads u1_enabled / u1_channel / moonraker_url from ConfigurationManager
// each call; everything else is internal state.
//
// Two entry points cover the full integration:
//   publishFromDetection()    — smart tags (OpenSpool, OPT, TigerTag, OT3D, Bambu)
//   publishFromSpoolmanSync() — generic UID tags (NFC+) and smart-tag augment
//
// Both no-op when U1 integration is disabled in NVS, so callers can invoke
// unconditionally without gating.
//
// Threading: must be called only from the ApplicationManager dispatch loop
// (handleSpoolDetected / handleSpoolmanSynced). Internal state
// (moonrakerBackoffUntilMs_, pendingAugment_) is not mutex-protected — the
// dispatch loop is single-threaded so no synchronization is needed in the
// current design. Cross-thread invocation would require revisiting both
// fields, not just adding atomics around millis() math.
class U1Manager {
public:
    static U1Manager& getInstance();

    // ── Stage mode (#Phase 2) ────────────────────────────────────────────
    // In stage mode, scans are staged instead of posted; the user then picks
    // the channel via the web UI picker or the keypad (both always available;
    // neither is required hardware). A staged spool expires after
    // STAGE_TTL_MS if unassigned.
    struct StagedState {
        bool active = false;
        uint32_t remainingMs = 0;
        char vendor[64] = {};
        char material[24] = {};
        int rgb = -1;              // -1 = unknown color
    };
    // Thread-safe snapshot for the web UI (/api/status) — callable from any task
    StagedState getStagedState();
    // Assign the staged spool to a channel (0-3) and POST it. Must be called
    // from the ApplicationManager dispatch loop (same thread as publishes).
    // Returns false if nothing is staged / expired / POST failed.
    bool assignStagedToChannel(uint8_t channel);
    // True when a staged spool is waiting — used by the keypad flow to route
    // digits to channel selection. Dispatch-loop only.
    bool hasStagedSpool();

    // Smart tag scan path. Builds U1 info from on-tag data + per-material
    // defaults, POSTs to /printer/filament_detect/set, and (if anything is
    // still missing and Spoolman is configured) registers a pending augment
    // for publishFromSpoolmanSync to fill in later.
    void publishFromDetection(const SpoolDetectedPayload& payload);

    // Spoolman sync result path.
    //  - For generic UID tags (is_uid_lookup): single POST using lookup
    //    result; skipped if the reader no longer holds the same tag.
    //  - For smart tags (write update): if a pending augment was registered
    //    by publishFromDetection, merges Spoolman data over the prior POST
    //    and re-publishes if Spoolman supplied something new; otherwise no-op.
    void publishFromSpoolmanSync(const SpoolmanSyncedPayload& sync,
                                  const CurrentSpoolState& state);

private:
    U1Manager() = default;
    U1Manager(const U1Manager&) = delete;
    U1Manager& operator=(const U1Manager&) = delete;

    // Reachability backoff — set after a transport failure so an offline U1
    // doesn't block the dispatch loop on every subsequent scan.
    uint32_t moonrakerBackoffUntilMs_ = 0;
    static constexpr uint32_t MOONRAKER_BACKOFF_MS = 30000;

    // Pending-augment tracking for smart tags that POSTed incomplete data and
    // are waiting on a Spoolman sync to fill missing fields. postedInfo holds
    // the exact wire-format struct that POST 1 sent — POST 2 starts from this
    // and overlays non-empty Spoolman fields, so on-tag data is never lost.
    struct PendingAugment {
        bool active = false;
        char uid[17] = {};
        uint32_t expiresAtMs = 0;
        U1FilamentInfo postedInfo;
    };
    PendingAugment pendingAugment_ = {};
    static constexpr uint32_t PENDING_AUGMENT_TTL_MS = 30000;

    // Stage-mode holding area. Written on the dispatch loop; snapshotted by
    // the web task via getStagedState() — guarded by a critical section since
    // the copy is a few dozen bytes.
    struct StagedSpool {
        bool active = false;
        uint32_t expiresAtMs = 0;
        char uid[17] = {};
        U1FilamentInfo info;
    };
    StagedSpool staged_ = {};
    portMUX_TYPE stagedMux_ = portMUX_INITIALIZER_UNLOCKED;
    static constexpr uint32_t STAGE_TTL_MS = 30000;

    void stageSpool(const U1FilamentInfo& info, const char* uid);
    void clearStaged();
};

#endif // U1_MANAGER_H
