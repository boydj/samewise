"""docs/gatt.json is generated from the firmware's GATT table and up to date."""

import importlib.util
import json
import shutil
import sys
import unittest
from pathlib import Path

_spec = importlib.util.spec_from_file_location("gatt_json_module", Path(__file__).with_name("gatt_json.py"))
gj = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = gj
_spec.loader.exec_module(gj)


@unittest.skipUnless(shutil.which("cc") or shutil.which("gcc"), "needs a C compiler")
class GattJson(unittest.TestCase):
    def test_checked_in_file_is_current(self):
        self.assertEqual(gj.main(["--check"]), 0, "run tools/gatt/gatt_json.py and commit docs/gatt.json and the Swift copy")

    def test_layout(self):
        g = json.loads(gj.OUT.read_text())
        names = [c["name"] for c in g["characteristics"]]
        self.assertEqual(names, ["counties", "travel_counties", "mode", "event_filter", "event_table",
                                 "time", "presets", "status", "alert_log", "control"])
        uuids = [g["service"]["uuid"]] + [c["uuid"] for c in g["characteristics"]]
        self.assertEqual(len(set(uuids)), len(uuids), "UUIDs are unique")
        self.assertTrue(all(u.endswith("-92fa-49ea-aaea-76bad817c54d") for u in uuids), "one frozen base")
        self.assertTrue(all(c["max_length"] <= 512 for c in g["characteristics"]), "ATT limit")
        self.assertEqual(g["characteristics"][7]["properties"], ["read", "notify"], "status")
        self.assertEqual(g["characteristics"][5]["properties"], ["write"], "time is write-only")

    def test_default_event_table_and_errors(self):
        g = json.loads(gj.OUT.read_text())
        codes = [e["code"] for e in g["default_event_table"]["entries"]]
        self.assertEqual(len(codes), len(set(codes)), "codes are unique")
        self.assertIn("TOR", codes)
        self.assertTrue(all(0 <= e["class"] <= 4 for e in g["default_event_table"]["entries"]))
        self.assertEqual({e["name"]: e["code"] for e in g["errors"]}["SCHEMA"], 0x80)


if __name__ == "__main__":
    unittest.main()
