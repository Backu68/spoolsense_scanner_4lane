#include "TFTManager.h"
#include "ConfigurationManager.h"
#include "MemoryDiagnostics.h"
#include "TaskUtils.h"
#include <Arduino.h>

// TFT display controller for 240x240 ST7789 or GC9A01 (round). Manages sprite rendering to PSRAM
// (16-bit color), screen timeout/breathing animations, and OTA progress. Task runs on core 0.
// Sprite reduces flicker vs direct-to-panel rendering; 5ms task loop allows other tasks to run.

// Color constants for UI elements
// ────────────────────────────────────────────────────────────────────────
static const uint32_t COLOR_BG        = 0x000000;
static const uint32_t COLOR_HEADER_BG = 0x1A1A2E;
static const uint32_t COLOR_TEXT      = 0xFFFFFF;
static const uint32_t COLOR_SUBTEXT   = 0xAAAAAA;
static const uint32_t COLOR_SPOOL_RIM = 0x444444;
static const uint32_t COLOR_SPOOL_HUB = 0x222222;
static const uint32_t COLOR_BAR_BG    = 0x333333;
static const uint32_t COLOR_BAR_FG    = 0x00CC66;
static const uint32_t COLOR_BAR_LOW   = 0xFF4444;
static const uint32_t COLOR_ACCENT    = 0x4FC3F7;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
TFTManager::TFTManager(TFTDriver driver)
    : _tft(driver),
      _sprite(&_tft),
      _messageQueue(nullptr),
      _taskHandle(nullptr),
      _screenTimeoutMs(DEFAULT_SCREEN_TIMEOUT_MS),
      _lastActivityMs(0),
      _screenOff(false),
      _stateMux(portMUX_INITIALIZER_UNLOCKED),
      _breathBrightness(255),
      _breathDirection(-1),
      _lastBreathMs(0),
      _isBreathing(false),
      _breathColor(0xFFFFFF),
      _driver(driver) {}

// ---------------------------------------------------------------------------
// begin / startTask
// ---------------------------------------------------------------------------
void TFTManager::begin() {
    _tft.init();
    delay(100);  // panel initialization delay
    _tft.setRotation(0);
    _tft.setBrightness(255);
    _tft.fillScreen(COLOR_BG);

#ifdef BOARD_HAS_PSRAM
    // 16-bit color needs PSRAM on S3-DevKitC: 240x240x2 bytes = 115KB, internal RAM only ~50KB free
    _sprite.setPsram(true);
    _sprite.setColorDepth(16);
    if (!_sprite.createSprite(_tft.width(), _tft.height())) {
        Serial.println("TFTManager: PSRAM sprite failed, falling back to 8-bit internal RAM");
        _sprite.setPsram(false);
        _sprite.setColorDepth(8);  // 8-bit indexed color fits in internal RAM; reduced colors
        if (!_sprite.createSprite(_tft.width(), _tft.height())) {
            Serial.println("TFTManager: WARNING — sprite allocation failed");
        }
    }
#else
    _sprite.setColorDepth(8);
    if (!_sprite.createSprite(_tft.width(), _tft.height())) {
        Serial.println("TFTManager: WARNING — sprite allocation failed");
    }
#endif

    _messageQueue = xQueueCreate(8, sizeof(TFTMessage));
    if (_messageQueue == nullptr) {
        Serial.println("TFTManager: WARNING — queue creation failed");
    }
    _lastActivityMs = millis();  // initialize timeout clock
}

