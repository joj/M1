/* See COPYING.txt for license details. */

/*
*
* esp_app_main.h
*
* Header for esp app
*
* M1 Project
*
*/

#ifndef ESP_APP_MAIN_H_
#define ESP_APP_MAIN_H_

#include <stdbool.h>
#include "ctrl_api.h"

bool get_esp32_main_init_status(void);
void esp32_main_init(void);
uint8_t wifi_ap_scan_list(ctrl_cmd_t *app_req);
uint8_t ble_scan_list(ctrl_cmd_t *app_req);
uint8_t ble_advertise(ctrl_cmd_t *app_req);
uint8_t esp_dev_reset(ctrl_cmd_t *app_req);

/* Phase 2 — BLE GATT client wrappers.
 *
 * Each function builds an AT command and waits synchronously for the
 * response. The typed result is written into app_req->u (and any allocated
 * lists must be freed by the caller). cmd_timeout_sec on the request bounds
 * the wait.
 */
uint8_t ble_gatt_connect(ctrl_cmd_t *app_req,
                         int conn_idx,
                         const char *bssid,
                         int addr_type,
                         int timeout_sec);
uint8_t ble_gatt_disconnect(ctrl_cmd_t *app_req, int conn_idx);
uint8_t ble_gatt_primsrv(ctrl_cmd_t *app_req, int conn_idx);
uint8_t ble_gatt_chars(ctrl_cmd_t *app_req, int conn_idx, int srv_idx);
uint8_t ble_gatt_read(ctrl_cmd_t *app_req, int conn_idx, int srv_idx, int char_idx);

/* Phase 3 — WiFi dictionary-attack wrappers.
 *
 * wifi_set_station_mode: AT+CWMODE=1.
 * wifi_try_connect: AT+CWJAP="<ssid>","<pwd>" with a bounded timeout.
 *   Populates app_req->u.wifi_try with try_status + at_err_code.
 * wifi_disconnect_ap: AT+CWQAP.
 */
uint8_t wifi_set_station_mode(ctrl_cmd_t *app_req);
uint8_t wifi_try_connect(ctrl_cmd_t *app_req,
                         const char *ssid, const char *pwd,
                         int timeout_sec);
uint8_t wifi_disconnect_ap(ctrl_cmd_t *app_req);

#endif /* ESP_APP_MAIN_H_ */
