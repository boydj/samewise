"""Tests for build_vectors: expectations, determinism and the generated C header."""

import importlib.util
import hashlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("build_vectors_module",
                                               Path(__file__).with_name("build_vectors.py"))
bv = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = bv
_spec.loader.exec_module(bv)


def digest(d: Path) -> dict:
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(d.iterdir())}


class DecoderGroup(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = Path(tempfile.mkdtemp())
        cls.entries = {n: s for n, _, s in bv.build("decoder", cls.tmp / "a")}

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp)

    def expect(self, name, headers, eoms=0):
        side = self.entries[name]
        self.assertEqual(len(side["expected_headers"]), headers, name)
        self.assertEqual(side["expected_eom_count"], eoms, name)

    def test_expectations(self):
        self.expect("clean", 1)
        self.expect("corrupt_one_copy", 1)
        self.expect("corrupt_each_copy", 1)  # every copy damaged, at different bytes
        self.expect("two_copies_drop1", 1)
        self.expect("two_copies_drop3", 1)
        self.expect("two_copies_disagree", 0)
        self.expect("one_copy", 0)
        self.expect("header_eom", 1, 1)
        self.expect("back_to_back", 2)
        self.expect("back_to_back_lost_copy", 2)
        self.expect("malformed", 0)
        for pct in ("-2", "-1", "+1", "+2"):
            self.expect(f"freq_{pct}", 1)

    def test_corrupt_each_copy_really_damages_every_copy(self):
        copies = self.entries["corrupt_each_copy"]["messages"][0]["copies"]
        positions = [set(c["corrupted_positions"]) for c in copies]
        self.assertTrue(all(len(p) == 2 for p in positions))
        self.assertFalse(positions[0] & positions[1] or positions[0] & positions[2] or positions[1] & positions[2])

    def test_loc31_has_31_locations(self):
        h = self.entries["loc31"]["expected_headers"][0]
        self.assertEqual(len(h.split("+")[0].split("-")[3:]), 31)
        self.assertTrue(bv.sg.is_valid_header(h))

    def test_rebuild_is_identical(self):
        bv.build("decoder", self.tmp / "b")
        a, b = digest(self.tmp / "a"), digest(self.tmp / "b")
        a.pop("vectors.h"), b.pop("vectors.h")  # holds absolute paths
        self.assertEqual(a, b)

    @unittest.skipUnless(shutil.which("gcc"), "needs gcc")
    def test_header_compiles(self):
        src = self.tmp / "t.c"
        src.write_text('#include "vectors.h"\nint main(void) { return test_vector_find("clean") == NULL; }\n')
        exe = self.tmp / "t"
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Werror", "-I", str(self.tmp / "a"), str(src), "-o", str(exe)],
                       check=True)
        self.assertEqual(subprocess.run([str(exe)]).returncode, 0)


class Snr(unittest.TestCase):
    def test_random_headers_are_valid_distinct_and_seeded(self):
        a = bv.random_headers(50, seed=2026)
        self.assertEqual(len(set(a)), 50)
        self.assertTrue(all(bv.sg.is_valid_header(h) for h in a))
        self.assertEqual(a, bv.random_headers(50, seed=2026))

    def test_sweep_points(self):
        self.assertEqual(bv.SNR_POINTS, [30, 27, 24, 21, 18, 15, 12, 9, 6, 3, 0])


if __name__ == "__main__":
    unittest.main()
