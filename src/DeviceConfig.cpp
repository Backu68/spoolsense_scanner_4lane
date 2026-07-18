

#include "DeviceConfig.h"
#include "UserConfig.h"

// Runtime device configuration: board profile, WiFi, MQTT, Spoolman, peripherals — populated from user build defines

// Fallback defaults for optional build flags
#ifndef AUTOMATION_MODE
#define AUTOMATION_MODE 0
#endif
#ifndef ENABLE_KEYPAD
#define ENABLE_KEYPAD 0
#endif
#ifndef ENABLE_BAMBU_DASHBOARD
#define ENABLE_BAMBU_DASHBOARD 0
#endif

static const DeviceConfig kDeviceConfig = {
    .device_name = DEVICE_NAME,

    // detect board at compile time to set SPI bus layout, pin definitions
    .board_profile =
#if defined(BOARD_ESP32_S3)
        BoardProfile::ESP32_S3,
#else
        BoardProfile::ESP32_WROOM,
#endif

    .wifi = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD,
    },

    .mqtt = {
        .host = MQTT_HOST,
        .port = MQTT_PORT,
        .username = MQTT_USERNAME,
        .password = MQTT_PASSWORD,
        .topic_prefix = MQTT_TOPIC_PREFIX,
    },

    .spoolman = {
        .enabled = SPOOLMAN_ENABLED,
        .base_url = SPOOLMAN_URL,
    },

    .peripherals = {
        .lcd_enabled = ENABLE_LCD,
        .status_led_enabled = ENABLE_STATUS_LED,
        .keypad_enabled = ENABLE_KEYPAD,
        .bambu_dashboard = ENABLE_BAMBU_DASHBOARD,
    },

    .automation_mode = AUTOMATION_MODE,
};

const DeviceConfig& getDeviceConfig()
{
    return kDeviceConfig;
}
