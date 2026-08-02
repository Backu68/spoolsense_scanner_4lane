#include "BambuTagParser.h"
#include <cstring>

static uint16_t readU16LE(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static float readFloatLE(const uint8_t* p) {
    uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float val;
    memcpy(&val, &bits, sizeof(float));
    return val;
}

static void trimString(char* str, size_t max_len) {
    size_t len = 0;
    while (len < max_len && str[len] != '\0') len++;
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\0')) len--;
    str[len] = '\0';
}

bool parseBambuBlocks(const uint8_t blocks[][16], BambuTagData& out) {
    // blocks[0] = block 1: Material variant (null-terminated ASCII, 16 bytes)
    memcpy(out.material_variant, blocks[0], 16);
    out.material_variant[15] = '\0';
    trimString(out.material_variant, 15);

    // blocks[1] = block 2: Filament type string (null-terminated ASCII)
    memcpy(out.filament_type, blocks[1], 16);
    out.filament_type[15] = '\0';
    trimString(out.filament_type, 15);

    // blocks[2] = block 4: Detailed filament specs (not parsed)

    // blocks[3] = block 5: RGBA color (4 bytes at 0), weight (uint16 LE at 4), diameter (float LE at 8)
    out.color_r = blocks[3][0];
    out.color_g = blocks[3][1];
    out.color_b = blocks[3][2];
    out.color_a = blocks[3][3];
    out.weight_g = readU16LE(&blocks[3][4]);
    out.diameter_mm = readFloatLE(&blocks[3][8]);

    // blocks[4] = block 6: [0-1] drying_temp, [2-3] drying_time, [4-5] bed_temp,
    // [6-7] unused, [8-9] hotend_max, [10-11] hotend_min
    out.drying_temp = readU16LE(&blocks[4][0]);
    out.drying_time = readU16LE(&blocks[4][2]);
    out.bed_temp = readU16LE(&blocks[4][4]);
    out.hotend_max = readU16LE(&blocks[4][8]);
    out.hotend_min = readU16LE(&blocks[4][10]);

    // blocks[5] = block 13: Production date (null-terminated ASCII)
    memcpy(out.production_date, blocks[5], 16);
    out.production_date[19] = '\0';
    trimString(out.production_date, 19);

    // blocks[6] = block 14: Filament length (uint16 LE at offset 4, in meters)
    out.filament_length_m = readU16LE(&blocks[6][4]);

    // blocks[7] = block 16: Extended color data (raw 16 bytes)
    memcpy(out.color_extended, blocks[7], 16);

    out.valid = (out.filament_type[0] != '\0');

    // Report validity: an all-zero or corrupt identity block must not be
    // published as a spool (it would fall back to a default material).
    return out.valid;
}

bool bambuIsEssentialIndex(uint8_t idx) {
    for (uint8_t i = 0; i < BAMBU_ESSENTIAL_COUNT; i++) {
        if (BAMBU_ESSENTIAL_INDEXES[i] == idx) return true;
    }
    return false;
}

bool bambuEssentialBlocksPresent(const bool blockOk[]) {
    for (uint8_t i = 0; i < BAMBU_ESSENTIAL_COUNT; i++) {
        if (!blockOk[BAMBU_ESSENTIAL_INDEXES[i]]) return false;
    }
    return true;
}
