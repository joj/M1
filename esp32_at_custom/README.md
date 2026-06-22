<!-- See COPYING.txt for license details. -->

# M1 ESP-AT custom component — passive WiFi capture

This component plugs into Espressif's ESP-AT firmware as a user
extension (per
<https://docs.espressif.com/projects/esp-at/en/latest/esp32c6/Compile_and_Develop/How_to_add_user-defined_AT_commands.html>),
without modifying any of the upstream ESP-AT source. It adds the AT
commands the M1 firmware drives during phase-3c "passive WiFi
capture":

| Command                          | What it does |
|----------------------------------|--------------|
| `AT+WIFISCAN=<chan>`             | Start promiscuous monitor on channel 1..14. `0` = hop 1..13 every ~250 ms. |
| `AT+WIFISTOP`                    | Stop monitor mode. |
| `AT+WIFIPMKID="<bssid>",<chan>`  | Pin the monitor channel so the M1/EAPOL of any associating client on the target BSSID lands. |

While monitoring, the firmware emits async lines:

```
+WIFIBEACON:<bssid_hex>,<chan>,<ssid_hex>
+WIFIEAPOL:<bssid_hex>,<sta_hex>,<msg_num>,<eapol_frame_hex>
+WIFIPMKID:<bssid_hex>,<sta_hex>,<pmkid_hex>
+WIFIDEAUTH:<bssid_hex>,<sta_hex>,<reason>
```

## Build

```bash
# Pre-reqs: ESP-IDF 5.3+ installed and exported.
source ~/esp/esp-idf/export.sh

cd ~/esp/esp-at
export AT_CUSTOM_COMPONENTS=$(realpath path/to/M1/esp32_at_custom)
./build.py set-target esp32c6
./build.py build

# Merge into a single flashable bin (matching the M1's existing layout
# per documentation/ESP32/M1 FW - ESP32 instructions.txt):
cd build
esptool.py --chip ESP32C6 merge_bin --format raw -o esp32c6-flash.bin \
    --flash_mode dio --flash_size 4MB \
    0x0000  bootloader/bootloader.bin \
    0x8000  partition_table/partition-table.bin \
    0xd000  ota_data_initial.bin \
    0x1e000 at_customize.bin \
    0x1f000 customized_partitions/mfg_nvs.bin \
    0x60000 esp-at.bin
md5sum esp32c6-flash.bin | awk '{print $1}' > esp32c6-flash.md5
```

Drop the resulting `esp32c6-flash.bin` and `esp32c6-flash.md5` into
`documentation/ESP32/`, then on the M1: *Menu → ESP32 Firmware Update*
to push them across SPI.

## Implementation notes

- Promiscuous filter is MGMT + DATA only (we don't need CTRL frames).
- Rx callback is short and ISR-safe; hex formatting + AT-port write
  happen in a separate FreeRTOS task fed by a 32-slot queue.
- EAPOL message number (1..4) derived from Key Info per IEEE 802.11i
  Table 11-6.
- PMKID extraction scans the EAPOL-Key data field for the Pairwise
  Master Key Name KDE (selector `00:0F:AC:04`, 16-byte value).
- Channel-hop walks 1..13 every 250 ms.

## Limitations

- `AT+WIFIPMKID` currently just pins the channel; future revision can
  actively issue a probe association via `esp_wifi_connect` to elicit
  M1 without waiting for a real client.
- No WPA3 SAE decoding (different hash mode).
- No deauth injection — stays unambiguously passive.
