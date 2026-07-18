// ESP32-C5 compatibility: the common soc/gpio_periph.h declares
// GPIO_PIN_MUX_REG[] for every target, but the C5 prebuilt libsoc.a in
// Arduino-ESP32 3.3.9 / IDF 5.5.4 does not define it (preview-target gap).
// LovyanGFX's pinMode/gpio save-restore links against it. Define the table
// here from the SDK's own per-pin IO_MUX_GPIO<n>_REG macros — same shape as
// gpio_periph.c on the other targets. Remove when the C5 SDK ships the table.
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32C5

#include <stdint.h>
#include "soc/io_mux_reg.h"
#include "soc/soc_caps.h"

// Weak so a future C5 SDK that ships the real table wins at link time
// instead of colliding with this stopgap.
__attribute__((weak)) const uint32_t GPIO_PIN_MUX_REG[SOC_GPIO_PIN_COUNT] = {
    IO_MUX_GPIO0_REG,  IO_MUX_GPIO1_REG,  IO_MUX_GPIO2_REG,  IO_MUX_GPIO3_REG,
    IO_MUX_GPIO4_REG,  IO_MUX_GPIO5_REG,  IO_MUX_GPIO6_REG,  IO_MUX_GPIO7_REG,
    IO_MUX_GPIO8_REG,  IO_MUX_GPIO9_REG,  IO_MUX_GPIO10_REG, IO_MUX_GPIO11_REG,
    IO_MUX_GPIO12_REG, IO_MUX_GPIO13_REG, IO_MUX_GPIO14_REG, IO_MUX_GPIO15_REG,
    IO_MUX_GPIO16_REG, IO_MUX_GPIO17_REG, IO_MUX_GPIO18_REG, IO_MUX_GPIO19_REG,
    IO_MUX_GPIO20_REG, IO_MUX_GPIO21_REG, IO_MUX_GPIO22_REG, IO_MUX_GPIO23_REG,
    IO_MUX_GPIO24_REG, IO_MUX_GPIO25_REG, IO_MUX_GPIO26_REG, IO_MUX_GPIO27_REG,
    IO_MUX_GPIO28_REG,
};

_Static_assert(SOC_GPIO_PIN_COUNT == 29,
               "ESP32-C5 GPIO count changed; update GPIO_PIN_MUX_REG table");

#endif // CONFIG_IDF_TARGET_ESP32C5
