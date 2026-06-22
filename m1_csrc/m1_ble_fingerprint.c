/* See COPYING.txt for license details. */

/*
 * m1_ble_fingerprint.c — passive BLE device identification.
 *
 * References:
 *   - Bluetooth Core Spec, Vol 3 Part C ยง11 (Generic Access Profile, AD types)
 *   - Bluetooth-assigned numbers (company IDs, GAP appearance, 16-bit UUIDs)
 *     https://www.bluetooth.com/specifications/assigned-numbers/
 *   - Apple Continuity reverse-engineering (furiousMAC, Hexway):
 *     https://github.com/furiousMAC/continuity
 *   - Microsoft CDP "Swift Pair" / Beacon payload (public docs)
 *   - Google Fast Pair model ID format
 *
 * Everything in this file is passive — payloads we get for free when the
 * ESP32 does a scan. No connection or pairing.
 *
 * M1 Project
 */

#include "m1_ble_fingerprint.h"
#include "m1_ble_uuid_names.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Max sane adv length is 31 bytes; allow scan-response to follow. */
#define BLE_AD_MAX  62

/* GAP AD types we care about. */
#define AD_FLAGS               0x01
#define AD_INCOMPLETE_UUID16   0x02
#define AD_COMPLETE_UUID16     0x03
#define AD_INCOMPLETE_UUID128  0x06
#define AD_COMPLETE_UUID128    0x07
#define AD_SHORT_LOCAL_NAME    0x08
#define AD_COMPLETE_LOCAL_NAME 0x09
#define AD_TX_POWER            0x0A
#define AD_SERVICE_DATA_16     0x16
#define AD_APPEARANCE          0x19
#define AD_MANUFACTURER_DATA   0xFF

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decode a hex string into bytes. Returns number of bytes written, or 0 on
 * empty/invalid. Trailing odd nibble is ignored. */
static size_t hex_to_bytes(const char *hex, uint8_t *out, size_t out_max)
{
    if (!hex || !out) return 0;
    size_t n = 0;
    while (*hex && n < out_max)
    {
        int hi = hex_nibble(*hex++);
        if (hi < 0) break;
        if (!*hex) break;
        int lo = hex_nibble(*hex++);
        if (lo < 0) break;
        out[n++] = (uint8_t)((hi << 4) | lo);
    }
    return n;
}

/* Concatenate adv + scan-rsp into one byte buffer. */
static size_t collect_payload(const char *adv_hex, const char *scan_rsp_hex,
                              uint8_t *out, size_t out_max)
{
    size_t n = hex_to_bytes(adv_hex, out, out_max);
    if (scan_rsp_hex && n < out_max)
        n += hex_to_bytes(scan_rsp_hex, out + n, out_max - n);
    return n;
}

/* Iterate AD structures: each is [len][type][data...] where len covers
 * type+data. Returns false when exhausted; otherwise writes ad_type and
 * sets payload / payload_len, advancing pos.
 */
static bool ad_next(const uint8_t *buf, size_t buflen, size_t *pos,
                    uint8_t *ad_type, const uint8_t **payload, size_t *payload_len)
{
    while (*pos < buflen)
    {
        uint8_t len = buf[*pos];
        if (len == 0) { (*pos)++; continue; } /* padding */
        if (*pos + 1 + len > buflen) return false;
        *ad_type = buf[*pos + 1];
        *payload = &buf[*pos + 2];
        *payload_len = (size_t)len - 1;
        *pos += 1 + (size_t)len;
        return true;
    }
    return false;
}

/* ---- Apple Continuity ---------------------------------------------------- */
/* Apple wraps multiple sub-records inside a single manuf-specific payload:
 *   [subtype:1][sublen:1][data:sublen]...
 * Subtypes we name (from public reverse-engineering):
 *   0x02 iBeacon
 *   0x05 AirDrop
 *   0x07 AirPods / "Proximity Pairing"
 *   0x09 Nearby Action
 *   0x0A Apple TV pairing
 *   0x0B Watch C
 *   0x0C Handoff
 *   0x0D Wi-Fi settings
 *   0x0E Hotspot
 *   0x0F Wi-Fi Join
 *   0x10 Nearby Info  <-- the most common one ("iPhone visible")
 *   0x12 Find My      <-- AirTag / offline finding
 *   0x16 Heysiri
 * For AirPods (0x07) the 2 bytes after sublen are a model ID we can map.
 */
static const struct { uint16_t id; const char *name; } APPLE_AIRPODS_MODELS[] = {
    { 0x0220, "AirPods (1st gen)" },
    { 0x0F20, "AirPods (2nd gen)" },
    { 0x1320, "AirPods (3rd gen)" },
    { 0x1420, "AirPods Pro (2nd)" },
    { 0x0E20, "AirPods Pro" },
    { 0x0A20, "AirPods Max" },
    { 0x0520, "AirPods Max" },
    { 0x0620, "AirPods Max" },
    { 0x0B20, "PowerBeats Pro" },
    { 0x0C20, "Beats Solo Pro" },
    { 0x1020, "Beats Studio Buds" },
    { 0x1120, "Beats Flex" },
    { 0x0320, "PowerBeats3" },
    { 0x0920, "Beats Solo3" },
    { 0x0420, "BeatsX" },
};

