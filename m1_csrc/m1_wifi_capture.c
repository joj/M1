/* See COPYING.txt for license details. */

/*
 * m1_wifi_capture.c — handshake/PMKID capture state machine.
 *
 * 4-way handshake we care about, per IEEE 802.11i:
 *   M1: AP -> STA  carries ANonce, optionally PMKID KDE.
 *   M2: STA -> AP  carries SNonce + MIC over the EAPOL frame (this MIC
 *                  is what hashcat verifies).
 *   M3: AP -> STA  carries GTK + MIC. We don't strictly need it.
 *   M4: STA -> AP  ACK + MIC.
 *
 * Minimum for a hashcat `.22000` "EAPOL" record:
 *   - ANonce (from M1)
 *   - MIC (we lift it from M2)
 *   - The EAPOL frame the MIC was computed over (M2, with MIC field
 *     zeroed per hashcat's convention).
 *
 * Minimum for a `.22000` "PMKID" record:
 *   - The 16-byte PMKID.
 *
 * We accept M3-as-substitute-for-M2 too (hashcat does), but for
 * simplicity v1 captures M2 only.
 *
 * Per-(BSSID, STA) session. Up to 8 in flight; LRU-evict the oldest.
 *
 * M1 Project
 */

#include "m1_wifi_capture.h"
#include "m1_pcap_writer.h"

#include <stdio.h>
#include <string.h>

/* MIC lives at offset 81..96 of the EAPOL-Key payload. */
#define EAPOL_MIC_OFFSET     81
#define EAPOL_MIC_LEN        16
/* AP nonce (in M1) lives at offset 17..48 of the EAPOL-Key payload. */
#define EAPOL_NONCE_OFFSET   17
#define EAPOL_NONCE_LEN      32

static int find_session(m1_capture_ctx_t *ctx,
                        const uint8_t bssid[6], const uint8_t sta[6])
{
    int free_slot = -1;
    int oldest = 0;
    for (int i = 0; i < M1_CAPTURE_MAX_SESSIONS; i++)
    {
        if (!ctx->sessions[i].in_use)
        {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (memcmp(ctx->sessions[i].bssid, bssid, 6) == 0
            && memcmp(ctx->sessions[i].sta, sta, 6) == 0)
            return i;
        oldest = i; /* arbitrary LRU stand-in: last-touched index */
    }
    if (free_slot >= 0) return free_slot;
    /* Evict the oldest. */
    memset(&ctx->sessions[oldest], 0, sizeof(ctx->sessions[oldest]));
    return oldest;
}

static void session_init(m1_capture_ctx_t *ctx, int idx,
                         const uint8_t bssid[6], const uint8_t sta[6])
{
    m1_capture_ctx_t *c = ctx;
    if (!c->sessions[idx].in_use
        || memcmp(c->sessions[idx].bssid, bssid, 6) != 0
        || memcmp(c->sessions[idx].sta, sta, 6) != 0)
    {
        memset(&c->sessions[idx], 0, sizeof(c->sessions[idx]));
        c->sessions[idx].in_use = true;
        memcpy(c->sessions[idx].bssid, bssid, 6);
        memcpy(c->sessions[idx].sta, sta, 6);
        /* Populate SSID from the BSSID->SSID cache if we've seen a beacon. */
        for (int i = 0; i < (int)(sizeof(c->bssid_ssid) / sizeof(c->bssid_ssid[0])); i++)
        {
            if (c->bssid_ssid[i].in_use
                && memcmp(c->bssid_ssid[i].bssid, bssid, 6) == 0)
            {
                snprintf(c->sessions[idx].ssid, sizeof(c->sessions[idx].ssid),
                         "%s", c->bssid_ssid[i].ssid);
                break;
            }
        }
    }
}

void m1_capture_init(m1_capture_ctx_t *ctx,
                     m1_capture_sink_fn sink, void *user)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->sink = sink;
    ctx->sink_user = user;
}

bool m1_capture_feed_beacon(m1_capture_ctx_t *ctx,
                            const uint8_t bssid[6], const char *ssid)
{
    if (!ctx || !bssid) return false;
    ctx->frames_seen++;
    if (!ssid || !*ssid) return false;
    /* Update BSSID->SSID cache (replace if BSSID seen, else round-robin). */
    int slot = -1;
    for (int i = 0; i < (int)(sizeof(ctx->bssid_ssid) / sizeof(ctx->bssid_ssid[0])); i++)
    {
        if (ctx->bssid_ssid[i].in_use
            && memcmp(ctx->bssid_ssid[i].bssid, bssid, 6) == 0)
        { slot = i; break; }
    }
    if (slot < 0)
    {
        slot = ctx->bssid_ssid_next % (int)(sizeof(ctx->bssid_ssid) / sizeof(ctx->bssid_ssid[0]));
        ctx->bssid_ssid_next++;
    }
    ctx->bssid_ssid[slot].in_use = true;
    memcpy(ctx->bssid_ssid[slot].bssid, bssid, 6);
    snprintf(ctx->bssid_ssid[slot].ssid, sizeof(ctx->bssid_ssid[slot].ssid),
             "%s", ssid);
    /* Also refresh any in-flight session without SSID yet. */
    for (int i = 0; i < M1_CAPTURE_MAX_SESSIONS; i++)
    {
        if (ctx->sessions[i].in_use
            && memcmp(ctx->sessions[i].bssid, bssid, 6) == 0
            && ctx->sessions[i].ssid[0] == '\0')
        {
            snprintf(ctx->sessions[i].ssid, sizeof(ctx->sessions[i].ssid),
                     "%s", ssid);
        }
    }
    return false;
}

