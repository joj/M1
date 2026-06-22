#!/usr/bin/env python3
# See COPYING.txt for license details.
"""
build_wifi_wordlist.py — Build a SD-card-bound dictionary of candidate
WPA-PSK passwords for the M1 firmware's online dictionary-attack feature.

Source: SecLists (github.com/danielmiessler/SecLists) — the de-facto
canonical pen-test wordlist repository. By default we pull a curated
mix that fits in a few MB and biases toward likely WPA-PSKs:

  - Passwords/WiFi-WPA/probable-v2-wpa-top62.txt        (~62)
  - Passwords/WiFi-WPA/probable-v2-wpa-top4800.txt      (~4,800)
  - Passwords/Common-Credentials/10-million-password-list-top-100000.txt
                                                        (~100,000)

You can override any of those with --source. Output is plain UTF-8
text, one candidate per line. Candidates are filtered for WPA-PSK
validity (8 <= length <= 63, printable ASCII), deduped preserving the
order of first appearance (so the high-quality WPA list ranks first).

Copy the resulting wifi_wordlist.txt onto the SD card at
/databases/wifi_wordlist.txt.

Usage examples:
    python3 build_wifi_wordlist.py
    python3 build_wifi_wordlist.py --top 10000
    python3 build_wifi_wordlist.py --source ./my_rockyou.txt --out custom.txt
    python3 build_wifi_wordlist.py --no-network   # uses cached/local sources only
"""

from __future__ import annotations

import argparse
import os
import sys
import urllib.request
from pathlib import Path

DEFAULT_SOURCES = [
    "https://raw.githubusercontent.com/danielmiessler/SecLists/master/"
    "Passwords/WiFi-WPA/probable-v2-wpa-top62.txt",
    "https://raw.githubusercontent.com/danielmiessler/SecLists/master/"
    "Passwords/WiFi-WPA/probable-v2-wpa-top4800.txt",
    "https://raw.githubusercontent.com/danielmiessler/SecLists/master/"
    "Passwords/Common-Credentials/Pwdb_top-100000.txt",
]


def fetch(source: str) -> str:
    if source.startswith(("http://", "https://")):
        sys.stderr.write(f"Fetching {source}\n")
        with urllib.request.urlopen(source, timeout=60) as r:
            return r.read().decode("utf-8", errors="replace")
    sys.stderr.write(f"Reading {source}\n")
    return Path(source).read_text(encoding="utf-8", errors="replace")


def is_valid_psk(s: str) -> bool:
    """WPA-PSK rules: 8 to 63 chars, printable ASCII (0x20-0x7E)."""
    if not (8 <= len(s) <= 63):
        return False
    return all(0x20 <= ord(c) <= 0x7E for c in s)


def main() -> int:
    ap = argparse.ArgumentParser(description="Build the M1 WiFi WPA wordlist.")
    ap.add_argument("--source", action="append", default=None,
                    help="Source URL or file path. Repeat for multiple. "
                         "Defaults to SecLists WPA + Top-100k.")
    ap.add_argument("--out", default="wifi_wordlist.txt",
                    help="Output path (default: wifi_wordlist.txt).")
    ap.add_argument("--top", type=int, default=0,
                    help="If > 0, truncate the final list to this many entries.")
    ap.add_argument("--no-network", action="store_true",
                    help="Refuse to fetch over the network; sources must be local paths.")
    args = ap.parse_args()

    sources = args.source if args.source else DEFAULT_SOURCES
    if args.no_network:
        for s in sources:
            if s.startswith(("http://", "https://")):
                sys.stderr.write(
                    f"error: --no-network set but {s} is a URL.\n")
                return 2

    seen: set[str] = set()
    out_lines: list[str] = []
    skipped_invalid = 0
    skipped_dup = 0
    for src in sources:
        try:
            text = fetch(src)
        except Exception as e:
            sys.stderr.write(f"warn: {src} failed ({e}); skipping\n")
            continue
        for raw in text.splitlines():
            # Strip BOM, CR/LF, leading/trailing space. Preserve interior space
            # exactly; that's valid in a PSK.
            line = raw.lstrip("\ufeff").rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            if not is_valid_psk(line):
                skipped_invalid += 1
                continue
            if line in seen:
                skipped_dup += 1
                continue
            seen.add(line)
            out_lines.append(line)
            if args.top and len(out_lines) >= args.top:
                break
        if args.top and len(out_lines) >= args.top:
            break

    out_path = Path(args.out)
    out_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    size = out_path.stat().st_size
    sys.stderr.write(
        f"Wrote {out_path} — {len(out_lines):,} candidates, "
        f"{size:,} bytes ({size / 1024:.1f} KB). "
        f"Filtered {skipped_invalid:,} invalid + {skipped_dup:,} dupes.\n"
        "Copy to the M1 SD card as /databases/wifi_wordlist.txt\n"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
