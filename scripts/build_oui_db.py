#!/usr/bin/env python3
# See COPYING.txt for license details.
"""
build_oui_db.py — fetch the IEEE OUI registry and write a compact, sorted
binary database that the M1 firmware can binary-search directly on the SD
card.

Output file format ('M1OUI001'):

    offset  size  meaning
    0       8     magic   = b'M1OUI001'
    8       4     count   (uint32 little-endian)
    12      4     reclen  (uint32 little-endian) = 32
    16      N*32  records, sorted ascending by OUI

Each record:

    offset  size  meaning
    0       3     OUI bytes (high-order first, e.g. 0x00 0x1A 0x2B)
    3       29    vendor name, ASCII, null-padded (always null-terminated)

Usage:
    python3 build_oui_db.py [--source URL_or_path] [--out PATH]

Default source: https://standards-oui.ieee.org/oui/oui.csv
Default output: oui.bin in the current directory.

Copy the resulting oui.bin onto the M1 SD card at  /databases/oui.bin .
"""

from __future__ import annotations

import argparse
import csv
import io
import re
import struct
import sys
import unicodedata
import urllib.request
from pathlib import Path

MAGIC = b"M1OUI001"
RECLEN = 32
VENDOR_LEN = RECLEN - 3  # 29
DEFAULT_URL = "https://standards-oui.ieee.org/oui/oui.csv"


def fold_ascii(s: str) -> str:
    """Normalise to ASCII and collapse whitespace."""
    s = unicodedata.normalize("NFKD", s)
    s = s.encode("ascii", "ignore").decode("ascii")
    s = re.sub(r"\s+", " ", s).strip()
    # Trim common boilerplate suffixes to keep the small budget useful.
    for suffix in (
        ", Inc.", " Inc.", " Inc", ", LLC", " LLC",
        ", Ltd.", " Ltd.", " Ltd",
        " Corporation", " Corp.", " Corp",
        " Co., Ltd.", " Co.,Ltd.", " Co., Ltd", " Co.,Ltd",
        " Co., LTD.", " CO., LTD.",
        " GmbH", " AG", " S.A.", " S.A", " B.V.", " BV",
        " Technologies", " Technology",
    ):
        if s.endswith(suffix):
            s = s[: -len(suffix)].rstrip(",. ")
    return s


def fetch(source: str) -> bytes:
    if source.startswith(("http://", "https://")):
        sys.stderr.write(f"Fetching {source}\n")
        with urllib.request.urlopen(source, timeout=60) as r:
            return r.read()
    return Path(source).read_bytes()


def parse_csv(data: bytes) -> dict[int, str]:
    """Return {oui_int: vendor_name}, deduped by OUI (last wins)."""
    text = data.decode("utf-8", errors="replace")
    reader = csv.DictReader(io.StringIO(text))
    out: dict[int, str] = {}
    for row in reader:
        reg = (row.get("Registry") or "").strip()
        if reg and reg != "MA-L":
            # MA-L = 24-bit OUI block. MA-M (28-bit) and MA-S (36-bit) share
            # the high 24 bits among multiple vendors; for a 24-bit lookup we
            # can't distinguish, so skip them — they'll fall through to
            # "Unknown" which is more honest than a wrong guess.
            continue
        assign = (row.get("Assignment") or "").strip()
        if len(assign) != 6:
            continue
        try:
            oui = int(assign, 16)
        except ValueError:
            continue
        vendor = fold_ascii(row.get("Organization Name") or "")
        if not vendor:
            continue
        out[oui] = vendor[:VENDOR_LEN]
    return out


def write_db(records: dict[int, str], out_path: Path) -> None:
    keys = sorted(records.keys())
    with out_path.open("wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<II", len(keys), RECLEN))
        for oui in keys:
            f.write(bytes(((oui >> 16) & 0xFF, (oui >> 8) & 0xFF, oui & 0xFF)))
            vendor = records[oui].encode("ascii", "ignore")[:VENDOR_LEN]
            f.write(vendor.ljust(VENDOR_LEN, b"\x00"))


def main() -> int:
    ap = argparse.ArgumentParser(description="Build the M1 OUI binary DB.")
    ap.add_argument("--source", default=DEFAULT_URL,
                    help="URL or local path to oui.csv (default: IEEE)")
    ap.add_argument("--out", default="oui.bin",
                    help="Output binary path (default: oui.bin)")
    args = ap.parse_args()

    data = fetch(args.source)
    records = parse_csv(data)
    if not records:
        sys.stderr.write("error: no records parsed; check --source\n")
        return 1
    out_path = Path(args.out)
    write_db(records, out_path)
    size = out_path.stat().st_size
    sys.stderr.write(
        f"Wrote {out_path} — {len(records):,} OUIs, {size:,} bytes "
        f"({size / 1024:.1f} KB).\n"
        "Copy to the M1 SD card as /databases/oui.bin\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
