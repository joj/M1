/* See COPYING.txt for license details. */
/*
 * m1_ir_mass.h
 *
 * "Mass Off" — walk every Power/Off code in the universal-remote
 * database file for a given device type and transmit each one in
 * sequence. Defeats virtually every consumer TV/audio receiver/
 * projector/AC in a single button press.
 *
 * The work is done in m1_ir_mass.c (this header just exposes one
 * entry point per device type and is wired into the menu by
 * m1_csrc/m1_menu.c).
 *
 * M1 Project
 */

#ifndef M1_IR_MASS_H_
#define M1_IR_MASS_H_

void infrared_mass_off_tv(void);
void infrared_mass_off_audio(void);
void infrared_mass_off_projector(void);
void infrared_mass_off_ac(void);

#endif /* M1_IR_MASS_H_ */
