/* See COPYING.txt for license details. */
/*
 * m1_ir_discover.c — "Find My Remote" discovery + Last entry points.
 *
 * Discover flow:
 *   1. Build a sorted listing of all per-model .ir files under
 *      /INFRARED/browse/<category>/. Each entry is one specific
 *      remote model (e.g. Samsung_AA59-00714A.ir).
 *   2. Cycle through the list. For each file, transmit just its
 *      Power code (Off for AC). Brief pause + redraw.
 *   3. Watch the keypad concurrently:
 *      - OK: lock the currently-firing model. Copy that file to
 *        /INFRARED/saved/last_<cat>.ir, then hand control off to the
 *        existing button-grid UI in m1_ir_remotes.c (via
 *        ir_set_db_path_override + infrared_universal_for_override).
 *      - BACK: abort, return to menu.
 *   4. When the listing is exhausted, show "End of list" and wait
 *      for BACK.
 *
 * Last flow:
 *   - Just point the override at /INFRARED/saved/last_<cat>.ir and
 *     call infrared_universal_for_override(). Errors if no Last has
 *     been saved.
 *
 * M1 Project
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "stm32h5xx_hal.h"
#include "main.h"
#include "ff.h"
#include "m1_log_debug.h"
#include "m1_display.h"
#include "m1_system.h"
#include "m1_sdcard.h"
#include "m1_infrared.h"
#include "m1_ir_signals.h"
#include "m1_ir_remotes.h"
#include "m1_ir_discover.h"
#include "irmp.h"
#include "irsnd.h"

#define M1_LOGDB_TAG          "IR_DISC"

#define DISCOVER_PAUSE_MS     250    /* between successive model attempts */
#define ROW_H                 (M1_GUI_FONT_HEIGHT)

#define FN_POWER              0       /* Power index in TV/Audio/Projector,
                                         Off index in AC — both 0 in
                                         ir_remote_*_functions[]. */

/* Persistent path buffers used as the override target. Held static so
 * the pointer passed to ir_set_db_path_override() remains valid for
 * the lifetime of the button-grid call. */
static char s_locked_path[80];

/* From m1_ir_remotes.c — extern declarations to access the shared
 * payload + protocol mapping and trigger transmission. */
extern uint8_t ir_remote_function_data_read(uint8_t remote_type, uint8_t function_type);
extern uint8_t ir_payload_info_get(S_M1_IR_Payload_t *payload);
extern void    ir_remote_file_deinit(void);
extern uint8_t ir_remote_file_header_check(const char *remotes_filename, uint8_t remote_type);
extern S_M1_IR_Payload_t ir_universal_payload;

extern const struct S_M1_IR_Protocol_Mapping {
	const char *protocol_name;
	uint8_t protocol_number;
} ir_protocols_mapping_table[];

#define IR_PROTOCOLS_MAPPING_MAX  15

static const char *browse_dir_for(uint8_t rt)
{
	switch (rt) {
		case IR_REMOTETYPE_TV:        return "0:/INFRARED/browse/TVs";
		case IR_REMOTETYPE_AUDIO:     return "0:/INFRARED/browse/Audio";
		case IR_REMOTETYPE_PROJECTOR: return "0:/INFRARED/browse/Projectors";
		case IR_REMOTETYPE_AC:        return "0:/INFRARED/browse/ACs";
		default:                      return NULL;
	}
}

static const char *last_path_for(uint8_t rt)
{
	switch (rt) {
		case IR_REMOTETYPE_TV:        return "0:/INFRARED/saved/last_tv.ir";
		case IR_REMOTETYPE_AUDIO:     return "0:/INFRARED/saved/last_audio.ir";
		case IR_REMOTETYPE_PROJECTOR: return "0:/INFRARED/saved/last_projector.ir";
		case IR_REMOTETYPE_AC:        return "0:/INFRARED/saved/last_ac.ir";
		default:                      return NULL;
	}
}

static const char *header_for(uint8_t rt)
{
	switch (rt) {
		case IR_REMOTETYPE_TV:        return "Find My TV";
		case IR_REMOTETYPE_AUDIO:     return "Find My Audio";
		case IR_REMOTETYPE_PROJECTOR: return "Find My Proj";
		case IR_REMOTETYPE_AC:        return "Find My AC";
		default:                      return "Find My Remote";
	}
}

