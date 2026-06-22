<!-- See COPYING.txt for license details. -->

# Device identification on wifi & BLE scans

The M1 wifi and BLE scan views can annotate each result with a best-effort
identification of the device behind the MAC address. Two passive sources are
used (no connection or pairing required):

1. **IEEE OUI registry** → manufacturer name.
   The first three bytes of any globally-administered MAC address identify
   the company that registered the OUI block. Works for wifi APs and for
   BLE *public* addresses. BLE random / RPA addresses have no meaningful
   OUI and are labelled `(random addr)` instead.

2. **BLE advertising-data fingerprint** → device type.
   BLE peripherals broadcast their identity in adv payloads. The M1 parses
   the standard GAP AD structures (Local Name, Appearance, Service UUIDs)
   and recognises well-known manufacturer-specific payloads:
   - Apple Continuity (`0x004C`) — iPhone/iPad (Nearby Info), AirPods/Beats
     (with per-model name), Watch, AirTag/Find My, Handoff, Nearby Action,
     HomeKit, Siri, AirDrop, Apple TV setup.
   - Microsoft (`0x0006`) — Swift Pair, Surface Pen, CDP beacon.
   - Google (`0x00E0`) — Fast Pair model ID.
   - Samsung (`0x0075`), Garmin (`0x0087`), Tile (`0x015D`), Xiaomi
     (`0x038F`), Logitech (`0x00D2`), Sony (`0x012D`), Espressif (`0x02E5`),
     and others.

## Building the OUI database

The OUI database is **not** bundled in the firmware — it lives as a binary
file on the SD card. Generate it once on your workstation, then copy it to
the card.

```bash
# Needs Python 3.7+ and network access.
python3 scripts/build_oui_db.py
# -> Wrote oui.bin — 39,000+ OUIs, ~1.2 MB
```

Optional flags:

```bash
python3 scripts/build_oui_db.py \
    --source /path/to/oui.csv \   # use an offline copy instead of fetching IEEE
    --out    /media/sd/oui.bin    # write directly to a mounted SD card
```

The default IEEE source URL is
<https://standards-oui.ieee.org/oui/oui.csv> and is freely redistributable.

## SD card layout

Copy `oui.bin` to the M1 SD card at:

```
/databases/oui.bin
```

(or, fully qualified, `0:/databases/oui.bin` in M1/FatFs notation).

If the file is absent, OUI lookup degrades silently to `Unknown` —
everything still works, scans just don't show vendors. The BLE
adv-payload fingerprint does not depend on the database and continues to
work either way.

## Binary format (informational)

```
offset  size  meaning
0       8     magic   = "M1OUI001"
8       4     count   (uint32 LE)
12      4     reclen  (uint32 LE) = 32
16      N*32  records, sorted ascending by 3-byte OUI

each record:
0       3     OUI bytes (big-endian, e.g. 00 1A 2B)
3       29    vendor name, ASCII, null-padded
```

Records are sorted on the OUI key so the firmware can binary-search the file
on the SD card without loading it into RAM. A 16-entry LRU cache absorbs
repeated lookups during a single scan.

## Updating the database

IEEE publishes new OUI assignments roughly every few days. Re-run
`scripts/build_oui_db.py` whenever you want a fresh snapshot and overwrite
the file on the SD card.

## Testing the BLE fingerprinter on your host

A standalone host test compiles `m1_ble_fingerprint.c` without any STM32
dependencies and verifies it against a dozen known adv payloads:

```bash
gcc -std=c11 -Wall -Wextra -I m1_csrc \
    scripts/test_ble_fingerprint.c m1_csrc/m1_ble_fingerprint.c -o tbf
./tbf
```
