/* See COPYING.txt for license details. */

/*
 * m1_wifi_capture_ui.h
 *
 * Live capture UI: confirm -> monitor + record -> result. Writes
 * hashcat .22000 records to /databases/captures/<ssid>_<bssid>_<ts>.22000.
 *
 * M1 Project
 */

#ifndef M1_WIFI_CAPTURE_UI_H_
#define M1_WIFI_CAPTURE_UI_H_

void m1_wifi_capture_ui_run(const char *ssid, const char *bssid, int channel);

#endif /* M1_WIFI_CAPTURE_UI_H_ */
