<!-- See COPYING.txt for license details. -->

# M1 User Manual — pen-test & reconnaissance features

This manual covers the UI features added on top of stock M1 firmware
(phases 1 through 4). Stock features like the GPIO menu, settings, and
firmware update are unchanged and not duplicated here.

## Key conventions

- **OK** = short tap on the OK button.
- **Long-press OK** = hold OK for about 800 ms. Several screens use
  this to expose extra actions on the currently-selected item.
- **BACK** = leave the current screen / abort the current action.
- **UP / DOWN** = scroll the list / navigate menus.

## SD-card files this firmware looks for

| Path on SD                                  | Used by                                |
|---------------------------------------------|-----------------------------------------|
| `/databases/oui.bin`                        | WiFi & BLE scans (vendor name)         |
| `/databases/wifi_wordlist.txt`              | WiFi dictionary attack                 |
| `/databases/cracked.txt`                    | (output) cracked PSKs                  |
| `/databases/captures/*.22000`               | (output) hashcat handshake/PMKID files |
| `/INFRARED/db/{tv,audio,projector,ac}.ir`   | Universal Remotes + Mass Off           |
| `/INFRARED/exports/learned_*.ir`            | (output) signals you've learned        |

The Windows helper `scripts\flash_to_sd.bat E:` (run from the repo
root) builds the missing inputs and stages everything in one go.

---

## WiFi (Menu → WiFi → Scan AP)

### Scan view

Each AP entry now shows:

```
SSID
BSSID                       <-- the MAC
Vendor: TP-Link             <-- looked up from oui.bin
Ch:6 Auth:4                 <-- channel + encryption mode
```

- **UP / DOWN** — scroll APs.
- **OK** — does nothing (reserved for future use).
- **Long-press OK** — opens a 2-option chooser **on the currently
  displayed AP**:
  - **Dict attack** → online dictionary attack (see below).
  - **Capture hand** → passive handshake / PMKID capture (see below).
- **BACK** — back to WiFi menu.

### Long-press → Dict attack

Online dictionary attack against the AP's WPA-PSK. Tries each
password in `/databases/wifi_wordlist.txt` via `AT+CWJAP`.

Screens:

1. **Confirm.** Shows SSID + BSSID + warning that attempts are
   visible to the AP. OK to start, BACK to cancel.
2. **Progress.** Shows current candidate, n/total, elapsed time,
   last AT status (`wrong` / `timeout` / `no AP` / ...). BACK aborts.
3. **Result.**
   - **CRACKED** banner with the PSK + saved to `/databases/cracked.txt`.
   - **Aborted.** if you BACKed out.
   - **Not in dict.** if the list ran out.

Realistic time budget: ~3-8 s per attempt. A 4,800-entry list runs
~4-11 hours; a 62-entry "top probable" list runs in ~5 minutes and
catches most weak networks. Build wordlists with
`scripts/build_wifi_wordlist.py`.

### Long-press → Capture hand

**Passive** WPA handshake + PMKID capture for offline cracking on a
workstation with hashcat. Requires the patched ESP-AT firmware
(`documentation/ESP32/esp32c6-flash.bin`).

Screens:

1. **Confirm.** SSID/BSSID/channel + "Passive, no Tx" note.
2. **Progress.** elapsed / frames seen / PMKIDs / Handshakes
   counters. BACK aborts.
3. **Result.** record counts + the path on SD.

Files are written to:

```
/databases/captures/<ssid>_<bssid>_<tick>.22000
```

Pop the SD into a workstation and run:

```bash
hashcat -m 22000 captures/*.22000 rockyou.txt
```

PMKID records appear within seconds on vulnerable APs (most consumer
routers pre-~2020). Handshake records appear whenever any client
actually connects during the capture window.

---

## Sub-GHz (Menu → Sub-GHz)

```
Sub-GHz
 ├── Record               (existing — raw pulse capture)
 ├── Replay               (existing — replay saved .sgh)
 ├── Validate             ← new
 ├── Frequency Reader     (existing)
 └── Regional Information (existing)
```

