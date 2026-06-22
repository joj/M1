/* See COPYING.txt for license details. */
/*
*
* m1_ir_remotes.h
*
* Header for infrared remotes
*
* M1 Project
*
*/

#ifndef M1_IR_REMOTES_H_
#define M1_IR_REMOTES_H_

uint8_t ir_remote_file_load(void);
void infrared_universal_tv_remotes(void);
void infrared_universal_audio_remotes(void);
void infrared_universal_projector_remotes(void);
void infrared_universal_ac_remotes(void);

void ir_set_db_path_override(const char *path);
void infrared_universal_for_override(uint8_t remote_type);

#endif /* M1_IR_REMOTES_H_ */
