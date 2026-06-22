/* See COPYING.txt for license details. */

/*
 * m1_ble_gatt.h
 *
 * High-level BLE GATT-client API for the phase-2 deep-probe feature.
 *
 * Hides the AT-command plumbing behind a synchronous, blocking API that the
 * probe UI uses to:
 *
 *   1. open       — connect to a peer by BSSID + addr type.
 *   2. read DIS   — pull manufacturer / model / firmware / hardware /
 *                   serial / system ID from Device Information Service (0x180A).
 *   3. list srv   — discover and return all primary services.
 *   4. list chars — for a chosen service, list characteristics and auto-read
 *                   any with the Read property.
 *   5. close      — disconnect.
 *
 * All functions are blocking and may take several seconds. Each returns 0
 * on success or a negative ESP-AT-style error code on failure.
 *
 * M1 Project
 */

#ifndef M1_BLE_GATT_H_
#define M1_BLE_GATT_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "ctrl_api.h"

#define M1_BLE_GATT_CONN_IDX        0   /* we only ever use one connection */
#define M1_BLE_GATT_CONNECT_TO_SEC  5
#define M1_BLE_GATT_OP_TO_SEC       6

/* Compact "what we learned from DIS" struct, screen-friendly. Members that
 * weren't readable on the peer are left as empty strings.
 */
typedef struct {
    char manufacturer[32];
    char model[32];
    char serial[32];
    char firmware_rev[32];
    char hardware_rev[32];
    char software_rev[32];
    char system_id[24];
} m1_ble_dis_t;

/* Connect to a BLE peripheral. addr_type: 0 = public, 1 = random,
 * 2 = RPA-public, 3 = RPA-random. Returns 0 on success, non-zero on error.
 */
int m1_ble_probe_open(const char *bssid, int addr_type);

/* Disconnect. Safe to call even if no connection was established. */
void m1_ble_probe_close(void);

/* Discover all primary services. Caller-owned list is allocated here;
 * release with m1_ble_probe_free_services(). Returns service count or -1.
 */
int m1_ble_probe_services(ble_gatt_srv_t **out_list);
void m1_ble_probe_free_services(ble_gatt_srv_t *list);

/* List characteristics for a given service. If `auto_read` is true, any
 * char with the Read property is read inline and its value populated.
 * Caller frees with m1_ble_probe_free_chars().
 */
int m1_ble_probe_chars(int srv_idx, bool auto_read,
                       ble_gatt_char_t **out_list);
void m1_ble_probe_free_chars(ble_gatt_char_t *list);

/* Convenience: walk the service list, find DIS (0x180A), read each of its
 * standard characteristics, and populate `out`. Returns 0 on success.
 * Sets fields not present on the peer to empty strings.
 */
int m1_ble_probe_dis(const ble_gatt_srv_t *services, int srv_count,
                     m1_ble_dis_t *out);

/* Render an ESP-AT hex string (lowercase, no separator) into ASCII when
 * printable, else returns false. ASCII output is null-terminated, length
 * capped by out_size. */
bool m1_ble_hex_to_ascii(const char *hex, char *out, size_t out_size);

#endif /* M1_BLE_GATT_H_ */
