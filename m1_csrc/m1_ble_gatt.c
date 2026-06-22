/* See COPYING.txt for license details. */

/*
 * m1_ble_gatt.c — high-level BLE GATT-client API.
 *
 * Thin wrapper over the ESP-AT GATT primitives (ble_gatt_connect / disconnect
 * / primsrv / chars / read) declared in esp_app_main.h. Hides the
 * ctrl_cmd_t / app_req lifecycle so the probe UI stays readable.
 *
 * M1 Project
 */

#include "m1_ble_gatt.h"
#include "m1_ble_uuid_names.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m1_log_debug.h"
#include "esp_app_main.h"

#define M1_LOGDB_TAG  "BLEGAT"

/* Standard DIS characteristic 16-bit UUIDs. */
#define DIS_MANUFACTURER     0x2A29
#define DIS_MODEL_NUMBER     0x2A24
#define DIS_SERIAL_NUMBER    0x2A25
#define DIS_FIRMWARE_REV     0x2A26
#define DIS_HARDWARE_REV     0x2A27
#define DIS_SOFTWARE_REV     0x2A28
#define DIS_SYSTEM_ID        0x2A23
#define SERVICE_DIS          0x180A

bool m1_ble_hex_to_ascii(const char *hex, char *out, size_t out_size)
{
    if (!hex || !out || out_size == 0) return false;
    size_t len = strlen(hex);
    if (len == 0 || (len & 1)) { out[0] = '\0'; return false; }
    size_t bytes = len / 2;
    if (bytes >= out_size) bytes = out_size - 1;
    bool printable = true;
    for (size_t i = 0; i < bytes; i++)
    {
        int hi = 0, lo = 0;
        char ch = hex[2*i];
        char cl = hex[2*i+1];
        if      (ch >= '0' && ch <= '9') hi = ch - '0';
        else if (ch >= 'a' && ch <= 'f') hi = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F') hi = ch - 'A' + 10;
        else { printable = false; break; }
        if      (cl >= '0' && cl <= '9') lo = cl - '0';
        else if (cl >= 'a' && cl <= 'f') lo = cl - 'a' + 10;
        else if (cl >= 'A' && cl <= 'F') lo = cl - 'A' + 10;
        else { printable = false; break; }
        unsigned char b = (unsigned char)((hi << 4) | lo);
        if (b < 0x20 || b > 0x7E) { printable = false; break; }
        out[i] = (char)b;
    }
    out[bytes] = '\0';
    return printable;
}

int m1_ble_probe_open(const char *bssid, int addr_type)
{
    ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
    app_req.cmd_timeout_sec = M1_BLE_GATT_OP_TO_SEC + M1_BLE_GATT_CONNECT_TO_SEC;
    uint8_t r = ble_gatt_connect(&app_req, M1_BLE_GATT_CONN_IDX,
                                 bssid, addr_type,
                                 M1_BLE_GATT_CONNECT_TO_SEC);
    if (r != SUCCESS)
    {
        M1_LOG_I(M1_LOGDB_TAG, "connect transport fail\n\r");
        return -1;
    }
    int status = app_req.u.ble_conn.connect_status;
    if (status != 0)
        M1_LOG_I(M1_LOGDB_TAG, "connect rejected (status=%d)\n\r", status);
    return status;
}

void m1_ble_probe_close(void)
{
    ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
    app_req.cmd_timeout_sec = M1_BLE_GATT_OP_TO_SEC;
    (void)ble_gatt_disconnect(&app_req, M1_BLE_GATT_CONN_IDX);
}

int m1_ble_probe_services(ble_gatt_srv_t **out_list)
{
    if (out_list) *out_list = NULL;
    ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
    app_req.cmd_timeout_sec = M1_BLE_GATT_OP_TO_SEC;
    uint8_t r = ble_gatt_primsrv(&app_req, M1_BLE_GATT_CONN_IDX);
    if (r != SUCCESS) return -1;
    int n = app_req.u.ble_srv_list.count;
    if (out_list) *out_list = app_req.u.ble_srv_list.out_list;
    else if (app_req.u.ble_srv_list.out_list)
        free(app_req.u.ble_srv_list.out_list);
    return n;
}

void m1_ble_probe_free_services(ble_gatt_srv_t *list)
{
    if (list) free(list);
}

