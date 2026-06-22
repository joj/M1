/* See COPYING.txt for license details. */

/*
 * m1_ble_probe_ui.c — three-screen GATT deep-probe UI.
 *
 * Screens:
 *   1. DIS view          — manufacturer / model / firmware / hardware / serial
 *                          OK or DOWN -> service list
 *   2. Service list      — UP/DOWN to navigate, OK to drill into a service,
 *                          BACK to return to DIS view
 *   3. Characteristic    — UP/DOWN to navigate, value auto-read inline,
 *                          BACK to return to service list
 *
 * Final BACK from screen 1 disconnects and returns to the caller (scan view).
 *
 * All BLE/GATT work happens via m1_ble_gatt.{c,h}. UI is u8g2; events come
 * from main_q_hdl + button_events_q_hdl, same as elsewhere in the codebase.
 *
 * M1 Project
 */

#include "m1_ble_probe_ui.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "m1_display.h"
#include "m1_system.h"
#include "m1_log_debug.h"
#include "m1_ble_gatt.h"
#include "m1_ble_uuid_names.h"
#include "m1_oui_lookup.h"

#define M1_LOGDB_TAG          "BLEPRB"
#define ROW_H                 (M1_GUI_FONT_HEIGHT)
#define MAX_CHARS_PER_ROW     ((M1_LCD_DISPLAY_WIDTH / M1_GUI_FONT_WIDTH) - 1)

static void draw_header(const char *title)
{
    u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
    char trunc[24];
    snprintf(trunc, sizeof(trunc), "%s", title);
    if ((int)strlen(trunc) > MAX_CHARS_PER_ROW) trunc[MAX_CHARS_PER_ROW] = '\0';
    u8g2_DrawStr(&m1_u8g2, 2, ROW_H, trunc);
}

static void draw_centered_msg(const char *line1, const char *line2)
{
    m1_u8g2_firstpage();
    u8g2_DrawStr(&m1_u8g2, 2, 25, line1);
    if (line2) u8g2_DrawStr(&m1_u8g2, 2, 25 + ROW_H, line2);
    m1_u8g2_nextpage();
}

static bool wait_event(S_M1_Buttons_Status *out_btn, uint32_t timeout_ms)
{
    S_M1_Main_Q_t q_item;
    TickType_t to = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xQueueReceive(main_q_hdl, &q_item, to) != pdTRUE) return false;
    if (q_item.q_evt_type != Q_EVENT_KEYPAD) return false;
    return xQueueReceive(button_events_q_hdl, out_btn, 0) == pdTRUE;
}

static void draw_dis_screen(const m1_ble_dis_t *dis, const char *bssid_short)
{
    char buf[32];
    m1_u8g2_firstpage();
    draw_header("Device Info");
    snprintf(buf, sizeof(buf), "%s", bssid_short);
    u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 12 * M1_GUI_FONT_WIDTH, ROW_H, buf);
    int y = 14 + ROW_H;
    const char *rows[5][2] = {
        { "Mfr",  dis->manufacturer },
        { "Mdl",  dis->model },
        { "FW",   dis->firmware_rev },
        { "HW",   dis->hardware_rev },
        { "SN",   dis->serial },
    };
    for (int i = 0; i < 5; i++)
    {
        const char *val = (rows[i][1][0] != '\0') ? rows[i][1] : "-";
        snprintf(buf, sizeof(buf), "%s:%s", rows[i][0], val);
        if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
        u8g2_DrawStr(&m1_u8g2, 2, y, buf);
        y += ROW_H;
    }
    m1_u8g2_nextpage();
}

static void draw_service_list(const ble_gatt_srv_t *list, int count, int sel)
{
    char buf[40];
    m1_u8g2_firstpage();
    draw_header("Services");
    snprintf(buf, sizeof(buf), "%d/%d", count ? sel + 1 : 0, count);
    u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6 * M1_GUI_FONT_WIDTH, ROW_H, buf);
    int y = 14 + ROW_H;
    /* Show 5 entries centered around sel. */
    int rows = 5;
    int start = sel - rows / 2;
    if (start < 0) start = 0;
    if (start + rows > count) start = count - rows;
    if (start < 0) start = 0;
    for (int i = 0; i < rows && start + i < count; i++)
    {
        const ble_gatt_srv_t *s = &list[start + i];
        char name[24];
        m1_ble_uuid_describe(s->uuid, /*is_characteristic=*/false,
                             name, sizeof(name));
        snprintf(buf, sizeof(buf), "%s%s", (start + i == sel) ? ">" : " ", name);
        if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
        u8g2_DrawStr(&m1_u8g2, 2, y, buf);
        y += ROW_H;
    }
    m1_u8g2_nextpage();
}

