/* See COPYING.txt for license details. */

/*
*
* m1_wifi.h
*
* Library for M1 Wifi
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_wifi.h"
#include "m1_esp32_hal.h"
//#include "control.h"
#include "ctrl_api.h"
#include "esp_app_main.h"
#include "m1_oui_lookup.h"
#include "m1_wifi_attack_ui.h"
#include "m1_wifi_capture_ui.h"
#include "m1_display.h"
#include "m1_system.h"

/*************************** D E F I N E S ************************************/

#define M1_LOGDB_TAG	"Wifi"

#define M1_WIFI_AP_SCANNING_TIME	30 // seconds

#define M1_GUI_ROW_SPACING			1

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void menu_wifi_init(void);
void menu_wifi_exit(void);

void menu_wifi_init(void);
void wifi_scan_ap(void);
void wifi_config(void);

static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir);
static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp);

static const wifi_scanlist_t *g_wifi_scan_selected = NULL;

/* Allows the long-press handler to retrieve the currently-displayed entry. */
bool wifi_scan_get_selected_ssid_bssid(char *ssid_out, size_t ssid_out_size,
                                       char *bssid_out, size_t bssid_out_size)
{
	if (!g_wifi_scan_selected) return false;
	if (ssid_out && ssid_out_size)
		snprintf(ssid_out, ssid_out_size, "%s",
		         (const char *)g_wifi_scan_selected->ssid);
	if (bssid_out && bssid_out_size)
		snprintf(bssid_out, bssid_out_size, "%s",
		         (const char *)g_wifi_scan_selected->bssid);
	return true;
}

/* Returns the wifi channel of the currently-displayed AP (0 if none). */
int wifi_scan_get_selected_channel(void)
{
	return g_wifi_scan_selected ? g_wifi_scan_selected->channel : 0;
}

/* Tiny 2-option chooser: returns 0 (Dict), 1 (Capture), or -1 (BACK). */
static int wifi_long_press_menu(const char *ssid)
{
	int sel = 0;
	while (true)
	{
		m1_u8g2_firstpage();
		u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
		u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_FONT_HEIGHT, "Target:");
		char buf[24];
		snprintf(buf, sizeof(buf), "%.20s", ssid ? ssid : "?");
		u8g2_DrawStr(&m1_u8g2, 2, 14 + M1_GUI_FONT_HEIGHT, buf);
		u8g2_DrawStr(&m1_u8g2, 2, 14 + 3*M1_GUI_FONT_HEIGHT,
		             sel == 0 ? ">Dict attack"  : " Dict attack");
		u8g2_DrawStr(&m1_u8g2, 2, 14 + 4*M1_GUI_FONT_HEIGHT,
		             sel == 1 ? ">Capture hand" : " Capture hand");
		u8g2_DrawStr(&m1_u8g2, 2, 14 + 5*M1_GUI_FONT_HEIGHT, "OK=go BACK=quit");
		m1_u8g2_nextpage();

		S_M1_Main_Q_t q;
		if (xQueueReceive(main_q_hdl, &q, portMAX_DELAY) != pdTRUE) continue;
		if (q.q_evt_type != Q_EVENT_KEYPAD) continue;
		S_M1_Buttons_Status b;
		if (xQueueReceive(button_events_q_hdl, &b, 0) != pdTRUE) continue;
		if (b.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) return -1;
		if (b.event[BUTTON_UP_KP_ID]   == BUTTON_EVENT_CLICK) sel = (sel + 1) & 1;
		if (b.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK) sel = (sel + 1) & 1;
		if (b.event[BUTTON_OK_KP_ID]   == BUTTON_EVENT_CLICK ||
		    b.event[BUTTON_OK_KP_ID]   == BUTTON_EVENT_LCLICK) return sel;
	}
}

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void menu_wifi_init(void)
{
	;
} // void menu_wifi_init(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void  menu_wifi_exit(void)
{
	;
} // void  menu_wifi_exit(void)



/*============================================================================*/
/**
  * @brief Scans for wifi access point list
  * @param
  * @retval
  */
