/* See COPYING.txt for license details. */
/*
 * m1_ir_export.c — write a captured IR frame to an SD-card .ir file in
 * Flipper-Zero compatible format.
 *
 * One file per learned signal. We use a timestamp (tick) in the filename
 * so successive captures don't clobber each other:
 *
 *   /INFRARED/exports/learned_<tick>.ir
 *
 * The file contains a single record with the canonical Flipper header.
 * Protocols that the M1's IRMP decoder names match Flipper's names for
 * the common cases (NEC, NECext, Samsung32, RC5, RC6, Sony SIRC*, RCA,
 * Pioneer, Kaseikyo). Anything else falls back to a `data:` raw stub
 * (which we can't fill in from a parsed frame, so we instead store
 * protocol + address + command and tag it as `type: parsed` regardless).
 *
 * M1 Project
 */

#include "m1_ir_export.h"

#include <stdio.h>
#include <string.h>

#include "stm32h5xx_hal.h"
#include "ff.h"
#include "m1_log_debug.h"
#include "m1_sdcard.h"

#define M1_LOGDB_TAG     "IR_EXP"
#define EXPORT_DIR       "0:/INFRARED/exports"

/* Map the IRMP protocol enum to a Flipper-Zero `.ir` protocol string.
 * Mirror of ir_protocols_mapping_table[] in Infrared/m1_ir_remotes.c.
 * Returns NULL if the protocol isn't representable.
 */
static const char *protocol_to_flipper(uint8_t proto)
{
    /* Numeric values come from Infrared/irmp-irsnd/irmp.h enum order. */
    switch (proto)
    {
        case 1:  return "SIRC";       /* IRMP_SIRCS_PROTOCOL */
        case 2:  return "NEC";        /* IRMP_NEC_PROTOCOL */
        case 3:  return "Samsung32";  /* IRMP_SAMSUNG_PROTOCOL — approx */
        case 5:  return "Kaseikyo";   /* IRMP_KASEIKYO_PROTOCOL */
        case 6:  return "NECext";     /* IRMP_NEC42_PROTOCOL — approx mapping */
        case 7:  return "RC5";        /* IRMP_RC5_PROTOCOL */
        case 8:  return "RC6";        /* IRMP_RC6_PROTOCOL */
        case 9:  return "RCA";        /* IRMP_RCA_PROTOCOL */
        case 12: return "Pioneer";    /* IRMP_PIONEER_PROTOCOL */
        default: return NULL;
    }
}

/* Format a 4-byte little-endian value as "XX XX XX XX". */
static void fmt_le_hex(uint32_t v, char *out)
{
    snprintf(out, 16, "%02X %02X %02X %02X",
             (unsigned)(v & 0xFF),
             (unsigned)((v >> 8) & 0xFF),
             (unsigned)((v >> 16) & 0xFF),
             (unsigned)((v >> 24) & 0xFF));
}

bool m1_ir_export_learned(const IRMP_DATA *irmp_data, const char *name)
{
    if (!irmp_data) return false;
    if (m1_sdcard_get_status() != SD_access_OK)
    {
        M1_LOG_I(M1_LOGDB_TAG, "SD not ready; export skipped\n\r");
        return false;
    }

    f_mkdir("0:/INFRARED");
    f_mkdir(EXPORT_DIR);

    char path[80];
    snprintf(path, sizeof(path),
             EXPORT_DIR "/learned_%010lu.ir",
             (unsigned long)HAL_GetTick());

    FIL fp;
    if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        M1_LOG_E(M1_LOGDB_TAG, "open %s failed\n\r", path);
        return false;
    }

    const char *proto = protocol_to_flipper(irmp_data->protocol);
    char addr_hex[16], cmd_hex[16];
    fmt_le_hex(irmp_data->address, addr_hex);
    fmt_le_hex(irmp_data->command, cmd_hex);

    char body[256];
    int n;
    if (proto)
    {
        n = snprintf(body, sizeof(body),
                     "Filetype: IR signals file\nVersion: 1\n#\n"
                     "name: %s\n"
                     "type: parsed\n"
                     "protocol: %s\n"
                     "address: %s\n"
                     "command: %s\n#\n",
                     name && *name ? name : "Learned",
                     proto, addr_hex, cmd_hex);
    }
    else
    {
        /* Unknown protocol — record what we have for diagnostic purposes;
         * Flipper will reject this entry but the M1 can replay it via the
         * generic IRMP path if loaded back. */
        n = snprintf(body, sizeof(body),
                     "Filetype: IR signals file\nVersion: 1\n#\n"
                     "name: %s\n"
                     "type: parsed\n"
                     "protocol: UNKNOWN_0x%02X\n"
                     "address: %s\n"
                     "command: %s\n#\n",
                     name && *name ? name : "Learned",
                     irmp_data->protocol, addr_hex, cmd_hex);
    }

    UINT bw = 0;
    if (n > 0) f_write(&fp, body, (UINT)n, &bw);
    f_close(&fp);

    if ((int)bw != n)
    {
        M1_LOG_E(M1_LOGDB_TAG, "short write to %s\n\r", path);
        return false;
    }
    M1_LOG_I(M1_LOGDB_TAG, "exported %s\n\r", path);
    return true;
}
