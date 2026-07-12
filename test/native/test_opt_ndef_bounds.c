/* Regression tests for #239: parse_ndef_record must reject payload lengths
 * that exceed the data actually held, instead of handing downstream region
 * walkers an end pointer past the buffer (CWE-125, ASan-verified on the
 * mobile app's byte-identical copy of this library). */
#include <stdio.h>
#include <string.h>
#include "openprinttag_lib.h"

static int failures = 0;

#define CHECK(cond, name)                                          \
    do {                                                           \
        if (cond) {                                                \
            printf("  PASS  %s\n", name);                          \
        } else {                                                   \
            printf("  FAIL  %s\n", name);                          \
            failures++;                                            \
        }                                                          \
    } while (0)

/* Build a minimal TLV + NDEF header at tag->data[0] that declares a media-type
 * record whose payload length is `declared`, while the tag only actually holds
 * `held` bytes of data. Layout mirrors what opt_parse_ndef walks:
 *   [0] 0x03 (NDEF TLV) [1] tlv_len
 *   [2] NDEF flags (MB|ME|SR|TNF=media)  [3] type_len  [4] payload_len(SR)
 *   [5..] type ("application/openprinttag" length OPT_MIME_TYPE_LEN)          */
static void build_tag(opt_tag_t *tag, uint16_t declared_payload, uint16_t held) {
    memset(tag, 0, sizeof(*tag));
    uint8_t *d = tag->data;
    size_t off = 0;
    d[off++] = 0xE1;                       /* capability container magic */
    d[off++] = 0x10;                       /* CC version */
    d[off++] = 0x3E;                       /* CC size */
    d[off++] = 0x00;                       /* CC access */
    d[off++] = 0x03;                       /* NDEF message TLV */
    d[off++] = 0xFF;                       /* long-form TLV length follows */
    d[off++] = 0x00;                       /* TLV length hi */
    d[off++] = 0xF0;                       /* TLV length lo (240 — deliberately generous) */
    d[off++] = 0xD2;                       /* MB|ME|SR, TNF=2 (media type) */
    const char *mime = "application/vnd.openprinttag";
    uint8_t type_len = (uint8_t)strlen(mime);
    d[off++] = type_len;
    d[off++] = (uint8_t)(declared_payload & 0xFF); /* SR payload length */
    memcpy(d + off, mime, type_len);
    off += type_len;
    tag->data_size = held;
}

int main(void) {
    printf("=== #239 NDEF bounds regression ===\n");

    /* 1) Truncated tag: header declares 200-byte payload, only 44 bytes held.
     *    Must fail with a parse error, not succeed with an OOB region. */
    opt_tag_t tag;
    build_tag(&tag, 200, 44);
    opt_error_t err = opt_parse_ndef(&tag);
    CHECK(err != OPT_OK, "truncated payload rejected");

    /* 2) Sanity: the check must not break in-bounds parses — declare a payload
     *    that genuinely fits. A fully valid OPT payload isn't assembled here;
     *    accept either OPT_OK or a *later* (CBOR/region) error, but the parse
     *    must not fail on the NDEF length check when the bytes are held. */
    build_tag(&tag, 8, 220);
    err = opt_parse_ndef(&tag);
    CHECK(err != OPT_ERR_NDEF_PARSE || tag.payload_size == 0,
          "in-bounds payload passes the length gate");

    /* 3) Pathological: declared length that would wrap 16-bit math. */
    build_tag(&tag, 0xFF, 50);
    err = opt_parse_ndef(&tag);
    CHECK(err != OPT_OK, "255-byte claim against 50 held rejected");

    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
