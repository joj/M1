/* See COPYING.txt for license details. */

/*
 * m1_wifi_capture_ui.c — confirm + live-stats + result screens for
 * passive WiFi handshake/PMKID capture (phase 3c).
 *
 * Capture file:
 *   /databases/captures/<sanitized_ssid>_<bssid>_<unix_tick>.22000
 *
 * One file per UI session; multiple records (PMKID and/or handshakes)
 * accumulate inside while the user holds the screen. We open it lazily
 * on the first record so empty sessions don't leave zero-byte files.
 *
 * M1 Project
 */

#include "m1_wifi_capture_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "ff.h"
#include "m1_display.h"
#include "m1_system.h"
#include "m1_log_debug.h"
#include "m1_esp32_hal.h"
#include "m1_sdcard.h"
#include "esp_app_main.h"
#include "ctrl_api.h"
#include "m1_wifi_capture.h"

#define M1_LOGDB_TAG          "WIFCAP"
#define ROW_H                 (M1_GUI_FONT_HEIGHT)
#define MAX_CHARS_PER_ROW     ((M1_LCD_DISPLAY_WIDTH / M1_GUI_FONT_WIDTH) - 1)
#define CAPTURE_DIR           "0:/databases/captures"

typedef struct {
    FIL    file;
    bool   open;
    char   path[120];
} sink_state_t;

static void sanitize(char *s)
{
    /* Replace anything that's not [A-Za-z0-9_-] with '_' for filename safety. */
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        if (!(isalnum(c) || c == '-' || c == '_')) *s = '_';
    }
}

