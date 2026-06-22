/* See COPYING.txt for license details. */

/*
*
* m1_at_response_parser.c
*
* M1 parser for EPS32 module
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "ctrl_api.h"
#include "m1_at_response_parser.h"

/*************************** D E F I N E S ************************************/

#define CHECK_NON_ZERO_VAL(x)		{if (!x) break;}

//************************** C O N S T A N T **********************************/

//************************** S T R U C T U R E S *******************************


/***************************** V A R I A B L E S ******************************/


/********************* F U N C T I O N   P R O T O T Y P E S ******************/


/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
char *m1_resp_string_strip(char *resp, const char *substr)
{
	size_t sublen;
	char *index;

	if ( !resp )
		return NULL;
    sublen = strlen(substr);
    if (sublen > 0)
    {
        index = resp;
        while (true)
        {
        	index = strstr(index, substr);
        	if ( !index )
        		break;
            memmove(index, index + sublen, strlen(index + sublen) + 1);
        }
    } // if (sublen > 0)

    return resp;
} // char *m1_resp_string_strip(char *resp, const char *substr)



/******************************************************************************/
/**
  * @brief
  * @param None
  * @retval None
  */
/******************************************************************************/
uint8_t m1_parse_spi_at_resp(char *resp, const char *resp_key, ctrl_cmd_t *app_resp)
{
	uint8_t ret = ERROR;
	char *index, *start_index, *end_index, *next_index;
	uint16_t ap_count = 0xFFFF;
	size_t cp_len;
	wifi_scanlist_t *out_list;
	/* Phase 2: GATT response parsing reads/writes different union members.
	 * Track item counts before parsing so we can decide whether to return
	 * SUCCESS for those msg_ids without disturbing the wifi-scan logic. */
	int gatt_srv_count_before = 0;
	int gatt_char_count_before = 0;
	int gatt_added_or_updated = 0;
	do
	{
		CHECK_NON_ZERO_VAL(resp);
		CHECK_NON_ZERO_VAL(resp_key);
		CHECK_NON_ZERO_VAL(app_resp);
		CHECK_NON_ZERO_VAL(strlen(resp_key));
		CHECK_NON_ZERO_VAL(strlen(resp));
		index = strstr(resp, resp_key);
		CHECK_NON_ZERO_VAL(index);
		switch (app_resp->msg_id)
		{
			case CTRL_RESP_GET_AP_SCAN_LIST:
				// +CWLAP:(<ecn>,<"ssid">,<rssi>,<"mac">,<channel>,<freq_offset>,<freqcal_val>,<pairwise_cipher>,<group_cipher>,<bgn>,<wps>)
				//Sample response: +CWLAP:(3,"MySSIDname",-73,"1a:2b:3c:4d:56:78",10,-1,-1,4,4,7,1)
				ap_count = app_resp->u.wifi_ap_scan.count;
				out_list = app_resp->u.wifi_ap_scan.out_list;
				while ( true )
				{
					start_index = strstr(index, "(");
					if ( !start_index )
						break;
					end_index = strstr(index, ")");
					if ( !end_index )
						break;
					next_index = end_index;
					out_list = realloc(out_list, sizeof(wifi_scanlist_t)*(app_resp->u.wifi_ap_scan.count + 1));
					if ( out_list==NULL )
						break;
					out_list[app_resp->u.wifi_ap_scan.count].encryption_mode = strtol(&start_index[1], &start_index, 10);
					end_index = strstr(&start_index[2], "\"");
					cp_len = end_index - start_index - 2;
					strncpy(out_list[app_resp->u.wifi_ap_scan.count].ssid, &start_index[2], cp_len);
					out_list[app_resp->u.wifi_ap_scan.count].ssid[cp_len] = 0x00; // Add end of string
					out_list[app_resp->u.wifi_ap_scan.count].rssi = strtol(&end_index[2], &start_index, 10);
					end_index = strstr(&start_index[2], "\"");
					cp_len = end_index - start_index - 2;
					strncpy(out_list[app_resp->u.wifi_ap_scan.count].bssid, &start_index[2], cp_len);
					out_list[app_resp->u.wifi_ap_scan.count].bssid[cp_len] = 0x00;
					out_list[app_resp->u.wifi_ap_scan.count].channel = strtol(&end_index[2], &start_index, 10);

					app_resp->u.wifi_ap_scan.count++; // Increase count
					index = strstr(next_index, resp_key); // Try to get another record
					if (!index)
						break;
				} // while ( true )
				break;

			case CTRL_RESP_GET_BLE_SCAN_LIST:
				//+BLESCAN:"7c:0a:3f:9b:d5:cd",-81,1bff750042040180667c0a3f9bd5cd7e0a3f9bd5cc01000000000000,,0,3
				// Fields after RSSI: <adv_data_hex>,<scan_rsp_data_hex>,<addr_type>,<adv_type>
				ap_count = app_resp->u.wifi_ap_scan.count;
				out_list = app_resp->u.wifi_ap_scan.out_list;
				while ( true )
				{
					start_index = strstr(index, "\"");
					if ( !start_index )
						break;
					end_index = strstr(index, "\r\n");
					if ( !end_index )
						break;
					next_index = end_index;
					out_list = realloc(out_list, sizeof(wifi_scanlist_t)*(app_resp->u.wifi_ap_scan.count + 1));
					if ( out_list==NULL )
						break;
					wifi_scanlist_t *e = &out_list[app_resp->u.wifi_ap_scan.count];
					/* Zero adv/scan-rsp fields up front so missing values stay empty. */
					e->adv_data[0] = '\0';
					e->scan_rsp_data[0] = '\0';

					/* bssid */
					end_index = strstr(&start_index[1], "\"");
					cp_len = end_index - start_index - 1;
					strncpy((char *)e->bssid, &start_index[1], cp_len);
					e->bssid[cp_len] = 0x00;

					/* rssi */
					e->rssi = strtol(&end_index[2], &start_index, 10);

					/* adv_data hex (field 3) — between comma after rssi and next comma */
					if ( *start_index == ',' )
					{
						const char *adv_start = start_index + 1;
						const char *adv_end = strchr(adv_start, ',');
						if ( adv_end && (adv_end < next_index) )
						{
							cp_len = (size_t)(adv_end - adv_start);
							if ( cp_len >= sizeof(e->adv_data) )
								cp_len = sizeof(e->adv_data) - 1;
							memcpy(e->adv_data, adv_start, cp_len);
							e->adv_data[cp_len] = '\0';

							/* scan_rsp_data hex (field 4) */
							const char *sr_start = adv_end + 1;
							const char *sr_end = strchr(sr_start, ',');
							if ( sr_end && (sr_end < next_index) )
							{
								cp_len = (size_t)(sr_end - sr_start);
								if ( cp_len >= sizeof(e->scan_rsp_data) )
									cp_len = sizeof(e->scan_rsp_data) - 1;
								memcpy(e->scan_rsp_data, sr_start, cp_len);
								e->scan_rsp_data[cp_len] = '\0';

								/* addr_type (field 5) */
								e->encryption_mode = strtol(sr_end + 1, &start_index, 10);
							}
							else
							{
								e->encryption_mode = 0;
								start_index = (char *)next_index;
							}
						}
						else
						{
							e->encryption_mode = 0;
							start_index = (char *)next_index;
						}
					}
					else
					{
						e->encryption_mode = 0;
					}

					app_resp->u.wifi_ap_scan.count++; // Increase count
					index = strstr(next_index, resp_key); // Try to get another record
					if (!index)
						break;
				} // while ( true )
				break;

			default:
				break;

			/* ---- Phase 2: BLE GATT client responses ---- */

			case CTRL_RESP_BLE_CONNECT:
				/* +BLECONN:<idx>,<status>   status 0 = success */
				{
					char *p = strchr(index, ',');
					if (p)
						app_resp->u.ble_conn.connect_status =
							(int)strtol(p + 1, NULL, 10);
					gatt_added_or_updated = 1;
				}
				break;

			case CTRL_RESP_BLE_GATT_PRIMSRV:
				/* +BLEGATTCPRIMSRV:<idx>,<srv_idx>,"<uuid>",<srv_type>
				 * Multiple lines per response.
				 */
				gatt_srv_count_before = app_resp->u.ble_srv_list.count;
				{
					ble_gatt_srv_t *list = app_resp->u.ble_srv_list.out_list;
					while (true)
					{
						char *line = strstr(index, resp_key);
						if (!line) break;
						char *eol = strstr(line, "\r\n");
						if (!eol) eol = line + strlen(line);
						/* skip past resp_key */
						char *p = line + strlen(resp_key);
						/* parse: idx,srv_idx,"uuid",srv_type */
						(void)strtol(p, &p, 10);     /* conn_idx, ignored */
						if (*p == ',') p++;
						int srv_idx = (int)strtol(p, &p, 10);
						if (*p == ',') p++;
						char uuid[BLE_GATT_UUID_STR_LEN] = "";
						if (*p == '"')
						{
							char *q = strchr(p + 1, '"');
							if (q)
							{
								size_t n = (size_t)(q - p - 1);
								if (n >= sizeof(uuid)) n = sizeof(uuid) - 1;
								memcpy(uuid, p + 1, n);
								uuid[n] = '\0';
								p = q + 1;
							}
						}
						if (*p == ',') p++;
						int srv_type = (int)strtol(p, &p, 10);

						list = realloc(list,
							sizeof(ble_gatt_srv_t) * (app_resp->u.ble_srv_list.count + 1));
						if (!list) break;
						ble_gatt_srv_t *e = &list[app_resp->u.ble_srv_list.count];
						e->srv_idx = srv_idx;
						e->srv_type = srv_type;
						snprintf(e->uuid, sizeof(e->uuid), "%s", uuid);
						app_resp->u.ble_srv_list.count++;
						/* advance past this line */
						index = eol;
					}
					app_resp->u.ble_srv_list.out_list = list;
					if (app_resp->u.ble_srv_list.count > gatt_srv_count_before)
						gatt_added_or_updated = 1;
				}
				break;

			case CTRL_RESP_BLE_GATT_CHAR:
				/* +BLEGATTCCHAR:<idx>,"char",<srv_idx>,<char_idx>,"<uuid>",<props>
				 * +BLEGATTCCHAR:<idx>,"desc",<srv_idx>,<char_idx>,<desc_idx>,"<uuid>"
				 * Only "char" lines are captured.
				 */
				gatt_char_count_before = app_resp->u.ble_char_list.count;
				{
					ble_gatt_char_t *list = app_resp->u.ble_char_list.out_list;
					while (true)
					{
						char *line = strstr(index, resp_key);
						if (!line) break;
						char *eol = strstr(line, "\r\n");
						if (!eol) eol = line + strlen(line);
						char *p = line + strlen(resp_key);
						(void)strtol(p, &p, 10); /* conn_idx, ignored */
						if (*p == ',') p++;
						/* type "char" or "desc" */
						bool is_char = false;
						if (*p == '"')
						{
							char *q = strchr(p + 1, '"');
							if (q)
							{
								is_char = (q == p + 5) && (memcmp(p + 1, "char", 4) == 0);
								p = q + 1;
							}
						}
						if (!is_char)
						{
							index = eol;
							continue;
						}
						if (*p == ',') p++;
						int srv_idx = (int)strtol(p, &p, 10);
						if (*p == ',') p++;
						int char_idx = (int)strtol(p, &p, 10);
						if (*p == ',') p++;
						char uuid[BLE_GATT_UUID_STR_LEN] = "";
						if (*p == '"')
						{
							char *q = strchr(p + 1, '"');
							if (q)
							{
								size_t n = (size_t)(q - p - 1);
								if (n >= sizeof(uuid)) n = sizeof(uuid) - 1;
								memcpy(uuid, p + 1, n);
								uuid[n] = '\0';
								p = q + 1;
							}
						}
						if (*p == ',') p++;
						int props = (int)strtol(p, &p, 10);

						list = realloc(list,
							sizeof(ble_gatt_char_t) * (app_resp->u.ble_char_list.count + 1));
						if (!list) break;
						ble_gatt_char_t *e = &list[app_resp->u.ble_char_list.count];
						e->srv_idx = srv_idx;
						e->char_idx = char_idx;
						e->props = (uint8_t)props;
						snprintf(e->uuid, sizeof(e->uuid), "%s", uuid);
						e->value_len = 0;
						e->value_hex[0] = '\0';
						e->value_str[0] = '\0';
						app_resp->u.ble_char_list.count++;
						index = eol;
					}
					app_resp->u.ble_char_list.out_list = list;
					if (app_resp->u.ble_char_list.count > gatt_char_count_before)
						gatt_added_or_updated = 1;
				}
				break;

			case CTRL_RESP_BLE_GATT_READ:
				/* +BLEGATTCRD:<idx>,<len>,<value_hex> */
				{
					char *p = strchr(index, ',');
					if (p)
					{
						p++;
						int len = (int)strtol(p, &p, 10);
						if (*p == ',') p++;
						char *eol = strstr(p, "\r\n");
						size_t hex_len = eol ? (size_t)(eol - p) : strlen(p);
						if (hex_len >= sizeof(app_resp->u.ble_read.value_hex))
							hex_len = sizeof(app_resp->u.ble_read.value_hex) - 1;
						memcpy(app_resp->u.ble_read.value_hex, p, hex_len);
						app_resp->u.ble_read.value_hex[hex_len] = '\0';
						app_resp->u.ble_read.value_len = (uint16_t)len;
						gatt_added_or_updated = 1;
					}
				}
				break;
		} // switch (app_resp->msg_id)
	} while (0);

	if ( (app_resp->msg_id == CTRL_RESP_GET_AP_SCAN_LIST
	   || app_resp->msg_id == CTRL_RESP_GET_BLE_SCAN_LIST)
	  && ap_count < app_resp->u.wifi_ap_scan.count )
	{
		ret = SUCCESS;
		app_resp->u.wifi_ap_scan.out_list = out_list; // Update
	}
	if (gatt_added_or_updated)
		ret = SUCCESS;

	return ret;
} // uint8_t m1_parse_spi_at_resp(char *resp, const char *resp_key, ctrl_cmd_t *app_resp)

