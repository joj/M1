<!-- See COPYING.txt for license details. -->

# BLE GATT deep-probe (phase 2)

When the user finds an interesting BLE device in the scan view, the M1
can drill into it over GATT to read product-level details (manufacturer,
model, firmware, serial) and enumerate its service catalog. Strictly
passive in the sense that no pairing is required — most BLE peripherals
accept anonymous connections.

## How to use

1. Menu → **Bluetooth → Scan**.
2. Walk UP/DOWN to the device you're curious about.
3. **Long-press OK** (~800 ms) on that device.
4. M1 connects and shows three screens, navigated with the keypad:

   - **Screen 1 – Device Info.** Manufacturer, Model number, Firmware
     revision, Hardware revision, Serial number. Anything the device
     doesn't expose shows up as `-`.
     OK or DOWN → screen 2; BACK → disconnect and return to scan view.

   - **Screen 2 – Services.** Scrollable list of every primary GATT
     service the device advertises. Friendly names where the UUID is
     Bluetooth-SIG assigned (Battery, Heart Rate, HID, Eddystone, ...);
     otherwise short hex.
     UP/DOWN to scroll, OK to drill in, BACK → screen 1.

   - **Screen 3 – Characteristic detail.** For the selected service,
     shows each characteristic with friendly name, property bitmap
     (R/W/N/I/...), and — for Read characteristics — the value
     auto-read inline. ASCII rendering when printable, hex otherwise.
     UP/DOWN to scroll through characteristics, BACK → screen 2.

5. Final BACK from screen 1 disconnects and returns to the scan view.

## Compatibility notes

- Phones (iPhone/Android) usually reject anonymous connects from a
  non-bonded central → expect `Connect rc=...`.
- Many fitness trackers, smart bulbs, BLE thermometers, dev boards, and
  open-DIS peripherals connect happily.
- Discovery + DIS read typically takes 2–5 s after a successful
  connect.
- Per-operation timeout is ~6 s; connect timeout is 5 s.

## On-device error messages

| Message              | Meaning                                                    |
|----------------------|------------------------------------------------------------|
| `Connect rc=N`       | ESP-AT connect returned status N (non-zero = peer/timeout).|
| `Discovery failed`   | Primary-service enumeration didn't return any records.     |
| `Char list failed`   | Characteristic enumeration on the chosen service failed.   |
| `(no chars)`         | Service has no advertised characteristics.                 |
| `v:(not readable)`   | Selected characteristic doesn't support Read.              |

## What's in the firmware

| File                                                     | Role |
|----------------------------------------------------------|------|
| `Esp_spi_at/.../esp_app_main.c` (`ble_gatt_*`)           | AT command wrappers: `AT+BLECONN`, `AT+BLEDISCONN`, `AT+BLEGATTCPRIMSRV`, `AT+BLEGATTCCHAR`, `AT+BLEGATTCRD`. |
| `Esp_spi_at/.../m1_at_response_parser.c`                 | Parsers for the matching `+BLE*` responses. |
| `Esp_spi_at/.../ctrl_api.h`                              | New typed payload structs (`ble_gatt_srv_t`, `ble_gatt_char_t`, ...) in the response union. |
| `m1_csrc/m1_ble_uuid_names.{c,h}`                        | Friendly names for SIG-assigned 16-bit service / characteristic UUIDs. |
| `m1_csrc/m1_ble_gatt.{c,h}`                              | High-level synchronous API used by the UI: `probe_open / services / chars / dis / close`. |
| `m1_csrc/m1_ble_probe_ui.{c,h}`                          | Three-screen UI; owns the interaction loop. |
| `m1_csrc/m1_bt.c`                                        | Long-press OK detection in the BLE scan view; invokes the probe UI. |

## Future work (phase 3)

Pen-testing endgame: WiFi password dictionary attack (online try-
and-associate + offline handshake crack) and active LAN recon once
associated. See `plan.md` in the session workspace.
