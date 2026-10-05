"""The size budget: what counts as flash and RAM, and when it fails."""

import importlib.util
import sys
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("size_report_module", Path(__file__).with_name("size_report.py"))
sr = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = sr
_spec.loader.exec_module(sr)

KB = 1024


class SizeReport(unittest.TestCase):
    def test_budget_follows_the_planned_layout(self):
        self.assertEqual(sr.SLOT, 472 * KB)
        self.assertEqual(sr.FLASH_BUDGET, 472 * KB * 75 // 100)
        self.assertEqual(sr.RAM_BUDGET, 256 * KB * 70 // 100)

    def test_usage_counts_initialised_data_in_both(self):
        segs = [
            sr.Segment(paddr=0x27000, vaddr=0x27000, filesz=200 * KB, memsz=200 * KB),  # text
            sr.Segment(paddr=0x59000, vaddr=0x20000000, filesz=2 * KB, memsz=2 * KB),   # data
            sr.Segment(paddr=0x20000800, vaddr=0x20000800, filesz=0, memsz=60 * KB),    # bss
        ]
        self.assertEqual(sr.usage(segs), (202 * KB, 62 * KB))

    def test_over_budget_fails(self):
        self.assertTrue(sr.Report(flash=sr.FLASH_BUDGET, ram=sr.RAM_BUDGET, clip=0).ok)
        self.assertFalse(sr.Report(flash=sr.FLASH_BUDGET + 1, ram=0, clip=0).ok)
        self.assertFalse(sr.Report(flash=0, ram=sr.RAM_BUDGET + 1, clip=0).ok)
        self.assertIn("**over**", sr.markdown(sr.Report(flash=sr.FLASH_BUDGET + 1, ram=0, clip=0), "t"))


if __name__ == "__main__":
    unittest.main()