### Validate

Compare a fresh remote press against a saved recording.

Workflow:
1. Use **Record** as usual on the remote you want to fingerprint —
   if the protocol decodes (PT2262/Princeton, Security+ 2.0, ...) a
   tiny `.sgv` sidecar is written next to the `.sgh`.
2. *Menu → Sub-GHz → Validate*. File browser opens. Pick the `.sgv`.
3. M1 enters RX on the saved frequency. Screen: "Press the remote..."
4. Press the remote. M1 captures one decoded packet and shows the
   verdict:

| Verdict | Meaning |
|---|---|
| **MATCH (identical)** | Bit-equal. Replay-attack works (for fixed code), or replay-in-progress (for rolling code — flagged **REPLAY RISK**). |
| **MATCH (rolling +N)** | Same physical remote, counter advanced by N. Security is working. |
| **MATCH (same family)** | Same protocol, different serial — sibling remote. |
| **NO MATCH** | Different protocol or fixed-code bits differ. |

BACK aborts; 30 s timeout if nothing arrives.

Deep dive: [documentation/subghz_validate.md](subghz_validate.md).

---

## Bluetooth (Menu → Bluetooth → Scan)

### Scan view

Each device entry shows:

```
BSSID
RSSI:-72dBm Typ:0           <-- BLE address type (0=public)
Vendor: Apple, Inc          <-- OUI lookup (or "(random addr)")
Apple iPhone/iPad           <-- adv-payload fingerprint
```

Recognised fingerprints include Apple Continuity (iPhone, iPad,
AirPods per model, Watch, AirTag, Find My, Handoff, ...), Microsoft
Swift Pair, Google Fast Pair, Samsung, Tile, Garmin, Eddystone,
HomeKit, and generic Local-Name / Appearance / Service-UUID fallbacks.

- **UP / DOWN** — scroll devices.
- **OK** — does nothing.
- **Long-press OK** — open the **GATT deep-probe** for the currently
  displayed device.
- **BACK** — back to Bluetooth menu.

### Long-press → GATT deep-probe

