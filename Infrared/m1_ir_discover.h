/* See COPYING.txt for license details. */
/*
 * m1_ir_discover.h
 *
 * "Find My Remote" flow — cycle every per-model .ir file under
 * /INFRARED/browse/<category>/, transmitting each file's Power code
 * (or Off, for ACs) until the user presses OK to lock the current
 * model. On lock:
 *
 *   - the locked file is copied to /INFRARED/saved/last_<cat>.ir
 *   - the existing button grid is shown for that file
 *
 * Companion flow "Last": directly load /INFRARED/saved/last_<cat>.ir
 * and present the button grid (skip the cycling).
 *
 * M1 Project
 */

#ifndef M1_IR_DISCOVER_H_
#define M1_IR_DISCOVER_H_

void infrared_discover_tv(void);
void infrared_discover_audio(void);
void infrared_discover_projector(void);
void infrared_discover_ac(void);

void infrared_last_tv(void);
void infrared_last_audio(void);
void infrared_last_projector(void);
void infrared_last_ac(void);

#endif /* M1_IR_DISCOVER_H_ */