static const char *apple_subtype_name(uint8_t st)
{
    switch (st)
    {
        case 0x02: return "iBeacon";
        case 0x05: return "AirDrop";
        case 0x07: return "AirPods/Beats";
        case 0x09: return "Nearby Action";
        case 0x0A: return "AppleTV setup";
        case 0x0B: return "Watch";
        case 0x0C: return "Handoff";
        case 0x0D: return "WiFi settings";
        case 0x0E: return "Hotspot";
        case 0x0F: return "WiFi join";
        case 0x10: return "iPhone/iPad";       /* Nearby Info */
        case 0x12: return "Find My / AirTag";
        case 0x16: return "Hey Siri";
        default:   return NULL;
    }
}

static bool decode_apple_continuity(const uint8_t *data, size_t len,
                                    char *out, size_t out_size)
{
    /* The 2-byte company-ID has already been consumed by the caller. */
    size_t i = 0;
    while (i + 1 < len)
    {
        uint8_t st = data[i++];
        uint8_t sl = data[i++];
        if (i + sl > len) break;
        const char *name = apple_subtype_name(st);
        if (st == 0x07 && sl >= 2)
        {
            /* AirPods/Beats: 2-byte model id, big-endian on the wire. */
            uint16_t mid = ((uint16_t)data[i] << 8) | data[i + 1];
            for (size_t k = 0; k < sizeof(APPLE_AIRPODS_MODELS) / sizeof(APPLE_AIRPODS_MODELS[0]); k++)
            {
                if (APPLE_AIRPODS_MODELS[k].id == mid)
                {
                    snprintf(out, out_size, "Apple %s", APPLE_AIRPODS_MODELS[k].name);
                    return true;
                }
            }
            snprintf(out, out_size, "Apple AirPods/Beats");
            return true;
        }
        if (name)
        {
            snprintf(out, out_size, "Apple %s", name);
            return true;
        }
        i += sl;
    }
    snprintf(out, out_size, "Apple device");
    return true;
}

/* ---- Microsoft beacon / Swift Pair --------------------------------------- */
/* Microsoft CDP advertisement layout (public): first byte is the scenario.
 *   0x01 = Swift Pair (pairing); 0x03 = Cross-device experiences ("BLE Beacon").
 *   Following bytes are device-class / sub-scenario.
 */
static bool decode_microsoft(const uint8_t *data, size_t len,
                             char *out, size_t out_size)
{
    if (len < 1) { snprintf(out, out_size, "Microsoft"); return true; }
    switch (data[0])
    {
        case 0x01:
            snprintf(out, out_size, "MS Swift Pair");
            return true;
        case 0x02:
            snprintf(out, out_size, "MS Surface Pen");
            return true;
        case 0x03:
            snprintf(out, out_size, "MS CDP beacon");
            return true;
        default:
            snprintf(out, out_size, "Microsoft");
            return true;
    }
}

/* ---- Google Fast Pair ---------------------------------------------------- */
/* Fast Pair adv (mfgr 0x00E0): version + model ID; the model ID is a
 * registered 24-bit value. We can't enumerate the whole registry but we can
 * at least surface that it IS a Fast Pair device + the model ID hex. */
static bool decode_google(const uint8_t *data, size_t len,
                          char *out, size_t out_size)
{
    if (len >= 3)
    {
        uint32_t mid = ((uint32_t)data[0] << 16) | ((uint32_t)data[1] << 8) | data[2];
        snprintf(out, out_size, "Fast Pair %06lX", (unsigned long)mid);
        return true;
    }
    snprintf(out, out_size, "Google");
    return true;
}

/* ---- Samsung ------------------------------------------------------------- */
static bool decode_samsung(const uint8_t *data, size_t len,
                           char *out, size_t out_size)
{
    (void)data; (void)len;
    snprintf(out, out_size, "Samsung");
    return true;
}

