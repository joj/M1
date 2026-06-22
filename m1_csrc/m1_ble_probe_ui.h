/* See COPYING.txt for license details. */

/*
 * m1_ble_probe_ui.h
 *
 * GATT deep-probe UI invoked from the BLE scan view (m1_bt.c) when the
 * user long-presses OK on a scanned device. Owns the entire interaction
 * loop until the user backs out, at which point control returns to the
 * scan view.
 *
 * M1 Project
 */

#ifndef M1_BLE_PROBE_UI_H_
#define M1_BLE_PROBE_UI_H_

void m1_ble_probe_ui_run(const char *bssid, int addr_type);

#endif /* M1_BLE_PROBE_UI_H_ */
