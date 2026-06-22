/* See COPYING.txt for license details. */

/*
 * m1_wifi_attack_ui.c
 *
 * Three-screen UI for the WiFi WPA-PSK dictionary attack:
 *
 *   1. Confirm — shows SSID/BSSID + a warning that this generates
 *                association attempts that may be logged. OK to start,
 *                BACK to cancel.
 *   2. Progress — current candidate, n/N, last error, elapsed time.
 *                BACK aborts.
 *   3. Result — success banner with the PSK (also persisted to SD)
 *                or "Not in dictionary".
 *
 * The outer dictionary loop polls main_q_hdl with a 0-tick timeout
 * between attempts so the user can BACK out promptly.
 *
 * M1 Project
 */

#include "m1_wifi_attack_ui.h"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "m1_display.h"
#include "m1_system.h"
#include "m1_log_debug.h"
#include "m1_wifi_attack.h"
#include "m1_esp32_hal.h"
#include "esp_app_main.h"
#include "ctrl_api.h"

#define M1_LOGDB_TAG          "WIFAUI"
#define ROW_H                 (M1_GUI_FONT_HEIGHT)
#define MAX_CHARS_PER_ROW     ((M1_LCD_DISPLAY_WIDTH / M1_GUI_FONT_WIDTH) - 1)
#define TRY_TIMEOUT_SEC       8

static void draw_header(const char *title)
{
    u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
    char trunc[24];
    snprintf(trunc, sizeof(trunc), "%s", title);
    if ((int)strlen(trunc) > MAX_CHARS_PER_ROW) trunc[MAX_CHARS_PER_ROW] = '\0';
    u8g2_DrawStr(&m1_u8g2, 2, ROW_H, trunc);
}

static bool wait_keypad(S_M1_Buttons_Status *out_btn, TickType_t to)
{
    S_M1_Main_Q_t q;
    if (xQueueReceive(main_q_hdl, &q, to) != pdTRUE) return false;
    if (q.q_evt_type != Q_EVENT_KEYPAD) return false;
    return xQueueReceive(button_events_q_hdl, out_btn, 0) == pdTRUE;
}

static void draw_confirm(const char *ssid, const char *bssid)
{
    char buf[40];
    m1_u8g2_firstpage();
    draw_header("Dict attack");
    int y = 14 + ROW_H;
    snprintf(buf, sizeof(buf), "SSID:%s", ssid ? ssid : "?");
    if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    snprintf(buf, sizeof(buf), "%s", bssid ? bssid : "?");
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    u8g2_DrawStr(&m1_u8g2, 2, y, "Attempts WILL");        y += ROW_H;
    u8g2_DrawStr(&m1_u8g2, 2, y, "be visible on AP.");    y += ROW_H;
    u8g2_DrawStr(&m1_u8g2, 2, y, "OK=go  BACK=quit");
    m1_u8g2_nextpage();
}

static const char *status_short(int s)
{
    switch (s)
    {
        case M1_WIFI_TRY_OK:         return "OK!";
        case M1_WIFI_TRY_TIMEOUT:    return "timeout";
        case M1_WIFI_TRY_WRONG_PASS: return "wrong";
        case M1_WIFI_TRY_NO_AP:      return "no AP";
        case M1_WIFI_TRY_CONN_FAIL:  return "connfail";
        case M1_WIFI_TRY_OTHER:      return "other";
        case M1_WIFI_TRY_TRANSPORT:  return "esp err";
        default:                     return "?";
    }
}

static void draw_progress(const char *ssid, uint32_t n_tried, uint32_t n_est,
                          const char *current, int last_status, uint32_t elapsed_s)
{
    char buf[40];
    m1_u8g2_firstpage();
    draw_header("Attacking");
    int y = 14 + ROW_H;
    snprintf(buf, sizeof(buf), "%.20s", ssid ? ssid : "?");
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;

    if (n_est)
        snprintf(buf, sizeof(buf), "%lu/%lu  %lus",
                 (unsigned long)n_tried, (unsigned long)n_est,
                 (unsigned long)elapsed_s);
    else
        snprintf(buf, sizeof(buf), "%lu  %lus",
                 (unsigned long)n_tried, (unsigned long)elapsed_s);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;

    snprintf(buf, sizeof(buf), "try:%.16s", current ? current : "");
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;

    snprintf(buf, sizeof(buf), "last:%s", status_short(last_status));
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;

    u8g2_DrawStr(&m1_u8g2, 2, y, "BACK to stop");
    m1_u8g2_nextpage();
}

