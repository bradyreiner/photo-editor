"""Guard the Canvas-inset to native retained-rectangle boundary."""
import unittest

from canvas_agent_plan import _frame_values


class CanvasCropTranslationTest(unittest.TestCase):
    def frame(self, crop=(0.0, 0.0, 0.0, 0.0)):
        return _frame_values(0, 0, 400, 300, "fill", 1, 0, crop=crop)

    def test_full_source_retains_all_four_edges(self):
        frame = self.frame()
        self.assertEqual([frame[key] for key in
                          ("cropLeft", "cropTop", "cropRight", "cropBottom")],
                         [0, 0, 1, 1])

    def test_poster_vertical_insets_leave_nonempty_source(self):
        frame = self.frame((0, 1 / 12, 0, 1 / 12))
        self.assertEqual(frame["cropLeft"], 0)
        self.assertEqual(frame["cropRight"], 1)
        self.assertAlmostEqual(frame["cropTop"], 1 / 12)
        self.assertAlmostEqual(frame["cropBottom"], 11 / 12)

    def test_asymmetric_insets_preserve_the_selected_source_region(self):
        frame = self.frame((0.25958, 0.41806, 0.17292, 0.28325))
        self.assertAlmostEqual(frame["cropRight"] - frame["cropLeft"], 0.56750)
        self.assertAlmostEqual(frame["cropBottom"] - frame["cropTop"], 0.29869)


if __name__ == "__main__":
    unittest.main()
