/* See COPYING.txt for license details. */

/*
 * m1_oui_lookup.c
 *
 * IEEE OUI -> manufacturer lookup. See m1_oui_lookup.h for usage.
 *
 * The on-disk format is produced by scripts/build_oui_db.py:
 *
 *   0..7    8 bytes  magic = "M1OUI001"
 *   8..11   uint32   record count (little-endian)
 *   12..15  uint32   record size  (little-endian) = 32
 *   16..    records, sorted ascending by 3-byte OUI key
 *
 * Each record: 3 bytes OUI + 29 bytes ASCII vendor (null-padded).
 *
 * M1 Project
 */

#include "m1_oui_lookup.h"

#include <string.h>
#include <stdio.h>

#include "ff.h"
#include "m1_log_debug.h"
#include "m1_sdcard.h"

#define M1_LOGDB_TAG          "OUI"

#define OUI_MAGIC             "M1OUI001"
#define OUI_MAGIC_LEN         8
#define OUI_HDR_LEN           16   /* magic + count + reclen */
#define OUI_RECLEN            32   /* must match build_oui_db.py */
#define OUI_CACHE_SLOTS       16

typedef struct {
    uint32_t key;      /* 24-bit OUI; 0xFFFFFFFF = slot unused */
    uint16_t hit_seq;  /* used to find LRU victim */
    bool found;
    char vendor[M1_OUI_VENDOR_LEN + 1];
} oui_cache_slot_t;

static FIL    s_fp;
static bool   s_file_open = false;
static bool   s_open_attempted = false;
static uint32_t s_record_count = 0;
static uint16_t s_seq_counter = 0;
static oui_cache_slot_t s_cache[OUI_CACHE_SLOTS];

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse "AA:BB:CC:..." (or '-' separators, or no separators) and write the
 * first 3 bytes (the OUI) into `oui`. Returns true on success.
 */
static bool parse_oui_from_mac(const char *s, uint8_t oui[3])
{
    if (!s) return false;
    int produced = 0;
    while (produced < 3 && *s)
    {
        int hi = hex_nibble(*s++);
        if (hi < 0) return false;
        if (!*s) return false;
        int lo = hex_nibble(*s++);
        if (lo < 0) return false;
        oui[produced++] = (uint8_t)((hi << 4) | lo);
        if (*s == ':' || *s == '-') s++;
    }
    return produced == 3;
}

static void cache_init_if_needed(void)
{
    static bool initialised = false;
    if (initialised) return;
    for (int i = 0; i < OUI_CACHE_SLOTS; i++)
        s_cache[i].key = 0xFFFFFFFFu;
    initialised = true;
}

static bool cache_get(uint32_t key, char *out, size_t out_size, bool *found_out)
{
    for (int i = 0; i < OUI_CACHE_SLOTS; i++)
    {
        if (s_cache[i].key == key)
        {
            s_cache[i].hit_seq = ++s_seq_counter;
            *found_out = s_cache[i].found;
            if (out && out_size)
            {
                if (s_cache[i].found)
                    snprintf(out, out_size, "%s", s_cache[i].vendor);
                else
                    snprintf(out, out_size, "Unknown");
            }
            return true;
        }
    }
    return false;
}

static void cache_put(uint32_t key, bool found, const char *vendor)
{
    int victim = 0;
    uint16_t oldest = 0xFFFFu;
    for (int i = 0; i < OUI_CACHE_SLOTS; i++)
    {
        if (s_cache[i].key == 0xFFFFFFFFu) { victim = i; break; }
        /* Treat hit_seq as a monotonically increasing counter; pick the
         * slot with the smallest delta-to-now. */
        uint16_t age = (uint16_t)(s_seq_counter - s_cache[i].hit_seq);
        if (age > oldest) { oldest = age; victim = i; }
    }
    s_cache[victim].key = key;
    s_cache[victim].hit_seq = ++s_seq_counter;
    s_cache[victim].found = found;
    if (found && vendor)
        snprintf(s_cache[victim].vendor, sizeof(s_cache[victim].vendor), "%s", vendor);
    else
        s_cache[victim].vendor[0] = '\0';
}

void m1_oui_cache_reset(void)
{
    for (int i = 0; i < OUI_CACHE_SLOTS; i++)
        s_cache[i].key = 0xFFFFFFFFu;
    if (s_file_open)
    {
        f_close(&s_fp);
        s_file_open = false;
    }
    s_open_attempted = false;
    s_record_count = 0;
}

