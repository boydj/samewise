"""The bench report: what passes and what fails."""

import importlib.util
import sys
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("bench_module", Path(__file__).with_name("bench.py"))
b = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = b
_spec.loader.exec_module(b)

HEADER = "WX HEADER ZCZC-WXR-TOR-048453+0030-2781915-KEWX/NWS-"
CONSOLE = f"""*** Booting ***\r
Pocket WX Radio firmware 0.1.0 (xiao_ble/nrf52840)\r
WX BENCH START\r
{HEADER}\r
WX EOM\r
WX BENCH END samples=104167 ms=148\r
Thread analyze:\r
 wx_supervisor       : STACK: unused 1760 usage 288 / 2048 (14 %); CPU: 0 %\r
                     : Total CPU cycles used: 0\r
 main                : STACK: unused 1448 usage 600 / 2048 (29 %); CPU: 99 %\r
WX BENCH DONE\r
"""


class Bench(unittest.TestCase):
    def test_parses_console(self):
        lines = b.console_lines(CONSOLE)
        self.assertEqual(b.decoded(lines), [HEADER, "WX EOM"])
        self.assertEqual(b.samples(lines), 104167)
        self.assertEqual([s["thread"] for s in b.stacks(lines)], ["wx_supervisor", "main"])
        self.assertEqual(b.stacks(lines)[1]["pct"], 29.3)

    def test_cpu_load(self):
        c = b.cpu(instructions=6_400_000, n_samples=104167)  # 10 s of audio
        self.assertAlmostEqual(c["audio_seconds"], 10.0, places=2)
        self.assertEqual(c["load_pct"]["1.0"], 1.0)
        self.assertEqual(c["load_pct"]["1.5"], 1.5)

    def test_checks(self):
        r = b.evaluate(CONSOLE, 6_400_000, [HEADER, "WX EOM"], b.console_lines(CONSOLE), {"ok": True})
        self.assertTrue(r["ok"], r["checks"])
        self.assertFalse(b.evaluate(CONSOLE, 6_400_000, [HEADER], None, None)["checks"]["decodes_expected"])
        self.assertFalse(b.evaluate(CONSOLE, 6_400_000, [HEADER, "WX EOM"], ["WX EOM"], None)
                         ["checks"]["matches_native_sim"])
        busy = b.evaluate(CONSOLE, 45_000_000, [HEADER, "WX EOM"], None, None)  # 4.5 M/s x 1.5 = 10.5%
        self.assertFalse(busy["checks"]["cpu_within_limit"])
        deep = CONSOLE.replace("usage 600 / 2048", "usage 1600 / 2048")
        self.assertFalse(b.evaluate(deep, 6_400_000, [HEADER, "WX EOM"], None, None)["checks"]["stacks_within_limit"])
        self.assertIn("**FAIL**", b.markdown(busy))


if __name__ == "__main__":
    unittest.main()
