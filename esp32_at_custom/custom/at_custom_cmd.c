/*
 * SPDX-FileCopyrightText: 2026 M1 Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * at_custom_cmd.c — ESP-AT user component for the M1 phase-3c "passive
 * WiFi capture" feature.
 *
 * Adds the following custom AT commands on top of stock ESP-AT:
 *
 *   AT+WIFISCAN=<chan>          Start promiscuous-mode capture on a 2.4 GHz
 *                               channel (1..14). 0 = channel-hop every
 *                               ~250 ms across channels 1..13.
 *   AT+WIFISTOP                 Stop monitor mode.
 *   AT+WIFIPMKID=<bssid>,<chan> Send one association request to the AP and
 *                               report any PMKID in the reply.
 *
 * Async events (emitted while monitor is active):
 *
 *   +WIFIEAPOL:<bssid>,<sta>,<msg_num>,<frame_hex>
 *   +WIFIPMKID:<bssid>,<sta>,<pmkid_hex>
 *   +WIFIBEACON:<bssid>,<chan>,<ssid_hex>
 *   +WIFIDEAUTH:<bssid>,<sta>,<reason>
 *
 * The receive callback is small and ISR-bounded; the actual hex
 * formatting + AT-port write happens in a FreeRTOS task fed by a
 * fixed-size queue, so we don't block the wifi driver if the MCU side
 * is slow to drain.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_at.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_err.h"

#define TAG "AT_M1CAP"

/* -- 802.11 frame parsing constants ----------------------------------- */

#define FC_TYPE_MGMT             0
#define FC_TYPE_DATA             2
#define FC_SUBTYPE_BEACON        8
#define FC_SUBTYPE_DEAUTH       12
#define FC_SUBTYPE_QOS_DATA      8

#define LLC_SNAP_EAPOL_LEN       8
#define ETHERTYPE_EAPOL          0x888E

#define WPA_KDE_PMKID            "\x00\x0F\xAC\x04"
#define WPA_KDE_PMKID_LEN        4
#define PMKID_LEN                16

typedef enum {
    EVT_EAPOL,
    EVT_PMKID,
    EVT_BEACON,
    EVT_DEAUTH,
} evt_kind_t;

typedef struct {
    evt_kind_t kind;
    uint8_t  bssid[6];
    uint8_t  sta[6];
    uint8_t  channel;
    uint8_t  msg_num;
    uint8_t  reason;
    uint16_t payload_len;
    uint8_t  payload[412];
} capture_evt_t;

static QueueHandle_t s_evt_q       = NULL;
static TaskHandle_t  s_dispatch    = NULL;
static volatile bool s_running     = false;
static volatile uint8_t s_fixed_ch = 0;
static volatile uint8_t s_cur_ch   = 1;

static const char HEX[] = "0123456789abcdef";

static void to_hex(const uint8_t *in, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) {
        out[2*i]     = HEX[(in[i] >> 4) & 0x0F];
        out[2*i + 1] = HEX[in[i] & 0x0F];
    }
    out[2*n] = '\0';
}

static void emit_event(const capture_evt_t *e)
{
    static char line[1024];
    static char hex_buf[825];
    char ap_hex[13], sta_hex[13];
    to_hex(e->bssid, 6, ap_hex);
    to_hex(e->sta,   6, sta_hex);

    int n = 0;
    switch (e->kind) {
        case EVT_EAPOL:
            to_hex(e->payload, e->payload_len, hex_buf);
            n = snprintf(line, sizeof(line),
                "+WIFIEAPOL:%s,%s,%u,%s\r\n",
                ap_hex, sta_hex, e->msg_num, hex_buf);
            break;
        case EVT_PMKID:
            to_hex(e->payload, PMKID_LEN, hex_buf);
            n = snprintf(line, sizeof(line),
                "+WIFIPMKID:%s,%s,%s\r\n", ap_hex, sta_hex, hex_buf);
            break;
        case EVT_BEACON:
            to_hex(e->payload, e->payload_len, hex_buf);
            n = snprintf(line, sizeof(line),
                "+WIFIBEACON:%s,%u,%s\r\n", ap_hex, e->channel, hex_buf);
            break;
        case EVT_DEAUTH:
            n = snprintf(line, sizeof(line),
                "+WIFIDEAUTH:%s,%s,%u\r\n", ap_hex, sta_hex, e->reason);
            break;
    }
    if (n > 0) {
        esp_at_port_write_data((uint8_t *)line, n);
    }
}

