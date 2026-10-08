"""The county table builder, and the committed table once it exists."""

import importlib.util
import sys
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("build_counties_module", Path(__file__).with_name("build_counties.py"))
bc = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = bc
_spec.loader.exec_module(bc)

STATES = "STATE|STATEFP|STATENS|STATE_NAME\nTX|48|01779801|Texas\nPR|72|01779808|Puerto Rico\n"
COUNTIES = ("STATE|STATEFP|COUNTYFP|COUNTYNS|COUNTYNAME|CLASSFP|FUNCSTAT\n"
            "TX|48|453|01384012|Travis County|H1|A\n"
            "PR|72|127|01804544|San Juan Municipio|H1|A\n")


class BuildCounties(unittest.TestCase):
    def test_format(self):
        text = bc.build(COUNTIES, STATES, "2026-10-06")
        lines = text.splitlines()
        self.assertTrue(lines[1].startswith("# Source: U.S. Census Bureau"))
        self.assertIn("retrieved 2026-10-06", lines[1])
        data = [l for l in lines if not l.startswith("#")]
        self.assertEqual(data, ["48|000||TX|Texas", "48|453|Travis County|TX|Texas",
                                "72|000||PR|Puerto Rico", "72|127|San Juan Municipio|PR|Puerto Rico"])

    def test_bom_and_whitespace(self):
        text = bc.build(COUNTIES.replace("Travis County", " Travis County "), "﻿" + STATES, "x")
        self.assertIn("48|453|Travis County|TX|Texas", text)

    @unittest.skipUnless(bc.OUT.exists(), "counties.txt not generated yet")
    def test_committed_table(self):
        data = [l.split("|") for l in bc.OUT.read_text().splitlines() if not l.startswith("#")]
        self.assertTrue(all(len(f) == 5 for f in data))
        counties = [f for f in data if f[1] != "000"]
        self.assertGreater(len(counties), 3200)
        postal = {f[3] for f in data}
        for territory in ["DC", "PR", "GU", "VI", "AS", "MP"]:
            self.assertIn(territory, postal)
        self.assertIn(["48", "453", "Travis County", "TX", "Texas"], data)


if __name__ == "__main__":
    unittest.main()