static void draw_screen(const char *header, const char *line1,
                        const char *line2, const char *bottom)
{
	m1_u8g2_firstpage();
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
	u8g2_DrawStr(&m1_u8g2, 2, ROW_H, header);
	int y = 14 + ROW_H;
	if (line1) { u8g2_DrawStr(&m1_u8g2, 2, y, line1); y += ROW_H; }
	if (line2) { u8g2_DrawStr(&m1_u8g2, 2, y, line2); y += ROW_H; }
	if (bottom) u8g2_DrawStr(&m1_u8g2, 2, y + ROW_H, bottom);
	m1_u8g2_nextpage();
}

/* Transmit the first Power/Off entry from a specific .ir file path.
 * Returns true on success. */
static bool transmit_power_from_file(const char *path, uint8_t remote_type)
{
	if (ir_remote_file_header_check(path, remote_type)) return false;
	if (ir_remote_function_data_read(remote_type, FN_POWER)) { ir_remote_file_deinit(); return false; }
	if (ir_payload_info_get(&ir_universal_payload))         { ir_remote_file_deinit(); return false; }

	IRMP_DATA ir_remote_data = { 0 };
	if (ir_universal_payload.ir_data_type == IR_DATATYPE_PARSED)
	{
		int i;
		for (i = 0; i < IR_PROTOCOLS_MAPPING_MAX; i++)
			if (!strcmp((const char *)ir_universal_payload.pprotocol,
			            ir_protocols_mapping_table[i].protocol_name))
				break;
		if (i >= IR_PROTOCOLS_MAPPING_MAX) { ir_remote_file_deinit(); return false; }
		ir_remote_data.protocol  = ir_protocols_mapping_table[i].protocol_number;
		ir_remote_data.address   = ir_universal_payload.address;
		ir_remote_data.command   = ir_universal_payload.command;
		ir_remote_data.duty_cycle = ir_universal_payload.duty_cycle;
		ir_remote_data.frequency = ir_universal_payload.frequency;
		ir_remote_data.flags     = 1;
	}
	else if (ir_universal_payload.ir_data_type == IR_DATATYPE_RAW)
	{
		ir_remote_data.protocol  = IRMP_RAW_PROTOCOL;
		ir_remote_data.duty_cycle = ir_universal_payload.duty_cycle;
		ir_remote_data.frequency = ir_universal_payload.frequency;
		ir_remote_data.flags     = 1;
	}
	else { ir_remote_file_deinit(); return false; }

	irsnd_generate_tx_data(ir_remote_data);
	infrared_transmit(1, ir_remote_data.protocol);
	ir_remote_file_deinit();
	return true;
}

