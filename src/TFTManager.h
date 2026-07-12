#pragma once

#if defined(BOARD_NO_TFT)

#include <Arduino.h>
#include "DisplayI.h"

// C5/C6 have one general-purpose SPI bus, reserved for NFC. Keeping a tiny
// compile-time stub lets shared application code remain board-agnostic while
// the build omits LovyanGFX and all TFT implementation objects.
enum class TFTDriver : uint8_t { ST7789, GC9A01, ILI9341, ILI9488 };

class TFTManager : public DisplayI {
public:
    explicit TFTManager(TFTDriver = TFTDriver::ST7789) {}
    void begin() {}
    void startTask() {}
    void showBoot(const char*) {}
    void showWifiConnecting() {}
    void showWifiConnected(const char*) {}
    void showReady() {}
    void showSpoolScanned(const DisplaySpoolData&) {}
    void showWriting(const char*) {}
    void showWriteResult(bool, const char*) override {}
    void showKeypadEntry(const char*) {}
    void showError(const char*) {}
    void freeForOTA() override {}
    void updateOTAProgress(uint8_t) override {}
    void showOTAError(const char*) override {}
    void setScreenTimeoutMs(uint32_t) override {}
    void showText(const char*, const char* = nullptr) override {}
    void showText4(const char*, const char*, const char*, const char*) override {}
    void showSpool(const DisplaySpoolData&) override {}
    void showKeypad(const char*) override {}
    void showTrayDashboard(const TrayDashboardState&) override {}
};

#else

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "BoardPins.h"
#include "TFTConfig.h"
#include "DisplayI.h"
#include "TrayDashboardTypes.h"
#include "TFTDashboard.h"

// Tag type constants — one icon per type
#define TAG_TYPE_UNKNOWN      0
#define TAG_TYPE_OPENPRINTTAG 1
#define TAG_TYPE_TIGERTAG     2
#define TAG_TYPE_OPENTAG3D    3
#define TAG_TYPE_BAMBU        4
#define TAG_TYPE_NFC_PLAIN    5
#define TAG_TYPE_OPENSPOOL    6

// ---------------------------------------------------------------------------
// Display states
// ---------------------------------------------------------------------------
enum class TFTState {
    Boot,
    WifiConnecting,
    Ready,
    SpoolScanned,
    Writing,
    WriteResult,
    KeypadEntry,
    Error,
    TrayDashboard
};

// ---------------------------------------------------------------------------
// Internal message queued to the TFT task
// ---------------------------------------------------------------------------
struct TFTMessage {
    TFTState state;
    DisplaySpoolData spool; // valid when state == SpoolScanned
    char statusText[48];   // used for Boot/Ready/Error/Writing/KeypadEntry
    char statusText2[48];  // second line for generic status display
    bool writeSuccess;     // used for WriteResult
    TrayDashboardState dashboardState; // valid when state == TrayDashboard
};

// ---------------------------------------------------------------------------
// TFTManager
// ---------------------------------------------------------------------------
class TFTManager : public DisplayI {
public:
    TFTManager(TFTDriver driver = TFTDriver::ST7789);

    void begin();
    void startTask();

    // --- Call these from the main task, same callers that call LCDManager ---
    void showBoot(const char* version);
    void showWifiConnecting();
    void showWifiConnected(const char* ip);
    void showReady();
    void showSpoolScanned(const DisplaySpoolData& spool);
    void showWriting(const char* tagFormat);
    void showWriteResult(bool success, const char* tagFormat) override;
    void showKeypadEntry(const char* toolNumber);
    void showError(const char* msg);

    // OTA support — free sprite heap, render progress directly to panel
    void freeForOTA() override;
    void updateOTAProgress(uint8_t percent) override;
    void showOTAError(const char* error) override;

    void setScreenTimeoutMs(uint32_t timeoutMs) override;

    // DisplayI interface
    void showText(const char* line1, const char* line2 = nullptr) override;
    void showText4(const char* line1, const char* line2,
                   const char* line3, const char* line4) override;
    void showSpool(const DisplaySpoolData& spool) override;
    void showKeypad(const char* digits) override;
    void showTrayDashboard(const TrayDashboardState& state) override;

private:
    static void taskFunc(void* param);
    void taskLoop();
    void processQueue();

    // --- Rendering ---
    void renderBoot(const char* version);
    void renderReady();
    void renderSpoolScanned(const DisplaySpoolData& spool);
    void renderStatus(const char* line1, const char* line2 = nullptr);
    void renderWriteResult(bool success, const char* tagFormat);
    void renderKeypadEntry(const char* toolNumber);

    // --- Drawing helpers ---
    void drawSpool(int cx, int cy, int outerR, int innerR, uint32_t fillColor);
    void drawWeightBar(int x, int y, int w, int h, float remaining, float total);
    void drawTagIcon(uint8_t tagType, int x, int y);
    void blitCanvas();  // push _sprite at the panel-aware (centered on wide panels) origin
    uint32_t hexToRgb(const char* hex);
    uint32_t dimColor(uint32_t color, uint8_t brightness); // for low-spool breathing

    LGFX _tft;
    LGFX_Sprite _sprite;
    TFTDriver _driver;
    TFTDashboard _dashboard;

    QueueHandle_t _messageQueue;
    TaskHandle_t _taskHandle;
    bool _began = false;  // panel + bus initialized; render task must not start otherwise
    bool _wide = false;   // panel larger than 240x240 (ILI9488) — landscape-capable
    int  _blitOx = 0;     // centered-blit origin for the 240x240 sprite on wide panels
    int  _blitOy = 0;

    uint32_t _screenTimeoutMs;
    unsigned long _lastActivityMs;
    bool _screenOff;

    portMUX_TYPE _stateMux;

    // Breathing animation state (low spool)
    uint8_t _breathBrightness;
    int8_t _breathDirection;
    unsigned long _lastBreathMs;
    bool _isBreathing;
    uint32_t _breathColor;

    static constexpr uint32_t DEFAULT_SCREEN_TIMEOUT_MS = 30000;
    static constexpr uint32_t BREATH_STEP_MS = 20;
};

#endif // BOARD_NO_TFT