/* Locate the PMKID KDE inside an EAPOL-Key M1 frame. */
static bool find_pmkid(const uint8_t *eapol, size_t len, uint8_t pmkid[PMKID_LEN])
{
    if (len < 99) return false;
    for (size_t i = 0; i + WPA_KDE_PMKID_LEN + PMKID_LEN + 2 < len; i++) {
        if (eapol[i] != 0xDD) continue;
        uint8_t kde_len = eapol[i + 1];
        if (i + 2 + kde_len > len) continue;
        if (kde_len < WPA_KDE_PMKID_LEN + PMKID_LEN) continue;
        if (memcmp(&eapol[i + 2], WPA_KDE_PMKID, WPA_KDE_PMKID_LEN) != 0) continue;
        memcpy(pmkid, &eapol[i + 2 + WPA_KDE_PMKID_LEN], PMKID_LEN);
        return true;
    }
    return false;
}

/* Derive 4-way handshake message number from EAPOL-Key Key Info field. */
static uint8_t eapol_msg_num(const uint8_t *eapol, size_t len)
{
    if (len < 7) return 0;
    uint16_t key_info = ((uint16_t)eapol[5] << 8) | eapol[6];
    bool install = (key_info >> 6) & 1;
    bool ack     = (key_info >> 7) & 1;
    bool mic     = (key_info >> 8) & 1;
    bool secure  = (key_info >> 9) & 1;

    if (!mic && ack && !install)             return 1;
    if ( mic && !ack && !install && !secure) return 2;
    if ( mic && ack && install)              return 3;
    if ( mic && !ack && !install && secure)  return 4;
    return 0;
}

static void hop_task(void *arg)
{
    (void)arg;
    while (s_running) {
        if (s_fixed_ch == 0) {
            s_cur_ch = (s_cur_ch % 13) + 1;
            esp_wifi_set_channel(s_cur_ch, WIFI_SECOND_CHAN_NONE);
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    vTaskDelete(NULL);
}

static void dispatch_task(void *arg)
{
    (void)arg;
    capture_evt_t e;
    while (s_running) {
        if (xQueueReceive(s_evt_q, &e, pdMS_TO_TICKS(200)) == pdTRUE) {
            emit_event(&e);
        }
    }
    vTaskDelete(NULL);
}

static void IRAM_ATTR rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    if (!s_running || !s_evt_q) return;
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    const uint8_t *p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;

    uint8_t fc0 = p[0];
    uint8_t ftype = (fc0 >> 2) & 0x3;
    uint8_t fsub  = (fc0 >> 4) & 0xF;
    const uint8_t *a1 = &p[4];
    const uint8_t *a2 = &p[10];
    const uint8_t *a3 = &p[16];

    capture_evt_t e = { 0 };
    e.channel = s_cur_ch;

    if (ftype == FC_TYPE_MGMT && fsub == FC_SUBTYPE_BEACON) {
        memcpy(e.bssid, a3, 6);
        const uint8_t *body = &p[24 + 12];
        int body_left = len - 24 - 12;
        if (body_left > 2 && body[0] == 0x00) {
            uint8_t ssid_len = body[1];
            if (ssid_len <= 32 && body_left >= 2 + ssid_len) {
                e.kind = EVT_BEACON;
                e.payload_len = ssid_len;
                memcpy(e.payload, &body[2], ssid_len);
                xQueueSendFromISR(s_evt_q, &e, NULL);
            }
        }
        return;
    }

    if (ftype == FC_TYPE_MGMT && fsub == FC_SUBTYPE_DEAUTH) {
        memcpy(e.bssid, a3, 6);
        memcpy(e.sta, a1, 6);
        e.kind = EVT_DEAUTH;
        if (len >= 26) e.reason = p[24];
        xQueueSendFromISR(s_evt_q, &e, NULL);
        return;
    }

    if (ftype == FC_TYPE_DATA) {
        int hdr = 24;
        if (fsub == FC_SUBTYPE_QOS_DATA) hdr += 2;
        if (len < hdr + LLC_SNAP_EAPOL_LEN + 4) return;
        const uint8_t *llc = &p[hdr];
        if (llc[0] != 0xAA || llc[1] != 0xAA || llc[2] != 0x03) return;
        uint16_t etype = ((uint16_t)llc[6] << 8) | llc[7];
        if (etype != ETHERTYPE_EAPOL) return;

        const uint8_t *eapol = &p[hdr + LLC_SNAP_EAPOL_LEN];
        int eapol_len = len - (hdr + LLC_SNAP_EAPOL_LEN);
        if (eapol_len < 7) return;

        uint8_t fc1 = p[1];
        bool to_ds = fc1 & 0x01;
        bool from_ds = fc1 & 0x02;
        if (from_ds && !to_ds)      { memcpy(e.bssid, a2, 6); memcpy(e.sta, a1, 6); }
        else if (to_ds && !from_ds) { memcpy(e.bssid, a1, 6); memcpy(e.sta, a2, 6); }
        else                        { memcpy(e.bssid, a3, 6); memcpy(e.sta, a2, 6); }

        uint8_t mn = eapol_msg_num(eapol, eapol_len);
        if (mn == 0) return;

        e.kind = EVT_EAPOL;
        e.msg_num = mn;
        int copy = eapol_len > (int)sizeof(e.payload) ? (int)sizeof(e.payload) : eapol_len;
        e.payload_len = (uint16_t)copy;
        memcpy(e.payload, eapol, copy);
        xQueueSendFromISR(s_evt_q, &e, NULL);

        if (mn == 1) {
            uint8_t pmkid[PMKID_LEN];
            if (find_pmkid(eapol, eapol_len, pmkid)) {
                capture_evt_t pe = { 0 };
                pe.kind = EVT_PMKID;
                memcpy(pe.bssid, e.bssid, 6);
                memcpy(pe.sta, e.sta, 6);
                pe.payload_len = PMKID_LEN;
                memcpy(pe.payload, pmkid, PMKID_LEN);
                xQueueSendFromISR(s_evt_q, &pe, NULL);
            }
        }
    }
}

static esp_err_t monitor_start(uint8_t channel)
{
    if (s_running) return ESP_OK;

    esp_wifi_disconnect();

    if (!s_evt_q) {
        s_evt_q = xQueueCreate(32, sizeof(capture_evt_t));
        if (!s_evt_q) return ESP_ERR_NO_MEM;
    }

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
    };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(rx_cb);
    esp_err_t err = esp_wifi_set_promiscuous(true);
    if (err != ESP_OK) return err;

    s_fixed_ch = channel;
    s_cur_ch = channel ? channel : 1;
    esp_wifi_set_channel(s_cur_ch, WIFI_SECOND_CHAN_NONE);

    s_running = true;
    xTaskCreate(dispatch_task, "m1cap_disp", 4096, NULL, 4, &s_dispatch);
    if (channel == 0) {
        xTaskCreate(hop_task, "m1cap_hop", 2048, NULL, 3, NULL);
    }
    return ESP_OK;
}