/* Copy a small file (≤4 KB — one model entry). */
static bool copy_file(const char *src, const char *dst)
{
	FIL fs, fd;
	if (m1_sdcard_get_status() != SD_access_OK) return false;
	if (f_open(&fs, src, FA_OPEN_EXISTING | FA_READ) != FR_OK) return false;
	/* Create parent dir best-effort. */
	f_mkdir("0:/INFRARED/saved");
	if (f_open(&fd, dst, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) { f_close(&fs); return false; }
	uint8_t buf[256];
	UINT br, bw;
	bool ok = true;
	while (f_read(&fs, buf, sizeof(buf), &br) == FR_OK && br > 0)
	{
		if (f_write(&fd, buf, br, &bw) != FR_OK || bw != br) { ok = false; break; }
	}
	f_close(&fs);
	f_close(&fd);
	return ok;
}

/* Hand off to the existing universal-remote button grid, pointed at
 * the locked file. The override is held in s_locked_path so the
 * pointer stays valid during the grid run. */
static void handoff_to_grid(uint8_t remote_type, const char *path)
{
	snprintf(s_locked_path, sizeof(s_locked_path), "%s", path);
	ir_set_db_path_override(s_locked_path);
	infrared_universal_for_override(remote_type);
	ir_set_db_path_override(NULL);
}

static void discover_run(uint8_t remote_type)
{
	if (remote_type >= IR_REMOTETYPE_UNKNOWN) return;
	const char *header = header_for(remote_type);
	const char *dir    = browse_dir_for(remote_type);

	if (m1_sdcard_get_status() != SD_access_OK)
	{
		draw_screen(header, "No SD card", "", "BACK to return");
		goto wait_back;
	}

	DIR d;
	if (f_opendir(&d, dir) != FR_OK)
	{
		draw_screen(header, "No browse dir:", dir + 2, "BACK to return");
		goto wait_back;
	}

	infrared_encode_sys_init();
	draw_screen(header, "Cycling models...", "OK = lock", "BACK = abort");

	FILINFO fi;
	bool aborted = false;
	bool locked = false;
	char locked_path[80] = "";
	uint32_t fired = 0;

	while (!aborted && !locked)
	{
		FRESULT rr = f_readdir(&d, &fi);
		if (rr != FR_OK || fi.fname[0] == 0)
			break;  /* end of directory */
		if (fi.fattrib & (AM_DIR | AM_HID | AM_SYS))
			continue;
		/* Filter on .ir extension. */
		size_t n = strlen(fi.fname);
		if (n < 4 || strcasecmp(&fi.fname[n - 3], ".ir") != 0)
			continue;

		char path[100];
		snprintf(path, sizeof(path), "%s/%s", dir, fi.fname);

		fired++;
		char line[24];
		snprintf(line, sizeof(line), "%lu: %.18s",
		         (unsigned long)fired, fi.fname);
		draw_screen(header, line, "OK=lock BACK=quit", NULL);

		(void)transmit_power_from_file(path, remote_type);

		/* Wait DISCOVER_PAUSE_MS for either:
		 *  - a Q_EVENT_IRRED_TX (current transmission complete) -> advance
		 *  - a keypad event (OK locks, BACK aborts)
		 */
		TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(DISCOVER_PAUSE_MS);
		while (!aborted && !locked && xTaskGetTickCount() < deadline)
		{
			S_M1_Main_Q_t q;
			TickType_t now = xTaskGetTickCount();
			TickType_t to = (deadline > now) ? (deadline - now) : 0;
			if (to == 0) break;
			if (xQueueReceive(main_q_hdl, &q, to) != pdTRUE) break;
			if (q.q_evt_type == Q_EVENT_KEYPAD)
			{
				S_M1_Buttons_Status b;
				if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
				if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { aborted = true; break; }
				if (b.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
				{
					locked = true;
					snprintf(locked_path, sizeof(locked_path), "%s", path);
					break;
				}
			}
			/* Q_EVENT_IRRED_TX (transmit complete) — drain so we don't
			 * spin, but otherwise no action. */
		}
	}
	f_closedir(&d);

	if (ir_ota_data_tx_active)
		m1_ir_ota_frame_repeat_handler(IRMP_UNKNOWN_PROTOCOL);

	if (aborted)
	{
		xQueueReset(main_q_hdl);
		return;
	}

	if (!locked)
	{
		draw_screen(header, "End of list.", "No model locked.", "BACK to return");
		goto wait_back;
	}

	/* Persist Last + drop into the button grid. */
	(void)copy_file(locked_path, last_path_for(remote_type));
	handoff_to_grid(remote_type, locked_path);
	return;

wait_back:
	{
		S_M1_Main_Q_t q;
		S_M1_Buttons_Status b;
		while (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) == pdTRUE)
		{
			if (q.q_evt_type != Q_EVENT_KEYPAD) continue;
			if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
			if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
		}
		xQueueReset(main_q_hdl);
	}
}

static void last_run(uint8_t remote_type)
{
	const char *path = last_path_for(remote_type);
	const char *header = header_for(remote_type);
	FIL fp;
	if (m1_sdcard_get_status() != SD_access_OK
	    || f_open(&fp, path, FA_OPEN_EXISTING | FA_READ) != FR_OK)
	{
		draw_screen(header, "No saved Last.", "Use Find My first.", "BACK to return");
		S_M1_Main_Q_t q;
		S_M1_Buttons_Status b;
		while (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) == pdTRUE)
		{
			if (q.q_evt_type != Q_EVENT_KEYPAD) continue;
			if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
			if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
		}
		return;
	}
	f_close(&fp);
	handoff_to_grid(remote_type, path);
}

void infrared_discover_tv(void)        { discover_run(IR_REMOTETYPE_TV); }
void infrared_discover_audio(void)     { discover_run(IR_REMOTETYPE_AUDIO); }
void infrared_discover_projector(void) { discover_run(IR_REMOTETYPE_PROJECTOR); }
void infrared_discover_ac(void)        { discover_run(IR_REMOTETYPE_AC); }

void infrared_last_tv(void)        { last_run(IR_REMOTETYPE_TV); }
void infrared_last_audio(void)     { last_run(IR_REMOTETYPE_AUDIO); }
void infrared_last_projector(void) { last_run(IR_REMOTETYPE_PROJECTOR); }
void infrared_last_ac(void)        { last_run(IR_REMOTETYPE_AC); }