/*============================================================================*/
void wifi_scan_ap(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	ctrl_cmd_t app_req = CTRL_CMD_DEFAULT_REQ();
	uint16_t list_count;

    /* Graphic work starts here */
	u8g2_SetFont(&m1_u8g2, M1_DISP_MAIN_MENU_FONT_N);
	if ( !m1_esp32_get_init_status() )
	{
		m1_esp32_init();
		if ( !get_esp32_main_init_status() )
			esp32_main_init();
		m1_u8g2_firstpage();
		u8g2_DrawStr(&m1_u8g2, 6, 15, "Initializing...");
		u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
		m1_u8g2_nextpage();
	}

	list_count = 0;

	m1_u8g2_firstpage();
	if ( get_esp32_main_init_status() )
	{
		u8g2_DrawStr(&m1_u8g2, 6, 15, "Scanning AP...");
		u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32, hourglass_18x32);
		m1_u8g2_nextpage();

		// implemented synchronous
		app_req.cmd_timeout_sec = M1_WIFI_AP_SCANNING_TIME; //DEFAULT_CTRL_RESP_TIMEOUT //30 sec
		app_req.msg_id = CTRL_RESP_GET_AP_SCAN_LIST;
		ret = wifi_ap_scan_list(&app_req);
		ret = wifi_ap_list_validation(&app_req);
		if ( ret )
		{
			list_count = wifi_ap_list_print(&app_req, true);
		} // if ( ret )
		else
		{
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
			u8g2_DrawBox(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 18/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 18, 32); // Clear old image
			u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
			u8g2_DrawXBMP(&m1_u8g2, M1_LCD_DISPLAY_WIDTH/2 - 32/2, M1_LCD_DISPLAY_HEIGHT/2 - 2, 32, 32, wifi_error_32x32);
			u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Failed. Let retry!");
			m1_u8g2_nextpage();
			// Reset the ESP module
			esp32_disable();
			m1_hard_delay(1);
			esp32_enable();
			/* stop spi transactions short time to avoid slave sync issues */
			m1_hard_delay(200);
		}
	} // if ( get_esp32_main_init_status() )
	else
	{
		u8g2_DrawStr(&m1_u8g2, 6, 15 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "ESP32 not ready!");
		m1_u8g2_nextpage();
	}
	// mode declared in control.c should be set to MODE_STATION after M1 has been connected to a Wifi network
	//mode |= MODE_STATION;

	while (1 ) // Main loop of this task
	{
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					if (app_req.u.wifi_ap_scan.out_list != NULL)
					{
						free(app_req.u.wifi_ap_scan.out_list);
					}
					wifi_ap_list_print(NULL, false);

					xQueueReset(main_q_hdl); // Reset main q before return
					m1_esp32_deinit();
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK ) // go up?
				{
					if ( list_count )
						wifi_ap_list_print(&app_req, true);
				}
				else if ( this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK ) // go down?
				{
					if ( list_count )
						wifi_ap_list_print(&app_req, false);
				}
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK ) // Select?
				{
					; // Do other things for this task, if needed
				}
				else if ( this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_LCLICK ) // Long-press OK: chooser
				{
					char sel_ssid[SSID_LENGTH];
					char sel_bssid[BSSID_STR_SIZE];
					if (list_count &&
					    wifi_scan_get_selected_ssid_bssid(sel_ssid, sizeof(sel_ssid),
					                                     sel_bssid, sizeof(sel_bssid)))
					{
						int choice = wifi_long_press_menu(sel_ssid);
						int chan = wifi_scan_get_selected_channel();
						if (choice == 0)
							m1_wifi_attack_ui_run(sel_ssid, sel_bssid);
						else if (choice == 1)
							m1_wifi_capture_ui_run(sel_ssid, sel_bssid, chan);
						/* Redraw current AP on return. */
						if (list_count)
							wifi_ap_list_print(&app_req, true);
					}
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void wifi_scan_ap(void)



/*============================================================================*/
/**
  * @brief Displays all scanned AP list.
  * @param
  * @retval
  */
/*============================================================================*/
static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)
{
	static uint16_t i;
	static wifi_ap_scan_list_t *w_scan_p;
	static wifi_scanlist_t *list;
	static bool init_done = false;
	char prn_msg[25];
	uint8_t y_offset;

	if ( !app_resp && !up_dir ) // reset condition?
	{
		init_done = false;
		return 0;
	} // if ( !app_resp && !up_dir )

	if ( !init_done )
	{
		init_done = true;
		w_scan_p = &app_resp->u.wifi_ap_scan;
		list = w_scan_p->out_list;

		if (!w_scan_p->count)
		{
			strcpy(prn_msg, "No AP found!");
			M1_LOG_I(M1_LOGDB_TAG, "No AP found\n\r");
			init_done = false;
		}
		else if (!list)
		{
			strcpy(prn_msg, "Try again!");
			M1_LOG_I(M1_LOGDB_TAG, "Failed to get scanned AP list\n\r");
			init_done = false;
		}
		else
		{
			M1_LOG_I(M1_LOGDB_TAG, "Number of available APs is %d\n\r", w_scan_p->count);
		}

		if ( !init_done )
		{
			u8g2_DrawStr(&m1_u8g2, 6, 25 + M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);
			m1_u8g2_nextpage(); // Update display RAM
			return 0;
		}
		// Display first AP in the list
		i = 1;
		up_dir = true; // Overwrite the up_dir for the AP to be displayed for the first time
	} // if ( !init_done )

	if ( up_dir )
	{
		if ( i )
			i--;
		else
			i = w_scan_p->count-1; // roll over
	}
	else
	{
		i++;
		if ( i >= w_scan_p->count )
			i = 0; // roll over
	}

	m1_u8g2_firstpage();
	u8g2_DrawXBMP(&m1_u8g2, 0, 0, 128, 14, m1_frame_128_14);
	u8g2_DrawStr(&m1_u8g2, 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, "Total AP:");

	sprintf(prn_msg, "%d", w_scan_p->count);
	u8g2_DrawStr(&m1_u8g2, 2 + strlen("Total AP: ")*M1_GUI_FONT_WIDTH + 2, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

	sprintf(prn_msg, "%d/%d", i + 1, w_scan_p->count); // Current AP
	u8g2_DrawStr(&m1_u8g2, M1_LCD_DISPLAY_WIDTH - 6*M1_GUI_FONT_WIDTH, M1_GUI_ROW_SPACING + M1_GUI_FONT_HEIGHT, prn_msg);

	y_offset = 14 + M1_GUI_FONT_HEIGHT - 1;
	// Draw text
	if ( list[i].ssid[0]==0x00 ) // Hidden SSID?
		strcpy(prn_msg, "*hidden*");
	else
		strncpy(prn_msg, (const char *)list[i].ssid, M1_LCD_DISPLAY_WIDTH/M1_GUI_FONT_WIDTH);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
	y_offset += M1_GUI_FONT_HEIGHT;
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, (const char *)list[i].bssid);
	y_offset += M1_GUI_FONT_HEIGHT + M1_GUI_ROW_SPACING;
	// Vendor (OUI lookup); truncated to fit the screen.
	{
		char vendor[M1_OUI_VENDOR_LEN + 1];
		m1_oui_lookup_str((const char *)list[i].bssid, vendor, sizeof(vendor));
		int max_chars = (M1_LCD_DISPLAY_WIDTH / M1_GUI_FONT_WIDTH) - 1;
		if ((int)strlen(vendor) > max_chars)
			vendor[max_chars] = '\0';
		u8g2_DrawStr(&m1_u8g2, 2, y_offset, vendor);
	}
	y_offset += M1_GUI_FONT_HEIGHT;
	sprintf(prn_msg, "RSSI: %ddBm", list[i].rssi);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);
	y_offset += M1_GUI_FONT_HEIGHT;
	// Merge channel + auth on a single line to free a row for vendor.
	sprintf(prn_msg, "Ch:%d Auth:%d", list[i].channel, list[i].encryption_mode);
	u8g2_DrawStr(&m1_u8g2, 2, y_offset, prn_msg);

	m1_u8g2_nextpage(); // Update display RAM

	g_wifi_scan_selected = &list[i];

	M1_LOG_D(M1_LOGDB_TAG, "%d) ssid \"%s\" bssid \"%s\" rssi \"%d\" channel \"%d\" auth mode \"%d\" \n\r",\
						i, list[i].ssid, list[i].bssid, list[i].rssi,
						list[i].channel, list[i].encryption_mode);

	return w_scan_p->count;
} // static uint16_t wifi_ap_list_print(ctrl_cmd_t *app_resp, bool up_dir)



