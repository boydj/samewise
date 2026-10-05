"""The firmware clip: deterministic, complete, and what the decoder should print."""

import importlib.util
import re
import sys
import tempfile
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("build_clip_module", Path(__file__).with_name("build_clip.py"))
bc = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = bc
_spec.loader.exec_module(bc)


def samples_in(c_text):
    body = c_text[c_text.index("{") + 1:c_text.index("};")]
    return [int(x) for x in re.findall(r"-?\d+", body)]


class BuildClip(unittest.TestCase):
    def test_sixteen_bit_matches_samegen_and_is_reproducible(self):
        with tempfile.TemporaryDirectory() as d:
            side = bc.build(Path(d) / "a")
            bc.build(Path(d) / "b")
            a = (Path(d) / "a" / "wx_clip.c").read_text()
            self.assertEqual(a, (Path(d) / "b" / "wx_clip.c").read_text(), "seeded, identical")
            samples, _ = bc.sg.generate([bc.HEADER, "EOM"], bc.OPTIONS)
            self.assertEqual(samples_in(a), samples.tolist())
            self.assertIn(f"wx_clip_count = {len(samples)}U", a)
            self.assertAlmostEqual(side["samples"] / bc.sg.SAME_RATE, 8.6, delta=0.5, msg="about 8 s")

    def test_eight_bit_is_the_top_byte(self):
        with tempfile.TemporaryDirectory() as d:
            bc.build(Path(d), bits=8)
            samples, _ = bc.sg.generate([bc.HEADER, "EOM"], bc.OPTIONS)
            self.assertEqual(samples_in((Path(d) / "wx_clip.c").read_text()), [s >> 8 for s in samples.tolist()])
            self.assertIn("WX_CLIP_SHIFT 8", (Path(d) / "wx_clip.h").read_text())

    def test_expected_lines(self):
        with tempfile.TemporaryDirectory() as d:
            bc.build(Path(d))
            self.assertEqual((Path(d) / "wx_clip.txt").read_text().split("\n")[:-1],
                             [f"WX HEADER {bc.HEADER}", "WX EOM"])


if __name__ == "__main__":
    unittest.main()
