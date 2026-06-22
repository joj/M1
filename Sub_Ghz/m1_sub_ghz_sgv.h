/* See COPYING.txt for license details. */
/*
 * m1_sub_ghz_sgv.h
 *
 * Read/write helpers for the M1's `.sgv` files — the tiny metadata
 * file written by the Sub-GHz Record screen alongside the raw `.sgh`
 * whenever a decode succeeded. Used by the Validate screen as the
 * "what should the next press look like?" reference.
 *
 * On-disk format (newline-terminated key:value):
 *
 *   Filetype: M1 SubGHz Validate
 *   Version: 1
 *   Frequency: 433920000
 *   Modulation: OOK
 *   Protocol: 0           # PRINCETON, SECURITY_PLUS_20, ...
 *   BitLen: 24
 *   Key: 0xABCDEF
 *   Serial: 0x00000000
 *   Rolling: 0x00000000
 *   Button: 0
 *
 * Reads the file straight via FatFs; writes likewise. Tiny files (~150
 * bytes), no streaming buffer needed.
 *
 * Pure logic for the format itself is host-testable via the serialize/
 * parse functions which work on strings.
 *
 * M1 Project
 */

#ifndef M1_SUB_GHZ_SGV_H_
#define M1_SUB_GHZ_SGV_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "m1_sub_ghz_decenc.h"

#define M1_SGV_FILE_EXTENSION   ".sgv"

/* Serialize a SubGHz_Dec_Info_t (plus modulation/freq the file load
 * step lost) into a NUL-terminated `.sgv` body. Returns the length
 * written (excluding NUL), or 0 on overflow / bad args.
 */
size_t m1_sgv_serialize(const SubGHz_Dec_Info_t *info,
                        uint32_t frequency_hz,
                        const char *modulation_text,
                        char *out, size_t out_size);

/* Parse a `.sgv` body. Populates `out_info`, and optionally
 * `out_frequency_hz` and `out_modulation` (string written into a
 * caller-supplied buffer). Returns true on success.
 */
bool m1_sgv_parse(const char *body,
                  SubGHz_Dec_Info_t *out_info,
                  uint32_t *out_frequency_hz,
                  char *out_modulation, size_t out_modulation_size);

/* On-device convenience: write `.sgv` next to a `.sgh` path. The
 * caller supplies the `.sgh` path (e.g. "0:/SUBGHZ/sghz_433_OOK_001.sgh");
 * we replace the extension. Returns true on success. */
bool m1_sgv_write_for_path(const char *sgh_path,
                           const SubGHz_Dec_Info_t *info,
                           uint32_t frequency_hz,
                           const char *modulation_text);

/* On-device convenience: read a `.sgv` file from SD into the supplied
 * struct. Returns true on success. */
bool m1_sgv_read_file(const char *sgv_path,
                      SubGHz_Dec_Info_t *out_info,
                      uint32_t *out_frequency_hz,
                      char *out_modulation, size_t out_modulation_size);

#endif /* M1_SUB_GHZ_SGV_H_ */
