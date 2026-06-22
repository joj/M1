/* See COPYING.txt for license details. */

/*
 * m1_pcap_writer.c — hashcat `.22000` line formatter.
 *
 * See m1_pcap_writer.h for the format.
 *
 * M1 Project
 */

#include "m1_pcap_writer.h"

#include <stdio.h>
#include <string.h>

static const char HEX[] = "0123456789abcdef";

static size_t append_lit(char *out, size_t out_size, size_t pos, const char *s)
{
    size_t n = strlen(s);
    if (pos + n + 1 > out_size) return 0;
    memcpy(out + pos, s, n);
    out[pos + n] = '\0';
    return pos + n;
}

static size_t append_hex(char *out, size_t out_size, size_t pos,
                        const uint8_t *in, size_t n)
{
    if (pos + 2*n + 1 > out_size) return 0;
    for (size_t i = 0; i < n; i++)
    {
        out[pos + 2*i]     = HEX[(in[i] >> 4) & 0x0F];
        out[pos + 2*i + 1] = HEX[in[i] & 0x0F];
    }
    out[pos + 2*n] = '\0';
    return pos + 2*n;
}

static size_t append_ssid_hex(char *out, size_t out_size, size_t pos,
                              const char *ssid)
{
    if (!ssid || !*ssid) return pos;  /* empty SSID field */
    size_t n = strlen(ssid);
    if (n > 32) n = 32;               /* spec limit */
    return append_hex(out, out_size, pos, (const uint8_t *)ssid, n);
}

size_t m1_pcap_format_pmkid(char *out, size_t out_size,
                            const uint8_t mac_ap[6],
                            const uint8_t mac_sta[6],
                            const char *ssid,
                            const uint8_t pmkid[16])
{
    if (!out || out_size == 0 || !mac_ap || !mac_sta || !pmkid) return 0;
    size_t pos = 0;
    pos = append_lit(out, out_size, pos, "WPA*" M1_PCAP22000_TYPE_PMKID "*");
    if (!pos) return 0;
    pos = append_hex(out, out_size, pos, pmkid, 16);             if (!pos) return 0;
    pos = append_lit(out, out_size, pos, "*");                   if (!pos) return 0;
    pos = append_hex(out, out_size, pos, mac_ap, 6);             if (!pos) return 0;
    pos = append_lit(out, out_size, pos, "*");                   if (!pos) return 0;
    pos = append_hex(out, out_size, pos, mac_sta, 6);            if (!pos) return 0;
    pos = append_lit(out, out_size, pos, "*");                   if (!pos) return 0;
    pos = append_ssid_hex(out, out_size, pos, ssid);             /* may be empty */
    pos = append_lit(out, out_size, pos, "***00\n");             if (!pos) return 0;
    return pos;
}

size_t m1_pcap_format_eapol(char *out, size_t out_size,
                            const uint8_t mac_ap[6],
                            const uint8_t mac_sta[6],
                            const char *ssid,
                            const uint8_t mic[16],
                            const uint8_t anonce[32],
                            const uint8_t *eapol, size_t eapol_len)
{
    if (!out || out_size == 0 || !mac_ap || !mac_sta || !mic || !anonce
        || !eapol || eapol_len == 0) return 0;
    size_t pos = 0;
    pos = append_lit(out, out_size, pos, "WPA*" M1_PCAP22000_TYPE_EAPOL "*");
    if (!pos) return 0;
    pos = append_hex(out, out_size, pos, mic, 16);               if (!pos) return 0;
    pos = append_lit(out, out_size, pos, "*");                   if (!pos) return 0;
    pos = append_hex(out, out_size, pos, mac_ap, 6);             if (!pos) return 0;
    pos = append_lit(out, out_size, pos, "*");                   if (!pos) return 0;
    pos = append_hex(out, out_size, pos, mac_sta, 6);            if (!pos) return 0;
    pos = append_lit(out, out_size, pos, "*");                   if (!pos) return 0;
    pos = append_ssid_hex(out, out_size, pos, ssid);
    pos = append_lit(out, out_size, pos, "*");                   if (!pos) return 0;
    pos = append_hex(out, out_size, pos, anonce, 32);            if (!pos) return 0;
    pos = append_lit(out, out_size, pos, "*");                   if (!pos) return 0;
    pos = append_hex(out, out_size, pos, eapol, eapol_len);      if (!pos) return 0;
    pos = append_lit(out, out_size, pos, "*00\n");               if (!pos) return 0;
    return pos;
}
