/* See COPYING.txt for license details. */
/*
 * test_subghz_validate.c — host-side tests for the verdict engine.
 *
 * Build & run:
 *   gcc -std=c11 -Wall -Wextra -I Sub_Ghz -I m1_csrc \
 *     scripts/test_subghz_validate.c \
 *     Sub_Ghz/m1_sub_ghz_validate.c -o tsv
 *   ./tsv
 *
 * No firmware dependencies pulled in — the validate engine touches
 * only SubGHz_Dec_Info_t fields, and we mock that struct here to
 * avoid dragging in the full Sub-GHz decoder.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mock just enough of m1_sub_ghz_decenc.h to compile the validate engine
 * without bringing in the M1's actual headers. */
#define _M1_SUB_GHZ_DECENC_H
typedef struct SubGHz_Dec_Info_s {
    uint32_t frequency;
    uint64_t key;
    uint16_t protocol;
    int16_t  rssi;
    uint16_t *raw_data;
    uint16_t te;
    uint16_t bit_len;
    bool     raw;
    uint32_t serial;
    uint32_t rolling;
    uint8_t  button_id;
} SubGHz_Dec_Info_t;

#include "m1_sub_ghz_validate.h"
#define M1_HOST_TEST_SGV
#include "m1_sub_ghz_sgv.h"

static int pass = 0, fail = 0;
#define EXPECT(c, lbl) do { if (c) { pass++; printf("PASS  %s\n", lbl); } \
                            else   { fail++; printf("FAIL  %s\n", lbl); } } while (0)

