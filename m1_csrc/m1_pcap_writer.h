/* See COPYING.txt for license details. */

/*
 * m1_pcap_writer.h
 *
 * Writer for hashcat-format `.22000` files (mode 22000: combined
 * WPA-PBKDF2-PMKID+EAPOL hash). One line per crackable record:
 *
 *   PROTOCOL*TYPE*PMKID_or_MIC*MAC_AP*MAC_CLIENT*ESSID*ANONCE*EAPOL*MC
 *
 *   PROTOCOL    = "WPA" (literal)
 *   TYPE        = "01" (PMKID) or "02" (EAPOL handshake)
 *   PMKID_or_MIC = 32 hex chars (16 bytes)
 *   MAC_AP      = 12 hex chars
 *   MAC_CLIENT  = 12 hex chars
 *   ESSID       = SSID as hex (empty allowed for hidden)
 *   ANONCE      = 64 hex chars (32 bytes) -- empty for PMKID rows
 *   EAPOL       = EAPOL frame as hex -- empty for PMKID rows
 *   MC          = "00" (message-pair / nonce-error flags; safe default
 *                       per hashcat docs)
 *
 * Pure logic — m1_pcap_writer_format_* return a NUL-terminated line that
 * the caller can write to FatFs, UART, etc. Easy to unit-test on host.
 *
 * M1 Project
 */

#ifndef M1_PCAP_WRITER_H_
#define M1_PCAP_WRITER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M1_PCAP22000_TYPE_PMKID  "01"
#define M1_PCAP22000_TYPE_EAPOL  "02"

#define M1_PCAP_LINE_MAX         2048  /* generous: ~190B EAPOL -> ~400 hex */

/* Format one PMKID record as a `.22000` line into `out`. Returns the
 * number of bytes written (excl. NUL), or 0 on overflow / bad args.
 * `ssid` may be NULL or empty (hidden network).
 */
size_t m1_pcap_format_pmkid(char *out, size_t out_size,
                            const uint8_t mac_ap[6],
                            const uint8_t mac_sta[6],
                            const char *ssid,
                            const uint8_t pmkid[16]);

/* Format one EAPOL handshake record. `anonce` is the AP nonce from M1,
 * `eapol` + `eapol_len` is the EAPOL frame whose MIC is being tested
 * (usually M2). `mic` is the 16-byte MIC extracted from that frame.
 */
size_t m1_pcap_format_eapol(char *out, size_t out_size,
                            const uint8_t mac_ap[6],
                            const uint8_t mac_sta[6],
                            const char *ssid,
                            const uint8_t mic[16],
                            const uint8_t anonce[32],
                            const uint8_t *eapol, size_t eapol_len);

#endif /* M1_PCAP_WRITER_H_ */
