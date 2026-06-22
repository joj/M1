/* See COPYING.txt for license details. */
/*
 * m1_ir_discover.c — "Find My Remote" discovery + Last entry points.
 *
 * Reads a single `.pack` bundle per category from
 *   /INFRARED/browse/{tv,audio,projector,ac}.pack
 *
 * Pack format (LF-only throughout):
 *   "M1IR1\n"
 *   "<count>\n"
 *   <for each model:>
 *       "<model_name>\n"        (<= ~75 chars)
 *       "<length>\n"            (ASCII decimal payload size)
 *       <length bytes of standard .ir content>
 *
 * Discover flow:
 *   1. Open the .pack, validate magic, read model count.
 *   2. Pass 1 — walk every entry, collect unique brand prefixes
 *      (filename portion before the first '_'). Skip payloads.
 *   3. Show brand picker; user picks "<All brands>" or a specific
 *      brand. Sorted alphabetically.
 *   4. Pass 2 — re-walk. For each model whose brand matches:
 *        - stream payload into /INFRARED/.cycle.tmp.ir
 *        - transmit just its Power code (Off for AC)
 *        - poll keypad for 250 ms
 *           - OK: rename .cycle.tmp.ir -> /INFRARED/saved/last_<cat>.ir,
 *             hand off to the existing button-grid UI in m1_ir_remotes.c
 *             (via ir_set_db_path_override + infrared_universal_for_override).
 *           - BACK: abort.
 *   5. End-of-pack with no lock => "End of list", wait BACK.
 *
 * Last flow:
 *   Point the override at /INFRARED/saved/last_<cat>.ir if it exists
 *   and call infrared_universal_for_override(). Error if not present.
 *
 * Switching from per-model files to a single .pack collapses 749
 * tiny SD writes (on initial flashing) and 393 metadata reads
 * (on each discovery pass) into 4 fast sequential reads.
 *
 * M1 Project
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

#define DISCOVER_PAUSE_MS     250
#define ROW_H                 (M1_GUI_FONT_HEIGHT)
#define FN_POWER              0

#define PACK_MAGIC            "M1IR1"
#define PACK_TMP              "0:/INFRARED/.cycle.tmp.ir"

/* Buffers held static so handoff override pointer remains valid. */
static char s_locked_path[80];

/* Brand picker state. */
#define MAX_BRANDS 128
#define BRAND_LEN  20
#define PICK_ROWS  5
static char s_brands[MAX_BRANDS][BRAND_LEN];
static int  s_brand_count;

/* From m1_ir_remotes.c. */
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

/* ---------------- path helpers ---------------- */

