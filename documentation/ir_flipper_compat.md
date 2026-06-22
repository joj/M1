<!-- See COPYING.txt for license details. -->

# Flipper-compatible IR remotes & Mass Off

The M1's IR file format is the same one Flipper Zero uses (header
`Filetype: IR signals file` / `Version: 1`, keys `name:`, `protocol:`,
`address:`, `command:` for parsed signals, `frequency:` + `data:` for
raw). So we get Flipper IRDB interoperability essentially for free:

- **The full Flipper-IRDB** (~9k `.ir` files at
  github.com/Lucaslhm/Flipper-IRDB) repacks into the four files the M1
  reads → universal remote covers thousands of TVs / receivers /
  projectors / ACs.
- **Mass Off** walks every Power code in the file for one device type
  and fires them in sequence — equivalent to a TV-Be-Gone but for
  receivers / projectors / ACs too.
- **Learn → Export** writes captured signals to
  `/INFRARED/exports/learned_<tick>.ir` in pure Flipper format so you
  can hand them off to a Flipper or replay later.

## Building the IR database (on your workstation)

```bash
python3 scripts/fetch_flipper_irdb.py
# Default output: ./ir_db_out/INFRARED/db/{tv,audio,projector,ac}.ir
```

The default fetch consumes about 1,000-1,200 deduplicated entries
across the four categories. Distribution from a recent run:

| File           | Entries | Power codes |
|----------------|--------:|------------:|
| `tv.ir`        |     536 |      92 TVs |
| `audio.ir`     |     304 |      54 RXs |
| `projector.ir` |     179 |  56 projs   |
| `ac.ir`        |     159 |   129 ACs (Off) |

Flags:
- `--out <path>`         change output directory (default: `./ir_db_out`)
- `--offline <path>`     reuse an existing Flipper-IRDB clone
- `--keep-clone`         don't delete the temp clone after building

## Staging on the SD card

The Windows batch helper does it all in one shot:

```
scripts\flash_to_sd.bat E:
```

This now also builds (if missing) and copies the IR database to
`<SD>:\INFRARED\db\{tv,audio,projector,ac}.ir`. Pass `--no-irdb` to
skip that step if you have your own DB.

## Using Mass Off on-device

1. *Menu → Infrared → Mass Off*.
2. Pick the device type:
   - "TVs (every Power)"
   - "Audio (every Power)"
   - "Projectors (every Pwr)"
   - "ACs (every Off)"
3. The M1 walks the file, transmitting each Power/Off code with a
   100 ms inter-frame pause. Progress bar shows `n/total`.
4. BACK aborts.

Typical run times (with 100 ms inter-frame + each frame ~100-200 ms):
- TVs: ~45 s for 92 codes
- Audio: ~25 s for 54 codes
- Projectors: ~25 s for 56 codes
- ACs: ~60 s for 129 codes

Most consumer devices will turn off within the first ~20 codes
matching their brand (since the IRDB is alphabetised). If yours
doesn't, hit BACK and use the Learn flow instead.

## Protocols supported

From `ir_protocols_mapping_table[]` in
`Infrared/m1_ir_remotes.c`. Entries using protocols outside this list
are skipped by the fetch script:

```
UNKNOWN, SIRC, SIRC15, SIRC20, NEC, NECext, NEC42,
RC5, RC5X, RC6, Samsung32, Kaseikyo, RCA, Pioneer,
raw
```

WPA-IE-style coverage of consumer IR — Pronto Hex (used by some
Flipper entries) is **not** supported; those entries are dropped.

## Learn → Export

When you capture a remote button via *Menu → Infrared → Learn*, the M1
now also writes it to `/INFRARED/exports/learned_<tick>.ir` on the SD
card. The file is in pure Flipper format and drops straight into a
Flipper Zero's `infrared/` folder:

```
Filetype: IR signals file
Version: 1
#
name: Learned
type: parsed
protocol: NEC
address: 04 00 00 00
command: 0A 00 00 00
#
```
