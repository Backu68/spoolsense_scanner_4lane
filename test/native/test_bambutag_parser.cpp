// Bambu tag parser tests — the contract the read path in NFCManager relies on
// after #257: blocks that never read arrive zeroed, and a zeroed identity block
// must never produce a "valid" spool.
#include "BambuTagParser.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

// Fill a plausible tag: block 1 variant, block 2 type, block 5 colour/weight/
// diameter, block 6 temps. Indexes follow BAMBU_BLOCKS.
static void makeTag(uint8_t blocks[BAMBU_BLOCK_COUNT][16]) {
    memset(blocks, 0, BAMBU_BLOCK_COUNT * 16);
    memcpy(blocks[0], "A00-K0", 6);            // material variant
    memcpy(blocks[1], "PLA Basic", 9);         // filament type
    blocks[3][0] = 0xFF; blocks[3][1] = 0x66;  // colour RGBA
    blocks[3][2] = 0x00; blocks[3][3] = 0xFF;
    blocks[3][4] = 0xE8; blocks[3][5] = 0x03;  // weight 1000 g (LE)
    blocks[4][8] = 0xE6; blocks[4][9] = 0x00;  // hotend max 230
    blocks[4][10] = 0xC8; blocks[4][11] = 0x00; // hotend min 200
}

int main() {
    printf("=== Bambu parser tests ===\n");
    uint8_t blocks[BAMBU_BLOCK_COUNT][16];
    BambuTagData d;

    makeTag(blocks);
    d = BambuTagData();
    parseBambuBlocks(blocks, d);
    CHECK(d.valid, "complete tag is valid");
    CHECK(strcmp(d.filament_type, "PLA Basic") == 0, "filament type parsed");
    CHECK(d.weight_g == 1000, "weight parsed");
    CHECK(d.color_r == 0xFF && d.color_g == 0x66, "colour parsed");
    CHECK(d.hotend_max == 230 && d.hotend_min == 200, "hotend temps parsed");

    // #257: a failed/never-read block reaches the parser zeroed, not as stack
    // garbage. A zeroed identity block must not yield a usable spool.
    makeTag(blocks);
    memset(blocks[1], 0, 16);   // block 2 (filament type) missing
    d = BambuTagData();
    parseBambuBlocks(blocks, d);
    CHECK(!d.valid, "missing filament-type block is not valid");

    // Zeroed colour/weight block yields zeros, never random values — the read
    // path rejects this case outright, but the parser must stay deterministic.
    makeTag(blocks);
    memset(blocks[3], 0, 16);
    d = BambuTagData();
    parseBambuBlocks(blocks, d);
    CHECK(d.weight_g == 0, "missing weight block reads as zero");
    CHECK(d.color_r == 0 && d.color_g == 0 && d.color_b == 0,
          "missing colour block reads as zero");

    // Entirely zeroed input (worst case) must be inert.
    memset(blocks, 0, sizeof(blocks));
    d = BambuTagData();
    parseBambuBlocks(blocks, d);
    CHECK(!d.valid, "all-zero blocks are not valid");
    CHECK(d.filament_type[0] == '\0', "all-zero blocks leave type empty");

    // #257: the essential-block completeness gate the read path uses. Every
    // essential index must veto; every non-essential index must be tolerated.
    {
        bool ok[BAMBU_BLOCK_COUNT];
        for (int i = 0; i < BAMBU_BLOCK_COUNT; i++) ok[i] = true;
        CHECK(bambuEssentialBlocksPresent(ok), "all blocks read -> complete");

        for (int e = 0; e < BAMBU_ESSENTIAL_COUNT; e++) {
            for (int i = 0; i < BAMBU_BLOCK_COUNT; i++) ok[i] = true;
            ok[BAMBU_ESSENTIAL_INDEXES[e]] = false;
            char msg[64];
            snprintf(msg, sizeof(msg), "missing essential block %d -> incomplete",
                     BAMBU_BLOCKS[BAMBU_ESSENTIAL_INDEXES[e]]);
            CHECK(!bambuEssentialBlocksPresent(ok), msg);
        }

        for (int idx = 0; idx < BAMBU_BLOCK_COUNT; idx++) {
            bool essential = false;
            for (int e = 0; e < BAMBU_ESSENTIAL_COUNT; e++)
                if (BAMBU_ESSENTIAL_INDEXES[e] == idx) essential = true;
            if (essential) continue;
            for (int i = 0; i < BAMBU_BLOCK_COUNT; i++) ok[i] = true;
            ok[idx] = false;
            char msg[64];
            snprintf(msg, sizeof(msg), "missing optional block %d -> still complete",
                     BAMBU_BLOCKS[idx]);
            CHECK(bambuEssentialBlocksPresent(ok), msg);
        }
    }

    // Per-index essential predicate — drives the read path's fail-fast exit.
    {
        for (int idx = 0; idx < BAMBU_BLOCK_COUNT; idx++) {
            bool expected = false;
            for (int e = 0; e < BAMBU_ESSENTIAL_COUNT; e++)
                if (BAMBU_ESSENTIAL_INDEXES[e] == idx) expected = true;
            char msg[64];
            snprintf(msg, sizeof(msg), "block %d essential=%d", BAMBU_BLOCKS[idx], expected);
            CHECK(bambuIsEssentialIndex(idx) == expected, msg);
        }
    }

    // parseBambuBlocks must report validity, not an unconditional true —
    // the read path uses the return value to decide whether to publish.
    memset(blocks, 0, sizeof(blocks));
    d = BambuTagData();
    CHECK(!parseBambuBlocks(blocks, d), "parse returns false for zeroed blocks");
    makeTag(blocks);
    d = BambuTagData();
    CHECK(parseBambuBlocks(blocks, d), "parse returns true for a complete tag");

    // Strings must stay terminated even when a block has no NUL byte.
    makeTag(blocks);
    memset(blocks[1], 'X', 16);
    d = BambuTagData();
    parseBambuBlocks(blocks, d);
    CHECK(strlen(d.filament_type) <= 15, "unterminated type string is bounded");

    printf(failures ? "\nFAILURES: %d\n" : "\nOK: 0 failure(s)\n", failures);
    return failures ? 1 : 0;
}
