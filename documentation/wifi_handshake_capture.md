<!-- See COPYING.txt for license details. -->

# Passive WiFi handshake / PMKID capture (phase 3c)

Passive capture of the WPA-PSK material needed to crack a network
offline on a workstation GPU with **hashcat** (mode 22000) or
aircrack-ng. The M1 turns into a hashcat capture device: it listens,
the AP doesn't see it, and on the SD card you get hashcat-ready
`.22000` files.

## How to use

> **Prerequisite:** this feature requires the M1 ESP32 to be running
> the **patched ESP-AT firmware** from `esp32_at_custom/`. Stock
> ESP-AT does **not** expose promiscuous mode. See *"Building the
> ESP-AT firmware"* below.

1. Menu → WiFi → Scan AP.
2. UP/DOWN to the target AP.
3. **Long-press OK** → choose **"Capture hand"**.
4. Confirm screen — review SSID/BSSID/channel. OK to start. (No
   frames are sent during capture; the screen warning is just so you
   know what's happening.)
5. Progress screen ticks "elapsed / frames seen / PMKIDs / Handshakes".
   - **PMKID** records appear seconds after monitoring starts on a
     vulnerable AP (most consumer routers pre-~2020).
   - **Handshake** records appear whenever any client connects to the
     AP during your capture window.
6. BACK to stop. Result screen shows record counts and the SD path.
7. Pop the SD card into your workstation:
   ```
   hashcat -m 22000 captures/MyNet_aabbcc112233_12345.22000 my_wordlist.txt
   ```

## File layout on the SD card

```
/databases/captures/<sanitized_ssid>_<bssid_hex>_<tick>.22000
```

One file per UI session; records (PMKID and/or handshake) are
appended as they're captured. Hashcat can also take an entire
directory as input — just point it at `captures/` and it'll process
everything.

## Building the ESP-AT firmware

The M1's stock ESP-AT firmware doesn't expose promiscuous mode. We
ship a small custom user-component in `esp32_at_custom/` that adds
three AT commands and four async event lines. Build once with the
ESP-IDF 5.3+ toolchain, then push the resulting flash image to the
ESP32 via the existing on-device ESP32 firmware update.

```bash
# One-time prerequisites (Ubuntu / WSL):
sudo apt install -y python3-venv python3-pip libusb-1.0-0

# Clone esp-at + esp-idf 5.3.x (~3 GB total, one-time):
mkdir -p ~/esp && cd ~/esp
git clone --recursive --depth 1 https://github.com/espressif/esp-at.git
git clone -b v5.3.2 --recursive --depth 1 --shallow-submodules \
    https://github.com/espressif/esp-idf.git
~/esp/esp-idf/install.sh esp32c6

# Build:
source ~/esp/esp-idf/export.sh
cd ~/esp/esp-at
export AT_CUSTOM_COMPONENTS=$(realpath path/to/M1/esp32_at_custom)
python build.py set-target esp32c6
python build.py build

# Merge into a single flash image (matches M1's existing layout, per
# documentation/ESP32/M1 FW - ESP32 instructions.txt):
cd build
esptool.py --chip ESP32C6 merge_bin --format raw \
    -o esp32c6-flash.bin --flash_mode dio --flash_size 4MB \
    0x0000  bootloader/bootloader.bin \
    0x8000  partition_table/partition-table.bin \
    0xd000  ota_data_initial.bin \
    0x1e000 at_customize.bin \
    0x1f000 customized_partitions/mfg_nvs.bin \
    0x60000 esp-at.bin
md5sum esp32c6-flash.bin | awk '{print $1}' > esp32c6-flash.md5
```

Copy `esp32c6-flash.bin` and `esp32c6-flash.md5` into
`documentation/ESP32/`, then on the M1: *Menu → ESP32 Firmware
Update*.

The custom AT commands added on top of stock ESP-AT:

| AT command                       | Purpose                                       |
|----------------------------------|-----------------------------------------------|
| `AT+WIFISCAN=<chan>`             | Start promiscuous monitor; 0 = channel-hop    |
| `AT+WIFISTOP`                    | Stop monitor + return to normal STA mode      |
| `AT+WIFIPMKID="<bssid>",<chan>`  | Pin the monitor channel to a single target    |

Async event lines emitted while monitoring (all hex, lowercase):

```
+WIFIBEACON:<bssid12>,<chan>,<ssid_hex>
+WIFIEAPOL:<bssid12>,<sta12>,<msg_num_1_to_4>,<eapol_frame_hex>
+WIFIPMKID:<bssid12>,<sta12>,<pmkid32>
+WIFIDEAUTH:<bssid12>,<sta12>,<reason>
```

## What's in the M1 firmware

| File                                                  | Role |
|-------------------------------------------------------|------|
| `Esp_spi_at/.../esp_at_list.h`                        | New AT command + response-key strings. |
| `Esp_spi_at/.../esp_app_main.{c,h}` `wifi_monitor_*` / `wifi_capture_pump` | AT wrappers + the event pump that pulls async `+WIFI*` lines from the SPI driver. |
| `m1_csrc/m1_pcap_writer.{c,h}`                        | Hashcat `.22000` line formatter (pure logic). |
| `m1_csrc/m1_wifi_capture.{c,h}`                       | Per-(BSSID,STA) state machine; emits one record per complete capture. |
| `m1_csrc/m1_wifi_capture_ui.{c,h}`                    | Confirm / live stats / result UI; opens the SD file lazily. |
| `m1_csrc/m1_wifi.c`                                   | Long-press OK now offers Dictionary or Capture. |
| `scripts/test_pcap_writer.c`                          | Host-side smoke test: 22/22 pass. |

## What we DON'T do (and why)

- **No deauth.** Easy to add (`esp_wifi_80211_tx` is publicly exposed),
  but it's an active frame that some APs log. Out of scope for
  "passive" v1.
- **No WPA3 SAE.** Different hashcat mode (22000 PROTOCOL=WPA only
  covers WPA/WPA2). SAE-only networks won't yield 4-way handshakes
  here.
- **No on-device cracking.** PBKDF2 on STM32H5 is ~0.2-1k cand/s vs
  ~100k-1M on a midrange GPU. Use the workstation.
