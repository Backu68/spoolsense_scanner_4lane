"""Build-time ESP32-C5 enablement patch for the pinned LovyanGFX 1.2.21.

LovyanGFX 1.2.21 has no ESP32-C5 device profile. Upstream's sanctioned path
(https://github.com/lovyan03/LovyanGFX/issues/700) is the develop branch plus
extending every CONFIG_IDF_TARGET_ESP32C3/C6 conditional to include C5. We are
pinned to the 1.2.21 release, so this script applies that exact, minimal
aliasing to the env-local libdeps copy at build time:

  1. device.hpp: the C6 profile include block also matches C5.
  2. platforms/esp32/common.{cpp,hpp}: every `defined CONFIG_IDF_TARGET_ESP32C6`
     disjunct also matches C5 (C5 takes the C6 register branches).
  3. common.cpp / Bus_SPI.cpp: C5's DMA is the AHB-DMA block (soc/ahb_dma_reg.h,
     AHB_DMA_* names, extern AHB_DMA) where C3/C6 have GDMA_*. A C5-only block
     pre-defines the library's DMA_* aliases from the AHB_DMA_* names (verified
     1:1 in the C5 soc headers) so the library's own `#if !defined` GDMA
     fallback skips itself; `GDMA` is aliased to the AHB_DMA instance for the
     channel-stride sizeof.

Fail-closed by design: the patch verifies the dependency is exactly 1.2.21 and
that every replacement lands the expected number of times; any mismatch aborts
the build rather than silently compiling something unexpected. Idempotent: a
marker file skips re-patching. C5 GPIO/SPI register compatibility with C6 is
an assumption validated on hardware, not here.
"""
Import("env")  # noqa: F821  (PlatformIO SConscript context)

import json
import os
import sys

LIB_DIR = env.subst(os.path.join("$PROJECT_LIBDEPS_DIR", "$PIOENV", "LovyanGFX"))
MARKER = os.path.join(LIB_DIR, ".spoolsense_c5_patch_applied")
PINNED_VERSION = "1.2.21"

_AXI_STRUCT_ANCHOR = (
    " #elif __has_include(<soc/axi_dma_struct.h>) // ESP32P4\n"
    "  #include <soc/axi_dma_struct.h>\n"
    " #endif\n"
)

_C5_DMA_COMMON = _AXI_STRUCT_ANCHOR + (
    " #if defined (CONFIG_IDF_TARGET_ESP32C5) // AHB-DMA naming; no gdma_struct.h on C5\n"
    "  #include <soc/ahb_dma_struct.h>\n"
    "  #include <soc/ahb_dma_reg.h>\n"
    "  #define DMA_OUT_PERI_SEL_CH0_REG  AHB_DMA_OUT_PERI_SEL_CH0_REG\n"
    "  #define DMA_IN_PERI_SEL_CH0_REG  AHB_DMA_IN_PERI_SEL_CH0_REG\n"
    "  #define DMA_PERI_OUT_SEL_CH0_M  AHB_DMA_PERI_OUT_SEL_CH0_M\n"
    "  #define DMA_PERI_IN_SEL_CH0_M  AHB_DMA_PERI_IN_SEL_CH0_M\n"
    "  #define GDMA AHB_DMA\n"
    " #endif\n"
)

_C5_DMA_BUS_SPI = _AXI_STRUCT_ANCHOR + (
    " #if defined (CONFIG_IDF_TARGET_ESP32C5) // AHB-DMA naming; no gdma_reg.h on C5\n"
    "  #include <soc/ahb_dma_struct.h>\n"
    "  #include <soc/ahb_dma_reg.h>\n"
    "  #define DMA_OUT_LINK_CH0_REG       AHB_DMA_OUT_LINK_CH0_REG\n"
    "  #define DMA_OUTFIFO_STATUS_CH0_REG AHB_DMA_OUTFIFO_STATUS_CH0_REG\n"
    "  #define DMA_OUTLINK_START_CH0      AHB_DMA_OUTLINK_START_CH0\n"
    "  #define DMA_OUTFIFO_EMPTY_CH0      AHB_DMA_OUTFIFO_EMPTY_CH0\n"
    "  #define GDMA AHB_DMA\n"
    " #endif\n"
)

