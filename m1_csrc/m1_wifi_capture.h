/* See COPYING.txt for license details. */

/*
 * m1_wifi_capture.h
 *
 * Per-(BSSID, STA) state machine that consumes EAPOL/PMKID events
 * from the ESP-AT monitor stream and emits hashcat-ready `.22000`
 * records.
 *
 * Inputs come in via m1_wifi_capture_feed_*(). When a session has
 * enough material to be crackable, the engine writes one line to the
 * provided output file. Sessions are bounded — we keep state for at
 * most 8 in-flight handshakes.
 *
 * The engine is pure logic + callbacks: it doesn't open files itself.
 * That keeps it host-testable.
 *
 * M1 Project
 */

#ifndef M1_WIFI_CAPTURE_H_
#define M1_WIFI_CAPTURE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define M1_CAPTURE_MAX_SESSIONS   8
#define M1_CAPTURE_EAPOL_MAX      512
#define M1_CAPTURE_SSID_MAX       33

/* Caller-supplied sink for a finished `.22000` line. Returns true on
 * success. Engine doesn't care what's at the other end (file, log,
 * test buffer). */
typedef bool (*m1_capture_sink_fn)(const char *line22000, size_t len, void *user);

typedef struct {
    m1_capture_sink_fn sink;
    void              *sink_user;
    /* Counters for UI. */
    uint32_t frames_seen;     /* every EAPOL + PMKID input */
    uint32_t records_written; /* records emitted */
    uint32_t pmkids_written;
    uint32_t handshakes_written;
    /* Per-session state. */
    struct m1_capture_session_s {
        bool     in_use;
        uint8_t  bssid[6];
        uint8_t  sta[6];
        char     ssid[M1_CAPTURE_SSID_MAX];
        bool     have_m1;       /* anonce + (optional) pmkid */
        bool     have_m2;       /* mic + eapol frame */
        uint8_t  anonce[32];
        uint8_t  pmkid[16];
        bool     have_pmkid;
        uint8_t  mic[16];
        uint8_t  eapol[M1_CAPTURE_EAPOL_MAX];
        uint16_t eapol_len;
    } sessions[M1_CAPTURE_MAX_SESSIONS];
    /* BSSID -> SSID map, populated by feed_beacon and consulted by
     * session creation. 16 slots, LRU. */
    struct m1_capture_bssid_ssid_s {
        bool    in_use;
        uint8_t bssid[6];
        char    ssid[M1_CAPTURE_SSID_MAX];
    } bssid_ssid[16];
    uint8_t bssid_ssid_next;
} m1_capture_ctx_t;

/* Reset all state, configure the sink. */
void m1_capture_init(m1_capture_ctx_t *ctx,
                     m1_capture_sink_fn sink, void *user);

/* Feed one parsed event. Returns true if a `.22000` record was emitted
 * as a side-effect.
 */
bool m1_capture_feed_beacon(m1_capture_ctx_t *ctx,
                            const uint8_t bssid[6], const char *ssid);

bool m1_capture_feed_eapol(m1_capture_ctx_t *ctx,
                           const uint8_t bssid[6], const uint8_t sta[6],
                           uint8_t msg_num,
                           const uint8_t *frame, size_t frame_len);

bool m1_capture_feed_pmkid(m1_capture_ctx_t *ctx,
                           const uint8_t bssid[6], const uint8_t sta[6],
                           const uint8_t pmkid[16]);

#endif /* M1_WIFI_CAPTURE_H_ */
