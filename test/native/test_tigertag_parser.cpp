// TigerTag parser tests — focused on the unknown-version permissive fallback
// (#256), where the type check read the diameter byte instead of the type byte.
#include "TigerTagParser.h"
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } \
                              else { printf("  PASS %s\n", msg); } } while (0)

// Build a 32-byte TigerTag payload (offsets relative to page 4).
//   0-3 version, 8-9 material, 12 type, 13 diameter
static void makeTag(uint8_t* buf, uint32_t versionId, uint16_t materialId,
                    uint8_t typeId, uint8_t diameterId) {
    memset(buf, 0, 32);
    buf[0] = (uint8_t)(versionId >> 24); buf[1] = (uint8_t)(versionId >> 16);
    buf[2] = (uint8_t)(versionId >> 8);  buf[3] = (uint8_t)versionId;
    buf[8] = (uint8_t)(materialId >> 8); buf[9] = (uint8_t)materialId;
    buf[12] = typeId;
    buf[13] = diameterId;
}

int main() {
    printf("=== TigerTag parser tests ===\n");
    uint8_t buf[32];

    // A known version ID is accepted regardless of the type/diameter bytes.
    makeTag(buf, TIGERTAG_V10, 8345, TIGERTAG_TYPE_FILAMENT, 56);
    CHECK(tigerTagCheckMagic(buf, sizeof(buf)), "known version accepted");

    // #256: unknown version + real filament tag. Type (byte 12) says filament,
    // diameter (byte 13) is 56 = 1.75mm. Reading byte 13 as the type rejects it.
    makeTag(buf, 0x00ABCDEF, 8345, TIGERTAG_TYPE_FILAMENT, 56);
    CHECK(tigerTagCheckMagic(buf, sizeof(buf)),
          "unknown version + filament type accepted (1.75mm)");

    makeTag(buf, 0x00ABCDEF, 8345, TIGERTAG_TYPE_FILAMENT, 221);
    CHECK(tigerTagCheckMagic(buf, sizeof(buf)),
          "unknown version + filament type accepted (2.85mm)");

    makeTag(buf, 0x00ABCDEF, 8394, TIGERTAG_TYPE_RESIN, 0);
    CHECK(tigerTagCheckMagic(buf, sizeof(buf)),
          "unknown version + resin type accepted");

    // The inverse of the bug: a diameter byte that happens to hold a type
    // constant must not make a non-TigerTag payload look valid.
    makeTag(buf, 0x00ABCDEF, 8345, 0x00, TIGERTAG_TYPE_FILAMENT);
    CHECK(!tigerTagCheckMagic(buf, sizeof(buf)),
          "type constant in the diameter byte does not validate");

    // Guards that must still hold.
    makeTag(buf, 0x00ABCDEF, 0, TIGERTAG_TYPE_FILAMENT, 56);
    CHECK(!tigerTagCheckMagic(buf, sizeof(buf)), "zero material rejected");

    makeTag(buf, 0x00ABCDEF, 8345, 0x42, 56);
    CHECK(!tigerTagCheckMagic(buf, sizeof(buf)), "unknown type byte rejected");

    // Parsed fields keep their documented offsets.
    makeTag(buf, TIGERTAG_V10, 8345, TIGERTAG_TYPE_FILAMENT, 221);
    TigerTagData d = tigerTagParse(buf, sizeof(buf));
    CHECK(d.type_id == TIGERTAG_TYPE_FILAMENT, "parse: type_id from byte 12");
    CHECK(d.diameter_id == 221, "parse: diameter_id from byte 13");
    CHECK(d.material_id == 8345, "parse: material_id from bytes 8-9");

    printf(failures ? "\nFAILURES: %d\n" : "\nOK: 0 failure(s)\n", failures);
    return failures ? 1 : 0;
}
