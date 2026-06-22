<!-- See COPYING.txt for license details. -->

# WiFi WPA-PSK dictionary attack (phase 3)

When you find an AP of interest in the wifi scan view, the M1 can run
an **online dictionary attack** against its WPA-PSK by repeatedly
trying `AT+CWJAP=<ssid>,<pwd>` with passwords drawn from a wordlist on
the SD card.

> ⚠️ This is an active, observable attack. Each attempt is an
> association request the AP sees and may log. Use it only on networks
> you own or have explicit permission to audit.

## How to use

1. Build a wordlist on your workstation and copy it to the SD card:

   ```
   python3 scripts/build_wifi_wordlist.py
   # -> Wrote wifi_wordlist.txt — 49,160 candidates, ~466 KB
   ```

   Copy `wifi_wordlist.txt` to the SD card at
   `/databases/wifi_wordlist.txt`.

2. Boot the M1, **Menu → WiFi → Scan AP**.
3. UP/DOWN to the target AP.
4. **Long-press OK** (~800 ms) on that AP.
5. Confirmation screen — review SSID / BSSID, press OK to start
   (BACK cancels).
6. Progress screen ticks through candidates. UP/DOWN does nothing
   here (the loop is busy with attempts); BACK aborts.
7. Result:
   - **CRACKED** banner with the PSK — also appended to
     `/databases/cracked.txt` on the SD card.
   - `Not in dict.` if the wordlist ran out.
   - `Aborted.` if you BACKed out early.

## Wordlist

Default source is **SecLists** — the de-facto canonical pen-test
wordlist repository:

| File on SecLists                                                    | Why |
|----------------------------------------------------------------------|-----|
| `Passwords/WiFi-WPA/probable-v2-wpa-top62.txt`                      | 62 most common WPA passwords (~5 min run) |
| `Passwords/WiFi-WPA/probable-v2-wpa-top4800.txt`                    | curated WPA-valid (8+ chars) top-4800 |
| `Passwords/Common-Credentials/Pwdb_top-100000.txt`                  | the top-100k from password breaches, filtered for PSK validity |

The script:
- Filters every candidate for WPA-PSK rules: length 8–63, printable
  ASCII (anything outside that can't be associated with via WPA-PSK
  anyway).
- Dedupes preserving first-appearance order.
- Writes a plain UTF-8 text file with one candidate per line.

Useful flags:

```
python3 scripts/build_wifi_wordlist.py --top 1000     # cap at 1000 entries
python3 scripts/build_wifi_wordlist.py --source rockyou.txt --top 10000
python3 scripts/build_wifi_wordlist.py --no-network --source ./local.txt
```

The on-device iterator prepends a handful of **SSID-derived seeds**
to whatever's in the file (e.g. `<ssid>`, `<ssid>123`, `<ssid>2024`,
`<ssid>password`) since router-default PSKs frequently include the
SSID.

## How long will it take?

Each association attempt takes ~3-8 s of real time (AP-side handshake
plus our ~500 ms inter-attempt sleep). So:

| Wordlist size | Optimistic | Pessimistic |
|---------------|-----------:|------------:|
| 62            |     ~3 min |      ~8 min |
| 1,000         |    ~50 min |      ~2.2 h |
| 4,800         |     ~4 h   |    ~11 h    |
| 49,000        |    ~41 h   |     ~4.5 d  |

In practice most APs that aren't using a generated PSK fall to the
WPA-top-62 list in minutes. Networks with strong unique PSKs won't
fall to *any* online list — that's what offline cracking (phase 3c,
not implemented yet) is for.

## What's in the firmware

| File                                                  | Role |
|-------------------------------------------------------|------|
| `Esp_spi_at/.../esp_at_list.h`                        | AT+CWMODE, AT+CWJAP, AT+CWQAP defines. |
| `Esp_spi_at/.../esp_app_main.c` `wifi_*`              | AT wrappers, especially `wifi_try_connect()` that watches for OK / FAIL / `+CWJAP:<err>`. |
| `Esp_spi_at/.../ctrl_api.h` `wifi_try_result_t`       | Typed status (TRY_OK / WRONG_PASS / TIMEOUT / NO_AP / OTHER). |
| `m1_csrc/m1_wifi_attack.{c,h}`                        | SD wordlist iterator, SSID-seed builder, single-try AT wrapper, hit-recorder. |
| `m1_csrc/m1_wifi_attack_ui.{c,h}`                     | Three-screen UI: confirm → progress → result. |
| `m1_csrc/m1_wifi.c`                                   | Long-press OK detection in `wifi_scan_ap` → invoke the attack UI. |

## Future work

- **Phase 3a — passive WiFi vendor IE / WPS harvesting.** ESP-AT side
  change to surface 802.11 beacon vendor IEs and WPS device name.
- **Phase 3c — offline handshake capture.** Promiscuous-mode EAPOL
  capture + dump-to-SD as `.cap` so you can crack arbitrarily large
  lists on a workstation with hashcat. Optional embedded PBKDF2-based
  cracker for very small lists.
- **Phase 3d — LAN recon.** Once associated, do ARP sweep, top-100
  port scan, mDNS / SSDP / NetBIOS / UPnP discovery, render on-device.