static void draw_success(const char *ssid, const char *psk)
{
    char buf[40];
    m1_u8g2_firstpage();
    draw_header("CRACKED");
    int y = 14 + ROW_H;
    snprintf(buf, sizeof(buf), "%.20s", ssid ? ssid : "?");
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H + 2;
    /* PSK in default font (bold-ish single line, truncated if needed). */
    snprintf(buf, sizeof(buf), "%.20s", psk ? psk : "");
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H + 2;
    u8g2_DrawStr(&m1_u8g2, 2, y, "saved to:");          y += ROW_H;
    u8g2_DrawStr(&m1_u8g2, 2, y, "cracked.txt");
    m1_u8g2_nextpage();
}

static void draw_summary(const char *line1, const char *line2)
{
    m1_u8g2_firstpage();
    draw_header("Done");
    u8g2_DrawStr(&m1_u8g2, 2, 14 + ROW_H, line1);
    if (line2) u8g2_DrawStr(&m1_u8g2, 2, 14 + 2*ROW_H, line2);
    u8g2_DrawStr(&m1_u8g2, 2, 14 + 4*ROW_H, "BACK to return");
    m1_u8g2_nextpage();
}

void m1_wifi_attack_ui_run(const char *ssid, const char *bssid)
{
    if (!ssid || !*ssid) return;

    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);

    /* Screen 1 — confirm. */
    draw_confirm(ssid, bssid);
    while (true)
    {
        S_M1_Buttons_Status b;
        if (!wait_keypad(&b, portMAX_DELAY)) continue;
        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) return;
        if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
            b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_LCLICK)  break;
    }

    /* Ensure ESP-AT is up and station-mode. */
    if (!m1_esp32_get_init_status())
    {
        m1_esp32_init();
        if (!get_esp32_main_init_status())
            esp32_main_init();
    }
    if (!get_esp32_main_init_status())
    {
        draw_summary("ESP32 not ready", NULL);
        S_M1_Buttons_Status b;
        while (wait_keypad(&b, portMAX_DELAY))
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        m1_esp32_deinit();
        return;
    }
    {
        ctrl_cmd_t app = CTRL_CMD_DEFAULT_REQ();
        app.cmd_timeout_sec = 5;
        (void)wifi_set_station_mode(&app);
    }

    /* Screen 2 — progress loop. */
    size_t seed_count = 0;
    m1_wifi_attack_iter_t *it = m1_wifi_attack_open(ssid, &seed_count);
    if (!it)
    {
        draw_summary("Open failed.", "No wordlist?");
        S_M1_Buttons_Status b;
        while (wait_keypad(&b, portMAX_DELAY))
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        m1_esp32_deinit();
        return;
    }

    uint32_t total_est = (uint32_t)m1_wifi_attack_total_estimate(it);
    uint32_t n_tried = 0;
    int last_status = M1_WIFI_TRY_OTHER;
    char current[M1_WIFI_PSK_MAX] = "";
    bool aborted = false;
    bool cracked = false;
    char found_psk[M1_WIFI_PSK_MAX] = "";
    uint32_t start_ms = HAL_GetTick();

    draw_progress(ssid, 0, total_est, "", last_status, 0);

    while (m1_wifi_attack_next(it, current, sizeof(current)))
    {
        /* Check for user abort before issuing the next attempt. */
        S_M1_Buttons_Status b;
        if (wait_keypad(&b, 0))
        {
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
            { aborted = true; break; }
        }

        last_status = m1_wifi_attack_try(ssid, current, TRY_TIMEOUT_SEC);
        n_tried++;
        uint32_t elapsed_s = (HAL_GetTick() - start_ms) / 1000;
        draw_progress(ssid, n_tried, total_est, current, last_status, elapsed_s);

        if (last_status == M1_WIFI_TRY_OK)
        {
            cracked = true;
            snprintf(found_psk, sizeof(found_psk), "%s", current);
            m1_wifi_attack_record_hit(ssid, bssid, current);
            break;
        }
        /* Tiny inter-attempt sleep so the ESP-AT can settle. */
        S_M1_Main_Q_t q;
        (void)xQueueReceive(main_q_hdl, &q, pdMS_TO_TICKS(500));
    }

    m1_wifi_attack_close(it);
    /* Always disconnect to release any half-open state. */
    {
        ctrl_cmd_t app = CTRL_CMD_DEFAULT_REQ();
        app.cmd_timeout_sec = 5;
        (void)wifi_disconnect_ap(&app);
    }

    /* Screen 3 — result. */
    if (cracked)
        draw_success(ssid, found_psk);
    else if (aborted)
        draw_summary("Aborted.", "No hit yet.");
    else
        draw_summary("Not in dict.", "End of list.");

    while (true)
    {
        S_M1_Buttons_Status b;
        if (!wait_keypad(&b, portMAX_DELAY)) continue;
        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
    }
    xQueueReset(main_q_hdl);
    m1_esp32_deinit();
}
