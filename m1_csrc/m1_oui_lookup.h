/* See COPYING.txt for license details. */

/*
 * m1_oui_lookup.h
 *
 * IEEE OUI -> manufacturer lookup, backed by a binary database file on the
 * SD card (default path: 0:/databases/oui.bin). Used by the wifi and BLE
 * scan views to annotate scan results with the registered vendor.
 *
 * Generate the database with scripts/build_oui_db.py and copy the resulting
 * oui.bin to the M1 SD card at /databases/oui.bin .
 *
 * Lookups go directly against the SD file via FatFs (binary search, ~log2 N
 * sector reads). A tiny in-RAM LRU cache absorbs repeat hits during a scan.
 *
 * M1 Project
 */

#ifndef M1_OUI_LOOKUP_H_
#define M1_OUI_LOOKUP_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M1_OUI_DB_PATH        "0:/databases/oui.bin"
#define M1_OUI_VENDOR_LEN     29  /* matches build_oui_db.py: RECLEN-3 */

/* Parse a 6-byte MAC/BSSID string ("AA:BB:CC:DD:EE:FF" or with '-' separators)
 * and write the manufacturer name into `out`. Returns true on hit, false on
 * miss / DB unavailable. On false, `out` is set to "Unknown" (or "(invalid)"
 * if the input couldn't be parsed) when out_size >= 8.
 */
bool m1_oui_lookup_str(const char *bssid_str, char *out, size_t out_size);

/* Same but with raw 3-byte OUI (high-order first). */
bool m1_oui_lookup_bytes(const uint8_t oui[3], char *out, size_t out_size);

/* Clear the LRU cache. Call after SD card insertion/removal to force the next
 * lookup to reopen the DB file.
 */
void m1_oui_cache_reset(void);

#endif /* M1_OUI_LOOKUP_H_ */
