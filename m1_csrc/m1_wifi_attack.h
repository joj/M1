/* See COPYING.txt for license details. */

/*
 * m1_wifi_attack.h
 *
 * SD-backed WPA-PSK candidate iterator for the dictionary-attack feature.
 *
 * The wordlist file lives at /databases/wifi_wordlist.txt on the SD card
 * (one PSK per line, UTF-8 / ASCII). The iterator reads it line by line,
 * filtering anything that isn't a valid WPA-PSK (8..63 printable-ASCII
 * chars). It also injects a handful of SSID-derived seeds at the top of
 * the stream (the SSID itself, then a few simple variants) since router
 * defaults frequently include the SSID.
 *
 * The UI owns the outer loop so it can poll the keypad between attempts.
 *
 * M1 Project
 */

#ifndef M1_WIFI_ATTACK_H_
#define M1_WIFI_ATTACK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M1_WIFI_WORDLIST_PATH        "0:/databases/wifi_wordlist.txt"
#define M1_WIFI_CRACKED_PATH         "0:/databases/cracked.txt"
#define M1_WIFI_PSK_MAX              64  /* 63 + NUL */
#define M1_WIFI_SSID_MAX             33

/* Return codes from m1_wifi_attack_try() and the underlying AT wrapper. */
#define M1_WIFI_TRY_OK               0
#define M1_WIFI_TRY_TIMEOUT          1
#define M1_WIFI_TRY_WRONG_PASS       2
#define M1_WIFI_TRY_NO_AP            3
#define M1_WIFI_TRY_CONN_FAIL        4
#define M1_WIFI_TRY_OTHER            5
#define M1_WIFI_TRY_TRANSPORT        6  /* couldn't even send command */

typedef struct m1_wifi_attack_iter_s m1_wifi_attack_iter_t;

/* Open the wordlist for the supplied target SSID. seed_count is the number
 * of SSID-derived candidates that will be yielded *before* the file is
 * read; this is informational so the UI can show progress correctly.
 * Returns NULL if the file can't be opened (no SD, missing file, etc.).
 */
m1_wifi_attack_iter_t *m1_wifi_attack_open(const char *target_ssid,
                                           size_t *out_seed_count);

/* Fetch the next candidate into `out`. Returns true on success, false when
 * exhausted. Filters and dedupes are applied. `out` is null-terminated.
 */
bool m1_wifi_attack_next(m1_wifi_attack_iter_t *it,
                         char *out, size_t out_size);

/* Approximate total candidate count, for ETA / progress display. Combines
 * the seed list (exact) with an estimate of the wordlist (file size /
 * average line length). Cheap to call.
 */
size_t m1_wifi_attack_total_estimate(const m1_wifi_attack_iter_t *it);

/* Release the file handle and iterator state. */
void m1_wifi_attack_close(m1_wifi_attack_iter_t *it);

/* Convenience that owns ctrl_cmd_t lifecycle: one CWJAP attempt with
 * the given timeout. Returns one of M1_WIFI_TRY_*.
 */
int m1_wifi_attack_try(const char *ssid, const char *pwd, int timeout_sec);

/* Append a successful crack to /databases/cracked.txt (creates the file
 * if needed). Format: timestamp + SSID + BSSID + PSK, one per line.
 */
void m1_wifi_attack_record_hit(const char *ssid, const char *bssid,
                               const char *psk);

/* Convenience for tests: PSK validity (8..63 chars, printable ASCII). */
bool m1_wifi_psk_valid(const char *s);

#endif /* M1_WIFI_ATTACK_H_ */