static bool try_emit_eapol_record(m1_capture_ctx_t *ctx, int idx)
{
    if (!ctx->sink) return false;
    if (!ctx->sessions[idx].have_m1 || !ctx->sessions[idx].have_m2) return false;

    char line[M1_PCAP_LINE_MAX];
    size_t n = m1_pcap_format_eapol(line, sizeof(line),
                                    ctx->sessions[idx].bssid,
                                    ctx->sessions[idx].sta,
                                    ctx->sessions[idx].ssid,
                                    ctx->sessions[idx].mic,
                                    ctx->sessions[idx].anonce,
                                    ctx->sessions[idx].eapol,
                                    ctx->sessions[idx].eapol_len);
    if (n == 0) return false;
    if (!ctx->sink(line, n, ctx->sink_user)) return false;
    ctx->records_written++;
    ctx->handshakes_written++;
    /* One record per (BSSID, STA, ANonce) — mark consumed. */
    ctx->sessions[idx].have_m2 = false;
    return true;
}

static bool try_emit_pmkid_record(m1_capture_ctx_t *ctx, int idx)
{
    if (!ctx->sink) return false;
    if (!ctx->sessions[idx].have_pmkid) return false;
    char line[M1_PCAP_LINE_MAX];
    size_t n = m1_pcap_format_pmkid(line, sizeof(line),
                                    ctx->sessions[idx].bssid,
                                    ctx->sessions[idx].sta,
                                    ctx->sessions[idx].ssid,
                                    ctx->sessions[idx].pmkid);
    if (n == 0) return false;
    if (!ctx->sink(line, n, ctx->sink_user)) return false;
    ctx->records_written++;
    ctx->pmkids_written++;
    ctx->sessions[idx].have_pmkid = false;
    return true;
}

bool m1_capture_feed_eapol(m1_capture_ctx_t *ctx,
                           const uint8_t bssid[6], const uint8_t sta[6],
                           uint8_t msg_num,
                           const uint8_t *frame, size_t frame_len)
{
    if (!ctx || !bssid || !sta || !frame) return false;
    ctx->frames_seen++;
    int idx = find_session(ctx, bssid, sta);
    if (idx < 0) return false;
    session_init(ctx, idx, bssid, sta);

    if (msg_num == 1 && frame_len >= EAPOL_NONCE_OFFSET + EAPOL_NONCE_LEN)
    {
        memcpy(ctx->sessions[idx].anonce, frame + EAPOL_NONCE_OFFSET,
               EAPOL_NONCE_LEN);
        ctx->sessions[idx].have_m1 = true;
    }
    else if ((msg_num == 2 || msg_num == 3 || msg_num == 4)
             && frame_len >= EAPOL_MIC_OFFSET + EAPOL_MIC_LEN)
    {
        /* hashcat wants the EAPOL frame with the MIC field zeroed. */
        size_t cap = frame_len > M1_CAPTURE_EAPOL_MAX
                       ? M1_CAPTURE_EAPOL_MAX : frame_len;
        memcpy(ctx->sessions[idx].mic, frame + EAPOL_MIC_OFFSET, EAPOL_MIC_LEN);
        memcpy(ctx->sessions[idx].eapol, frame, cap);
        memset(ctx->sessions[idx].eapol + EAPOL_MIC_OFFSET, 0, EAPOL_MIC_LEN);
        ctx->sessions[idx].eapol_len = (uint16_t)cap;
        ctx->sessions[idx].have_m2 = true;
    }
    /* Try to emit if both halves present. */
    bool emitted = try_emit_eapol_record(ctx, idx);
    return emitted;
}

bool m1_capture_feed_pmkid(m1_capture_ctx_t *ctx,
                           const uint8_t bssid[6], const uint8_t sta[6],
                           const uint8_t pmkid[16])
{
    if (!ctx || !bssid || !sta || !pmkid) return false;
    ctx->frames_seen++;
    int idx = find_session(ctx, bssid, sta);
    if (idx < 0) return false;
    session_init(ctx, idx, bssid, sta);
    memcpy(ctx->sessions[idx].pmkid, pmkid, 16);
    ctx->sessions[idx].have_pmkid = true;
    return try_emit_pmkid_record(ctx, idx);
}
