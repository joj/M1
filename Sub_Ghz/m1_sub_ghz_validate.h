/* See COPYING.txt for license details. */
/*
 * m1_sub_ghz_validate.h
 *
 * Verdict engine for the Sub-GHz "Validate" feature. Given a
 * previously-saved capture and a freshly-received one, decide whether
 * they came from the same remote and (if rolling-code) whether the
 * counter advanced.
 *
 * Pure logic — no FatFs, no radio, no STM32 headers — so the verdict
 * tables can be exercised on the host.
 *
 * M1 Project
 */

#ifndef M1_SUB_GHZ_VALIDATE_H_
#define M1_SUB_GHZ_VALIDATE_H_

#include <stdbool.h>
#include <stdint.h>
#include "m1_sub_ghz_decenc.h"

typedef enum {
    /* Bit-for-bit identical (or the rolling-code counter happens to be
     * the same — which for a real remote shouldn't happen but does
     * indicate a replay-able fixed-code device). */
    M1_VALIDATE_MATCH_IDENTICAL = 0,
    /* Same protocol + same serial; rolling counter advanced. The
     * remote is the same physical unit. Counter delta is reported via
     * out_counter_delta (always > 0 for a forward press; we also
     * accept counters that "rolled over" — see implementation). */
    M1_VALIDATE_MATCH_ROLLING,
    /* Same protocol but different serial — sibling remote or another
     * unit of the same chip family. */
    M1_VALIDATE_MATCH_FAMILY,
    /* Different protocol, or one of the two failed to decode, or raw
     * captures differ beyond tolerance. */
    M1_VALIDATE_NO_MATCH,
} m1_validate_verdict_t;

/* Decode the verdict between two captures. `saved` and `fresh` are the
 * decoded summaries produced by subghz_decenc_read(). On
 * M1_VALIDATE_MATCH_ROLLING, *out_counter_delta is set to the forward
 * counter advance (fresh - saved, wrapped to a sensible 32-bit
 * window). For other verdicts the delta is left untouched.
 *
 * Either argument may be NULL — that yields M1_VALIDATE_NO_MATCH.
 */
m1_validate_verdict_t m1_subghz_validate(const SubGHz_Dec_Info_t *saved,
                                         const SubGHz_Dec_Info_t *fresh,
                                         int32_t *out_counter_delta);

/* Short user-facing label for a verdict. Never NULL. */
const char *m1_subghz_validate_label(m1_validate_verdict_t v);

#endif /* M1_SUB_GHZ_VALIDATE_H_ */
