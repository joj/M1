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

/* Phase 3c — passive WiFi capture (long-running event stream).
 *
 * wifi_monitor_start: AT+WIFISCAN=<channel>. 0 = channel-hop.
 * wifi_monitor_stop:  AT+WIFISTOP.
 * wifi_pmkid_probe:   AT+WIFIPMKID="<bssid>",<channel> -- pin channel
 *                     so a single target's M1/PMKID will land here.
 *
 * After start, the caller drives wifi_capture_pump(callback, user_ctx,
 * max_wait_ms) repeatedly to pull async events. The pump dispatches one
 * event per call (or none if the buffer is empty) and returns. UI code
 * polls the keypad between pumps.
 */
typedef enum {
    M1_WIFI_EVT_NONE = 0,
    M1_WIFI_EVT_BEACON,
    M1_WIFI_EVT_EAPOL,
    M1_WIFI_EVT_PMKID,
    M1_WIFI_EVT_DEAUTH,
} m1_wifi_evt_kind_t;

typedef struct {
    m1_wifi_evt_kind_t kind;
    uint8_t  bssid[6];
    uint8_t  sta[6];
    uint8_t  channel;
    uint8_t  msg_num;
    uint8_t  reason;
    uint16_t payload_len;
    uint8_t  payload[412];        /* EAPOL frame, or beacon SSID, or PMKID */
    char     ssid[33];            /* beacon only */
} m1_wifi_evt_t;

uint8_t wifi_monitor_start(int channel);
uint8_t wifi_monitor_stop(void);
uint8_t wifi_pmkid_probe(const char *bssid, int channel);

/* Pull one event from the ESP-AT async stream, or NONE if nothing
 * arrived within max_wait_ms. Safe to call repeatedly. */
uint8_t wifi_capture_pump(m1_wifi_evt_t *out_evt, int max_wait_ms);

#endif /* ESP_APP_MAIN_H_ */
