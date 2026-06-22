#!/usr/bin/env python3
# See COPYING.txt for license details.
"""
fetch_flipper_irdb.py — pull the Flipper-Zero community IR database and
repack it into the four consolidated `.ir` files the M1 firmware expects.

The Flipper-IRDB lives at https://github.com/Lucaslhm/Flipper-IRDB and uses
exactly the file format the M1 already speaks (`Filetype: IR signals file`,
`name:`, `protocol:`, `address:`, `command:`, `data:` ...). We:

  1. clone (shallow) the IRDB,
  2. walk a curated subset of category folders,
  3. drop entries that use protocols the M1 firmware doesn't support,
  4. normalise the function name to the M1's expected vocabulary,
  5. dedupe by (protocol, address, command, data),
  6. concatenate everything into one file per category,
  7. write into the SD-card layout: `<out>/INFRARED/db/{tv,audio,projector,ac}.ir`

Output is meant to live on the SD card at `0://INFRARED/db/`. The M1's
existing "Universal Remotes" UI plays the first matching entry per
function; phase-4's new "Mass Off" mode walks every Power/Off entry in
sequence.

Usage:
    python3 fetch_flipper_irdb.py                       # default out dir
    python3 fetch_flipper_irdb.py --out C:/sd_stage
    python3 fetch_flipper_irdb.py --offline ./Flipper-IRDB  # reuse a local clone
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

IRDB_URL = "https://github.com/Lucaslhm/Flipper-IRDB.git"

# Protocol names the M1 firmware actually supports (from
# ir_protocols_mapping_table[] in Infrared/m1_ir_remotes.c). Raw signals
# always pass through. Case-sensitive match against the file's `protocol:`
# field.
SUPPORTED_PROTOCOLS = {
    "UNKNOWN",
    "SIRC", "SIRC15", "SIRC20",
    "NEC", "NECext", "NEC42",
    "RC5", "RC5X", "RC6",
    "Samsung32",
    "Kaseikyo",
    "RCA",
    "Pioneer",
}

# Function-name canonicalisation per category. Keys are lowercased Flipper
# names that we'll accept; values are the exact name the M1 expects.
TV_NAMES = {
    "power": "Power", "pwr": "Power", "power_off": "Power", "poweroff": "Power",
    "on/off": "Power", "on_off": "Power",
    "mute": "Mute", "mute_unmute": "Mute",
    "vol_up": "Vol_up", "volume_up": "Vol_up", "vol+": "Vol_up", "volup": "Vol_up",
    "vol_dn": "Vol_dn", "vol_down": "Vol_dn", "volume_down": "Vol_dn", "vol-": "Vol_dn", "voldn": "Vol_dn",
    "ch_next": "Ch_next", "ch+": "Ch_next", "ch_up": "Ch_next", "channel_up": "Ch_next",
    "ch_prev": "Ch_prev", "ch-": "Ch_prev", "ch_down": "Ch_prev", "channel_down": "Ch_prev",
}
AUDIO_NAMES = {
    "power": "Power", "on/off": "Power", "on_off": "Power",
    "mute": "Mute",
    "play": "Play",
    "pause": "Pause", "play_pause": "Play", "playpause": "Play",
    "vol_up": "Vol_up", "volume_up": "Vol_up", "vol+": "Vol_up",
    "vol_dn": "Vol_dn", "vol_down": "Vol_dn", "vol-": "Vol_dn",
    "next": "Next", "track_next": "Next", "skip_next": "Next", "fwd": "Next",
    "prev": "Prev", "track_prev": "Prev", "skip_prev": "Prev", "rew": "Prev",
}
PROJECTOR_NAMES = {
    "power": "Power", "on/off": "Power", "on_off": "Power",
    "mute": "Mute",
    "vol_up": "Vol_up", "vol+": "Vol_up", "volume_up": "Vol_up",
    "vol_dn": "Vol_dn", "vol-": "Vol_dn", "volume_down": "Vol_dn",
}
AC_NAMES = {
    "off": "Off", "power": "Off", "power_off": "Off",
    "dh": "Dh", "dry": "Dh", "dehumidify": "Dh",
    "cool_hi": "Cool_hi", "cool_high": "Cool_hi",
    "cool_lo": "Cool_lo", "cool_low": "Cool_lo",
    "heat_hi": "Heat_hi", "heat_high": "Heat_hi",
    "heat_lo": "Heat_lo", "heat_low": "Heat_lo",
}

CATEGORIES = [
    # (Flipper top-level dir, output filename, name map, max entries kept)
    ("TVs",                       "tv.ir",        TV_NAMES,        2000),
    ("Audio_and_Video_Receivers", "audio.ir",     AUDIO_NAMES,     1500),
    ("Projectors",                "projector.ir", PROJECTOR_NAMES, 1000),
    ("ACs",                       "ac.ir",        AC_NAMES,        1500),
]


def parse_ir_file(path: Path):
    """Yield dicts: {name, type, protocol, address, command, frequency,
    duty_cycle, data} for each entry. Best-effort tolerant parser."""
    entry = {}
    with path.open("r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if not line or line.startswith("#"):
                if entry:
                    yield entry
                    entry = {}
                continue
            if line.startswith("Filetype:") or line.startswith("Version:"):
                continue
            m = re.match(r"^([A-Za-z_]+):\s*(.*)$", line)
            if not m:
                continue
            key, val = m.group(1), m.group(2).strip()
            entry[key] = val
    if entry:
        yield entry


def is_supported(entry: dict) -> bool:
    t = entry.get("type", "")
    if t == "raw":
        return True
    if t == "parsed":
        return entry.get("protocol", "") in SUPPORTED_PROTOCOLS
    return False


def canonical_name(name_map: dict, raw_name: str) -> str | None:
    if not raw_name:
        return None
    key = raw_name.strip().lower().replace(" ", "_")
    return name_map.get(key)


def entry_key(entry: dict) -> tuple:
    """Deduplication key. Two entries with same protocol+address+command
    (or same raw data string) yield the same key."""
    if entry.get("type") == "parsed":
        return ("p",
                entry.get("protocol", ""),
                entry.get("address", ""),
                entry.get("command", ""))
    # Raw — full data string differentiates.
    return ("r", entry.get("frequency", ""), entry.get("data", ""))


def format_entry(entry: dict, name: str) -> str:
    lines = [f"name: {name}", f"type: {entry.get('type', 'parsed')}"]
    if entry.get("type") == "parsed":
        for k in ("protocol", "address", "command"):
            if k in entry:
                lines.append(f"{k}: {entry[k]}")
    else:
        for k in ("frequency", "duty_cycle", "data"):
            if k in entry:
                lines.append(f"{k}: {entry[k]}")
    return "\n".join(lines) + "\n#\n"


def fetch_irdb(target: Path) -> Path:
    """Clone the IRDB (shallow) into `target` if it isn't already there."""
    if (target / ".git").exists():
        sys.stderr.write(f"Reusing existing IRDB clone at {target}\n")
        return target
    sys.stderr.write(f"Cloning {IRDB_URL} -> {target} (shallow) ...\n")
    subprocess.check_call([
        "git", "clone", "--depth", "1", IRDB_URL, str(target),
    ])
    return target


