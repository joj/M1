/* See COPYING.txt for license details. */

/*
 * m1_ble_uuid_names.h
 *
 * Lookup helpers that translate Bluetooth-assigned 16-bit UUIDs (services
 * and characteristics) into short, screen-friendly names. Used by:
 *
 *   - m1_ble_fingerprint.c (passive scan view, single-line summary)
 *   - m1_ble_gatt.c        (deep-probe drill-down, per-service / per-char)
 *
 * UUIDs may arrive in three forms from ESP-AT:
 *
 *   "0x180A"
 *   "180A"
 *   "0000180a-0000-1000-8000-00805f9b34fb"   (canonical 128-bit form)
 *
 * m1_ble_uuid16_from_str() handles all three and yields the 16-bit value
 * when one of the Bluetooth SIG-assigned base UUIDs is matched, else 0.
 *
 * M1 Project
 */

#ifndef M1_BLE_UUID_NAMES_H_
#define M1_BLE_UUID_NAMES_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Parse a UUID string and return its 16-bit short form if possible.
 * Returns 0 (with *is_short=false) when the UUID is a non-SIG 128-bit value.
 */
uint16_t m1_ble_uuid16_from_str(const char *uuid_str, bool *is_short);

/* Short friendly name for a 16-bit Service UUID. NULL if unknown. */
const char *m1_ble_service_name(uint16_t uuid16);

/* Short friendly name for a 16-bit Characteristic UUID. NULL if unknown. */
const char *m1_ble_characteristic_name(uint16_t uuid16);

/* Render the supplied UUID string into a short label suitable for the
 * 128x64 display: friendly name if known, otherwise the short hex form
 * (or truncated 128-bit). Always null-terminates and never overflows.
 */
void m1_ble_uuid_describe(const char *uuid_str, bool is_characteristic,
                          char *out, size_t out_size);

#endif /* M1_BLE_UUID_NAMES_H_ */