/*
https://docs.espressif.com/projects/esp-at/en/latest/esp32/AT_Command_Set/BLE_AT_Commands.html#cmd-bscan

cli> mtest 70 0
CLI mtest: ESP32 - init esp32 module

cli> mtest 72 AT+BLEINIT=1
CLI mtest: ESP32 - send AT command

AT+BLEINIT=1

OK

cli> mtest 72 AT+BLESCAN=1,3
CLI mtest: ESP32 - send AT command

AT+BLESCAN=1,3

OK

+BLESCAN:"21:bb:62:d8:f0:6a",-82,1e16f3fe4a172345524e4611321e7207d8255b90bc8efe50d3eb09fcd01099,,1,3

+BLESCAN:"c0:f8:53:87:27:00",-76,,,0,0

+BLESCAN:"f4:dd:06:22:e4:42",-65,02011819ff7500021834a1208f1e623f908407618fcf8befd925fa59cc,,0,0
...
+BLESCAN:"74:38:b7:4e:84:d6",-87,,020a06031900021109454f5352365f46373444453600000000,0,4
...
+BLESCAN:"f4:dd:06:22:e4:42",-80,02011819ff7500021834a1208f1e623f908407618fcf8befd925fa59cc,,0,0
+BLESCAN:"f4:dd:06:22:e4:42",-80,,,0,4
...
+BLESCAN:"b0:99:d7:b2:b4:eb",-76,1bff75004204018066b099d7b2b4ebb299d7b2b4ea01000000000000,,0,3

+BLESCAN:"b0:99:d7:b2:47:7b",-86,1bff75004204018066b099d7b2477bb299d7b2477a01000000000000,,0,3

+BLESCAN:"2c:99:75:bc:8e:23",-82,1bff750042040180662c9975bc8e232e9975bc8e2201000000000000,,0,3

+BLESCAN:"74:38:b7:4e:84:d6",-88,020106110721a8ff2f49d80000001000000000010009ffa90101f532f7fe05,,0,0

+BLESCAN:"f4:dd:06:22:e4:42",-78,02011819ff7500021834a1208f1e623f908407618fcf8befd925fa59cc,,0,0
+BLESCAN:"f4:dd:06:22:e4:42",-78,,,0,4

+BLESCAN:"7c:0a:3f:9b:d5:cd",-81,1bff750042040180667c0a3f9bd5cd7e0a3f9bd5cc01000000000000,,0,3

+BLESCAN:"56:6b:e2:a4:f4:a3",-67,1eff060001092002b0a35bbb626d95882709bad08dd1022b29e29ddd98672a,,1,3

+BLESCAN:"e0:74:5f:8b:a7:61",-76,0201181bff750042098102141503210149e217012d06bcb09700000000a300,,1,2

+BLESCAN:"e0:74:5f:8b:a7:61",-74,,0909427564732050726f10ff750000d1744ef04e80656526000101,1,4

+BLESCAN:"f4:dd:06:22:e4:42",-72,02011819ff7500021834a1208f1e623f908407618fcf8befd925fa59cc,,0,0
+BLESCAN:"f4:dd:06:22:e4:42",-75,,,0,4

+BLESCAN:"b0:99:d7:b2:47:7b",-85,1bff75004204018066b099d7b2477bb299d7b2477a01000000000000,,0,3

+BLESCAN:"21:bb:62:d8:f0:6a",-79,1e16f3fe4a172345524e4611321e7207d8255b90bc8efe50d3eb09fcd01099,,1,3

+BLESCAN:"e0:74:5f:8b:a7:61",-80,0201181bff750042098102141503210149e217012d06bcb09700000000a300,,1,2

+BLESCAN:"e0:74:5f:8b:a7:61",-75,,0909427564732050726f10ff750000d1744ef04e80656526000101,1,4

+BLESCAN:"2c:99:75:bc:8e:23",-75,1bff750042040180662c9975bc8e232e9975bc8e2201000000000000,,0,3

+BLESCAN:"c0:f8:53:87:27:00",-58,,,0,0

+BLESCAN:"c0:f8:53:87:27:00",-64,,09ff006052572d424c45,0,4

+BLESCAN:"f4:dd:06:22:e4:42",-66,02011819ff7500021834a1208f1e623f908407618fcf8befd925fa59cc,,0,0
+BLESCAN:"f4:dd:06:22:e4:42",-66,,,0,4

+BLESCAN:"e0:74:5f:8b:a7:61",-82,0201181bff750042098102141503210149e217012d06bcb09700000000a300,,1,2

+BLESCAN:"e0:74:5f:8b:a7:61",-82,,0909427564732050726f10ff750000d1744ef04e80656526000101,1,4

+BLESCAN:"20:57:9e:2f:55:cf",-81,0201051106f6e01ce42c53a1960f4d79c2371ad92608094d79512d353734,,0,0
+BLESCAN:"20:57:9e:2f:55:cf",-81,,0319000005ff78082200,0,4

+BLESCAN:"7c:0a:3f:9b:d5:cd",-74,1bff750042040180667c0a3f9bd5cd7e0a3f9bd5cc01000000000000,,0,3

+BLESCAN:"56:6b:e2:a4:f4:a3",-57,1eff060001092002b0a35bbb626d95882709bad08dd1022b29e29ddd98672a,,1,3

+BLESCAN:"b0:99:d7:b2:b4:eb",-75,1bff75004204018066b099d7b2b4ebb299d7b2b4ea01000000000000,,0,3

+BLESCAN:"e0:74:5f:8b:a7:61",-80,0201181bff750042098102141503210149e217012d06bcb09700000000a300,,1,2

+BLESCAN:"e0:74:5f:8b:a7:61",-75,,0909427564732050726f10ff750000d1744ef04e80656526000101,1,4

+BLESCAN:"c0:f8:53:87:27:00",-58,,,0,0

+BLESCAN:"2c:99:75:bc:8e:23",-71,1bff750042040180662c9975bc8e232e9975bc8e2201000000000000,,0,3

+BLESCAN:"f4:dd:06:22:e4:42",-81,02011819ff7500021834a1208f1e623f908407618fcf8befd925fa59cc,,0,0
+BLESCAN:"f4:dd:06:22:e4:42",-81,,,0,4

+BLESCAN:"20:57:9e:2f:55:cf",-79,0201051106f6e01ce42c53a1960f4d79c2371ad92608094d79512d353734,,0,0

+BLESCAN:"20:57:9e:2f:55:cf",-77,,0319000005ff78082200,0,4

+BLESCAN:"56:6b:e2:a4:f4:a3",-57,1eff060001092002b0a35bbb626d95882709bad08dd1022b29e29ddd98672a,,1,3

+BLESCAN:"f4:dd:06:22:e4:42",-69,0201181bff75004204018067f4dd0622e442f6dd0622e44130000000000000,,0,0

+BLESCAN:"f4:dd:06:22:e4:42",-70,,1008353022204372797374616c20554844,0,4

+BLESCAN:"e0:74:5f:8b:a7:61",-77,0201181bff750042098102141503210149e217012d06bcb09700000000a300,,1,2

+BLESCAN:"e0:74:5f:8b:a7:61",-82,,0909427564732050726f10ff750000d1744ef04e80656526000101,1,4

+BLESCAN:"56:6b:e2:a4:f4:a3",-70,1eff060001092002b0a35bbb626d95882709bad08dd1022b29e29ddd98672a,,1,3

+BLESCAN:"b0:99:d7:b2:b4:eb",-73,1bff75004204018066b099d7b2b4ebb299d7b2b4ea01000000000000,,0,3

+BLESCAN:"e0:74:5f:8b:a7:61",-76,0201181bff750042098102141503210149e217012d06bcb09700000000a300,,1,2

+BLESCAN:"e0:74:5f:8b:a7:61",-78,,0909427564732050726f10ff750000d1744ef04e80656526000101,1,4
+BLESCAN:"b0:99:d7:b2:47:7b",-88,1bff75004204018066b099d7b2477bb299d7b2477a01000000000000,,0,3

+BLESCAN:"2c:99:75:bc:8e:23",-81,1bff750042040180662c9975bc8e232e9975bc8e2201000000000000,,0,3

+BLESCAN:"c0:f8:53:87:27:00",-58,,,0,0

+BLESCAN:"56:6b:e2:a4:f4:a3",-69,1eff060001092002b0a35bbb626d95882709bad08dd1022b29e29ddd98672a,,1,3

+BLESCAN:"b0:99:d7:b2:47:7b",-84,1bff75004204018066b099d7b2477bb299d7b2477a01000000000000,,0,3

+BLESCAN:"c0:f8:53:87:27:00",-64,,09ff006052572d424c45,0,4
+BLESCAN:"20:57:9e:2f:55:cf",-92,0201051106f6e01ce42c53a1960f4d79c2371ad92608094d79512d353734,,0,0

+BLESCAN:"b0:99:d7:b2:b4:eb",-74,1bff75004204018066b099d7b2b4ebb299d7b2b4ea01000000000000,,0,3

+BLESCANDONE
 */