static const char *pack_path_for(uint8_t rt)
{
	switch (rt) {
		case IR_REMOTETYPE_TV:        return "0:/INFRARED/browse/tv.pack";
		case IR_REMOTETYPE_AUDIO:     return "0:/INFRARED/browse/audio.pack";
		case IR_REMOTETYPE_PROJECTOR: return "0:/INFRARED/browse/projector.pack";
		case IR_REMOTETYPE_AC:        return "0:/INFRARED/browse/ac.pack";
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

/* ---------------- UI helpers ---------------- */

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

static void brand_of(const char *fname, char *out, size_t outsz)
{
	size_t i = 0;
	while (fname[i] && fname[i] != '_' && i + 1 < outsz)
	{
		out[i] = fname[i];
		i++;
	}
	out[i] = 0;
}

static int brand_cmp(const void *a, const void *b)
{
	return strcasecmp((const char *)a, (const char *)b);
}

/* ---------------- pack reader ---------------- */

/* Read a line up to LF, strip CR/LF. Returns length read (>=0) or -1 on EOF. */
static int pack_readline(FIL *fp, char *out, size_t outsz)
{
	size_t i = 0;
	UINT br;
	while (i + 1 < outsz)
	{
		char c;
		if (f_read(fp, &c, 1, &br) != FR_OK || br != 1)
			return (i == 0) ? -1 : (int)i;
		if (c == '\n') break;
		if (c == '\r') continue;
		out[i++] = c;
	}
	out[i] = 0;
	return (int)i;
}

/* Open the pack, validate magic, read count. Returns false on failure. */
static bool pack_open(FIL *fp, const char *path, uint32_t *out_count)
{
	if (f_open(fp, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) return false;
	char line[16];
	if (pack_readline(fp, line, sizeof(line)) < 0 ||
	    strcmp(line, PACK_MAGIC) != 0)
	{
		f_close(fp);
		return false;
	}
	if (pack_readline(fp, line, sizeof(line)) < 0)
	{
		f_close(fp);
		return false;
	}
	*out_count = (uint32_t)strtoul(line, NULL, 10);
	return true;
}

/* Read next entry header from current position. Returns:
 *   1 = ok (fp left at payload start; *name + *len filled)
 *   0 = end of pack
 *  -1 = malformed
 */
static int pack_next_header(FIL *fp, char *name, size_t namesz, uint32_t *out_len)
{
	int n = pack_readline(fp, name, namesz);
	if (n < 0) return 0;
	if (n == 0) return -1;
	char lenbuf[16];
	if (pack_readline(fp, lenbuf, sizeof(lenbuf)) <= 0) return -1;
	*out_len = (uint32_t)strtoul(lenbuf, NULL, 10);
	return 1;
}

/* Advance fp past `len` bytes (skip a payload). */
static bool pack_skip(FIL *fp, uint32_t len)
{
	return f_lseek(fp, f_tell(fp) + len) == FR_OK;
}

/* Stream-copy `len` bytes from fp into dst_path. Overwrites existing. */
static bool pack_extract(FIL *fp, uint32_t len, const char *dst_path)
{
	FIL out;
	if (f_open(&out, dst_path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
		return false;
	uint8_t buf[256];
	uint32_t left = len;
	bool ok = true;
	while (left > 0)
	{
		UINT want = (left > sizeof(buf)) ? sizeof(buf) : (UINT)left;
		UINT br, bw;
		if (f_read(fp, buf, want, &br) != FR_OK || br == 0) { ok = false; break; }
		if (f_write(&out, buf, br, &bw) != FR_OK || bw != br) { ok = false; break; }
		left -= br;
	}
	f_close(&out);
	if (!ok) f_unlink(dst_path);
	return ok;
}

/* Pass 1: enumerate every model header, collect unique brand prefixes. */
static bool collect_brands(const char *pack_path)
{
	s_brand_count = 0;
	FIL fp;
	uint32_t count;
	if (!pack_open(&fp, pack_path, &count)) return false;
	char name[96];
	uint32_t len;
	for (uint32_t i = 0; i < count; i++)
	{
		int rc = pack_next_header(&fp, name, sizeof(name), &len);
		if (rc != 1) break;
		if (!pack_skip(&fp, len)) break;
		char brand[BRAND_LEN];
		brand_of(name, brand, sizeof(brand));
		if (!brand[0]) continue;
		bool dup = false;
		for (int b = 0; b < s_brand_count; b++)
			if (!strcasecmp(s_brands[b], brand)) { dup = true; break; }
		if (dup) continue;
		if (s_brand_count >= MAX_BRANDS) break;
		snprintf(s_brands[s_brand_count++], BRAND_LEN, "%s", brand);
	}
	f_close(&fp);
	qsort(s_brands, s_brand_count, BRAND_LEN, brand_cmp);
	return s_brand_count > 0;
}

/* ---------------- brand picker ---------------- */

static void draw_picker(const char *header, int cursor, int top)
{
	int total = s_brand_count + 1;
	m1_u8g2_firstpage();
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
	u8g2_DrawStr(&m1_u8g2, 2, ROW_H, header);
	for (int r = 0; r < PICK_ROWS; r++)
	{
		int idx = top + r;
		if (idx >= total) break;
		const char *label = (idx == 0) ? "<All brands>" : s_brands[idx - 1];
		int y = 14 + ROW_H + r * ROW_H;
		if (idx == cursor)
			u8g2_DrawStr(&m1_u8g2, 2, y, ">");
		u8g2_DrawStr(&m1_u8g2, 10, y, label);
	}
	m1_u8g2_nextpage();
}

static int pick_brand(const char *header, char *out_brand, size_t outsz)
{
	int cursor = 0, top = 0;
	int total = s_brand_count + 1;
	draw_picker(header, cursor, top);
	for (;;)
	{
		S_M1_Main_Q_t q;
		if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) continue;
		if (q.q_evt_type != Q_EVENT_KEYPAD) continue;
		S_M1_Buttons_Status b;
		if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) return 0;
		if (b.event[BUTTON_OK_KP_ID]   == BUTTON_EVENT_CLICK)
		{
			if (cursor == 0) out_brand[0] = 0;
			else snprintf(out_brand, outsz, "%s", s_brands[cursor - 1]);
			return 1;
		}
		bool moved = false;
		if (b.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (cursor > 0) { cursor--; moved = true; }
			if (cursor < top) top = cursor;
		}
		if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
		{
			if (cursor + 1 < total) { cursor++; moved = true; }
			if (cursor >= top + PICK_ROWS) top = cursor - PICK_ROWS + 1;
		}
		if (moved) draw_picker(header, cursor, top);
	}
}

/* ---------------- transmit + handoff ---------------- */

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

static void handoff_to_grid(uint8_t remote_type, const char *path)
{
	snprintf(s_locked_path, sizeof(s_locked_path), "%s", path);
	ir_set_db_path_override(s_locked_path);
	infrared_universal_for_override(remote_type);
	ir_set_db_path_override(NULL);
}

/* ---------------- main flows ---------------- */

static void wait_back(void)
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

static void discover_run(uint8_t remote_type)
{
	if (remote_type >= IR_REMOTETYPE_UNKNOWN) return;
	const char *header = header_for(remote_type);
	const char *pack   = pack_path_for(remote_type);

	if (m1_sdcard_get_status() != SD_access_OK)
	{
		draw_screen(header, "No SD card", "", "BACK to return");
		wait_back();
		return;
	}

	draw_screen(header, "Loading pack...", NULL, NULL);
	if (!collect_brands(pack))
	{
		draw_screen(header, "No pack file:", pack + 2, "BACK to return");
		wait_back();
		return;
	}
	char brand_filter[BRAND_LEN] = "";
	if (!pick_brand(header, brand_filter, sizeof(brand_filter)))
	{
		xQueueReset(main_q_hdl);
		return;
	}

	FIL fp;
	uint32_t count;
	if (!pack_open(&fp, pack, &count))
	{
		draw_screen(header, "Pack reopen fail", NULL, "BACK to return");
		wait_back();
		return;
	}

	infrared_encode_sys_init();
	if (brand_filter[0])
		draw_screen(header, "Cycling models...", brand_filter, "OK=lock BACK=quit");
	else
		draw_screen(header, "Cycling models...", "OK = lock", "BACK = abort");

	bool aborted = false;
	bool locked  = false;
	uint32_t fired = 0;
	char name[96];

	for (uint32_t i = 0; i < count && !aborted && !locked; i++)
	{
		uint32_t len;
		int rc = pack_next_header(&fp, name, sizeof(name), &len);
		if (rc != 1) break;

		if (brand_filter[0])
		{
			char fb[BRAND_LEN];
			brand_of(name, fb, sizeof(fb));
			if (strcasecmp(fb, brand_filter) != 0)
			{
				pack_skip(&fp, len);
				continue;
			}
		}

		if (!pack_extract(&fp, len, PACK_TMP)) continue;

		fired++;
		char line[24];
		snprintf(line, sizeof(line), "%lu: %.18s", (unsigned long)fired, name);
		draw_screen(header, line, "OK=lock BACK=quit", NULL);

		(void)transmit_power_from_file(PACK_TMP, remote_type);

		TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(DISCOVER_PAUSE_MS);
		while (!aborted && !locked && xTaskGetTickCount() < deadline)
		{
			S_M1_Main_Q_t q;
			TickType_t now = xTaskGetTickCount();
			TickType_t to = (deadline > now) ? (deadline - now) : 0;
			if (to == 0) break;
			if (xQueueReceive(main_q_hdl, &q, to) != pdTRUE) break;
			if (q.q_evt_type != Q_EVENT_KEYPAD) continue;
			S_M1_Buttons_Status b;
			if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
			if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) { aborted = true; break; }
			if (b.event[BUTTON_OK_KP_ID]   == BUTTON_EVENT_CLICK) { locked  = true;  break; }
		}
	}
	f_close(&fp);

	if (ir_ota_data_tx_active)
		m1_ir_ota_frame_repeat_handler(IRMP_UNKNOWN_PROTOCOL);

	if (aborted)
	{
		f_unlink(PACK_TMP);
		xQueueReset(main_q_hdl);
		return;
	}
	if (!locked)
	{
		f_unlink(PACK_TMP);
		draw_screen(header, "End of list.", "No model locked.", "BACK to return");
		wait_back();
		return;
	}

	/* Lock: promote temp file to the permanent Last slot. */
	const char *last = last_path_for(remote_type);
	f_mkdir("0:/INFRARED/saved");
	f_unlink(last);  /* f_rename refuses to overwrite. */
	if (f_rename(PACK_TMP, last) != FR_OK)
	{
		/* Fallback: keep temp around and hand off pointing at it. */
		handoff_to_grid(remote_type, PACK_TMP);
		f_unlink(PACK_TMP);
		return;
	}
	handoff_to_grid(remote_type, last);
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
		wait_back();
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