/*============================================================================*/
/**
  * @brief Validates the AP list.
  * @param
  * @retval
  */
/*============================================================================*/
static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp)
{
	if (!app_resp || (app_resp->msg_type != CTRL_RESP))
	{
		if (app_resp)
			M1_LOG_I(M1_LOGDB_TAG, "Msg type is not response[%u]\n\r", app_resp->msg_type);
		return false;
	}
	if (app_resp->resp_event_status != SUCCESS)
	{
		//process_failed_responses(app_resp);
		return false;
	}
	if (app_resp->msg_id != CTRL_RESP_GET_AP_SCAN_LIST)
	{
		M1_LOG_I(M1_LOGDB_TAG, "Invalid Response[%u] to parse\n\r", app_resp->msg_id);
		return false;
	}

	return true;
} // static uint8_t wifi_ap_list_validation(ctrl_cmd_t *app_resp)


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void wifi_config(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;

	m1_gui_let_update_fw();

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		// Wait for the notification from button_event_handler_task to subfunc_handler_task.
		// This task is the sub-task of subfunc_handler_task.
		// The notification is given in the form of an item in the main queue.
		// So let read the main queue.
		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed

					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( m1_buttons_status[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else
				{
					; // Do other things for this task, if needed
				}
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void wifi_config(void)
