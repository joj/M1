/* See COPYING.txt for license details. */

/*
 * m1_ble_uuid_names.c — friendly names for Bluetooth-SIG-assigned 16-bit
 * UUIDs (services and characteristics). Curated subset focused on what
 * commonly shows up in adv payloads and Device Information Service reads.
 *
 * Tables are small (~50 entries each) so a linear scan is fine. If we ever
 * need to grow them substantially, switch to a sorted array + bsearch.
 *
 * References:
 *   https://www.bluetooth.com/specifications/assigned-numbers/
 *
 * M1 Project
 */

#include "m1_ble_uuid_names.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t uuid16;
    const char *name;
} uuid_name_t;

/* Standard Bluetooth-SIG base for 16-bit-assigned UUIDs:
 * 0000XXXX-0000-1000-8000-00805F9B34FB
 */
static bool is_sig_base(const char *u128_lower)
{
    /* expects 36 chars, lowercase */
    return strncmp(u128_lower + 8,  "-0000-1000-8000-00805f9b34fb", 28) == 0
        && strncmp(u128_lower + 0,  "0000", 4) == 0;
}

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

uint16_t m1_ble_uuid16_from_str(const char *uuid_str, bool *is_short)
{
    if (is_short) *is_short = false;
    if (!uuid_str || !*uuid_str) return 0;
    /* Skip optional "0x" prefix. */
    const char *p = uuid_str;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;

    /* Short forms: 4 or fewer hex chars, terminated. */
    size_t len = strlen(p);
    bool all_hex = true;
    for (size_t i = 0; i < len; i++)
        if (hex_nibble((unsigned char)p[i]) < 0) { all_hex = false; break; }

    if (all_hex && len > 0 && len <= 4)
    {
        unsigned v = 0;
        for (size_t i = 0; i < len; i++)
            v = (v << 4) | (unsigned)hex_nibble((unsigned char)p[i]);
        if (is_short) *is_short = true;
        return (uint16_t)v;
    }

    /* 128-bit canonical form: lower-case and check against SIG base. */
    if (len >= 36)
    {
        char lower[40];
        size_t copy = len < sizeof(lower) - 1 ? len : sizeof(lower) - 1;
        for (size_t i = 0; i < copy; i++) lower[i] = (char)tolower((unsigned char)p[i]);
        lower[copy] = '\0';
        if (is_sig_base(lower))
        {
            unsigned v = 0;
            for (int i = 4; i < 8; i++)
            {
                int n = hex_nibble((unsigned char)lower[i]);
                if (n < 0) return 0;
                v = (v << 4) | (unsigned)n;
            }
            if (is_short) *is_short = true;
            return (uint16_t)v;
        }
    }
    return 0;
}

/* ---- Service UUIDs (selected) ---- */
static const uuid_name_t SERVICE_NAMES[] = {
    { 0x1800, "Generic Access" },
    { 0x1801, "Generic Attr" },
    { 0x180A, "Device Info" },
    { 0x180D, "Heart Rate" },
    { 0x180F, "Battery" },
    { 0x1810, "Blood Pressure" },
    { 0x1812, "HID" },
    { 0x1813, "Scan Param" },
    { 0x1816, "Cycling Speed" },
    { 0x1818, "Cycling Power" },
    { 0x1819, "Location/Nav" },
    { 0x181A, "Environmental" },
    { 0x181B, "Body Composition" },
    { 0x181C, "User Data" },
    { 0x181D, "Weight Scale" },
    { 0x181E, "Bond Mgmt" },
    { 0x181F, "Continuous Glucose" },
    { 0x1822, "Pulse Oximeter" },
    { 0x1826, "Fitness Machine" },
    { 0x1827, "Mesh Provisioning" },
    { 0x1828, "Mesh Proxy" },
    /* 16-bit allocations from the GATT spec / member registrations */
    { 0xFEAA, "Eddystone" },
    { 0xFEED, "Tile" },
    { 0xFE13, "HomeKit" },
    { 0xFE9F, "Google" },
    { 0xFD6F, "Exposure Notif" },
    { 0xFD43, "Apple FindMy" },
    { 0xFE17, "Garmin" },
    { 0xFE2C, "Google Cast" },
    { 0xFD5A, "Nordic UART" },
    { 0xFEF3, "Google Fast Pair" },
    { 0xFCD2, "Allterco Shelly" },
    { 0xFFE0, "HM-10/clone" },
};

const char *m1_ble_service_name(uint16_t uuid16)
{
    for (size_t i = 0; i < sizeof(SERVICE_NAMES) / sizeof(SERVICE_NAMES[0]); i++)
        if (SERVICE_NAMES[i].uuid16 == uuid16) return SERVICE_NAMES[i].name;
    return NULL;
}

/* ---- Characteristic UUIDs (selected; focus on Device Information +
 * common readable ones we want to surface during deep-probe) ---- */
static const uuid_name_t CHAR_NAMES[] = {
    /* GAP service */
    { 0x2A00, "Device Name" },
    { 0x2A01, "Appearance" },
    { 0x2A04, "PPCP" },
    { 0x2AA6, "Address Resolution" },
    /* Device Information Service */
    { 0x2A23, "System ID" },
    { 0x2A24, "Model #" },
    { 0x2A25, "Serial #" },
    { 0x2A26, "Firmware Rev" },
    { 0x2A27, "Hardware Rev" },
    { 0x2A28, "Software Rev" },
    { 0x2A29, "Manufacturer" },
    { 0x2A2A, "IEEE Reg Cert" },
    { 0x2A50, "PnP ID" },
    /* Battery */
    { 0x2A19, "Battery Level" },
    /* Heart Rate */
    { 0x2A37, "HR Measurement" },
    { 0x2A38, "HR Body Sensor" },
    /* HID */
    { 0x2A4A, "HID Info" },
    { 0x2A4B, "HID Report Map" },
    { 0x2A4C, "HID Ctrl Pt" },
    { 0x2A4D, "HID Report" },
    /* Environmental */
    { 0x2A6E, "Temperature" },
    { 0x2A6F, "Humidity" },
    { 0x2A6D, "Pressure" },
    /* CTS */
    { 0x2A2B, "Current Time" },
    { 0x2A0F, "Local Time" },
};

const char *m1_ble_characteristic_name(uint16_t uuid16)
{
    for (size_t i = 0; i < sizeof(CHAR_NAMES) / sizeof(CHAR_NAMES[0]); i++)
        if (CHAR_NAMES[i].uuid16 == uuid16) return CHAR_NAMES[i].name;
    return NULL;
}

void m1_ble_uuid_describe(const char *uuid_str, bool is_characteristic,
                          char *out, size_t out_size)
{
    if (!out || out_size == 0) return;
    if (!uuid_str || !*uuid_str)
    {
        snprintf(out, out_size, "?");
        return;
    }
    bool is_short = false;
    uint16_t u16 = m1_ble_uuid16_from_str(uuid_str, &is_short);
    const char *name = NULL;
    if (is_short)
        name = is_characteristic
                 ? m1_ble_characteristic_name(u16)
                 : m1_ble_service_name(u16);
    if (name)
        snprintf(out, out_size, "%s", name);
    else if (is_short)
        snprintf(out, out_size, "0x%04X", (unsigned)u16);
    else if (strlen(uuid_str) >= 8)
        snprintf(out, out_size, "%.8s..", uuid_str);
    else
        snprintf(out, out_size, "%s", uuid_str);
}