/* Manufacturer ID dispatch (16-bit, little-endian on wire). */
static bool decode_manuf(const uint8_t *payload, size_t payload_len,
                         char *out, size_t out_size)
{
    if (payload_len < 2) return false;
    uint16_t cid = (uint16_t)payload[0] | ((uint16_t)payload[1] << 8);
    const uint8_t *data = payload + 2;
    size_t      dlen   = payload_len - 2;

    switch (cid)
    {
        case 0x004C: return decode_apple_continuity(data, dlen, out, out_size);
        case 0x0006: return decode_microsoft(data, dlen, out, out_size);
        case 0x00E0: return decode_google(data, dlen, out, out_size);
        case 0x0075: return decode_samsung(data, dlen, out, out_size);
        case 0x0087: snprintf(out, out_size, "Garmin");     return true;
        case 0x015D: snprintf(out, out_size, "Tile");       return true;
        case 0x038F: snprintf(out, out_size, "Xiaomi/Mi");  return true;
        case 0x00D2: snprintf(out, out_size, "Logitech");   return true;
        case 0x0059: snprintf(out, out_size, "Nordic dev"); return true;
        case 0x0001: snprintf(out, out_size, "Ericsson");   return true;
        case 0x000D: snprintf(out, out_size, "Texas Inst"); return true;
        case 0x0131: snprintf(out, out_size, "Cypress");    return true;
        case 0x02E5: snprintf(out, out_size, "Espressif");  return true;
        case 0x004F: snprintf(out, out_size, "Sony Ericsson"); return true;
        case 0x012D: snprintf(out, out_size, "Sony");       return true;
        case 0x0157: snprintf(out, out_size, "Anhui Huami/Xiaomi"); return true;
        default:
            snprintf(out, out_size, "MfgID 0x%04X", cid);
            return true;
    }
}

/* Well-known 16-bit Service UUIDs — delegate to the shared module. */
static const char *known_service_uuid16(uint16_t u)
{
    return m1_ble_service_name(u);
}

bool m1_ble_describe(const char *adv_hex,
                     const char *scan_rsp_hex,
                     int addr_type,
                     char *out, size_t out_size)
{
    if (!out || out_size == 0) return false;
    out[0] = '\0';

    uint8_t buf[BLE_AD_MAX];
    size_t  buflen = collect_payload(adv_hex, scan_rsp_hex, buf, sizeof(buf));
    if (buflen == 0)
    {
        snprintf(out, out_size,
                 addr_type == M1_BLE_ADDR_TYPE_PUBLIC ? "BLE device" : "BLE (random)");
        return false;
    }

    /* Pass 1: manufacturer-specific data wins. */
    size_t pos = 0;
    uint8_t t;
    const uint8_t *p;
    size_t plen;
    while (ad_next(buf, buflen, &pos, &t, &p, &plen))
    {
        if (t == AD_MANUFACTURER_DATA && plen >= 2)
        {
            if (decode_manuf(p, plen, out, out_size))
                return true;
        }
    }

    /* Pass 2: prefer Complete Local Name, then Shortened Local Name. */
    char short_name[32] = "";
    pos = 0;
    while (ad_next(buf, buflen, &pos, &t, &p, &plen))
    {
        if (t == AD_COMPLETE_LOCAL_NAME && plen > 0)
        {
            size_t n = plen < out_size - 1 ? plen : out_size - 1;
            memcpy(out, p, n);
            out[n] = '\0';
            return true;
        }
        if (t == AD_SHORT_LOCAL_NAME && plen > 0 && short_name[0] == '\0')
        {
            size_t n = plen < sizeof(short_name) - 1 ? plen : sizeof(short_name) - 1;
            memcpy(short_name, p, n);
            short_name[n] = '\0';
        }
    }
    if (short_name[0] != '\0')
    {
        snprintf(out, out_size, "%s", short_name);
        return true;
    }

    /* Pass 3: 16-bit service UUIDs / service data. */
    pos = 0;
    while (ad_next(buf, buflen, &pos, &t, &p, &plen))
    {
        if ((t == AD_COMPLETE_UUID16 || t == AD_INCOMPLETE_UUID16) && plen >= 2)
        {
            for (size_t i = 0; i + 1 < plen; i += 2)
            {
                uint16_t u = (uint16_t)p[i] | ((uint16_t)p[i + 1] << 8);
                const char *name = known_service_uuid16(u);
                if (name) { snprintf(out, out_size, "%s", name); return true; }
            }
            uint16_t u = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            snprintf(out, out_size, "UUID 0x%04X", u);
            return true;
        }
        if (t == AD_SERVICE_DATA_16 && plen >= 2)
        {
            uint16_t u = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            const char *name = known_service_uuid16(u);
            if (name) { snprintf(out, out_size, "%s svc", name); return true; }
        }
        if (t == AD_APPEARANCE && plen >= 2)
        {
            uint16_t a = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            const char *role;
            switch (a >> 6)
            {
                case 0x00 >> 6: role = "Unknown"; break; /* 0..63 */
                case 0x40 >> 6: role = "Phone"; break;
                case 0x80 >> 6: role = "Computer"; break;
                case 0xC0 >> 6: role = "Watch"; break;
                default:        role = "BLE"; break;
            }
            snprintf(out, out_size, "%s", role);
            return true;
        }
    }

    snprintf(out, out_size,
             addr_type == M1_BLE_ADDR_TYPE_PUBLIC ? "BLE device" : "BLE (random)");
    return false;
}