int main(void)
{
    /* 1. Both NULL -> NO MATCH. */
    EXPECT(m1_subghz_validate(NULL, NULL, NULL) == M1_VALIDATE_NO_MATCH,
           "Both NULL -> NO MATCH");

    /* 2. Different protocols -> NO MATCH. */
    {
        SubGHz_Dec_Info_t a = { .protocol = 0, .key = 0xABCD, .bit_len = 24 };
        SubGHz_Dec_Info_t b = { .protocol = 1, .key = 0xABCD, .bit_len = 24 };
        EXPECT(m1_subghz_validate(&a, &b, NULL) == M1_VALIDATE_NO_MATCH,
               "Different protocols -> NO MATCH");
    }

    /* 3. Fixed-code, identical key -> IDENTICAL. */
    {
        SubGHz_Dec_Info_t a = { .protocol = 0, .key = 0xDEADBEEF, .bit_len = 32 };
        SubGHz_Dec_Info_t b = { .protocol = 0, .key = 0xDEADBEEF, .bit_len = 32 };
        EXPECT(m1_subghz_validate(&a, &b, NULL) == M1_VALIDATE_MATCH_IDENTICAL,
               "Princeton identical key -> IDENTICAL");
    }

    /* 4. Fixed-code, different key -> NO MATCH. */
    {
        SubGHz_Dec_Info_t a = { .protocol = 0, .key = 0xDEADBEEF, .bit_len = 32 };
        SubGHz_Dec_Info_t b = { .protocol = 0, .key = 0xDEADBEE0, .bit_len = 32 };
        EXPECT(m1_subghz_validate(&a, &b, NULL) == M1_VALIDATE_NO_MATCH,
               "Princeton different key -> NO MATCH");
    }

    /* 5. Rolling, same serial + advanced counter -> ROLLING + delta. */
    {
        SubGHz_Dec_Info_t a = {
            .protocol = 1, .serial = 0xABC123, .rolling = 100,
        };
        SubGHz_Dec_Info_t b = {
            .protocol = 1, .serial = 0xABC123, .rolling = 105,
        };
        int32_t delta = 0;
        m1_validate_verdict_t v = m1_subghz_validate(&a, &b, &delta);
        EXPECT(v == M1_VALIDATE_MATCH_ROLLING && delta == 5,
               "Rolling +5 counter -> ROLLING delta=5");
    }

    /* 6. Rolling, same serial + same counter -> IDENTICAL (replay). */
    {
        SubGHz_Dec_Info_t a = {
            .protocol = 1, .serial = 0xABC123, .rolling = 9999,
        };
        SubGHz_Dec_Info_t b = a;
        EXPECT(m1_subghz_validate(&a, &b, NULL) == M1_VALIDATE_MATCH_IDENTICAL,
               "Rolling same counter -> IDENTICAL (replay risk)");
    }

    /* 7. Rolling, same serial + counter went backward -> IDENTICAL. */
    {
        SubGHz_Dec_Info_t a = {
            .protocol = 1, .serial = 0xABC123, .rolling = 200,
        };
        SubGHz_Dec_Info_t b = {
            .protocol = 1, .serial = 0xABC123, .rolling = 198,
        };
        EXPECT(m1_subghz_validate(&a, &b, NULL) == M1_VALIDATE_MATCH_IDENTICAL,
               "Rolling counter went backward -> IDENTICAL (replay)");
    }

    /* 8. Rolling, different serial -> FAMILY. */
    {
        SubGHz_Dec_Info_t a = {
            .protocol = 1, .serial = 0xABC123, .rolling = 100,
        };
        SubGHz_Dec_Info_t b = {
            .protocol = 1, .serial = 0xABC124, .rolling = 100,
        };
        EXPECT(m1_subghz_validate(&a, &b, NULL) == M1_VALIDATE_MATCH_FAMILY,
               "Rolling different serial -> FAMILY");
    }

    /* 9. Rolling near wrap-around: saved=0xFFFFFFF0, fresh=0x00000005 -> ROLLING delta=21. */
    {
        SubGHz_Dec_Info_t a = {
            .protocol = 1, .serial = 0xABC123, .rolling = 0xFFFFFFF0u,
        };
        SubGHz_Dec_Info_t b = {
            .protocol = 1, .serial = 0xABC123, .rolling = 0x00000005u,
        };
        int32_t delta = 0;
        m1_validate_verdict_t v = m1_subghz_validate(&a, &b, &delta);
        EXPECT(v == M1_VALIDATE_MATCH_ROLLING && delta == 21,
               "Rolling wrap-around 0xFFFFFFF0 -> 0x00000005 = +21");
    }

    /* 10. Rolling huge forward jump (>= WRAP_FORWARD_WINDOW) -> FAMILY. */
    {
        SubGHz_Dec_Info_t a = {
            .protocol = 1, .serial = 0xABC123, .rolling = 1,
        };
        SubGHz_Dec_Info_t b = {
            .protocol = 1, .serial = 0xABC123, .rolling = 0x80000000u,
        };
        EXPECT(m1_subghz_validate(&a, &b, NULL) == M1_VALIDATE_MATCH_FAMILY,
               "Rolling implausible jump -> FAMILY (likely serial collision)");
    }

    /* 11. Verdict labels are non-empty. */
    EXPECT(m1_subghz_validate_label(M1_VALIDATE_MATCH_IDENTICAL)[0] != '\0', "Label IDENTICAL");
    EXPECT(m1_subghz_validate_label(M1_VALIDATE_MATCH_ROLLING)[0]   != '\0', "Label ROLLING");
    EXPECT(m1_subghz_validate_label(M1_VALIDATE_MATCH_FAMILY)[0]    != '\0', "Label FAMILY");
    EXPECT(m1_subghz_validate_label(M1_VALIDATE_NO_MATCH)[0]        != '\0', "Label NO MATCH");

    /* 12. .sgv serialize -> parse round-trip preserves the fields we care about. */
    {
        SubGHz_Dec_Info_t in = {
            .protocol = 1, .bit_len = 56, .key = 0xCAFEBABEull,
            .serial = 0x00ABCDEF, .rolling = 0x12345678, .button_id = 3,
        };
        char body[256];
        size_t n = m1_sgv_serialize(&in, 433920000, "OOK", body, sizeof(body));
        EXPECT(n > 0, "sgv serialize produces output");
        EXPECT(strstr(body, "Frequency: 433920000\n") != NULL, "sgv contains Frequency");

        SubGHz_Dec_Info_t out = {0};
        uint32_t freq = 0;
        char mod[8] = "";
        EXPECT(m1_sgv_parse(body, &out, &freq, mod, sizeof(mod)),
               "sgv parse accepts our own output");
        EXPECT(out.protocol == 1, "round-trip protocol");
        EXPECT(out.bit_len == 56, "round-trip bit_len");
        EXPECT(out.key == 0xCAFEBABEull, "round-trip key");
        EXPECT(out.serial == 0x00ABCDEF, "round-trip serial");
        EXPECT(out.rolling == 0x12345678, "round-trip rolling");
        EXPECT(out.button_id == 3, "round-trip button");
        EXPECT(freq == 433920000, "round-trip frequency");
        EXPECT(strcmp(mod, "OOK") == 0, "round-trip modulation");
    }

    /* 13. Wrong magic -> parse rejects. */
    {
        SubGHz_Dec_Info_t out = {0};
        EXPECT(!m1_sgv_parse("Filetype: Something else\n", &out, NULL, NULL, 0),
               "sgv parse rejects wrong magic");
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
