#!/usr/bin/env python3
"""Acceptance checks must reject missing first-use and invalid timing samples."""
import importlib.util
import math
from pathlib import Path
import sys
import unittest

spec = importlib.util.spec_from_file_location("actual_app_gate", Path(__file__).with_name("run-actual-app-workflow-gate.py"))
gate = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = gate
spec.loader.exec_module(gate)


class AcceptanceTests(unittest.TestCase):
    def setUp(self):
        self.summaries = {b.stage: {"iterations": 1, "p95_ms": 1.0, "max_ms": 1.0} for b in gate.DEFAULT_BUDGETS}
        self.first = "brush_controller_point_to_qml_frame_first_use"

    def test_valid(self):
        self.assertEqual(gate.check_budgets(self.summaries), [])

    def test_first_use_is_required(self):
        del self.summaries[self.first]
        self.assertTrue(any(self.first in failure for failure in gate.check_budgets(self.summaries)))

    def test_first_use_latency_is_gated(self):
        self.summaries[self.first].update(p95_ms=591, max_ms=591)
        self.assertEqual(len(gate.check_budgets(self.summaries)), 2)

    def test_setup_cost_is_gated(self):
        self.summaries["brush_stroke_begin_to_qml_frame_first_use"].update(p95_ms=100, max_ms=100)
        self.assertEqual(len(gate.check_budgets(self.summaries)), 2)

    def test_empty_and_invalid_samples_fail(self):
        for count in (None, 0, -1, True, 1.5):
            with self.subTest(count=count):
                self.summaries[self.first]["iterations"] = count
                self.assertTrue(gate.check_budgets(self.summaries))
        self.summaries[self.first]["iterations"] = 1
        for value in (None, math.nan, math.inf, -1, True, "1"):
            with self.subTest(value=value):
                self.summaries[self.first]["p95_ms"] = value
                self.assertTrue(gate.check_budgets(self.summaries))

    def test_focused_gate_does_not_require_other_routes(self):
        budgets = tuple(b for b in gate.DEFAULT_BUDGETS if b.stage.startswith("brush_"))
        summaries = {b.stage: self.summaries[b.stage] for b in budgets}
        self.assertEqual(gate.check_budgets(summaries, budgets), [])
        del summaries[self.first]
        self.assertTrue(gate.check_budgets(summaries, budgets))


if __name__ == "__main__":
    unittest.main()
