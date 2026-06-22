/* See COPYING.txt for license details. */
/*
 * m1_ir_export.h
 *
 * Persist a captured IRMP frame to an SD card file in Flipper-Zero
 * compatible `.ir` format. The file can be dropped straight into a
 * Flipper or back into the M1's universal-remotes database after
 * trimming.
 *
 * Output path:
 *   /INFRARED/exports/learned_<tick>.ir
 *
 * M1 Project
 */

#ifndef M1_IR_EXPORT_H_
#define M1_IR_EXPORT_H_

#include <stdbool.h>
#include "irmp.h"

/* Append (or create + write) a Flipper-format record for one captured
 * IR frame. `name` is the human-readable function name (e.g. "Power").
 * Returns true on success.
 */
bool m1_ir_export_learned(const IRMP_DATA *irmp_data, const char *name);

#endif /* M1_IR_EXPORT_H_ */
