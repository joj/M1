/* See COPYING.txt for license details. */

/*
 * m1_ble_fingerprint.h
 *
 * Best-effort identification of BLE devices from passively observed
 * advertising / scan-response payloads. No connection required.
 *
 * The ESP32 (running ESP-AT) hands us the adv and scan-response bytes as
 * hex strings via +BLESCAN. This module decodes them and tries, in order:
 *
 *   1. Recognised manufacturer-specific payloads
 *      - Apple Continuity (mfgr ID 0x004C)
 *      - Microsoft Beacon  (mfgr ID 0x0006)
 *      - Google Fast Pair  (mfgr ID 0x00E0)
 *      - Samsung           (mfgr ID 0x0075)
 *   2. Complete Local Name (AD type 0x09) or Shortened Local Name (0x08)
 *   3. Well-known 16-bit Service UUIDs (Tile 0xFEED, Eddystone 0xFEAA,
 *      HomeKit 0xFE13, Find My 0xFD43, Garmin 0xFE17, ...)
 *   4. Generic "BLE device" / "(random addr)" fallback.
 *
 * The returned string is intended for direct display on the 128x64 OLED
 * (truncate at caller if needed).
 *
 * M1 Project
 */

#ifndef M1_BLE_FINGERPRINT_H_
#define M1_BLE_FINGERPRINT_H_

#include <stdbool.h>
#include <stddef.h>

/* BLE address types as reported by ESP-AT (matches BLE core spec):
 *   0 = Public          -> OUI lookup meaningful
 *   1 = Random          -> OUI usually not meaningful
 *   2 = RPA / public id -> rotating
 *   3 = RPA / random id -> rotating
 */
#define M1_BLE_ADDR_TYPE_PUBLIC   0

/* Describe a BLE device. adv_hex and scan_rsp_hex may be NULL or empty
 * strings. addr_type is the BLE address type (0=public, else random/RPA).
 * Returns true if any fingerprint hit was produced (manuf data or local
 * name or known UUID); false means the fallback string was used. `out` is
 * always written (caller can ignore the return).
 */
bool m1_ble_describe(const char *adv_hex,
                     const char *scan_rsp_hex,
                     int addr_type,
                     char *out, size_t out_size);

#endif /* M1_BLE_FINGERPRINT_H_ */
