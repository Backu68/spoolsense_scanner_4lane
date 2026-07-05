#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#ifndef NATIVE_TEST
#include <WebServer.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#endif

#include "NFCTypes.h"

class DisplayI;

class WebServerManager {
public:
    static WebServerManager& getInstance();

    // Call once in setup() after WiFi is connected or AP mode started.
    // Registers routes, starts mDNS as "spoolsense" (STA only).
    bool begin(bool apMode = false, uint16_t port = 80);

    // Call from loop() — processes pending HTTP requests.
    void handleClient();
    void setDisplay(DisplayI* display) { _display = display; }

private:
    WebServerManager() = default;
    WebServerManager(const WebServerManager&) = delete;
    WebServerManager& operator=(const WebServerManager&) = delete;

#ifndef NATIVE_TEST
    WebServer _server{80};
    bool _initialized = false;
    bool _apMode = false;

    // Page handlers
    void handleLanding();
    void handleReader();
    void handleOpenPrintTagWriter();
    void handleTigerTagWriter();
    void handleOpenTag3DWriter();
    void handleOpenSpoolWriter();
    void handleSharedCSS();
    void handleSharedJS();
    void handleOpenPrintTagLogo();
    void handleTigerTagLogo();
    void handleOpenTag3DLogo();
    void handleOpenSpoolLogo();
    void handleUpdatePage();
    void handleConfigPage();

    // API handlers
    void handleApiStatus();
    void handleApiWriteTag();
    void handleApiFormatTag();
    void handleApiWriteTigerTag();
    void handleApiWriteOpenTag3D();
    void handleApiWriteOpenSpool();
    void handleApiVersion();
    void handleApiUploadFirmwareComplete();
    void handleApiUploadFirmwareChunk();
    void handleApiUpdateFromUrl();
    void handleApiOtaStatus();
    void handleApiGetConfig();
    void handleApiPostConfig();
    void handleApiDiagnostics();
    void handleTroubleshootingPage();
    void handleUIDRegistrationPage();
    void handleApiRegisterUid();
    void handleApiSpoolmanSpools();
    void handleApiSpoolmanLink();
    void handleApiSpoolmanPendingLink();
    void handleApiSpoolmanFindVendor();
    void handleApiSpoolmanFindFilament();
    void handleApiSpoolmanSaveEnrichment();

    // Log viewer
    void handleLogViewer();
    void handleApiLogs();
    void handleApiLogsClear();

    // OTA download state
    static void otaDownloadTask(void* param);
    enum class OtaState : uint8_t { IDLE, DOWNLOADING, FLASHING, SUCCESS, FAILED };
    volatile OtaState _otaState = OtaState::IDLE;
    char _otaUrl[512] = {0};
    char _otaError[64] = {0};
    volatile uint8_t _otaProgress = 0;

    DisplayI* _display = nullptr;

    // Status serializers — per-tag-type JSON builders for /api/status
    void serializeTigerTagStatus(JsonDocument& doc);
    void serializeOpenTag3DStatus(JsonDocument& doc);
    void serializeOpenSpoolStatus(JsonDocument& doc);
    void serializeBambuTagStatus(JsonDocument& doc);
    void serializeGenericUidStatus(JsonDocument& doc);
    void serializeOpenPrintTagStatus(JsonDocument& doc, const CurrentSpoolState& state);
    void serializeEnrichment(JsonDocument& doc);

    // Enrichment save helpers — each step of the Spoolman save pipeline
    int enrichFindOrCreateVendor(WiFiClient& client, HTTPClient& http, const char* baseUrl,
                                  const char* manufacturer, int confirmedId);
    int enrichFindOrCreateFilament(WiFiClient& client, HTTPClient& http, const char* baseUrl,
                                    const char* material, const char* colorHex, int vendorId,
                                    float density, float diameter, int bedTemp, int nozzleTemp, int confirmedId);
    int enrichFindSpoolByUid(WiFiClient& client, HTTPClient& http, const char* baseUrl,
                              const char* quotedUid, float& outInitialWeight, String& outExtraJson,
                              String& outFilamentMaterial, String& outFilamentColor);
    bool enrichUpdateSpool(WiFiClient& client, HTTPClient& http, const char* baseUrl,
                            int spoolId, int filamentId, float remainingG, float existingInitialWeight,
                            const char* existingExtraJson, const char* tagFormat);
    int enrichCreateSpool(WiFiClient& client, HTTPClient& http, const char* baseUrl,
                           int filamentId, float remainingG, const char* quotedUid, const char* tagFormat);

    void sendError(int code, const char* msg);
#endif
};

#endif // WEB_SERVER_MANAGER_H