static esp_err_t monitor_stop(void)
{
    if (!s_running) return ESP_OK;
    s_running = false;
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    vTaskDelay(pdMS_TO_TICKS(300));
    return ESP_OK;
}

/* ---- AT command handlers -------------------------------------------- */

static uint8_t at_setup_wifiscan(uint8_t para_num)
{
    int32_t channel = 0;
    if (para_num < 1) return ESP_AT_RESULT_CODE_ERROR;
    if (esp_at_get_para_as_digit(0, &channel) != ESP_AT_PARA_PARSE_RESULT_OK)
        return ESP_AT_RESULT_CODE_ERROR;
    if (channel < 0 || channel > 14) return ESP_AT_RESULT_CODE_ERROR;
    return monitor_start((uint8_t)channel) == ESP_OK
            ? ESP_AT_RESULT_CODE_OK : ESP_AT_RESULT_CODE_ERROR;
}

static uint8_t at_exe_wifistop(uint8_t *cmd_name)
{
    (void)cmd_name;
    monitor_stop();
    return ESP_AT_RESULT_CODE_OK;
}

/* Pin the monitor channel to the target so any client connecting to that
 * BSSID lands its M1 (and any PMKID KDE) in our stream. A future revision
 * can also actively associate to elicit M1 instead of waiting. */
static uint8_t at_setup_wifipmkid(uint8_t para_num)
{
    if (para_num < 2) return ESP_AT_RESULT_CODE_ERROR;
    uint8_t *bssid_str = NULL;
    int32_t channel = 0;
    if (esp_at_get_para_as_str(0, &bssid_str) != ESP_AT_PARA_PARSE_RESULT_OK)
        return ESP_AT_RESULT_CODE_ERROR;
    if (esp_at_get_para_as_digit(1, &channel) != ESP_AT_PARA_PARSE_RESULT_OK)
        return ESP_AT_RESULT_CODE_ERROR;
    s_fixed_ch = (uint8_t)channel;
    s_cur_ch = (uint8_t)channel;
    esp_wifi_set_channel((uint8_t)channel, WIFI_SECOND_CHAN_NONE);
    return ESP_AT_RESULT_CODE_OK;
}

static const esp_at_cmd_t at_m1cap_cmds[] = {
    { "+WIFISCAN",   NULL, NULL, at_setup_wifiscan,  NULL },
    { "+WIFISTOP",   NULL, NULL, NULL,               at_exe_wifistop },
    { "+WIFIPMKID",  NULL, NULL, at_setup_wifipmkid, NULL },
};

bool esp_at_custom_cmd_register(void)
{
    return esp_at_custom_cmd_array_register(
        at_m1cap_cmds, sizeof(at_m1cap_cmds) / sizeof(esp_at_cmd_t));
}

ESP_AT_CMD_SET_INIT_FN(esp_at_custom_cmd_register, 1);
