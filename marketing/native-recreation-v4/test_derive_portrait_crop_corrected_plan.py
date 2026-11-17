"""Offline checks for the unexecuted portrait crop-plan derivation."""

from copy import deepcopy
import unittest

import derive_portrait_crop_corrected_plan as crop_plan


class PortraitCropPlanTests(unittest.TestCase):
    def test_bound_v7_derives_exactly_the_sixteen_reviewed_canvas_frames(self):
        bound_raw, bound = crop_plan._load_bound_v7()
        _, reference, rows = crop_plan._landscape_crop_reference()
        derived = crop_plan.build_crop_corrected_plan(
            bound, bound_raw, reference, rows)

        changes = derived["cropRepairProvenance"]["changed"]
        self.assertEqual(len(changes), 16)
        self.assertEqual(derived["composition"], {"width": 1080, "height": 1920, "fps": 30})
        self.assertEqual(derived["resourceId"], bound["resourceId"])
        self.assertEqual(derived["taskId"], bound["taskId"])
        self.assertEqual(derived["startingDocumentHash"], bound["startingDocumentHash"])
        self.assertFalse(derived["bindingProvenance"]["nativeAuthoringExecuted"])
        self.assertIsNone(derived["bindingProvenance"]["executionReceipt"])
        self.assertEqual(derived["outputPath"], str(crop_plan.DEFAULT_NATIVE_OUTPUT))

        before = {step["id"]: step for step in bound["steps"]}
        after = {step["id"]: step for step in derived["steps"]}
        changed_ids = {row["id"] for row in changes}
        self.assertEqual(len(before), len(after))
        for step_id, original in before.items():
            if step_id in changed_ids:
                updated = after[step_id]
                self.assertEqual(original["commandId"], updated["commandId"])
                for field in original["values"]:
                    if field not in {"cropRight", "cropBottom"}:
                        self.assertEqual(original["values"][field], updated["values"][field])
                self.assertEqual(updated["values"]["cropRight"], 1.0 - original["values"]["cropRight"])
                self.assertEqual(updated["values"]["cropBottom"], 1.0 - original["values"]["cropBottom"])
                self.assertEqual(updated["values"]["cropLeft"], original["values"]["cropLeft"])
                self.assertEqual(updated["values"]["cropTop"], original["values"]["cropTop"])
            elif step_id == crop_plan.SAVE_STEP:
                updated = deepcopy(original)
                updated["values"]["outputPath"] = str(crop_plan.DEFAULT_NATIVE_OUTPUT)
                self.assertEqual(after[step_id], updated)
            else:
                self.assertEqual(after[step_id], original)

    def test_drifted_canvas_frame_is_rejected_without_broadening_scope(self):
        bound_raw, bound = crop_plan._load_bound_v7()
        _, reference, rows = crop_plan._landscape_crop_reference()
        mutated = deepcopy(bound)
        target = next(step for step in mutated["steps"] if step.get("id") == rows[0]["id"])
        target["values"]["cropTop"] += 0.001
        with self.assertRaisesRegex(crop_plan.CropPlanError, "differ from reviewed landscape evidence"):
            crop_plan.build_crop_corrected_plan(mutated, bound_raw, reference, rows)


if __name__ == "__main__":
    unittest.main()
