/* See COPYING.txt for license details. */

/*
*
* esp_at_list.h
*
* AT commands list
*
* M1 Project
*
*/

#ifndef ESP32C6_AT_LIST_H_
#define ESP32C6_AT_LIST_H_

#define CONCAT_CMD_PARAM(cmd, param)		cmd param ESP32C6_AT_REQ_CRLF

#define ESP32C6_WIFI_MODE_NULL		"0" // Null mode. Wi-Fi RF will be disabled.
#define ESP32C6_WIFI_MODE_STA		"1" // Station mode.
#define ESP32C6_WIFI_MODE_AP		"2" // SoftAP mode.
#define ESP32C6_WIFI_MODE_APSTA		"3" // SoftAP+Station mode.

#define ESP32C6_BLE_MODE_NULL		"0" // deinit Bluetooth LE
#define ESP32C6_BLE_MODE_CLI		"1" // client role
#define ESP32C6_BLE_MODE_SER		"2" // server role

// Syntax: AT+CWMODE=<mode>[,<auto_connect>]
// Response; OK
#define ESP32C6_AT_REQ_WIFI_MODE		"AT+CWMODE="

#define ESP32C6_AT_REQ_LIST_AP			"AT+CWLAP"
#define ESP32C6_AT_RES_LIST_AP_KEY		"+CWLAP:"

#define ESP32C6_AT_REQ_CRLF				"\r\n"

#define ESP32C6_AT_RES_OK				"OK"
#define ESP32C6_AT_RES_READY			"ready"

#define ESP32C6_AT_REQ_BLE_MODE			"AT+BLEINIT="
#define ESP32C6_AT_REQ_BLE_SCAN			"AT+BLESCAN=1," // AT+BLESCAN=<enable>[,<duration>][,<filter_type>,<filter_param>]
#define ESP32C6_AT_RES_BLE_SCAN_KEY		"+BLESCAN:"

#define ESP32C6_AT_REQ_ADVERTISE		"AT+BLEADVDATAEX=" // AT+BLEADVDATAEX=<dev_name>,<uuid>,<manufacturer_data>,<include_power>
#define ESP32C6_AT_REQ_ADV_DATA			"\"MONSTATEK-M1\",\"A000\",\"1A2B3C4D5E\",1"

/* BLE GATT client commands (phase 2 deep-probe).
 *   AT+BLECONN=<conn_idx>,"<addr>",<addr_type>,<timeout>      -> +BLECONN:<idx>,<status>
 *   AT+BLEDISCONN=<conn_idx>                                  -> OK / +BLEDISCONN:<idx>,"<addr>"
 *   AT+BLEGATTCPRIMSRV=<conn_idx>                             -> +BLEGATTCPRIMSRV:<idx>,<srv_idx>,<uuid>,<srv_type>
 *   AT+BLEGATTCCHAR=<conn_idx>,<srv_idx>                      -> +BLEGATTCCHAR:<idx>,"char",<srv_idx>,<char_idx>,<uuid>,<props>
 *                                                             -> +BLEGATTCCHAR:<idx>,"desc",<srv_idx>,<char_idx>,<desc_idx>,<uuid>
 *   AT+BLEGATTCRD=<conn_idx>,<srv_idx>,<char_idx>[,<desc_idx>] -> +BLEGATTCRD:<idx>,<len>,<value_hex>
 */
#define ESP32C6_AT_REQ_BLE_CONN			"AT+BLECONN="
#define ESP32C6_AT_REQ_BLE_DISCONN		"AT+BLEDISCONN="
#define ESP32C6_AT_REQ_BLE_PRIMSRV		"AT+BLEGATTCPRIMSRV="
#define ESP32C6_AT_REQ_BLE_GATTCHAR		"AT+BLEGATTCCHAR="
#define ESP32C6_AT_REQ_BLE_GATTRD		"AT+BLEGATTCRD="

#define ESP32C6_AT_RES_BLE_CONN_KEY		"+BLECONN:"
#define ESP32C6_AT_RES_BLE_PRIMSRV_KEY	"+BLEGATTCPRIMSRV:"
#define ESP32C6_AT_RES_BLE_GATTCHAR_KEY	"+BLEGATTCCHAR:"
#define ESP32C6_AT_RES_BLE_GATTRD_KEY	"+BLEGATTCRD:"

#define ESP32C6_AT_RESET				"AT+RST"

/* Phase 3 — WPA station/join (dictionary-attack feature).
 *   AT+CWMODE=1                   -> station mode
 *   AT+CWJAP="<ssid>","<pwd>"     -> connect; OK / +CWJAP:<err> / FAIL
 *   AT+CWQAP                      -> disconnect
 */
#define ESP32C6_AT_REQ_STATION_MODE   "AT+CWMODE=1"
#define ESP32C6_AT_REQ_CWJAP          "AT+CWJAP="
#define ESP32C6_AT_REQ_CWQAP          "AT+CWQAP"
#define ESP32C6_AT_RES_CWJAP_FAIL_KEY "+CWJAP:"
#define ESP32C6_AT_RES_FAIL           "FAIL"

#endif /* ESP32C6_AT_LIST_H_ */
