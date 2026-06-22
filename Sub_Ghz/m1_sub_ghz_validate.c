/* See COPYING.txt for license details. */
/*
 * m1_sub_ghz_validate.c — verdict engine.
 *
 * Decision tree (in order, first match wins):
 *
 *   1. If either input is NULL, or protocols differ -> NO MATCH.
 *   2. If both report a rolling-capable protocol (serial != 0) and
 *      serials differ -> FAMILY (sibling remote on the same chip).
 *   3. If both report a rolling-capable protocol and serials match:
 *      a. If rolling counters match exactly -> IDENTICAL.
 *      b. If fresh.rolling > saved.rolling -> ROLLING, delta is the
 *         difference.
 *      c. If fresh.rolling < saved.rolling -> ROLLING, delta is
 *         computed as a wrapped 32-bit advance, but only if the wrap
 *         distance is smaller than the reverse distance; otherwise
 *         IDENTICAL (i.e. real replay) is reported.
 *   4. Otherwise it's a fixed-code protocol — compare `key` (and bit
 *      length) bit-for-bit. Equal -> IDENTICAL, unequal -> NO MATCH.
 *
 * The "raw" path (no decode) is intentionally NOT supported here —
 * comparing raw pulse trains is noisy and out of scope for v1. The
 * caller should fall back to NO_MATCH when both inputs lack a decoded
 * protocol.
 */

#include "m1_sub_ghz_validate.h"

#include <stdint.h>
#include <stdlib.h>

/* Heuristic threshold for the rolling-counter wrap detection. If the
 * "forward wrap" distance is shorter than this, we still call it a
 * rolling advance — handy when the counter is near its rollover. */
#define WRAP_FORWARD_WINDOW   0x10000000u  /* ~2^28; most rolling codes
                                              have plenty of headroom */

m1_validate_verdict_t m1_subghz_validate(const SubGHz_Dec_Info_t *saved,
                                         const SubGHz_Dec_Info_t *fresh,
                                         int32_t *out_counter_delta)
{
    if (!saved || !fresh) return M1_VALIDATE_NO_MATCH;

    /* Protocol must match. */
    if (saved->protocol != fresh->protocol) return M1_VALIDATE_NO_MATCH;

    /* Treat a non-zero serial as the marker for "this protocol has a
     * rolling counter". Currently only Security+ 2.0 does in the M1
     * decoders, but the rule is open-ended. */
    bool is_rolling = (saved->serial != 0) || (fresh->serial != 0);

    if (is_rolling)
    {
        if (saved->serial != fresh->serial)
            return M1_VALIDATE_MATCH_FAMILY;

        if (saved->rolling == fresh->rolling)
            return M1_VALIDATE_MATCH_IDENTICAL;

        uint32_t fwd, rev;
        if (fresh->rolling > saved->rolling)
        {
            fwd = fresh->rolling - saved->rolling;
            rev = (uint32_t)((uint64_t)0x100000000ULL
                             - (uint64_t)(fresh->rolling - saved->rolling));
        }
        else
        {
            fwd = (uint32_t)((uint64_t)0x100000000ULL
                             - (uint64_t)(saved->rolling - fresh->rolling));
            rev = saved->rolling - fresh->rolling;
        }
        /* Prefer the smaller absolute advance. If it's reverse, that
         * means the saved counter is ahead of the fresh — which is
         * what a replay looks like (older capture being re-emitted).
         * We surface that as IDENTICAL because no real button press
         * can produce a backward counter. */
        if (rev <= fwd)
        {
            return M1_VALIDATE_MATCH_IDENTICAL;
        }
        if (fwd < WRAP_FORWARD_WINDOW)
        {
            if (out_counter_delta) *out_counter_delta = (int32_t)fwd;
            return M1_VALIDATE_MATCH_ROLLING;
        }
        /* Counter advanced by an implausibly large amount — probably a
         * different physical remote whose serial happens to collide. */
        return M1_VALIDATE_MATCH_FAMILY;
    }

    /* Fixed-code protocol: bit-exact key compare. Some decoders zero-
     * pad the high bits, so compare bit_len too. */
    if (saved->key == fresh->key && saved->bit_len == fresh->bit_len)
        return M1_VALIDATE_MATCH_IDENTICAL;

    return M1_VALIDATE_NO_MATCH;
}

const char *m1_subghz_validate_label(m1_validate_verdict_t v)
{
    switch (v)
    {
        case M1_VALIDATE_MATCH_IDENTICAL: return "MATCH (identical)";
        case M1_VALIDATE_MATCH_ROLLING:   return "MATCH (rolling)";
        case M1_VALIDATE_MATCH_FAMILY:    return "MATCH (same family)";
        case M1_VALIDATE_NO_MATCH:        return "NO MATCH";
        default:                          return "?";
    }
}
