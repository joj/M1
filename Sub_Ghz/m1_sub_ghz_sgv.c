/* See COPYING.txt for license details. */
/*
 * m1_sub_ghz_sgv.c — serializer / parser / FatFs I/O for `.sgv` files.
 *
 * See m1_sub_ghz_sgv.h for the on-disk format.
 *
 * The serialize and parse functions are pure-logic and host-testable.
 * The two file-I/O wrappers (m1_sgv_write_for_path, m1_sgv_read_file)
 * are guarded behind FatFs include only when not in host-test mode.
 *
 * M1 Project
 */

#include "m1_sub_ghz_sgv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M1_HOST_TEST_SGV
#  include "ff.h"
#endif

size_t m1_sgv_serialize(const SubGHz_Dec_Info_t *info,
                        uint32_t frequency_hz,
                        const char *modulation_text,
                        char *out, size_t out_size)
{
    if (!info || !out || out_size == 0) return 0;
    int n = snprintf(out, out_size,
        "Filetype: M1 SubGHz Validate\n"
        "Version: 1\n"
        "Frequency: %lu\n"
        "Modulation: %s\n"
        "Protocol: %u\n"
        "BitLen: %u\n"
        "Key: 0x%llX\n"
        "Serial: 0x%08lX\n"
        "Rolling: 0x%08lX\n"
        "Button: %u\n",
        (unsigned long)frequency_hz,
        modulation_text ? modulation_text : "OOK",
        (unsigned)info->protocol,
        (unsigned)info->bit_len,
        (unsigned long long)info->key,
        (unsigned long)info->serial,
        (unsigned long)info->rolling,
        (unsigned)info->button_id);
    if (n <= 0 || (size_t)n >= out_size) return 0;
    return (size_t)n;
}

static const char *find_line(const char *body, const char *key)
{
    /* Match key at the start of a line, return the value-start pointer. */
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p)
    {
        if (strncmp(p, key, klen) == 0 && p[klen] == ' ')
            return p + klen + 1;
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return NULL;
}

static bool parse_ul(const char *body, const char *key, unsigned long long *out, int base)
{
    const char *v = find_line(body, key);
    if (!v) return false;
    char *end = NULL;
    *out = strtoull(v, &end, base);
    return end != v;
}

bool m1_sgv_parse(const char *body,
                  SubGHz_Dec_Info_t *out_info,
                  uint32_t *out_frequency_hz,
                  char *out_modulation, size_t out_modulation_size)
{
    if (!body || !out_info) return false;
    /* Sanity-check the magic. */
    if (strncmp(body, "Filetype: M1 SubGHz Validate", 28) != 0) return false;

    memset(out_info, 0, sizeof(*out_info));

    unsigned long long ull = 0;
    if (parse_ul(body, "Frequency:", &ull, 10))
    {
        if (out_frequency_hz) *out_frequency_hz = (uint32_t)ull;
    }
    if (parse_ul(body, "Protocol:", &ull, 10))   out_info->protocol = (uint16_t)ull;
    if (parse_ul(body, "BitLen:",   &ull, 10))   out_info->bit_len  = (uint16_t)ull;
    if (parse_ul(body, "Key:",      &ull, 0))    out_info->key      = (uint64_t)ull;
    if (parse_ul(body, "Serial:",   &ull, 0))    out_info->serial   = (uint32_t)ull;
    if (parse_ul(body, "Rolling:",  &ull, 0))    out_info->rolling  = (uint32_t)ull;
    if (parse_ul(body, "Button:",   &ull, 10))   out_info->button_id = (uint8_t)ull;

    if (out_modulation && out_modulation_size > 0)
    {
        out_modulation[0] = '\0';
        const char *m = find_line(body, "Modulation:");
        if (m)
        {
            size_t i = 0;
            while (i < out_modulation_size - 1 && m[i] && m[i] != '\n' && m[i] != '\r')
            {
                out_modulation[i] = m[i];
                i++;
            }
            out_modulation[i] = '\0';
        }
    }
    return true;
}

#ifndef M1_HOST_TEST_SGV

static void swap_ext(const char *src, char *dst, size_t dst_size, const char *new_ext)
{
    snprintf(dst, dst_size, "%s", src);
    char *dot = strrchr(dst, '.');
    if (dot) *dot = '\0';
    size_t n = strlen(dst);
    if (n + strlen(new_ext) < dst_size)
        strcpy(dst + n, new_ext);
}

bool m1_sgv_write_for_path(const char *sgh_path,
                           const SubGHz_Dec_Info_t *info,
                           uint32_t frequency_hz,
                           const char *modulation_text)
{
    if (!sgh_path || !info) return false;
    char path[160];
    swap_ext(sgh_path, path, sizeof(path), M1_SGV_FILE_EXTENSION);

    char body[256];
    size_t n = m1_sgv_serialize(info, frequency_hz, modulation_text,
                                body, sizeof(body));
    if (n == 0) return false;

    FIL fp;
    if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return false;
    UINT bw = 0;
    FRESULT wr = f_write(&fp, body, (UINT)n, &bw);
    f_close(&fp);
    return wr == FR_OK && bw == n;
}

bool m1_sgv_read_file(const char *sgv_path,
                      SubGHz_Dec_Info_t *out_info,
                      uint32_t *out_frequency_hz,
                      char *out_modulation, size_t out_modulation_size)
{
    if (!sgv_path) return false;
    FIL fp;
    if (f_open(&fp, sgv_path, FA_OPEN_EXISTING | FA_READ) != FR_OK)
        return false;
    char body[300] = {0};
    UINT br = 0;
    FRESULT rr = f_read(&fp, body, sizeof(body) - 1, &br);
    f_close(&fp);
    if (rr != FR_OK) return false;
    body[br] = '\0';
    return m1_sgv_parse(body, out_info, out_frequency_hz,
                        out_modulation, out_modulation_size);
}

#endif /* !M1_HOST_TEST_SGV */