Connects to the device and walks its GATT services. No pairing
required; most peripherals accept anonymous connects (phones usually
don't).

Three drill-down screens:

1. **Device Info.** Pulled from the Device Information Service
   (0x180A): Mfr / Model / Firmware / Hardware / Serial. Missing
   fields show `-`.
   - OK or DOWN → screen 2.
   - BACK → disconnect and return to scan view.

2. **Services.** Scrollable list of every primary GATT service the
   device exposes. Friendly names where known (Battery, Heart Rate,
   HID, Eddystone, HomeKit, Apple FindMy, ...); otherwise short hex.
   - UP / DOWN → scroll services.
   - OK → drill into the selected service → screen 3.
   - BACK → screen 1.

3. **Characteristic detail.** Per-characteristic name, properties
   bitmap (`R`/`W`/`w`/`N`/`I`/...), and inline auto-read of the
   Read characteristics. ASCII rendering when printable, hex
   otherwise.
   - UP / DOWN → scroll characteristics.
   - BACK → screen 2.

Error messages: `Connect rc=N` (peer rejected / timeout),
`Discovery failed` (no services returned), `(no chars)` (service
empty), `v:(not readable)`.

---

## Infrared (Menu → Infrared)

```
Infrared
 ├── Universal Remotes
 │    ├── TV remotes
 │    ├── Audio player remotes
 │    ├── Projector remotes
 │    └── AC remotes
 ├── Mass Off                ← new
 │    ├── TVs (every Power)
 │    ├── Audio (every Power)
 │    ├── Projectors (every Pwr)
 │    └── ACs (every Off)
 ├── Learn                   ← now auto-exports
 └── Replay
```

### Universal Remotes

Unchanged behaviour. Press a button (Power / Vol / Mute / etc.) and
the M1 walks through every code in the database matching that
function, one per OK press. The database lives at
`/INFRARED/db/{tv,audio,projector,ac}.ir`.

Build the database from the Flipper-IRDB community library
(github.com/Lucaslhm/Flipper-IRDB) on your workstation:

```
python3 scripts/fetch_flipper_irdb.py
```

A recent run produces ~1,180 entries across the four files.

### Mass Off

Fires every Power code (or Off, for ACs) in the chosen category in
sequence, with a 100 ms inter-frame pause. Equivalent to TV-Be-Gone
but extended to receivers/projectors/ACs.

- Pick a category.
- The M1 starts immediately.
- Progress bar shows `n/total` and updates every few transmissions.
- BACK aborts.
- When the list is exhausted, banner reads "All sent. BACK".

Run times (with a ~1,180-entry Flipper IRDB):

| Category   | Codes | Approx duration |
|------------|------:|-----------------|
| TV         |    92 |  ~45 s          |
| Audio      |    54 |  ~25 s          |
| Projector  |    56 |  ~25 s          |
| AC (Off)   |   129 |  ~60 s          |

### Learn

Unchanged UI. Point a remote at the M1 and press a button; the M1
decodes it.

**New:** every successful capture is also written to
`/INFRARED/exports/learned_<tick>.ir` in Flipper-Zero format. Drop
the file into a Flipper's `infrared/` folder and it Just Works.

### Replay

Unchanged. Plays back the most recently learned signal.

---

## Quick troubleshooting

| Symptom                                          | Likely cause |
|--------------------------------------------------|--------------|
| Vendor lookup shows "Unknown" for everything     | `/databases/oui.bin` missing on SD |
| Dict attack shows "Open failed. No wordlist?"    | `/databases/wifi_wordlist.txt` missing on SD |
| Capture hand stuck on "frames seen: 0"           | ESP32 still running stock ESP-AT — flash the patched one at `documentation/ESP32/esp32c6-flash.bin` |
| GATT deep-probe says "Connect rc=N"              | Peer requires bonding (common for phones). Try a less paranoid peripheral. |
| BLE Services screen lists raw `0x180a` style     | Working as intended — the M1 names ~50 well-known UUIDs and falls back to hex for the rest. |
| Mass Off complains "DB error"                    | `/INFRARED/db/<type>.ir` missing or unreadable on SD |
| Learn writes nothing to `/INFRARED/exports/`     | SD not mounted; check status. The capture itself can still be Replayed from RAM. |

## Where the source for each feature lives

| Feature                | Files                                                  |
|------------------------|---------------------------------------------------------|
| OUI lookup             | `m1_csrc/m1_oui_lookup.{c,h}`                          |
| BLE adv fingerprint    | `m1_csrc/m1_ble_fingerprint.{c,h}`                     |
| BLE GATT probe         | `m1_csrc/m1_ble_gatt.{c,h}`, `m1_ble_probe_ui.{c,h}`   |
| WiFi dict attack       | `m1_csrc/m1_wifi_attack.{c,h}`, `..._attack_ui.{c,h}`  |
| WiFi handshake capture | `m1_csrc/m1_wifi_capture.{c,h}`, `..._capture_ui.{c,h}`, `m1_csrc/m1_pcap_writer.{c,h}`, `esp32_at_custom/` |
| IR Mass Off            | `Infrared/m1_ir_mass.{c,h}`                            |
| IR learn-export        | `Infrared/m1_ir_export.{c,h}`                          |
| IR DB builder          | `scripts/fetch_flipper_irdb.py`                        |
| SD card staging        | `scripts/flash_to_sd.bat`, `scripts/build_*.py`        |

Per-feature deep-dive docs:
[OUI/BLE ID](oui_database.md) ·
[BLE GATT](ble_gatt_probe.md) ·
[WiFi dict](wifi_dict_attack.md) ·
[WiFi handshake](wifi_handshake_capture.md) ·
[IR + Flipper](ir_flipper_compat.md).
