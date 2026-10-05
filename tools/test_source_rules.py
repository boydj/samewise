"""Checks for the source rules in CLAUDE.md.

- services/same/, services/match/ and the services/ble codec and time-zone
  code are plain C99: it builds with a host compiler in strict C99
  mode, includes only the C standard library and its own headers, and never
  promotes float to double implicitly.
- app/ and services/ include only hal/ interfaces for hardware: no driver
  headers, Zephyr device APIs or fakes. app/src/main.c, the composition
  root, may include drivers/ and fakes/ headers to wire them in.
- In services/ble, only ble_zephyr.c and .h (the binding to Zephyr's Bluetooth
  host) includes Zephyr headers, so the service logic runs on native_sim.
"""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP = ROOT / "app"
SAME = APP / "services" / "same"
MATCH = APP / "services" / "match"
BLE = APP / "services" / "ble"
COMPOSITION_ROOT = APP / "src" / "main.c"
PLAIN_C99 = (SAME, MATCH)
# services/ble: everything except the GATT service, which uses Zephyr's Bluetooth host.
BLE_PLAIN = ("gatt_table.h", "codec.h", "codec.c", "tz.h", "tz.c")
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
C_STD = {"stdbool.h", "stddef.h", "stdint.h", "string.h", "limits.h", "math.h"}


def includes(path: Path):
    return [m.group(2) for m in INCLUDE.finditer(path.read_text())]


def sources(*dirs):
    for d in dirs:
        yield from sorted(p for p in d.rglob("*") if p.suffix in (".c", ".h"))


def plain_c99_sources():
    yield from sources(*PLAIN_C99)
    yield from (BLE / f for f in BLE_PLAIN if (BLE / f).exists())


class ServicesArePlainC99(unittest.TestCase):
    def test_includes(self):
        for src in plain_c99_sources():
            for inc in includes(src):
                self.assertTrue(inc in C_STD or inc.startswith(("services/same/", "services/match/",
                                                                "services/ble/")),
                                f"{src.relative_to(ROOT)} includes {inc}")

    def test_no_double(self):
        for src in plain_c99_sources():
            self.assertNotRegex(src.read_text(), r"\bdouble\b", f"{src.relative_to(ROOT)} uses double")

    @unittest.skipUnless(shutil.which("gcc"), "needs gcc")
    def test_builds_as_strict_c99(self):
        with tempfile.TemporaryDirectory() as d:
            for src in sorted(p for p in plain_c99_sources() if p.suffix == ".c"):
                subprocess.run(
                    ["gcc", "-std=c99", "-pedantic-errors", "-Wall", "-Wextra", "-Wshadow",
                     "-Wdouble-promotion", "-Wfloat-conversion", "-Werror", "-I", str(APP),
                     "-c", str(src), "-o", str(Path(d) / (src.stem + ".o"))],
                    check=True)


class AppUsesOnlyHal(unittest.TestCase):
    FORBIDDEN = re.compile(r"^(zephyr/drivers/|zephyr/device\.h$|zephyr/devicetree|drivers/|fakes/)")

    def test_no_driver_or_fake_includes(self):
        for src in sources(APP / "src", APP / "services"):
            if src == COMPOSITION_ROOT:
                continue
            for inc in includes(src):
                self.assertIsNone(self.FORBIDDEN.match(inc), f"{src.relative_to(ROOT)} includes {inc}")

    def test_only_the_binding_includes_zephyr_in_ble(self):
        for src in sources(BLE):
            if src.name in ("ble_zephyr.c", "ble_zephyr.h"):
                continue
            for inc in includes(src):
                self.assertFalse(inc.startswith("zephyr/"), f"{src.relative_to(ROOT)} includes {inc}")

    def test_composition_root_still_avoids_device_apis(self):
        for inc in includes(COMPOSITION_ROOT):
            self.assertIsNone(re.match(r"^(zephyr/drivers/|zephyr/device\.h$|zephyr/devicetree)", inc),
                              f"main.c includes {inc}: device access belongs in drivers/")

    def test_hal_headers_are_standalone(self):
        hal = sorted((APP / "hal").glob("*.h"))
        self.assertEqual(len(hal), 10, "the ten interfaces")
        for h in hal:
            for inc in includes(h):
                self.assertIn(inc, C_STD, f"{h.relative_to(ROOT)} includes {inc}")


if __name__ == "__main__":
    unittest.main()
