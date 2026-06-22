/* See COPYING.txt for license details. */
/*
 * m1_ir_mass.c — "Mass Off / Mass Power" engine.
 *
 * Auto-fires every Power (TV / Audio / Projector) or Off (AC) code in
 * the relevant /INFRARED/db/*.ir database in sequence, with a brief
 * inter-frame pause. BACK aborts. The loop architecture mirrors
 * infrared_universal_all_remotes() in m1_ir_remotes.c but skips the
 * button-grid UI in favour of a single-line progress display.
 *
 * The Power/Off function is always index 0 in each category's
 * function-name list (see ir_remote_*_functions[] in m1_ir_signals.c),
 * so we hard-code function_type=0 for all four entry points.
 *
 * M1 Project
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_log_debug.h"
#include "m1_display.h"
#include "m1_system.h"
#include "m1_infrared.h"
#include "m1_ir_signals.h"
#include "m1_ir_mass.h"
#include "irmp.h"
#include "irsnd.h"

#define M1_LOGDB_TAG  "IR_MASS"
#define FN_POWER       0   /* Power for TV/Audio/Projector, Off for AC */

extern const struct S_M1_IR_Protocol_Mapping_t {
    const char *protocol_name;
    uint8_t protocol_number;
} ir_protocols_mapping_table[];
extern uint8_t ir_remote_function_data_read(uint8_t remote_type, uint8_t function_type);
extern uint8_t ir_payload_info_get(S_M1_IR_Payload_t *payload);
extern void ir_remote_file_deinit(void);
extern uint8_t ir_remote_file_header_check(const char *remotes_filename, uint8_t remote_type);
extern uint8_t ir_remote_file_data_check(uint8_t remote_type);
extern uint16_t ir_remote_dev_func_counter_get(uint8_t function_type);
extern S_M1_IR_Payload_t ir_universal_payload;

#define IR_PROTOCOLS_MAPPING_MAX  15

static const char *db_file_for(uint8_t rt)
{
    switch (rt) {
        case IR_REMOTETYPE_TV:        return IR_REMOTE_TYPE_TVS_FILENAME;
        case IR_REMOTETYPE_AUDIO:     return IR_REMOTE_TYPE_AUDIOS_FILENAME;
        case IR_REMOTETYPE_PROJECTOR: return IR_REMOTE_TYPE_PROJECTORS_FILENAME;
        case IR_REMOTETYPE_AC:        return IR_REMOTE_TYPE_ACS_FILENAME;
        default:                      return NULL;
    }
}

static const char *header_for(uint8_t rt)
{
    switch (rt) {
        case IR_REMOTETYPE_TV:        return "TV Mass Off";
        case IR_REMOTETYPE_AUDIO:     return "Audio Mass Off";
        case IR_REMOTETYPE_PROJECTOR: return "Proj Mass Off";
        case IR_REMOTETYPE_AC:        return "AC Mass Off";
        default:                      return "Mass Off";
    }
}

static void draw_screen(const char *header, uint32_t n, uint32_t total,
                        const char *bottom)
{
    char buf[24];
    m1_u8g2_firstpage();
    u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
    u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
    u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_FONT_HEIGHT, header);

    int y = 14 + M1_GUI_FONT_HEIGHT;
    if (total)
        snprintf(buf, sizeof(buf), "Trying %lu/%lu",
                 (unsigned long)n, (unsigned long)total);
    else
        snprintf(buf, sizeof(buf), "Trying %lu", (unsigned long)n);
    u8g2_DrawStr(&m1_u8g2, 2, y, buf);

    /* Simple progress bar. */
    if (total)
    {
        int x = 2, w = M1_LCD_DISPLAY_WIDTH - 4, h = 6;
        u8g2_DrawFrame(&m1_u8g2, x, y + 6, w, h);
        int fill = (int)((unsigned long long)(n) * (w - 2) / total);
        if (fill > w - 2) fill = w - 2;
        if (fill > 0)
            u8g2_DrawBox(&m1_u8g2, x + 1, y + 7, fill, h - 2);
        y += 6 + h;
    }

    y += M1_GUI_FONT_HEIGHT;
    u8g2_DrawStr(&m1_u8g2, 2, y, bottom ? bottom : "BACK to stop");
    m1_u8g2_nextpage();
}

/* Set up the next transmission from ir_universal_payload. Returns true if
 * irsnd_generate_tx_data + infrared_transmit(1) were issued. */
