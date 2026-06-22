/* See COPYING.txt for license details. */
/*
 * test_ble_fingerprint.c
 *
 * Host-side smoke test for m1_csrc/m1_ble_fingerprint.c. Compiles and runs
 * on Linux/macOS with no STM32 dependencies. The AD packets are built
 * programmatically (so we never miscount a length byte) and then converted
 * to the hex string the fingerprint API expects.
 *
 * Build & run:
 *     gcc -std=c11 -Wall -Wextra -I ../m1_csrc \
 *         test_ble_fingerprint.c ../m1_csrc/m1_ble_fingerprint.c -o tbf
 *     ./tbf
 */

#include "m1_ble_fingerprint.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static char *to_hex(const uint8_t *buf, size_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
    {
        out[2 * i]     = H[buf[i] >> 4];
        out[2 * i + 1] = H[buf[i] & 0x0F];
    }
    out[2 * n] = '\0';
    return out;
}

/* Build one AD structure: [len][type][data...]. len = 1 + data_len. */
static size_t ad_emit(uint8_t *out, size_t off, uint8_t type,
                      const uint8_t *data, size_t data_len)
{
    out[off++] = (uint8_t)(1 + data_len);
    out[off++] = type;
    memcpy(out + off, data, data_len);
    return off + data_len;
}

static int pass_n = 0, fail_n = 0;

static void check(const char *label, const uint8_t *adv, size_t adv_len,
                  const uint8_t *sr, size_t sr_len,
                  int addr_type, const char *expect_substr)
{
    char adv_hex[128] = "", sr_hex[128] = "";
    if (adv_len) to_hex(adv, adv_len, adv_hex);
    if (sr_len)  to_hex(sr,  sr_len,  sr_hex);
    char out[64];
    m1_ble_describe(adv_hex, sr_hex, addr_type, out, sizeof(out));
    int ok = (expect_substr[0] == '\0') ? (out[0] != '\0')
                                        : (strstr(out, expect_substr) != NULL);
    printf("%s  [%s] -> \"%s\"  (expect \"%s\")\n",
           ok ? "PASS" : "FAIL", label, out, expect_substr);
    if (ok) pass_n++; else fail_n++;
}

int main(void)
{
    uint8_t buf[64];
    size_t  n;

    /* ---- Apple Nearby Info (subtype 0x10): an iPhone. ---- */
    {
        /* mfgr-specific data = 4C 00 | 10 05 01 02 03 04 05 */
        uint8_t data[] = {0x4C,0x00, 0x10,0x05, 0x01,0x02,0x03,0x04,0x05};
        n = ad_emit(buf, 0, 0xFF, data, sizeof(data));
        check("Apple iPhone (Nearby Info 0x10)", buf, n, NULL, 0, 1, "iPhone");
    }

    /* ---- Apple AirPods 1st gen (subtype 0x07, model 0x0220). ---- */
    {
        uint8_t data[] = {0x4C,0x00, 0x07,0x19, 0x02,0x20,
                          0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
        n = ad_emit(buf, 0, 0xFF, data, sizeof(data));
        check("AirPods 1st gen (model 0x0220)", buf, n, NULL, 0, 1, "AirPods (1st gen)");
    }

    /* ---- Apple AirTag / Find My (subtype 0x12). ---- */
    {
        uint8_t data[] = {0x4C,0x00, 0x12,0x05, 0xAA,0xBB,0xCC,0xDD,0xEE};
        n = ad_emit(buf, 0, 0xFF, data, sizeof(data));
        check("AirTag (Find My 0x12)", buf, n, NULL, 0, 1, "Find My");
    }

    /* ---- Microsoft Swift Pair (mfgr 0x0006, scenario 0x01). ---- */
    {
        uint8_t data[] = {0x06,0x00, 0x01, 0x03,0x00,0x80,0x00,0x01,0x02};
        n = ad_emit(buf, 0, 0xFF, data, sizeof(data));
        check("Microsoft Swift Pair", buf, n, NULL, 0, 0, "Swift Pair");
    }

    /* ---- Complete Local Name "TileX" — no mfgr data. ---- */
    {
        const uint8_t name[] = {'T','i','l','e','X'};
        /* Flags AD first, then Local Name. */
        uint8_t flags[] = {0x06};
        n = 0;
        n = ad_emit(buf, n, 0x01, flags, sizeof(flags));
        n = ad_emit(buf, n, 0x09, name,  sizeof(name));
        check("Complete Local Name TileX", buf, n, NULL, 0, 0, "TileX");
    }

    /* ---- Eddystone: Complete UUID16 = 0xFEAA. ---- */
    {
        uint8_t flags[] = {0x06};
        uint8_t uuids[] = {0xAA,0xFE};
        n = 0;
        n = ad_emit(buf, n, 0x01, flags, sizeof(flags));
        n = ad_emit(buf, n, 0x03, uuids, sizeof(uuids));
        check("Eddystone (svc UUID 0xFEAA)", buf, n, NULL, 0, 0, "Eddystone");
    }

    /* ---- Tile mfgr 0x015D. ---- */
    {
        uint8_t data[] = {0x5D,0x01, 0xAA};
        n = ad_emit(buf, 0, 0xFF, data, sizeof(data));
        check("Tile (mfgr 0x015D)", buf, n, NULL, 0, 0, "Tile");
    }

    /* ---- Google Fast Pair: mfgr 0x00E0 + 3-byte model ID. ---- */
    {
        uint8_t data[] = {0xE0,0x00, 0xAA,0xBB,0xCC};
        n = ad_emit(buf, 0, 0xFF, data, sizeof(data));
        check("Google Fast Pair", buf, n, NULL, 0, 0, "Fast Pair");
    }

    /* ---- Empty adv, random addr -> generic random fallback. ---- */
    check("Empty adv (random)", buf, 0, NULL, 0, 1, "random");

    /* ---- Empty adv, public addr -> "BLE device". ---- */
    check("Empty adv (public)", buf, 0, NULL, 0, 0, "BLE device");

    /* ---- HID keyboard appearance (0x03C1). ---- */
    {
        uint8_t flags[] = {0x06};
        uint8_t app[]   = {0xC1,0x03};
        n = 0;
        n = ad_emit(buf, n, 0x01, flags, sizeof(flags));
        n = ad_emit(buf, n, 0x19, app,   sizeof(app));
        check("Appearance keyboard", buf, n, NULL, 0, 0, "");
    }

    /* ---- Scan-response carries the Local Name. ---- */
    {
        /* Adv has only flags. */
        uint8_t flags[] = {0x06};
        n = ad_emit(buf, 0, 0x01, flags, sizeof(flags));
        /* Scan-response has Complete Local Name = "MyDevice". */
        const uint8_t name[] = {'M','y','D','e','v','i','c','e'};
        uint8_t sr[16];
        size_t srn = ad_emit(sr, 0, 0x09, name, sizeof(name));
        check("Name in scan-rsp", buf, n, sr, srn, 0, "MyDevice");
    }

    printf("\n%d passed, %d failed\n", pass_n, fail_n);
    return fail_n == 0 ? 0 : 1;
}