static bool sink_open(sink_state_t *s, const char *ssid, const char *bssid)
{
    if (s->open) return true;
    if (m1_sdcard_get_status() != SD_access_OK) return false;
    /* Best-effort mkdir; ignore errors (might already exist). */
    f_mkdir("0:/databases");
    f_mkdir(CAPTURE_DIR);

    char safe_ssid[40] = "";
    char safe_bssid[20] = "";
    snprintf(safe_ssid,  sizeof(safe_ssid),  "%s", ssid && *ssid ? ssid : "hidden");
    snprintf(safe_bssid, sizeof(safe_bssid), "%s", bssid ? bssid : "0");
    sanitize(safe_ssid);
    sanitize(safe_bssid);
    snprintf(s->path, sizeof(s->path), "%s/%s_%s_%lu.22000",
             CAPTURE_DIR, safe_ssid, safe_bssid,
             (unsigned long)HAL_GetTick());
    if (f_open(&s->file, s->path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return false;
    s->open = true;
    return true;
}

static bool sink_write(const char *line, size_t len, void *user)
{
    sink_state_t *s = (sink_state_t *)user;
    if (!s->open) return false;
    UINT bw = 0;
    if (f_write(&s->file, line, (UINT)len, &bw) != FR_OK) return false;
    /* Flush every record so a pulled SD card still yields data. */
    f_sync(&s->file);
    return bw == len;
}

static void sink_close(sink_state_t *s)
{
    if (s->open) { f_close(&s->file); s->open = false; }
}

static bool wait_keypad(S_M1_Buttons_Status *out_btn, TickType_t to)
{
    S_M1_Main_Q_t q;
    if (xQueueReceive(main_q_hdl, &q, to) != pdTRUE) return false;
    if (q.q_evt_type != Q_EVENT_KEYPAD) return false;
    return xQueueReceive(button_events_q_hdl, out_btn, 0) == pdTRUE;
}

static void draw_header(const char *title)
{
    u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
    char buf[24];
    snprintf(buf, sizeof(buf), "%s", title);
    if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
    u8g2_DrawStr(&m1_u8g2, 2, ROW_H, buf);
}

static void draw_confirm(const char *ssid, const char *bssid, int channel)
{
    char buf[40];
    m1_u8g2_firstpage();
    draw_header("Capture");
    int y = 14 + ROW_H;
    snprintf(buf, sizeof(buf), "SSID:%s", ssid ? ssid : "?");
    if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    snprintf(buf, sizeof(buf), "%s", bssid ? bssid : "?");
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    snprintf(buf, sizeof(buf), "channel:%d (0=hop)", channel);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    u8g2_DrawStr(&m1_u8g2, 2, y, "Passive, no Tx");  y += ROW_H;
    u8g2_DrawStr(&m1_u8g2, 2, y, "OK=go  BACK=quit");
    m1_u8g2_nextpage();
}

static void draw_progress(uint32_t elapsed_s, uint32_t frames,
                          uint32_t pmkids, uint32_t handshakes)
{
    char buf[40];
    m1_u8g2_firstpage();
    draw_header("Capturing");
    int y = 14 + ROW_H;
    snprintf(buf, sizeof(buf), "elapsed:%lus", (unsigned long)elapsed_s);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    snprintf(buf, sizeof(buf), "frames :%lu", (unsigned long)frames);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    snprintf(buf, sizeof(buf), "PMKIDs :%lu", (unsigned long)pmkids);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    snprintf(buf, sizeof(buf), "Handshk:%lu", (unsigned long)handshakes);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    u8g2_DrawStr(&m1_u8g2, 2, y, "BACK to stop");
    m1_u8g2_nextpage();
}

static void draw_summary(uint32_t pmkids, uint32_t handshakes,
                         bool saved, const char *path)
{
    char buf[40];
    m1_u8g2_firstpage();
    draw_header(saved ? "SAVED" : "Done");
    int y = 14 + ROW_H;
    snprintf(buf, sizeof(buf), "PMKIDs : %lu", (unsigned long)pmkids);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    snprintf(buf, sizeof(buf), "Handshk: %lu", (unsigned long)handshakes);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H + 2;
    if (saved && path)
    {
        u8g2_DrawStr(&m1_u8g2, 2, y, "saved to:"); y += ROW_H;
        const char *short_path = strstr(path, "captures/");
        snprintf(buf, sizeof(buf), "%s",
                 short_path ? short_path : path);
        if ((int)strlen(buf) > MAX_CHARS_PER_ROW) buf[MAX_CHARS_PER_ROW] = '\0';
        u8g2_DrawStr(&m1_u8g2, 2, y, buf); y += ROW_H;
    }
    else
    {
        u8g2_DrawStr(&m1_u8g2, 2, y, "nothing saved.");
        y += ROW_H;
    }
    u8g2_DrawStr(&m1_u8g2, 2, y, "BACK to return");
    m1_u8g2_nextpage();
}

void m1_wifi_capture_ui_run(const char *ssid, const char *bssid, int channel)
{
    if (!bssid) return;

    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    draw_confirm(ssid, bssid, channel);

    /* Wait for confirmation. */
    while (true)
    {
        S_M1_Buttons_Status b;
        if (!wait_keypad(&b, portMAX_DELAY)) continue;
        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) return;
        if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK ||
            b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_LCLICK) break;
    }

    /* Ensure ESP32 is up. */
    if (!m1_esp32_get_init_status())
    {
        m1_esp32_init();
        if (!get_esp32_main_init_status())
            esp32_main_init();
    }
    if (!get_esp32_main_init_status())
    {
        draw_summary(0, 0, false, NULL);
        S_M1_Buttons_Status b;
        while (wait_keypad(&b, portMAX_DELAY))
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        m1_esp32_deinit();
        return;
    }

    /* Start monitor mode. */
    if (wifi_monitor_start(channel) != SUCCESS)
    {
        draw_summary(0, 0, false, NULL);
        S_M1_Buttons_Status b;
        while (wait_keypad(&b, portMAX_DELAY))
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        m1_esp32_deinit();
        return;
    }

    /* If a specific BSSID was requested, pin the channel for it. */
    if (bssid && channel > 0)
        (void)wifi_pmkid_probe(bssid, channel);

    sink_state_t sink = { 0 };
    m1_capture_ctx_t cap;
    m1_capture_init(&cap, sink_write, &sink);

    /* Seed the SSID->BSSID cache so the first record carries SSID. */
    if (ssid && *ssid)
    {
        uint8_t bssid_bytes[6] = {0};
        /* Parse "aa:bb:cc:dd:ee:ff" or "aabbccddeeff". */
        const char *p = bssid;
        for (int i = 0; i < 6 && *p; i++)
        {
            unsigned int b = 0;
            if (sscanf(p, "%2x", &b) != 1) break;
            bssid_bytes[i] = (uint8_t)b;
            p += 2;
            if (*p == ':' || *p == '-') p++;
        }
        m1_capture_feed_beacon(&cap, bssid_bytes, ssid);
    }

    uint32_t start_ms = HAL_GetTick();
    draw_progress(0, 0, 0, 0);

    bool aborted = false;
    uint32_t last_draw_ms = start_ms;

    while (true)
    {
        S_M1_Buttons_Status b;
        if (wait_keypad(&b, 0))
        {
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
            { aborted = true; break; }
        }

        m1_wifi_evt_t evt;
        uint8_t kind = wifi_capture_pump(&evt, 100);

        if (kind == M1_WIFI_EVT_BEACON)
        {
            m1_capture_feed_beacon(&cap, evt.bssid, evt.ssid);
        }
        else if (kind == M1_WIFI_EVT_EAPOL)
        {
            if (!sink.open) sink_open(&sink, ssid, bssid);
            m1_capture_feed_eapol(&cap, evt.bssid, evt.sta, evt.msg_num,
                                  evt.payload, evt.payload_len);
        }
        else if (kind == M1_WIFI_EVT_PMKID)
        {
            if (!sink.open) sink_open(&sink, ssid, bssid);
            m1_capture_feed_pmkid(&cap, evt.bssid, evt.sta, evt.payload);
        }

        uint32_t now = HAL_GetTick();
        if (now - last_draw_ms >= 500)
        {
            last_draw_ms = now;
            draw_progress((now - start_ms) / 1000,
                          cap.frames_seen,
                          cap.pmkids_written,
                          cap.handshakes_written);
        }
    }

    sink_close(&sink);
    (void)wifi_monitor_stop();

    bool saved = (cap.records_written > 0) && (sink.path[0] != '\0');
    draw_summary(cap.pmkids_written, cap.handshakes_written,
                 saved, saved ? sink.path : NULL);

    /* Wait for BACK. */
    while (true)
    {
        S_M1_Buttons_Status b;
        if (!wait_keypad(&b, portMAX_DELAY)) continue;
        if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
    }
    xQueueReset(main_q_hdl);
    m1_esp32_deinit();
    (void)aborted;
}
