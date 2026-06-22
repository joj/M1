/* See COPYING.txt for license details. */
/*
 * test_pcap_writer.c — host-side tests for the hashcat .22000 line
 * formatter and the capture state machine.
 *
 * Build & run:
 *   gcc -std=c11 -Wall -Wextra -I m1_csrc \
 *     scripts/test_pcap_writer.c \
 *     m1_csrc/m1_pcap_writer.c m1_csrc/m1_wifi_capture.c -o tpw
 *   ./tpw
 */

#include "m1_pcap_writer.h"
#include "m1_wifi_capture.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int pass = 0, fail = 0;
#define EXPECT(c, l) do { if (c) { pass++; printf("PASS  %s\n", l); } \
                          else   { fail++; printf("FAIL  %s\n", l); } } while (0)

/* Sink that just captures the last line for inspection. */
static char  last_line[2048];
static size_t last_len;
static bool sink_capture(const char *line, size_t len, void *u)
{
    (void)u;
    memcpy(last_line, line, len);
    last_line[len] = '\0';
    last_len = len;
    return true;
}

int main(void)
{
    /* ---- PMKID line ---- */
    {
        uint8_t ap[6]    = {0xAA,0xBB,0xCC,0x11,0x22,0x33};
        uint8_t sta[6]   = {0x44,0x55,0x66,0x77,0x88,0x99};
        uint8_t pmkid[16];
        for (int i = 0; i < 16; i++) pmkid[i] = (uint8_t)(0xA0 + i);
        char out[512];
        size_t n = m1_pcap_format_pmkid(out, sizeof(out), ap, sta, "MyNet", pmkid);
        EXPECT(n > 0, "pmkid line produced");
        EXPECT(strncmp(out, "WPA*01*", 7) == 0,                "pmkid starts with WPA*01*");
        EXPECT(strstr(out, "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf*") != NULL,
                                                              "pmkid hex present");
        EXPECT(strstr(out, "*aabbcc112233*445566778899*") != NULL,
                                                              "macs present");
        /* SSID "MyNet" => 4d794e6574 */
        EXPECT(strstr(out, "*4d794e6574***00\n") != NULL,      "ssid hex + tail ***00");
    }
    {
        /* hidden SSID: empty ESSID field. */
        uint8_t ap[6] = {1,2,3,4,5,6};
        uint8_t sta[6] = {7,8,9,10,11,12};
        uint8_t pmkid[16] = {0};
        char out[512];
        size_t n = m1_pcap_format_pmkid(out, sizeof(out), ap, sta, NULL, pmkid);
        EXPECT(n > 0, "pmkid with null ssid produced");
        EXPECT(strstr(out, "***00\n") != NULL, "hidden SSID -> empty essid + tail");
    }

    /* ---- EAPOL line ---- */
    {
        uint8_t ap[6]  = {0xDE,0xAD,0xBE,0xEF,0x00,0x01};
        uint8_t sta[6] = {0x10,0x20,0x30,0x40,0x50,0x60};
        uint8_t mic[16];   for (int i = 0; i < 16; i++) mic[i] = (uint8_t)i;
        uint8_t anon[32];  for (int i = 0; i < 32; i++) anon[i] = (uint8_t)(i + 0x40);
        uint8_t eapol[120]; for (int i = 0; i < 120; i++) eapol[i] = (uint8_t)(i ^ 0x55);
        char out[1024];
        size_t n = m1_pcap_format_eapol(out, sizeof(out),
                                        ap, sta, "HomeNet",
                                        mic, anon, eapol, sizeof(eapol));
        EXPECT(n > 0, "eapol line produced");
        EXPECT(strncmp(out, "WPA*02*", 7) == 0, "eapol starts with WPA*02*");
        EXPECT(strstr(out, "*deadbeef0001*102030405060*") != NULL, "macs present");
        EXPECT(strstr(out, "*48 6f 6d 65 4e 65 74*") != NULL || /* not literal, just check ssid hex */
               strstr(out, "*486f6d654e6574*") != NULL, "ssid hex 'HomeNet'");
        EXPECT(out[n-1] == '\n' && out[n-2] == '0' && out[n-3] == '0' && out[n-4] == '*',
               "tail is *00\\n");
        /* exact length check: 7 (WPA*02*) + 32 (mic) + 1 + 12 (ap) + 1 + 12 (sta)
         * + 1 + 14 (ssid hex 7 bytes) + 1 + 64 (anonce) + 1 + 240 (eapol hex) + 4 (*00\n)
         * = 390 */
        EXPECT(n == 390, "eapol line length sanity");
    }

    /* ---- Capture state machine: M1+M2 -> 1 EAPOL record ---- */
    {
        m1_capture_ctx_t ctx;
        m1_capture_init(&ctx, sink_capture, NULL);

        uint8_t bssid[6] = {0xa0,0xa1,0xa2,0xa3,0xa4,0xa5};
        uint8_t sta[6]   = {0xb0,0xb1,0xb2,0xb3,0xb4,0xb5};

        m1_capture_feed_beacon(&ctx, bssid, "TestNet");

        /* Fake M1 frame: zeros + ANonce at offset 17..49. */
        uint8_t m1[120] = {0};
        for (int i = 0; i < 32; i++) m1[17 + i] = (uint8_t)(0x10 + i);
        bool emitted_after_m1 = m1_capture_feed_eapol(&ctx, bssid, sta, 1,
                                                      m1, sizeof(m1));
        EXPECT(!emitted_after_m1, "M1 alone does not emit");

        /* Fake M2 frame: zeros + MIC at offset 81..96. */
        uint8_t m2[120] = {0};
        for (int i = 0; i < 16; i++) m2[81 + i] = (uint8_t)(0xC0 + i);
        bool emitted_after_m2 = m1_capture_feed_eapol(&ctx, bssid, sta, 2,
                                                      m2, sizeof(m2));
        EXPECT(emitted_after_m2, "M2 after M1 emits a record");
        EXPECT(ctx.records_written == 1, "records_written == 1");
        EXPECT(ctx.handshakes_written == 1, "handshakes_written == 1");
        EXPECT(strncmp(last_line, "WPA*02*c0c1c2c3c4c5c6c7c8c9cacbcccdcecf*", 39) == 0,
               "record carries MIC bytes from M2");
        EXPECT(strstr(last_line, "*54657374" /* 'Test' */) != NULL ||
               strstr(last_line, "*54657374" /* still 'Test' */) != NULL,
               "record carries SSID 'TestNet' hex");
    }

    /* ---- Capture state machine: PMKID alone -> 1 PMKID record ---- */
    {
        m1_capture_ctx_t ctx;
        m1_capture_init(&ctx, sink_capture, NULL);
        uint8_t bssid[6] = {1,2,3,4,5,6};
        uint8_t sta[6]   = {7,8,9,10,11,12};
        uint8_t pmkid[16] = {0x99};
        bool emitted = m1_capture_feed_pmkid(&ctx, bssid, sta, pmkid);
        EXPECT(emitted, "pmkid emits a record");
        EXPECT(ctx.pmkids_written == 1, "pmkids_written == 1");
        EXPECT(strncmp(last_line, "WPA*01*", 7) == 0, "pmkid record has WPA*01*");
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
