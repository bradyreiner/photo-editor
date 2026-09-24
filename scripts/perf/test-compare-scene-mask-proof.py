#!/usr/bin/env python3
import copy
from pathlib import Path
import runpy
import unittest

compare = runpy.run_path(str(Path(__file__).with_name("compare-scene-mask-proof.py")))["compare"]


class SceneMaskGateTest(unittest.TestCase):
    def setUp(self):
        self.before = {"ok": True, "foreground": True, "final_outline_ready": False,
                       "admission_ms": {"mask_stack": 1000, "active_mask_exposure": 900},
                       "elapsed_ms": 20000, "peak_memory_bytes": 2000000,
                       "image_hashes": {"neutral": "abc", "edited": "def"}}
        self.after = copy.deepcopy(self.before)
        self.after.update(final_outline_ready=True,
                          admission_ms={"mask_stack": 80, "active_mask_exposure": 60})

    def test_accepts_measured_improvement(self):
        self.assertTrue(compare(self.before, self.after)["accepted"])

    def test_rejects_each_broken_guard(self):
        for key, value in (("ok", False), ("foreground", False),
                           ("final_outline_ready", False), ("elapsed_ms", 23000),
                           ("peak_memory_bytes", 2300000), ("image_hashes", {}),
                           ("admission_ms", {"mask_stack": 80, "active_mask_exposure": 800})):
            with self.subTest(key=key):
                changed = copy.deepcopy(self.after)
                changed[key] = value
                self.assertFalse(compare(self.before, changed)["accepted"])

    def test_rejects_inactive_baseline(self):
        self.before["foreground"] = False
        self.assertFalse(compare(self.before, self.after)["accepted"])


if __name__ == "__main__":
    unittest.main()
