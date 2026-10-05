#!/usr/bin/env python3
"""Generate or check docs/gatt.json from app/services/ble/gatt_table.h.

  gatt_json.py            rewrite docs/gatt.json
  gatt_json.py --check    exit 1 if docs/gatt.json is out of date

Compiles tools/gatt/gen_gatt_json.c with the host C compiler, which
includes the firmware header directly, so nothing parses C by hand.
"""

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
OUT = ROOT / "docs" / "gatt.json"


def generate() -> str:
    cc = os.environ.get("CC", "cc")
    with tempfile.TemporaryDirectory() as d:
        exe = Path(d) / "gen_gatt_json"
        subprocess.run([cc, "-std=c99", "-Wall", "-Werror", "-I", str(ROOT / "app"),
                        str(ROOT / "tools" / "gatt" / "gen_gatt_json.c"), "-o", str(exe)], check=True)
        text = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout
    json.loads(text)  # must be valid JSON
    return text


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--check", action="store_true")
    a = p.parse_args(argv)
    text = generate()
    if a.check:
        if not OUT.exists() or OUT.read_text() != text:
            print(f"{OUT.relative_to(ROOT)} is out of date: run tools/gatt/gatt_json.py", file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text)
    print(f"wrote {OUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
