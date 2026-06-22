/* See COPYING.txt for license details. */

/*
 * m1_wifi_attack_ui.h
 *
 * On-device UI for the WiFi WPA-PSK dictionary attack. Invoked from
 * m1_wifi.c::wifi_scan_ap when the user long-presses OK on a selected AP.
 *
 * M1 Project
 */

#ifndef M1_WIFI_ATTACK_UI_H_
#define M1_WIFI_ATTACK_UI_H_

void m1_wifi_attack_ui_run(const char *ssid, const char *bssid);

#endif /* M1_WIFI_ATTACK_UI_H_ */