void TFTManager::startTask() {
    // 8192 bytes: TFT drawing operations + sprite manipulation use more stack than simple
    // I2C LCD operations; sprite creation/pushSprite are stack-heavy due to local buffers
    BaseType_t result = createTaskWithAffinity(
        taskFunc,
        "TFTTask",
        8192,
        this,
        1,      // priority 1 (low; allows other tasks to preempt during 20ms delays)
        &_taskHandle,
        0       // core 0: FreeRTOS main task and LCD also run here
    );
    if (result == pdPASS) {
        Serial.println("TFTManager: Task started on core 0");
    } else {
        Serial.println("TFTManager: WARNING — task creation failed");
        _taskHandle = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Public show* methods — called from main task
// ---------------------------------------------------------------------------
void TFTManager::showBoot(const char* version) {
    TFTMessage msg{};
    msg.state = TFTState::Boot;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", version);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showWifiConnecting() {
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting;
    snprintf(msg.statusText, sizeof(msg.statusText), "WiFi");
    snprintf(msg.statusText2, sizeof(msg.statusText2), "Connecting...");
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showWifiConnected(const char* ip) {
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting;
    snprintf(msg.statusText, sizeof(msg.statusText), "WiFi Connected");
    snprintf(msg.statusText2, sizeof(msg.statusText2), "%s", ip);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showReady() {
    TFTMessage msg{};
    msg.state = TFTState::Ready;
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showSpoolScanned(const DisplaySpoolData& spool) {
    TFTMessage msg{};
    msg.state = TFTState::SpoolScanned;
    msg.spool = spool;
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showWriting(const char* tagFormat) {
    TFTMessage msg{};
    msg.state = TFTState::Writing;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", tagFormat);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showWriteResult(bool success, const char* tagFormat) {
    TFTMessage msg{};
    msg.state = TFTState::WriteResult;
    msg.writeSuccess = success;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", tagFormat);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showKeypadEntry(const char* toolNumber) {
    TFTMessage msg{};
    msg.state = TFTState::KeypadEntry;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", toolNumber);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showError(const char* errMsg) {
    TFTMessage msg{};
    msg.state = TFTState::Error;
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", errMsg);
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::setScreenTimeoutMs(uint32_t timeoutMs) {
    // Update timeout config and wake screen if off; uses critical section to avoid race
    // with taskLoop checking _screenTimeoutMs during timeout calculation
    taskENTER_CRITICAL(&_stateMux);
    _screenTimeoutMs = timeoutMs;
    _lastActivityMs = millis();  // reset inactivity timer
    bool wasOff = _screenOff;
    _screenOff = false;
    taskEXIT_CRITICAL(&_stateMux);
    if (wasOff) {
        _tft.setBrightness(255);  // wake screen immediately
    }
}

// ---------------------------------------------------------------------------
// Task loop
// ---------------------------------------------------------------------------
void TFTManager::taskFunc(void* param) {
    TFTManager* self = static_cast<TFTManager*>(param);
    self->taskLoop();
}

void TFTManager::taskLoop() {
    while (true) {
        processQueue();
        MemoryDiagnostics::reportSelf(MemoryDiagnostics::Task::TFT);
        vTaskDelay(pdMS_TO_TICKS(20));  // 50 Hz update rate; allows other tasks to run between renders
    }
}

void TFTManager::processQueue() {
    TFTMessage msg;
    if (_messageQueue && xQueueReceive(_messageQueue, &msg, 0) == pdTRUE) {
        // Message received: wake screen, reset timeout, and render
        taskENTER_CRITICAL(&_stateMux);
        _lastActivityMs = millis();
        bool wasOff = _screenOff;
        _screenOff = false;
        _isBreathing = false;  // stop any prior breathing when new message arrives
        taskEXIT_CRITICAL(&_stateMux);

        if (wasOff) {
            _tft.setBrightness(255);
        }

        switch (msg.state) {
            case TFTState::Boot:
                renderBoot(msg.statusText);
                break;
            case TFTState::WifiConnecting:
                renderStatus(msg.statusText, msg.statusText2[0] ? msg.statusText2 : nullptr);
                break;
            case TFTState::Ready:
                renderReady();
                break;
            case TFTState::SpoolScanned:
                // Breathing animation for low-weight spools (< 100g): visual cue for respool
                _isBreathing = (msg.spool.remainingWeight > 0 &&
                                msg.spool.remainingWeight <= (float)ConfigurationManager::getInstance().getLowSpoolThreshold());
                if (_isBreathing) {
                    _breathColor = hexToRgb(msg.spool.colorHex);
                    _breathBrightness = 255;
                    _breathDirection = -1;  // start dimming
                }
                renderSpoolScanned(msg.spool);
                break;
            case TFTState::Writing:
                renderStatus("Writing...", msg.statusText);
                break;
            case TFTState::WriteResult:
                renderWriteResult(msg.writeSuccess, msg.statusText);
                break;
            case TFTState::KeypadEntry:
                renderKeypadEntry(msg.statusText);
                break;
            case TFTState::Error:
                renderStatus("Error", msg.statusText);
                break;
            case TFTState::TrayDashboard:
                _dashboard.render(&_sprite, msg.dashboardState);
                break;
        }
    }

    // Breathing animation: smooth pulse between 30 and 255 brightness; updates every BREATH_STEP_MS
    if (_isBreathing && (millis() - _lastBreathMs >= BREATH_STEP_MS)) {
        _lastBreathMs = millis();
        int16_t next = (int16_t)_breathBrightness + _breathDirection * 3;
        // Clamp bounds and reverse direction at limits
        if (next <= 30)  { next = 30;  _breathDirection = 1; }
        if (next >= 255) { next = 255; _breathDirection = -1; }
        _breathBrightness = (uint8_t)next;
        _tft.setBrightness(_breathBrightness);
    }

    // Screen timeout: turn off after inactivity period if configured (timeoutMs > 0)
    uint32_t timeoutMs;
    unsigned long lastActivity;
    bool screenOff;
    taskENTER_CRITICAL(&_stateMux);
    timeoutMs   = _screenTimeoutMs;
    lastActivity = _lastActivityMs;
    screenOff   = _screenOff;
    taskEXIT_CRITICAL(&_stateMux);

    if (!screenOff && timeoutMs > 0 && (millis() - lastActivity >= timeoutMs)) {
        _tft.setBrightness(0);
        taskENTER_CRITICAL(&_stateMux);
        _screenOff = true;
        taskEXIT_CRITICAL(&_stateMux);
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void TFTManager::renderBoot(const char* version) {
    _sprite.fillScreen(COLOR_BG);

    // Centered logo text
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(3);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, _tft.height() / 2 - 20);

    _sprite.setTextColor(COLOR_SUBTEXT);
    _sprite.setTextSize(1);
    _sprite.drawString(version, _tft.width() / 2, _tft.height() / 2 + 20);

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderReady() {
    _sprite.fillScreen(COLOR_BG);

    // Header bar
    _sprite.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    // Idle spool graphic: grey fill indicates no spool selected (waiting for tag)
    int cx = _tft.width() / 2;
    int cy = _tft.height() / 2 + 10;
    drawSpool(cx, cy, 70, 28, COLOR_SPOOL_RIM);  // grey color = idle state

    // Prompt
    _sprite.setTextColor(COLOR_SUBTEXT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("Tap a spool to scan", cx, _tft.height() - 16);

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderSpoolScanned(const DisplaySpoolData& spool) {
    _sprite.fillScreen(COLOR_BG);

    int W = _tft.width();   // 240
    int H = _tft.height();  // 240

    // Header bar
    _sprite.fillRect(0, 0, W, 28, COLOR_HEADER_BG);

    // Tag type icon top-left in header
    drawTagIcon(spool.tagType, 4, 2);

    // "SpoolSense" in header
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    // ---- Spool graphic ----
    uint32_t fillColor = hexToRgb(spool.colorHex);
    int cx = W / 2;
    int cy = 110;
    drawSpool(cx, cy, 68, 26, fillColor);

    // ---- Text area ----
    int textY = 190;
    _sprite.setTextDatum(MC_DATUM);

    // Brand + material
    char brandMat[48];
    snprintf(brandMat, sizeof(brandMat), "%s  %s", spool.brand, spool.material);
    _sprite.setTextColor(COLOR_TEXT);
    _sprite.setTextSize(1);
    _sprite.drawString(brandMat, cx, textY);

    // Weight bar
    if (spool.totalWeight > 0) {
        drawWeightBar(20, textY + 14, W - 40, 8,
                      spool.remainingWeight, spool.totalWeight);

        // Weight text
        char weightStr[32];
        snprintf(weightStr, sizeof(weightStr), "%.0fg / %.0fg",
                 spool.remainingWeight, spool.totalWeight);
        _sprite.setTextColor(COLOR_SUBTEXT);
        _sprite.setTextSize(1);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.drawString(weightStr, cx, textY + 30);
    }

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderStatus(const char* line1, const char* line2) {
    _sprite.fillScreen(COLOR_BG);

    _sprite.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    int cy = _tft.height() / 2;
    _sprite.setTextColor(COLOR_TEXT);
    _sprite.setTextSize(2);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString(line1, _tft.width() / 2, line2 ? cy - 12 : cy);

    if (line2) {
        _sprite.setTextColor(COLOR_SUBTEXT);
        _sprite.setTextSize(1);
        _sprite.drawString(line2, _tft.width() / 2, cy + 12);
    }

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderWriteResult(bool success, const char* tagFormat) {
    _sprite.fillScreen(COLOR_BG);
    _sprite.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    int cx = _tft.width() / 2;
    int cy = _tft.height() / 2;

    if (success) {
        // Green checkmark circle
        _sprite.fillCircle(cx, cy - 20, 22, 0x00CC66);
        _sprite.setTextColor(COLOR_BG);
        _sprite.setTextSize(3);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.drawString("OK", cx, cy - 20);
        _sprite.setTextColor(COLOR_TEXT);
        _sprite.setTextSize(1);
        _sprite.drawString("Write OK", cx, cy + 12);
        _sprite.setTextColor(COLOR_SUBTEXT);
        _sprite.drawString(tagFormat, cx, cy + 26);
    } else {
        _sprite.fillCircle(cx, cy - 20, 22, 0xFF4444);
        _sprite.setTextColor(COLOR_BG);
        _sprite.setTextSize(3);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.drawString("X", cx, cy - 20);
        _sprite.setTextColor(COLOR_TEXT);
        _sprite.setTextSize(1);
        _sprite.drawString("Write failed", cx, cy + 12);
        _sprite.setTextColor(COLOR_SUBTEXT);
        _sprite.drawString(tagFormat, cx, cy + 26);
    }

    _sprite.pushSprite(0, 0);
}

void TFTManager::renderKeypadEntry(const char* toolNumber) {
    _sprite.fillScreen(COLOR_BG);
    _sprite.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _sprite.setTextColor(COLOR_ACCENT);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString("SpoolSense", _tft.width() / 2, 14);

    int cx = _tft.width() / 2;
    _sprite.setTextColor(COLOR_SUBTEXT);
    _sprite.setTextSize(1);
    _sprite.drawString("Assign to tool:", cx, 100);

    _sprite.setTextColor(COLOR_TEXT);
    _sprite.setTextSize(4);
    _sprite.drawString(toolNumber, cx, 130);

    _sprite.setTextColor(COLOR_SUBTEXT);
    _sprite.setTextSize(1);
    _sprite.drawString("Press # to confirm", cx, 185);

    _sprite.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

void TFTManager::drawSpool(int cx, int cy, int outerR, int innerR, uint32_t fillColor) {
    // Soft shadow for depth perception
    _sprite.fillCircle(cx + 3, cy + 3, outerR, 0x111111);

    // Outer rim (dark grey ring)
    _sprite.fillCircle(cx, cy, outerR, COLOR_SPOOL_RIM);

    // Filament fill: inner area up to rim shows spool color (matches tag data)
    // Spokes drawn on top create visual separation between filled area and hub
    _sprite.fillCircle(cx, cy, outerR - 5, fillColor);

    // Hub (inner dark circle) — represents spool spindle
    _sprite.fillCircle(cx, cy, innerR, COLOR_SPOOL_HUB);

    // Spokes: 6 radial lines simulate spool structure; improves visual clarity at small scales
    for (int i = 0; i < 6; i++) {
        float angle = i * (M_PI / 3.0f);  // 60° spacing
        int x1 = cx + (int)(cos(angle) * (innerR - 2));
        int y1 = cy + (int)(sin(angle) * (innerR - 2));
        int x2 = cx + (int)(cos(angle) * (outerR - 8));
        int y2 = cy + (int)(sin(angle) * (outerR - 8));
        _sprite.drawLine(x1, y1, x2, y2, COLOR_SPOOL_RIM);
        _sprite.drawLine(x1+1, y1, x2+1, y2, COLOR_SPOOL_RIM);  // 2px wide for visibility
    }

    // Rim outline: crisp edge between spool and background
    _sprite.drawCircle(cx, cy, outerR, 0x666666);
    _sprite.drawCircle(cx, cy, outerR - 1, 0x555555);

    // Hub outline and center dot: focal point for visual balance
    _sprite.drawCircle(cx, cy, innerR, 0x555555);
    _sprite.fillCircle(cx, cy, 4, 0x888888);  // spindle center
}

void TFTManager::drawWeightBar(int x, int y, int w, int h,
                                float remaining, float total) {
    float ratio = (total > 0) ? (remaining / total) : 0.0f;
    ratio = constrain(ratio, 0.0f, 1.0f);
    int filled = (int)(w * ratio);

    // Red bar when ≤10% remaining (visual alert for respool soon)
    uint32_t barColor = (ratio <= 0.1f) ? COLOR_BAR_LOW : COLOR_BAR_FG;

    // Background tray
    _sprite.fillRoundRect(x, y, w, h, h / 2, COLOR_BAR_BG);
    // Filled portion (green or red)
    if (filled > 0) {
        _sprite.fillRoundRect(x, y, filled, h, h / 2, barColor);
    }
    // Outline for definition
    _sprite.drawRoundRect(x, y, w, h, h / 2, 0x555555);
}

void TFTManager::drawTagIcon(uint8_t tagType, int x, int y) {
    const char* label = nullptr;
    uint32_t color = COLOR_SUBTEXT;

    // Each tag format has a unique color and label for at-a-glance identification
    switch (tagType) {
        case TAG_TYPE_OPENPRINTTAG:
            label = "OPT";
            color = 0x4FC3F7;  // cyan
            break;
        case TAG_TYPE_TIGERTAG:
            label = "TT";
            color = 0xFF9800;  // orange
            break;
        case TAG_TYPE_OPENTAG3D:
            label = "OT3D";
            color = 0x4CAF50;  // green
            break;
        case TAG_TYPE_BAMBU:
            label = "Bambu";
            color = 0x1DB954;  // Bambu's signature green
            break;
        case TAG_TYPE_NFC_PLAIN:
            label = "NFC+";
            color = 0x00BCD4;  // cyan (generic NFC)
            break;
        case TAG_TYPE_OPENSPOOL:
            label = "OS";
            color = 0xE91E63;  // pink
            break;
        default:
            return;  // unknown type; skip label
    }

    _sprite.setTextColor(color);
    _sprite.setTextSize(1);
    _sprite.setTextDatum(TR_DATUM);  // top-right aligned
    _sprite.drawString(label, x + 22, y);
}

uint32_t TFTManager::hexToRgb(const char* hex) {
    if (!hex || strlen(hex) < 6) return 0xCCCCCC;  // grey fallback on invalid input
    char buf[7];
    // Strip leading # if present (Spoolman color format: "#RRGGBB")
    const char* h = (hex[0] == '#') ? hex + 1 : hex;
    strncpy(buf, h, 6);
    buf[6] = '\0';
    unsigned long val = strtoul(buf, nullptr, 16);
    return (uint32_t)val;
}

// Blend color by scaling RGB components: used to dim spool graphic when tag leaves,
// showing last-scanned spool at reduced brightness to indicate "no longer present"
uint32_t TFTManager::dimColor(uint32_t color, uint8_t brightness) {
    uint8_t r = ((color >> 16) & 0xFF) * brightness / 255;
    uint8_t g = ((color >> 8)  & 0xFF) * brightness / 255;
    uint8_t b = ((color)       & 0xFF) * brightness / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// DisplayI interface — renders text using the TFT status screen
void TFTManager::showText(const char* line1, const char* line2) {
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting; // generic two-line status renderer
    snprintf(msg.statusText, sizeof(msg.statusText), "%s", line1 ? line1 : "");
    snprintf(msg.statusText2, sizeof(msg.statusText2), "%s", line2 ? line2 : "");
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showText4(const char* line1, const char* line2,
                           const char* line3, const char* line4) {
    // DisplayI interface bridging: 4-line LCD display → 2-line TFT; prioritize line3 (primary) + line4 (secondary)
    // Strip line1 LCD decoration (asterisk borders: "*text*") for clean TFT output
    TFTMessage msg{};
    msg.state = TFTState::WifiConnecting;
    if (line1 && line3) {
        // Remove LCD-specific frame: asterisks at start/end, leading spaces
        const char* clean = line1;
        while (*clean == '*' || *clean == ' ') clean++;
        size_t len = strlen(clean);
        while (len > 0 && (clean[len-1] == '*' || clean[len-1] == ' ')) len--;
        char stripped[48] = {};
        if (len > 0 && len < sizeof(stripped)) { memcpy(stripped, clean, len); stripped[len] = '\0'; }
        // Prepend context label if available
        if (stripped[0]) {
            snprintf(msg.statusText, sizeof(msg.statusText), "%s: %s", stripped, line3);
        } else {
            snprintf(msg.statusText, sizeof(msg.statusText), "%s", line3);
        }
    } else {
        snprintf(msg.statusText, sizeof(msg.statusText), "%s", line3 ? line3 : (line1 ? line1 : ""));
    }
    snprintf(msg.statusText2, sizeof(msg.statusText2), "%s", line4 ? line4 : (line2 ? line2 : ""));
    if (_messageQueue) {
        if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
            Serial.println("TFTManager: Queue full, dropped display update");
        }
    }
}

void TFTManager::showKeypad(const char* digits) {
    showKeypadEntry(digits && digits[0] ? digits : "_");
}

void TFTManager::showTrayDashboard(const TrayDashboardState& state) {
    if (!_messageQueue) return;
    TFTMessage msg = {};
    msg.state = TFTState::TrayDashboard;
    msg.dashboardState = state;
    if (xQueueSend(_messageQueue, &msg, 0) != pdTRUE) {
        Serial.println("TFTManager: Queue full, dropping tray dashboard");
    }
}

void TFTManager::showSpool(const DisplaySpoolData& spool) {
    showSpoolScanned(spool);
}

// ---------------------------------------------------------------------------
// OTA support — free sprite for SSL heap, render directly to panel
// ---------------------------------------------------------------------------
void TFTManager::freeForOTA() {
    // Stop TFT task before OTA: queue messages from main task would race with direct
    // frame buffer writes. OTA may reboot (success) or hang (failure); sprite not restored.
    if (_taskHandle != nullptr) {
        vTaskDelete(_taskHandle);
        _taskHandle = nullptr;
        Serial.println("TFTManager: Task stopped for OTA");
    }

    // Delete sprite to recover 57.6KB heap for OTA SSL/HTTPS buffer (16-bit color on 240x240)
    _sprite.deleteSprite();
    Serial.printf("TFTManager: Sprite freed, heap now %u\n", ESP.getFreeHeap());

    // Wake screen for OTA progress display
    _tft.setBrightness(255);

    // Draw OTA screen directly to panel (bypass sprite to save time on each update)
    _tft.fillScreen(COLOR_BG);

    // Header bar
    _tft.fillRect(0, 0, _tft.width(), 28, COLOR_HEADER_BG);
    _tft.setTextColor(COLOR_ACCENT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("SpoolSense", _tft.width() / 2, 14);

    // Status text
    int cx = _tft.width() / 2;
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextSize(2);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("Updating...", cx, 80);

    // Progress bar outline (updateOTAProgress will fill this region)
    int barX = 20;
    int barY = 120;
    int barW = _tft.width() - 40;  // 200px on 240px display
    int barH = 20;
    _tft.drawRoundRect(barX, barY, barW, barH, barH / 2, 0x555555);

    // Initial percentage
    _tft.setTextColor(COLOR_SUBTEXT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("0%", cx, 155);
}

void TFTManager::updateOTAProgress(uint8_t percent) {
    if (percent > 100) percent = 100;

    int barX = 20;
    int barY = 120;
    int barW = _tft.width() - 40;  // 200px
    int barH = 20;
    int cx = _tft.width() / 2;

    // Update filled portion of progress bar (left-to-right)
    int filled = (barW * percent) / 100;
    if (filled > 0) {
        _tft.fillRoundRect(barX, barY, filled, barH, barH / 2, COLOR_ACCENT);
    }

    // Erase old percentage text, redraw new value
    _tft.fillRect(cx - 30, 148, 60, 16, COLOR_BG);  // clear old text area
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%u%%", percent);
    _tft.setTextColor(COLOR_SUBTEXT);
    _tft.setTextSize(1);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString(pctStr, cx, 155);
}

void TFTManager::showOTAError(const char* error) {
    int cx = _tft.width() / 2;

    // Clear progress bar and text area
    _tft.fillRect(0, 60, _tft.width(), _tft.height() - 60, COLOR_BG);

    // Error icon: red circle with X
    _tft.fillCircle(cx, 100, 22, 0xFF4444);
    _tft.setTextColor(COLOR_BG);
    _tft.setTextSize(3);
    _tft.setTextDatum(MC_DATUM);
    _tft.drawString("X", cx, 100);

    // Error status
    _tft.setTextColor(COLOR_TEXT);
    _tft.setTextSize(1);
    _tft.drawString("Update Failed", cx, 135);

    // Error details (e.g., "Connection timeout", "Not enough space")
    if (error && error[0]) {
        _tft.setTextColor(COLOR_SUBTEXT);
        _tft.drawString(error, cx, 155);
    }
}