def process_category(irdb_root: Path, sub: str, name_map: dict, cap: int):
    cat_dir = irdb_root / sub
    if not cat_dir.is_dir():
        sys.stderr.write(f"WARN: {cat_dir} not found in clone; skipping\n")
        return [], {}
    kept = []
    seen = set()
    per_name = defaultdict(int)
    for path in sorted(cat_dir.rglob("*.ir")):
        for entry in parse_ir_file(path):
            if not is_supported(entry):
                continue
            cname = canonical_name(name_map, entry.get("name", ""))
            if not cname:
                continue
            k = entry_key(entry)
            if k in seen:
                continue
            seen.add(k)
            kept.append((cname, entry))
            per_name[cname] += 1
            if len(kept) >= cap:
                break
        if len(kept) >= cap:
            break
    return kept, per_name


def write_category(out_dir: Path, fname: str, kept: list):
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / fname
    # The M1 firmware's IR parser expects LF-only line endings
    # (#define IR_SIGNALS_KEYWORD_CRLF "\n"). Python's text-mode open()
    # on Windows translates LF->CRLF, which corrupts every entry name's
    # match. Force LF explicitly by passing newline="".
    with out_path.open("w", encoding="utf-8", newline="") as f:
        f.write("Filetype: IR signals file\nVersion: 1\n#\n")
        # M1 reads function-grouped: every Power first, then every Mute, etc.
        kept.sort(key=lambda x: x[0])
        for cname, entry in kept:
            f.write(format_entry(entry, cname))
    return out_path


def main() -> int:
    ap = argparse.ArgumentParser(description="Repack Flipper-IRDB for M1.")
    ap.add_argument("--out", default="ir_db_out",
                    help="Output dir (will contain <out>/INFRARED/db/*.ir). "
                         "Default: ./ir_db_out — chosen to avoid colliding "
                         "with the firmware's source-tree Infrared/ folder on "
                         "case-insensitive filesystems like NTFS.")
    ap.add_argument("--offline", default=None,
                    help="Path to an existing Flipper-IRDB clone (skip git).")
    ap.add_argument("--keep-clone", action="store_true",
                    help="Don't delete the cloned IRDB after processing.")
    args = ap.parse_args()

    if args.offline:
        irdb_root = Path(args.offline)
        if not irdb_root.is_dir():
            sys.stderr.write(f"ERROR: --offline path {irdb_root} not found.\n")
            return 1
        cleanup_target = None
    else:
        cleanup_target = Path(tempfile.mkdtemp(prefix="flipper-irdb-"))
        irdb_root = fetch_irdb(cleanup_target)

    out_root = Path(args.out)
    db_dir = out_root / "INFRARED" / "db"
    db_dir.mkdir(parents=True, exist_ok=True)

    grand_total = 0
    for sub, fname, name_map, cap in CATEGORIES:
        kept, per_name = process_category(irdb_root, sub, name_map, cap)
        out_path = write_category(db_dir, fname, kept)
        size = out_path.stat().st_size
        breakdown = ", ".join(f"{k}={v}" for k, v in sorted(per_name.items()))
        sys.stderr.write(
            f"  {sub:30s} -> {out_path}  {len(kept):5d} entries, "
            f"{size:>7} B  ({breakdown})\n"
        )
        grand_total += len(kept)

    sys.stderr.write(f"\nTotal entries kept: {grand_total:,}\n")
    sys.stderr.write(f"Output: {out_root.resolve()}/INFRARED/db/\n")
    sys.stderr.write(
        "Copy the INFRARED/ directory from the output to the SD card root.\n"
    )

    if cleanup_target and not args.keep_clone:
        shutil.rmtree(cleanup_target, ignore_errors=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