static bool start_one_transmission(void)
{
    IRMP_DATA ir_remote_data = { 0 };
    int i;

    if (ir_universal_payload.ir_data_type == IR_DATATYPE_PARSED)
    {
        for (i = 0; i < IR_PROTOCOLS_MAPPING_MAX; i++)
        {
            if (!strcmp((const char *)ir_universal_payload.pprotocol,
                        ir_protocols_mapping_table[i].protocol_name))
                break;
        }
        if (i >= IR_PROTOCOLS_MAPPING_MAX) return false;
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
    else
    {
        return false;
    }
    irsnd_generate_tx_data(ir_remote_data);
    infrared_transmit(1, ir_remote_data.protocol);
    return true;
}

static bool advance_to_next(uint8_t remote_type)
{
    if (ir_remote_function_data_read(remote_type, FN_POWER)) return false;
    if (ir_payload_info_get(&ir_universal_payload)) return false;
    return start_one_transmission();
}

static void mass_off_run(uint8_t remote_type)
{
    if (remote_type >= IR_REMOTETYPE_UNKNOWN) return;
    const char *header = header_for(remote_type);

    /* Initial frame: hourglass + header. */
    m1_u8g2_firstpage();
    u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2,
                  M1_LCD_DISPLAY_HEIGHT/2 - 32/2, 18, 32, hourglass_18x32);
    m1_u8g2_nextpage();

    /* Load + validate the file. */
    uint8_t hdr_err = ir_remote_file_header_check(db_file_for(remote_type), remote_type);
    uint8_t data_err = hdr_err ? 0xFF : ir_remote_file_data_check(remote_type);
    if (hdr_err || data_err)
    {
        char line[40];
        if (hdr_err)
            snprintf(line, sizeof(line), "No DB at %s", db_file_for(remote_type));
        else
            snprintf(line, sizeof(line), "Bad DB content");
        draw_screen(header, 0, 0, line);
        S_M1_Main_Q_t q;
        S_M1_Buttons_Status b;
        while (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) == pdTRUE)
        {
            if (q.q_evt_type != Q_EVENT_KEYPAD) continue;
            if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        }
        ir_remote_file_deinit();
        return;
    }

    uint32_t total = ir_remote_dev_func_counter_get(FN_POWER);
    uint32_t fired = 0;
    bool aborted = false;
    bool done = false;

    /* Kick off the first transmission. */
    infrared_encode_sys_init();
    if (!advance_to_next(remote_type))
    {
        draw_screen(header, 0, total, "No codes");
        done = true;
    }
    else
    {
        fired = 1;
        draw_screen(header, fired, total, "BACK to stop");
    }

    S_M1_Main_Q_t q_item;
    while (!done && !aborted)
    {
        if (xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY) != pdTRUE)
            continue;

        if (q_item.q_evt_type == Q_EVENT_KEYPAD)
        {
            S_M1_Buttons_Status b;
            if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
            {
                aborted = true;
                continue;
            }
        }
        else if (q_item.q_evt_type == Q_EVENT_IRRED_TX)
        {
            S_M1_IR_Tx_States st = infrared_transmit(0, 0);
            if (st != IR_TX_COMPLETED) continue;

            /* Brief inter-frame pause so the AP^Wreceiver has time to
             * recover. Same delay the universal-remote loop uses. */
            vTaskDelay(pdMS_TO_TICKS(IR_REMOTE_PAUSE_TIME_BETWEEN_FRAME));

            irsnd_init(&timerhdl_ir_carrier, IR_ENCODE_TIMER_TX_CHANNEL);
            if (!advance_to_next(remote_type))
            {
                done = true;
                draw_screen(header, fired, total, "All sent. BACK");
                continue;
            }
            fired++;
            if ((fired & 0x3) == 0 || fired == total)
                draw_screen(header, fired, total, "BACK to stop");
        }
    }

    /* Wait for BACK to dismiss (only if we ran to completion). */
    if (done && !aborted)
    {
        S_M1_Main_Q_t q;
        S_M1_Buttons_Status b;
        while (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) == pdTRUE)
        {
            if (q.q_evt_type != Q_EVENT_KEYPAD) continue;
            if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
            if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) break;
        }
    }

    /* Reset TX state if anything was in-flight. */
    if (ir_ota_data_tx_active)
        m1_ir_ota_frame_repeat_handler(IRMP_UNKNOWN_PROTOCOL);

    ir_remote_file_deinit();
    xQueueReset(main_q_hdl);
}

void infrared_mass_off_tv(void)        { mass_off_run(IR_REMOTETYPE_TV); }
void infrared_mass_off_audio(void)     { mass_off_run(IR_REMOTETYPE_AUDIO); }
void infrared_mass_off_projector(void) { mass_off_run(IR_REMOTETYPE_PROJECTOR); }
void infrared_mass_off_ac(void)        { mass_off_run(IR_REMOTETYPE_AC); }