static bool open_db_if_needed(void)
{
    if (s_file_open) return true;
    if (s_open_attempted) return false; /* don't keep hammering a missing file */

    s_open_attempted = true;

    if (m1_sdcard_get_status() != SD_access_OK)
    {
        M1_LOG_D(M1_LOGDB_TAG, "SD not ready, OUI lookup disabled\n\r");
        return false;
    }
    if (f_open(&s_fp, M1_OUI_DB_PATH, FA_OPEN_EXISTING | FA_READ) != FR_OK)
    {
        M1_LOG_I(M1_LOGDB_TAG, "OUI DB not found at " M1_OUI_DB_PATH "\n\r");
        return false;
    }
    uint8_t hdr[OUI_HDR_LEN];
    UINT br = 0;
    if (f_read(&s_fp, hdr, OUI_HDR_LEN, &br) != FR_OK || br != OUI_HDR_LEN)
    {
        M1_LOG_E(M1_LOGDB_TAG, "OUI DB header read failed\n\r");
        f_close(&s_fp);
        return false;
    }
    if (memcmp(hdr, OUI_MAGIC, OUI_MAGIC_LEN) != 0)
    {
        M1_LOG_E(M1_LOGDB_TAG, "OUI DB bad magic\n\r");
        f_close(&s_fp);
        return false;
    }
    uint32_t count = (uint32_t)hdr[8]  | ((uint32_t)hdr[9]  << 8)
                   | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    uint32_t reclen = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8)
                   | ((uint32_t)hdr[14] << 16) | ((uint32_t)hdr[15] << 24);
    if (reclen != OUI_RECLEN || count == 0)
    {
        M1_LOG_E(M1_LOGDB_TAG, "OUI DB bad geometry (count=%lu reclen=%lu)\n\r",
                 (unsigned long)count, (unsigned long)reclen);
        f_close(&s_fp);
        return false;
    }
    s_record_count = count;
    s_file_open = true;
    M1_LOG_I(M1_LOGDB_TAG, "OUI DB ready, %lu entries\n\r",
             (unsigned long)s_record_count);
    return true;
}

static int read_record(uint32_t index, uint8_t *out_rec)
{
    DWORD ofs = (DWORD)OUI_HDR_LEN + (DWORD)index * (DWORD)OUI_RECLEN;
    if (f_lseek(&s_fp, ofs) != FR_OK) return -1;
    UINT br = 0;
    if (f_read(&s_fp, out_rec, OUI_RECLEN, &br) != FR_OK || br != OUI_RECLEN)
        return -1;
    return 0;
}

bool m1_oui_lookup_bytes(const uint8_t oui[3], char *out, size_t out_size)
{
    cache_init_if_needed();

    if (!out || out_size == 0)
        return false;
    if (!oui)
    {
        snprintf(out, out_size, "(invalid)");
        return false;
    }

    uint32_t key = ((uint32_t)oui[0] << 16) | ((uint32_t)oui[1] << 8) | (uint32_t)oui[2];

    bool cached_found = false;
    if (cache_get(key, out, out_size, &cached_found))
        return cached_found;

    if (!open_db_if_needed())
    {
        snprintf(out, out_size, "Unknown");
        /* Don't cache a DB-missing result; user might pop in the SD later. */
        return false;
    }

    uint32_t lo = 0, hi = s_record_count;
    uint8_t rec[OUI_RECLEN];
    while (lo < hi)
    {
        uint32_t mid = lo + (hi - lo) / 2;
        if (read_record(mid, rec) != 0)
        {
            snprintf(out, out_size, "Unknown");
            /* I/O error: invalidate so next call retries. */
            f_close(&s_fp); s_file_open = false; s_open_attempted = false;
            return false;
        }
        int cmp = memcmp(rec, oui, 3);
        if (cmp == 0)
        {
            /* Found. Vendor is at rec[3..3+29], null-padded. */
            char vendor[M1_OUI_VENDOR_LEN + 1];
            size_t n = 0;
            while (n < M1_OUI_VENDOR_LEN && rec[3 + n] != 0)
            {
                vendor[n] = (char)rec[3 + n];
                n++;
            }
            vendor[n] = '\0';
            snprintf(out, out_size, "%s", vendor);
            cache_put(key, true, vendor);
            return true;
        }
        if (cmp < 0) lo = mid + 1;
        else         hi = mid;
    }
    snprintf(out, out_size, "Unknown");
    cache_put(key, false, NULL);
    return false;
}

bool m1_oui_lookup_str(const char *bssid_str, char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return false;
    uint8_t oui[3];
    if (!parse_oui_from_mac(bssid_str, oui))
    {
        snprintf(out, out_size, "(invalid)");
        return false;
    }
    return m1_oui_lookup_bytes(oui, out, out_size);
}