static void draw_char_list(const ble_gatt_char_t *list, int count, int sel,
                           const char *srv_label)
{
    char buf[48];
    m1_u8g2_firstpage();
    draw_header(srv_label);
    snprintf(buf, sizeof(buf), "%d/%d", count ? sel + 1 : 0, count);
    u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6 * M1_GUI_FONT_WIDTH, ROW_H, buf);
    int y = 14 + ROW_H;

    if (count == 0)
    {
        u8g2_DrawStr(&m1_u8g2, 2, y, "(no chars)");
        m1_u8g2_nextpage();
        return;
    }

    const ble_gatt_char_t *c = &list[sel];
    char name[24];
    m1_ble_uuid_describe(c->uuid, /*is_characteristic=*/true,
                         name, sizeof(name));
    snprintf(buf, sizeof(buf), "%s", name);
    if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
    u8g2_DrawStr(&m1_u8g2, 2, y, buf);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s",
        (c->props & BLE_GATT_PROP_READ)     ? "R"  : "",
        (c->props & BLE_GATT_PROP_WRITE)    ? "W"  : "",
        (c->props & BLE_GATT_PROP_WRITE_NR) ? "w"  : "",
        (c->props & BLE_GATT_PROP_NOTIFY)   ? "N"  : "",
        (c->props & BLE_GATT_PROP_INDICATE) ? "I"  : "",
        (c->props & BLE_GATT_PROP_BROADCAST)? "B"  : "",
        (c->props & BLE_GATT_PROP_AUTH_SW)  ? "A"  : "",
        (c->props & BLE_GATT_PROP_EXT_PROPS)? "X"  : "");
    char props_line[32];
    snprintf(props_line, sizeof(props_line), "props:%s", buf[0] ? buf : "-");
    u8g2_DrawStr(&m1_u8g2, 2, y, props_line);
    y += ROW_H;

    if (c->value_str[0] != '\0')
        snprintf(buf, sizeof(buf), "v:%s", c->value_str);
    else if (c->value_hex[0] != '\0')
        snprintf(buf, sizeof(buf), "h:%s", c->value_hex);
    else if (c->props & BLE_GATT_PROP_READ)
        snprintf(buf, sizeof(buf), "v:(empty)");
    else
        snprintf(buf, sizeof(buf), "v:(not readable)");
    if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
    u8g2_DrawStr(&m1_u8g2, 2, y, buf);

    /* If the hex is longer than one row, show a second hex row. */
    if (c->value_str[0] == '\0' && (int)strlen(c->value_hex) > MAX_CHARS_PER_ROW - 2)
    {
        y += ROW_H;
        snprintf(buf, sizeof(buf), "  %s", c->value_hex + MAX_CHARS_PER_ROW - 2);
        if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
        u8g2_DrawStr(&m1_u8g2, 2, y, buf);
    }

    m1_u8g2_nextpage();
}

/* Inner loop: characteristic-detail screen for one service. */
static void run_char_screen(const ble_gatt_srv_t *svc)
{
    char srv_name[24];
    m1_ble_uuid_describe(svc->uuid, /*is_characteristic=*/false,
                         srv_name, sizeof(srv_name));

    draw_centered_msg("Reading chars...", srv_name);
    ble_gatt_char_t *chars = NULL;
    int n = m1_ble_probe_chars(svc->srv_idx, /*auto_read=*/true, &chars);
    if (n < 0)
    {
        draw_centered_msg("Char list failed", "Press BACK");
    }
    int sel = 0;
    draw_char_list(chars, n < 0 ? 0 : n, sel, srv_name);

    while (true)
    {
        S_M1_Buttons_Status b;
        if (!wait_event(&b, 0)) continue;
        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        if (n <= 0) continue;
        if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel + n - 1) % n;
            draw_char_list(chars, n, sel, srv_name);
        }
        else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel + 1) % n;
            draw_char_list(chars, n, sel, srv_name);
        }
    }
    m1_ble_probe_free_chars(chars);
}

void m1_ble_probe_ui_run(const char *bssid, int addr_type)
{
    if (!bssid) return;

    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    draw_centered_msg("Connecting...", bssid);

    int rc = m1_ble_probe_open(bssid, addr_type);
    if (rc != 0)
    {
        char line[24];
        snprintf(line, sizeof(line), "Connect rc=%d", rc);
        draw_centered_msg(line, "Press BACK");
        S_M1_Buttons_Status b;
        while (wait_event(&b, 0))
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        m1_ble_probe_close();
        return;
    }

    draw_centered_msg("Discovering...", bssid);
    ble_gatt_srv_t *services = NULL;
    int srv_count = m1_ble_probe_services(&services);
    if (srv_count < 0)
    {
        draw_centered_msg("Discovery failed", "Press BACK");
        S_M1_Buttons_Status b;
        while (wait_event(&b, 0))
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        m1_ble_probe_close();
        return;
    }

    draw_centered_msg("Reading DIS...", NULL);
    m1_ble_dis_t dis;
    (void)m1_ble_probe_dis(services, srv_count, &dis);

    /* Vendor in screen-1 corner. */
    char bssid_short[14];
    snprintf(bssid_short, sizeof(bssid_short), "%.12s", bssid);

    int srv_sel = 0;
    enum { SCREEN_DIS, SCREEN_SRV } screen = SCREEN_DIS;
    draw_dis_screen(&dis, bssid_short);

    while (true)
    {
        S_M1_Buttons_Status b;
        if (!wait_event(&b, 0)) continue;

        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (screen == SCREEN_DIS) break;
            screen = SCREEN_DIS;
            draw_dis_screen(&dis, bssid_short);
            continue;
        }

        if (screen == SCREEN_DIS)
        {
            if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
                b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
            {
                screen = SCREEN_SRV;
                draw_service_list(services, srv_count, srv_sel);
            }
        }
        else /* SCREEN_SRV */
        {
            if (srv_count == 0) continue;
            if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
            {
                srv_sel = (srv_sel + srv_count - 1) % srv_count;
                draw_service_list(services, srv_count, srv_sel);
            }
            else if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
            {
                srv_sel = (srv_sel + 1) % srv_count;
                draw_service_list(services, srv_count, srv_sel);
            }
            else if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                run_char_screen(&services[srv_sel]);
                draw_service_list(services, srv_count, srv_sel);
            }
        }
    }

    m1_ble_probe_free_services(services);
    m1_ble_probe_close();
    xQueueReset(main_q_hdl);
}
