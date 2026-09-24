#!/usr/bin/env python3
"""Synthetic tests of evidence rejection, not application speed evidence."""
import copy
import runpy
import unittest
from pathlib import Path


module = runpy.run_path(str(Path(__file__).with_name("compare-workspace-switch.py")))
compare = module["compare"]


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.before = {"schema": "framehouse-workspace-switch-proof-v2", "eligible": True,
                       "setup": "isolated-settings-onboarding-complete-auto-preview",
                       "error": "", "scenePoints": 100, "cameraPreserved": True,
                       "qmlDiskCacheDisabled": True, "totalMs": 5000,
                       "maxHeartbeatGapMs": 100, "sceneLoadMs": 1000,
                       "routes": [{"page": "Scene", "iteration": -1, "visibleMs": 100,
                                   "inputReleasedMs": 200, "presentedReadyMs": 1000,
                                   "foreground": True, "frameSwaps": 2}]}
        for iteration in range(6):
            for page in sorted(module["PAGES"]):
                self.before["routes"].append({"page": page, "iteration": iteration,
                    "visibleMs": 50, "inputReleasedMs": 150, "presentedReadyMs": 180,
                    "foreground": True, "frameSwaps": 2})
        self.after = copy.deepcopy(self.before)
        for row in self.after["routes"]:
            if row["iteration"] > 0:
                row.update(visibleMs=5, inputReleasedMs=15, presentedReadyMs=30)
        self.manifest = {"peak_rss_kb": 1000, "scenario": "perf-workspace-switch",
                         "target_events_met": True, "process_returncode": -15,
                         "command": ["driver", "--source", "fixture.jpg", "--scene-project", "scene",
                                     "--iterations", "6", "--timeout-ms", "20000"]}

    def result(self, after_manifest=None):
        return compare(self.before, self.after, self.manifest, after_manifest or self.manifest)

    def test_complete_foreground_improvement(self):
        self.assertTrue(self.result()["passed"])

    def test_missing_and_inactive_foreground(self):
        for value in (None, False):
            self.after["eligible"] = value
            self.assertFalse(self.result()["passed"])

    def test_missing_scene_round(self):
        self.after["routes"].pop()
        self.assertFalse(self.result()["passed"])

    def test_missing_rss(self):
        self.assertFalse(self.result(dict(self.manifest, peak_rss_kb=None))["passed"])

    def test_memory_regression(self):
        self.assertFalse(self.result(dict(self.manifest, peak_rss_kb=1101))["passed"])

    def test_cold_regression(self):
        self.after["routes"][0]["presentedReadyMs"] = 1101
        self.assertFalse(self.result()["passed"])

    def test_small_target_improvement(self):
        self.after = copy.deepcopy(self.before)
        self.assertFalse(self.result()["passed"])

    def test_bad_timing(self):
        self.after["routes"][2]["presentedReadyMs"] = float("nan")
        self.assertFalse(self.result()["passed"])

    def test_camera_regression(self):
        self.after["cameraPreserved"] = False
        self.assertFalse(self.result()["passed"])

    def test_different_cache_policy(self):
        self.after["qmlDiskCacheDisabled"] = False
        self.assertFalse(self.result()["passed"])

    def test_visual_run_rejected(self):
        self.after["captures"] = ["scene.png"]
        self.assertFalse(self.result()["passed"])

    def test_changed_source(self):
        other = copy.deepcopy(self.manifest)
        other["command"][2] = "different.jpg"
        self.assertFalse(self.result(other)["passed"])


if __name__ == "__main__":
    unittest.main()
