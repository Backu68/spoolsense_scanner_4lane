#ifndef SPOOLMAN_MANAGER_H
#define SPOOLMAN_MANAGER_H

#include <atomic>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <string>

struct SpoolmanSyncRequest {
    char spool_id[17];           // NFC tag UID hex string
    uint8_t material_type;       // OPT_MATERIAL_TYPE_PLA, etc.
    char manufacturer[33];       // Brand name from tag
    uint8_t color[4];            // RGBA from tag
    float remaining_weight_g;    // Remaining weight in grams
    float initial_weight_g;      // Full spool weight in grams
    float density;               // g/cm3 (from tag, or default)
    float diameter;              // mm (from tag, or default)
    int32_t spoolman_id;         // Spoolman ID from tag (-1 if absent)
    char material_name[32];      // Custom material name (e.g. "Blood Red PLA")
    int16_t min_print_temp;      // Min extruder temp C (0 = not set)
    int16_t max_print_temp;      // Max extruder temp C (0 = not set)
    int16_t min_bed_temp;        // Min bed temp C (0 = not set)
    int16_t max_bed_temp;        // Max bed temp C (0 = not set)
    // Extra field data (written opportunistically — ignored if fields don't exist in Spoolman)
    char aspect[16];             // TigerTag aspect: "Silk", "Wood", "Matt" etc.
    uint8_t dry_temp;            // Drying temp C (0 = not set)
    uint8_t dry_time_hours;      // Drying time hours (0 = not set)
    char tag_format[16];         // "OpenPrintTag", "TigerTag"
    bool lookup_only;            // True = UID lookup only, do not sync/write
};

struct SpoolDetails {
    int32_t spoolman_id;
    float remaining_weight_g;
    float initial_weight_g;      // capacity
    char color_hex[8];           // "#RRGGBB\0"
    char manufacturer[64];       // vendor name
    char material_type[32];      // e.g., "PLA", "PETG"
    int16_t extruder_temp;       // Spoolman settings_extruder_temp (0 = not set)
    int16_t bed_temp;            // Spoolman settings_bed_temp (0 = not set)
    float density;               // g/cm³ from filament record (0 = not set)
    float diameter_mm;           // mm from filament record (0 = not set)
    bool valid;                  // indicates successful retrieval
};

class SpoolmanManager {
public:
    static SpoolmanManager& getInstance();
    bool begin(SemaphoreHandle_t httpMutex);
    void startTask();
    bool enqueueSync(const SpoolmanSyncRequest& req);
    bool isConfigured() const;
    bool getSpoolDetails(int32_t spoolmanId, SpoolDetails& outDetails);
    void invalidateCachedSpoolmanId(const char* spoolId);
    // Pre-emptively link the next detected tag to an existing spool instead of auto-creating.
    // Set before write flow starts; consumed on first tag sync within PENDING_LINK_TIMEOUT_MS.
    void setPendingLink(int32_t spoolId);

    // Pending-link state for the web UI's link-only flow: returns true while a
    // link is armed and unexpired, filling the target spool and remaining ms.
    // Returns false once consumed by a sync or timed out.
    bool getPendingLinkState(int32_t& outSpoolId, uint32_t& outRemainingMs) const;

    // Streaming nfc_id → spool id lookup (no spool-count cap, archived spools
    // excluded). Does NOT take the HTTP mutex — the caller must already hold the
    // shared g_httpMutex. Returns id >= 0 on match, -1 not found, -2 lookup
    // failed (transport/parse) — callers must not create on -2.
    int findSpoolIdByUidNoLock(const char* uid);

    // Streaming vendor search by exact name (case-insensitive); canonical name
    // copied to outName on match. Does NOT take the HTTP mutex — caller must
    // hold g_httpMutex. Returns id >= 0, -1 not found, -2 lookup failed —
    // callers must not create on -2.
    int findVendorNoLock(const char* name, char* outName = nullptr, size_t outNameSize = 0);

    // Streaming filament search (per-vendor when vendorId > 0, unfiltered
    // otherwise); tiered match: exact material+color+name, else material+color.
    // Does NOT take the HTTP mutex — caller must hold g_httpMutex. Returns
    // id >= 0, -1 not found, -2 lookup failed — callers must not create on -2.
    int findFilamentNoLock(int vendorId, const char* material, const char* colorHex6, const char* name);

    // Deduct weight directly in Spoolman for non-writable tags.
    // Returns grams deducted, or 0 on failure (caller should retry later).
    float deductFromSpoolman(const char* uid, float grams);

private:
    struct SpoolIdCacheEntry {
        char spool_id[17];
        int32_t spoolman_id;
    };

    struct SyncStateCache {
        char spool_id[17];           // NFC tag UID
        int32_t spoolman_id;         // resolved Spoolman spool ID
        int32_t filament_id;         // resolved filament ID
        float remaining_weight_g;    // last synced weight
        uint32_t synced_at_ms;       // millis() when last synced
    };

    SpoolmanManager() = default;
    SpoolmanManager(const SpoolmanManager&) = delete;
    SpoolmanManager& operator=(const SpoolmanManager&) = delete;

    static void taskFunc(void* param);
    void taskLoop();
    bool syncSpool(const SpoolmanSyncRequest& req, int& resolvedSpoolmanId);
    bool lookupSpoolByUid(const char* uid, SpoolDetails& outDetails);
    int32_t lookupCachedSpoolmanId(const char* spoolId) const;
    void storeCachedSpoolmanId(const char* spoolId, int32_t spoolmanId);
    bool isSyncCacheHit(const char* spoolId, int32_t spoolmanId, int32_t filamentId, float remainingWeight);
    void storeSyncState(const char* spoolId, int32_t spoolmanId, int32_t filamentId, float remainingWeight);

    QueueHandle_t syncQueue = nullptr;
    SemaphoreHandle_t httpMutex_ = nullptr;
    TaskHandle_t taskHandle = nullptr;
    SpoolIdCacheEntry spoolIdCache_[8] = {};
    uint8_t spoolIdCacheWriteIndex_ = 0;
    SyncStateCache syncStateCache_[8] = {};
    uint8_t syncStateCacheWriteIndex_ = 0;
    SemaphoreHandle_t cacheMutex_ = nullptr;

    static constexpr size_t QUEUE_SIZE = 4;
    // 10240: the streaming matchers put a ~1.1KB json_reader in the sync call
    // tree — measured HWM floor dropped to 1232 free of 8192 on bench (phase 2
    // slice 3). Prior history: canary panic at 6144, ~1.7KB floor at 8192.
    static constexpr size_t TASK_STACK_SIZE = 10240;
    static constexpr UBaseType_t TASK_PRIORITY = 1;
    static constexpr TickType_t HTTP_MUTEX_TIMEOUT = pdMS_TO_TICKS(10000);
    static constexpr uint32_t SYNC_CACHE_TTL_MS = 2 * 60 * 60 * 1000;  // 2 hours
    static constexpr uint32_t PENDING_LINK_TIMEOUT_MS = 120000;         // 2 minutes

    std::atomic<int32_t> pendingLinkSpoolId_{-1};
    std::atomic<uint32_t> pendingLinkSetAt_{0};
};

#endif // SPOOLMAN_MANAGER_H
