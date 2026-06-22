/* See COPYING.txt for license details. */
/*
 * test_wifi_attack.c
 *
 * Host-side smoke test for the *pure* portions of m1_csrc/m1_wifi_attack.c.
 *
 * We don't drag in the firmware's FatFs / ESP-AT stack here; instead we
 * inline a copy of the two pure helpers (m1_wifi_psk_valid + seed builder)
 * so the test can exercise them on the host. The on-device iteration loop
 * is covered by the firmware build itself; this test guards against
 * silent regressions of the rules.
 *
 * Build & run:
 *   gcc -std=c11 -Wall -Wextra scripts/test_wifi_attack.c -o twa && ./twa
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define M1_WIFI_PSK_MAX  64
#define SEED_MAX         6

static bool psk_valid(const char *s)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 8 || n > 63) return false;
    for (size_t i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

static int seed_push(char seeds[SEED_MAX][M1_WIFI_PSK_MAX], int n,
                     const char *candidate)
{
    if (n >= SEED_MAX) return n;
    if (!psk_valid(candidate)) return n;
    for (int i = 0; i < n; i++)
        if (strcmp(seeds[i], candidate) == 0) return n;
    snprintf(seeds[n], M1_WIFI_PSK_MAX, "%s", candidate);
    return n + 1;
}

static int build_seeds(const char *ssid, char seeds[SEED_MAX][M1_WIFI_PSK_MAX])
{
    char buf[M1_WIFI_PSK_MAX];
    int n = 0;
    n = seed_push(seeds, n, ssid);
    snprintf(buf, sizeof(buf), "%s123", ssid);      n = seed_push(seeds, n, buf);
    snprintf(buf, sizeof(buf), "%s2024", ssid);     n = seed_push(seeds, n, buf);
    snprintf(buf, sizeof(buf), "%s2025", ssid);     n = seed_push(seeds, n, buf);
    snprintf(buf, sizeof(buf), "%s2026", ssid);     n = seed_push(seeds, n, buf);
    snprintf(buf, sizeof(buf), "%spassword", ssid); n = seed_push(seeds, n, buf);
    return n;
}

static int pass = 0, fail = 0;
#define EXPECT(cond, label) \
    do { if (cond) { pass++; printf("PASS  %s\n", label); } \
         else      { fail++; printf("FAIL  %s\n", label); } } while (0)

int main(void)
{
    /* ---- psk_valid ---- */
    EXPECT(!psk_valid(""),              "empty -> invalid");
    EXPECT(!psk_valid("short"),         "5 chars -> invalid");
    EXPECT(!psk_valid("1234567"),       "7 chars -> invalid");
    EXPECT( psk_valid("12345678"),      "8 chars -> valid");
    EXPECT( psk_valid("supersecret"),   "11 chars -> valid");
    EXPECT( psk_valid("abc def 1 2"),   "interior spaces ok");
    EXPECT(!psk_valid("badchar\xff"),   "non-ASCII -> invalid");
    EXPECT(!psk_valid("with\ttab"),     "control char -> invalid");
    {
        char too_long[80] = {0};
        memset(too_long, 'x', 64); /* 64 chars > 63 */
        EXPECT(!psk_valid(too_long), "64 chars -> invalid");
        too_long[63] = '\0';        /* 63 chars now */
        EXPECT( psk_valid(too_long), "63 chars -> valid");
    }

    /* ---- build_seeds ---- */
    {
        /* "HomeNet" itself is 7 chars (too short to be a PSK) and is dropped.
         * The 5 SSID+suffix variants are 10+ chars and valid. */
        char seeds[SEED_MAX][M1_WIFI_PSK_MAX] = {{0}};
        int n = build_seeds("HomeNet", seeds);
        EXPECT(n == 5,                                "HomeNet (7c) -> 5 valid seeds");
        EXPECT(strcmp(seeds[0], "HomeNet123") == 0,   "seed[0] = HomeNet123");
        EXPECT(strcmp(seeds[4], "HomeNetpassword") == 0, "seed[4] = HomeNetpassword");
    }
    {
        /* "foo" yields foo, foo123, foo2024, foo2025, foo2026 — all too short.
         * Only foopassword survives. */
        char seeds[SEED_MAX][M1_WIFI_PSK_MAX] = {{0}};
        int n = build_seeds("foo", seeds);
        EXPECT(n == 1,                              "foo -> only foopassword");
        EXPECT(strcmp(seeds[0], "foopassword") == 0, "seed[0] = foopassword");
    }
    {
        /* 8-char SSID is itself a valid PSK candidate. */
        char seeds[SEED_MAX][M1_WIFI_PSK_MAX] = {{0}};
        int n = build_seeds("ABCDEFGH", seeds);
        EXPECT(n == 6,                            "ABCDEFGH -> 6 seeds (SSID included)");
        EXPECT(strcmp(seeds[0], "ABCDEFGH") == 0, "seed[0] = ABCDEFGH");
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
