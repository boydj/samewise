"""Checks for the source rules in CLAUDE.md.

- services/same/ is plain C99: it builds with a host compiler in strict C99
  mode, includes only the C standard library and its own headers, and never
  promotes float to double implicitly.
- app/ and services/ include only hal/ interfaces for hardware: no driver
  headers, Zephyr device APIs or fakes.
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
INCLUDE = re.compile(r'^\s*#\s*include\s*([<"])([^>"]+)[>"]', re.M)
C_STD = {"stdbool.h", "stddef.h", "stdint.h", "string.h", "limits.h", "math.h"}


def includes(path: Path):
    return [m.group(2) for m in INCLUDE.finditer(path.read_text())]


def sources(*dirs):
    for d in dirs:
        yield from sorted(p for p in d.rglob("*") if p.suffix in (".c", ".h"))


class SameIsPlainC99(unittest.TestCase):
    def test_includes(self):
        for src in sources(SAME):
            for inc in includes(src):
                self.assertTrue(inc in C_STD or inc.startswith("services/same/"),
                                f"{src.relative_to(ROOT)} includes {inc}")

    def test_no_double(self):
        for src in sources(SAME):
            self.assertNotRegex(src.read_text(), r"\bdouble\b", f"{src.relative_to(ROOT)} uses double")

    @unittest.skipUnless(shutil.which("gcc"), "needs gcc")
    def test_builds_as_strict_c99(self):
        with tempfile.TemporaryDirectory() as d:
            for src in sorted(SAME.glob("*.c")):
                subprocess.run(
                    ["gcc", "-std=c99", "-pedantic-errors", "-Wall", "-Wextra", "-Wshadow",
                     "-Wdouble-promotion", "-Wfloat-conversion", "-Werror", "-I", str(APP),
                     "-c", str(src), "-o", str(Path(d) / (src.stem + ".o"))],
                    check=True)


class AppUsesOnlyHal(unittest.TestCase):
    FORBIDDEN = re.compile(r"^(zephyr/drivers/|zephyr/device\.h$|zephyr/devicetree|drivers/|fakes/)")

    def test_no_driver_or_fake_includes(self):
        for src in sources(APP / "src", APP / "services"):
            for inc in includes(src):
                self.assertIsNone(self.FORBIDDEN.match(inc), f"{src.relative_to(ROOT)} includes {inc}")

    def test_hal_headers_are_standalone(self):
        hal = sorted((APP / "hal").glob("*.h"))
        self.assertEqual(len(hal), 9, "the nine interfaces")
        for h in hal:
            for inc in includes(h):
                self.assertIn(inc, C_STD, f"{h.relative_to(ROOT)} includes {inc}")


if __name__ == "__main__":
    unittest.main()
