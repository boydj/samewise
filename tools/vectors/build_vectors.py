#!/usr/bin/env python3
"""Build the synthetic SAME test vectors for one test group.

Writes <name>.wav and <name>.json (sidecar) for every vector in the group,
plus vectors.h, a C table of the expectations read back from the sidecars.
Everything is seeded, so a rebuild gives identical files. The test suites
run this from CMake (tools/vectors/vectors.cmake) into their build
directory; vectors are never edited by hand.

  build_vectors.py --group decoder --out build/vectors
  build_vectors.py --list
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path

import numpy as np

_here = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("samegen_module", _here.parent / "samegen" / "samegen.py")
sg = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = sg
_spec.loader.exec_module(sg)

TOR = "ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-"
SVR = "ZCZC-WXR-SVR-048453-048491-048209+0045-2781930-KEWX/NWS-"
FFW = "ZCZC-WXR-FFW-048029-048091+0600-2782200-KEWX/NWS-"
RWT = "ZCZC-WXR-RWT-048000+0015-2791700-KEWX/NWS-"
# 31 locations, including a county subdivision (1xxxxx) and a whole state (xx000).
LOC31 = ("ZCZC-WXR-TOR-" + "-".join(
    ["148453", "048000", "000000"] + [f"0{48001 + 2 * i:05d}" for i in range(28)]
) + "+0100-2790105-KFWD/NWS-")

MALFORMED = [
    TOR.replace("048453", "04845"),        # short location code
    TOR.replace("048453", "04845A"),       # letter in a digit field
    TOR.replace("+0030", "+00A0"),         # letter in purge time
    TOR.replace("2781915", "3671915"),     # Julian day 367
    TOR.replace("2781915", "2782515"),     # hour 25
    TOR.replace("KEWX/NWS", "KEWX/NW"),    # 7-character station ID
    TOR.replace("WXR", "W1R"),             # digit in originator
    TOR.replace("WXR", "XYZ"),             # not an 11.31(d)(1) originator
    TOR.replace("KEWX/NWS", "KEWX+NWS"),   # '+' in station ID (11.31(b))
    "ZCZC-WXR-TOR-" + "-".join(["048453"] * 32) + "+0030-2781915-KEWX/NWS-",  # 32 locations
]


def O(**kw):  # noqa: N802 - short constructor for the tables below
    return sg.Options(**kw)


def _decoder_group():
    v = {
        "clean": ([TOR], O()),
        "clean_noise_lead": ([TOR], O(lead="noise", lead_s=3, trail="noise", trail_s=3, snr_db=30, seed=1)),
        "corrupt_one_copy": ([SVR], O(corrupt=[(None, 2, 1)], seed=2)),
        "corrupt_each_copy": ([SVR], O(corrupt=[(None, 1, 2), (None, 2, 2), (None, 3, 2)], seed=4)),  # disjoint positions
        "two_copies_drop1": ([SVR], O(drop=[(None, 1)])),
        "two_copies_drop3": ([SVR], O(drop=[(None, 3)])),
        "two_copies_disagree": ([SVR], O(drop=[(None, 3)], corrupt=[(None, 2, 1)], seed=4)),
        "one_copy": ([SVR], O(drop=[(None, 2), (None, 3)])),
        "loc31": ([LOC31], O()),
        "header_eom": ([SVR, "EOM"], O(tone1050_s=8, message_gap_s=5)),
        "eom_only": (["EOM"], O()),
        "back_to_back": ([TOR, FFW], O(message_gap_s=1)),
        "back_to_back_lost_copy": ([TOR, FFW], O(message_gap_s=1, drop=[(1, 2)])),
        "malformed": (MALFORMED, O()),
        "eighth_bit": ([TOR], O(eighth_bit=True)),
        "quiet": ([TOR], O(amplitude=0.01)),
        "loud_clipping": ([TOR], O(amplitude=1.5)),
        "rwt": ([RWT], O(tone1050_s=0)),
        "offsets_combined": ([SVR], O(freq_offset_pct=2, timing_offset_pct=-2, snr_db=20, seed=5)),
    }
    for pct in (-2, -1, 1, 2):
        v[f"freq_{pct:+d}"] = ([SVR], O(freq_offset_pct=pct))
        v[f"timing_{pct:+d}"] = ([SVR], O(timing_offset_pct=pct))
    return v


def _negative_group():
    ten_min = 600.0
    return {
        "noise_10min": ([], O(lead="noise", lead_s=ten_min, trail_s=0, seed=11)),
        "silence_10min": ([], O(lead="silence", lead_s=ten_min, trail_s=0)),
        "tone1050_10min": ([], O(lead="tone1050", lead_s=ten_min, trail_s=0)),
        "eas_attention_10min": ([], O(lead="eas_attention", lead_s=ten_min, trail_s=0)),
    }


SNR_POINTS = list(range(30, -1, -3))  # 30, 27, ..., 0 dB
SNR_ALERTS = 50

_EVENTS = ["TOR", "SVR", "FFW", "SVA", "TOA", "FFA", "WSW", "BZW", "SPS", "RWT", "FLW", "EWW"]
_STATIONS = ["KEWX/NWS", "KFWD/NWS", "KHGX/NWS", "KOUN/NWS", "KLWX/NWS", "WXK27   "]


def random_headers(n: int, seed: int) -> list[str]:
    """Distinct, valid random headers with 1 to 6 locations."""
    rng = np.random.default_rng(seed)
    out: list[str] = []
    while len(out) < n:
        locs = [f"{rng.integers(0, 10)}{rng.integers(1, 57):02d}{rng.integers(0, 1000):03d}"
                for _ in range(int(rng.integers(1, 7)))]
        purge = ["0015", "0030", "0045", "0100", "0130", "0200", "0300", "0600"][int(rng.integers(0, 8))]
        issue = f"{rng.integers(1, 367):03d}{rng.integers(0, 24):02d}{rng.integers(0, 60):02d}"
        h = (f"ZCZC-WXR-{_EVENTS[int(rng.integers(0, len(_EVENTS)))]}-{'-'.join(locs)}+{purge}-{issue}-"
             f"{_STATIONS[int(rng.integers(0, len(_STATIONS)))]}-")
        assert sg.is_valid_header(h), h
        if h not in out:
            out.append(h)
    return out


def _snr_group():
    headers = random_headers(SNR_ALERTS, seed=2026)
    return {f"snr_{snr:02d}db": (headers, O(snr_db=float(snr), seed=1000 + snr, lead_s=2, trail_s=2))
            for snr in SNR_POINTS}


def _scenario_group():
    """Alerts for the scenario tests: Travis County, TX (048453) is home."""
    tor = "ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-"
    return {
        "tor": ([tor], O(lead_s=0.5, trail_s=0.5)),
        "tor_eom": ([tor, "EOM"], O(lead_s=0.5, tone1050_s=8, message_gap_s=20, trail_s=0.5)),
        "toa": (["ZCZC-WXR-TOA-048453+0100-2781920-KEWX/NWS-"], O(lead_s=0.5, trail_s=0.5)),
        "svr_elsewhere": (["ZCZC-WXR-SVR-040109+0045-2781925-KOUN/NWS-"], O(lead_s=0.5, trail_s=0.5)),
        "rwt": (["ZCZC-WXR-RWT-048000+0015-2791700-KEWX/NWS-"], O(lead_s=0.5, trail_s=0.5)),
    }


GROUPS = {"decoder": _decoder_group, "negative": _negative_group, "snr": _snr_group,
          "scenario": _scenario_group}


def _c_str(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_header(out: Path, group: str, entries: list[tuple[str, Path, dict]]) -> None:
    lines = [
        f"/* Generated by tools/vectors/build_vectors.py --group {group}. Do not edit. */",
        "",
        "#ifndef WX_TEST_VECTORS_H_",
        "#define WX_TEST_VECTORS_H_",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "#include <string.h>",
        "",
        "struct test_vector {",
        "\tconst char *name;",
        "\tconst char *wav;           /* absolute host path */",
        "\tuint32_t samples;",
        "\tint has_snr;",
        "\tfloat snr_db;              /* full-band SNR, if has_snr */",
        "\tuint32_t n_headers;",
        "\tconst char *const *headers; /* expected, in order */",
        "\tuint32_t eom_count;          /* expected on_eom callbacks */",
        "};",
        "",
    ]
    for i, (name, _, side) in enumerate(entries):
        if side["expected_headers"]:
            lines.append(f"static const char *const test_vector_headers_{i}[] = {{ /* {name} */")
            lines += [f"\t{_c_str(h)}," for h in side["expected_headers"]]
            lines += ["};", ""]
    lines.append("static const struct test_vector test_vectors[] = {")
    for i, (name, wav, side) in enumerate(entries):
        snr = side["options"]["snr_db"]
        hdrs = f"test_vector_headers_{i}" if side["expected_headers"] else "NULL"
        lines.append(
            f"\t{{{_c_str(name)}, {_c_str(str(wav))}, {side['samples']}U, {int(snr is not None)}, "
            f"{float(snr or 0.0)!r}f, {len(side['expected_headers'])}U, {hdrs}, "
            f"{side['expected_eom_count']}U}},")
    lines += [
        "};",
        "",
        "#define TEST_VECTOR_COUNT (sizeof(test_vectors) / sizeof(test_vectors[0]))",
        "",
        "static inline const struct test_vector *test_vector_find(const char *name)",
        "{",
        "\tfor (size_t i = 0; i < TEST_VECTOR_COUNT; i++) {",
        "\t\tif (strcmp(test_vectors[i].name, name) == 0) {",
        "\t\t\treturn &test_vectors[i];",
        "\t\t}",
        "\t}",
        "\treturn NULL;",
        "}",
        "",
        "#endif /* WX_TEST_VECTORS_H_ */",
        "",
    ]
    (out / "vectors.h").write_text("\n".join(lines))


def build(group: str, out: Path) -> list[tuple[str, Path, dict]]:
    out = out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    entries = []
    for name, (messages, opts) in GROUPS[group]().items():
        wav = out / f"{name}.wav"
        samples, side = sg.generate(messages, opts)
        sg.write(wav, samples, side)
        # Expectations come from the sidecar as written, as they will for recordings.
        entries.append((name, wav, json.loads(wav.with_suffix(".json").read_text())))
    write_header(out, group, entries)
    return entries


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--group", choices=sorted(GROUPS))
    p.add_argument("--out", type=Path)
    p.add_argument("--list", action="store_true", help="list groups and vectors")
    a = p.parse_args(argv)
    if a.list:
        for g, fn in GROUPS.items():
            print(f"{g}: {', '.join(fn())}")
        return 0
    if not a.group or not a.out:
        p.error("--group and --out are required")
    entries = build(a.group, a.out)
    print(f"{len(entries)} {a.group} vectors in {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
