#pragma once
#include <cstdint>

struct BambuTagData {
    bool valid = false;
    char material_variant[16] = {};
    char filament_type[16] = {};
    uint8_t color_r = 0, color_g = 0, color_b = 0, color_a = 0;
    uint16_t weight_g = 0;
    float diameter_mm = 0.0f;
    uint16_t drying_temp = 0;
    uint16_t drying_time = 0;
    uint16_t bed_temp = 0;
    uint16_t hotend_min = 0;
    uint16_t hotend_max = 0;
    char production_date[20] = {};
    uint32_t filament_length_m = 0;
    uint8_t color_extended[16] = {};
};

static constexpr uint8_t BAMBU_BLOCKS[] = { 1, 2, 4, 5, 6, 13, 14, 16 };
static constexpr uint8_t BAMBU_BLOCK_COUNT = 8;

// Indexes into BAMBU_BLOCKS whose contents identify the filament or feed the
// printer: block 1 material variant, block 2 filament type, block 5 colour /
// weight / diameter, block 6 drying and nozzle/bed temperatures. A read that
// misses any of these must be discarded rather than parsed — the rest
// (block 4 unparsed, 13 date, 14 length, 16 extended colour) may be absent.
static constexpr uint8_t BAMBU_ESSENTIAL_INDEXES[] = { 0, 1, 3, 4 };
static constexpr uint8_t BAMBU_ESSENTIAL_COUNT = 4;


// Parses the eight blocks into `out`. Returns out.valid — false when the
// identity block is empty, so a zeroed/corrupt read is never published.
bool parseBambuBlocks(const uint8_t blocks[][16], BambuTagData& out);

// True when the block at this index into BAMBU_BLOCKS is essential.
bool bambuIsEssentialIndex(uint8_t idx);

// True when every essential block was read successfully. `blockOk` is indexed
// like BAMBU_BLOCKS and must have BAMBU_BLOCK_COUNT entries.
bool bambuEssentialBlocksPresent(const bool blockOk[]);