int m1_ble_probe_chars(int srv_idx, bool auto_read,
                       ble_gatt_char_t **out_list)
{
    if (out_list) *out_list = NULL;
    ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
    app_req.cmd_timeout_sec = M1_BLE_GATT_OP_TO_SEC;
    uint8_t r = ble_gatt_chars(&app_req, M1_BLE_GATT_CONN_IDX, srv_idx);
    if (r != SUCCESS) return -1;
    int n = app_req.u.ble_char_list.count;
    ble_gatt_char_t *chars = app_req.u.ble_char_list.out_list;

    if (auto_read && chars)
    {
        for (int i = 0; i < n; i++)
        {
            if (!(chars[i].props & BLE_GATT_PROP_READ))
                continue;
            ctrl_cmd_t rd = CTRL_CMD_DEFAULT_REQ();
            rd.cmd_timeout_sec = M1_BLE_GATT_OP_TO_SEC;
            if (ble_gatt_read(&rd, M1_BLE_GATT_CONN_IDX,
                              srv_idx, chars[i].char_idx) == SUCCESS)
            {
                chars[i].value_len = rd.u.ble_read.value_len;
                snprintf(chars[i].value_hex, sizeof(chars[i].value_hex),
                         "%s", rd.u.ble_read.value_hex);
                m1_ble_hex_to_ascii(chars[i].value_hex,
                                    chars[i].value_str,
                                    sizeof(chars[i].value_str));
            }
        }
    }

    if (out_list) *out_list = chars;
    else if (chars) free(chars);
    return n;
}

void m1_ble_probe_free_chars(ble_gatt_char_t *list)
{
    if (list) free(list);
}

static void copy_value_to(char *dst, size_t dst_size, const ble_gatt_char_t *c)
{
    if (!dst || !dst_size) return;
    /* Prefer ASCII rendering when printable, else hex (truncated). */
    if (c->value_str[0] != '\0')
        snprintf(dst, dst_size, "%s", c->value_str);
    else
        snprintf(dst, dst_size, "%s", c->value_hex);
}

int m1_ble_probe_dis(const ble_gatt_srv_t *services, int srv_count,
                     m1_ble_dis_t *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    /* Locate DIS in the service list. */
    int dis_srv_idx = -1;
    for (int i = 0; i < srv_count; i++)
    {
        bool is_short = false;
        uint16_t u = m1_ble_uuid16_from_str(services[i].uuid, &is_short);
        if (is_short && u == SERVICE_DIS) { dis_srv_idx = services[i].srv_idx; break; }
    }
    if (dis_srv_idx < 0)
    {
        M1_LOG_I(M1_LOGDB_TAG, "no DIS on peer\n\r");
        return -2;
    }
    ble_gatt_char_t *chars = NULL;
    int n = m1_ble_probe_chars(dis_srv_idx, /*auto_read=*/true, &chars);
    if (n < 0 || !chars) return -3;

    for (int i = 0; i < n; i++)
    {
        bool is_short = false;
        uint16_t u = m1_ble_uuid16_from_str(chars[i].uuid, &is_short);
        if (!is_short) continue;
        switch (u)
        {
            case DIS_MANUFACTURER:  copy_value_to(out->manufacturer,  sizeof(out->manufacturer),  &chars[i]); break;
            case DIS_MODEL_NUMBER:  copy_value_to(out->model,         sizeof(out->model),         &chars[i]); break;
            case DIS_SERIAL_NUMBER: copy_value_to(out->serial,        sizeof(out->serial),        &chars[i]); break;
            case DIS_FIRMWARE_REV:  copy_value_to(out->firmware_rev,  sizeof(out->firmware_rev),  &chars[i]); break;
            case DIS_HARDWARE_REV:  copy_value_to(out->hardware_rev,  sizeof(out->hardware_rev),  &chars[i]); break;
            case DIS_SOFTWARE_REV:  copy_value_to(out->software_rev,  sizeof(out->software_rev),  &chars[i]); break;
            case DIS_SYSTEM_ID:     copy_value_to(out->system_id,     sizeof(out->system_id),     &chars[i]); break;
            default: break;
        }
    }
    m1_ble_probe_free_chars(chars);
    return 0;
}