# (relative path, [(old, new, expected_count), ...])
EDITS = [
    (
        os.path.join("src", "lgfx", "v1", "platforms", "device.hpp"),
        [
            (
                "#if defined (CONFIG_IDF_TARGET_ESP32C6)",
                "#if defined (CONFIG_IDF_TARGET_ESP32C6) || defined (CONFIG_IDF_TARGET_ESP32C5)",
                1,
            ),
        ],
    ),
    (
        os.path.join("src", "lgfx", "v1", "platforms", "esp32", "common.hpp"),
        [
            (
                "defined ( CONFIG_IDF_TARGET_ESP32C6 )",
                "defined ( CONFIG_IDF_TARGET_ESP32C6 ) || defined ( CONFIG_IDF_TARGET_ESP32C5 )",
                1,
            ),
        ],
    ),
    (
        os.path.join("src", "lgfx", "v1", "platforms", "esp32", "common.cpp"),
        [
            (
                "defined ( CONFIG_IDF_TARGET_ESP32C6 )",
                "defined ( CONFIG_IDF_TARGET_ESP32C6 ) || defined ( CONFIG_IDF_TARGET_ESP32C5 )",
                14,
            ),
            (
                "defined CONFIG_IDF_TARGET_ESP32C6",
                "defined CONFIG_IDF_TARGET_ESP32C6 || defined CONFIG_IDF_TARGET_ESP32C5",
                6,
            ),
            (_AXI_STRUCT_ANCHOR, _C5_DMA_COMMON, 1),
        ],
    ),
    (
        os.path.join("src", "lgfx", "v1", "platforms", "esp32", "Bus_SPI.cpp"),
        [
            (_AXI_STRUCT_ANCHOR, _C5_DMA_BUS_SPI, 1),
        ],
    ),
]


def fail(msg):
    sys.stderr.write("\npatch_lovyangfx_c5: FATAL: %s\n" % msg)
    sys.stderr.write("patch_lovyangfx_c5: refusing to build an unpatched/unknown "
                     "LovyanGFX for ESP32-C5.\n\n")
    env.Exit(1)


def verify_version():
    lib_json = os.path.join(LIB_DIR, "library.json")
    try:
        with open(lib_json) as f:
            version = json.load(f).get("version", "")
    except OSError as e:
        fail("cannot read %s: %s" % (lib_json, e))
    if version != PINNED_VERSION:
        fail("LovyanGFX version is %r, patch is only validated against %s"
             % (version, PINNED_VERSION))


def verify_already_patched():
    # The marker alone is not trusted: re-verify the version and that every
    # patched file actually contains its rewritten text (a restored/corrupted
    # source with a stale marker must fail closed, not silently build).
    verify_version()
    for rel, subs in EDITS:
        path = os.path.join(LIB_DIR, rel)
        try:
            with open(path) as f:
                content = f.read()
        except OSError as e:
            fail("marker present but cannot read %s: %s" % (rel, e))
        for _old, new, expected in subs:
            if content.count(new) < expected:
                fail("marker present but %s lacks the patched text %r — "
                     "stale marker; wipe .pio/libdeps/%s/LovyanGFX and rebuild"
                     % (rel, new[:60], env.subst("$PIOENV")))


def main():
    if os.path.isfile(MARKER):
        verify_already_patched()
        print("patch_lovyangfx_c5: already applied (marker + content verified)")
        return

    if not os.path.isdir(LIB_DIR):
        fail("LovyanGFX not found at %s (lib_deps not installed yet?)" % LIB_DIR)

    verify_version()

    for rel, subs in EDITS:
        path = os.path.join(LIB_DIR, rel)
        try:
            with open(path) as f:
                content = f.read()
        except OSError as e:
            fail("cannot read %s: %s" % (path, e))
        if "CONFIG_IDF_TARGET_ESP32C5" in content:
            fail("%s already mentions ESP32C5 but marker is missing — "
                 "mixed/unknown state, wipe .pio/libdeps/%s and rebuild"
                 % (rel, env.subst("$PIOENV")))
        # Apply bare-token edits only after parenthesized ones so the bare
        # pattern cannot match inside an already-rewritten parenthesized form.
        for old, new, expected in subs:
            found = content.count(old)
            if found != expected:
                fail("%s: expected %d occurrence(s) of %r, found %d — "
                     "1.2.21 text drifted, re-verify the patch"
                     % (rel, expected, old, found))
            content = content.replace(old, new)
        with open(path, "w") as f:
            f.write(content)
        print("patch_lovyangfx_c5: patched %s" % rel)

    with open(MARKER, "w") as f:
        f.write("LovyanGFX %s patched for ESP32-C5 (see scripts/patch_lovyangfx_c5.py)\n"
                % PINNED_VERSION)
    print("patch_lovyangfx_c5: done (LovyanGFX %s + C5 aliasing)" % PINNED_VERSION)


main()
