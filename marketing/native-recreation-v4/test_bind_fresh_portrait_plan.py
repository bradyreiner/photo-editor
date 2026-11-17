"""Negative-only binder tests; none of these fixtures are native proof."""

from pathlib import Path
import json
import tempfile
import unittest

import bind_fresh_portrait_plan as binder
from copy import deepcopy


def _native_response(body):
    return {"jsonrpc": "2.0", "id": 1,
            "result": {"isError": False, "structuredContent": body}}


class FreshPortraitBinderNegativeTests(unittest.TestCase):
    """Synthetic fixtures only exercise fail-closed validation branches."""

    def test_existing_landscape_create_receipt_is_rejected_for_portrait(self):
        evidence = binder.HERE / "evidence"
        with self.assertRaisesRegex(binder.BinderError, "fresh portrait"):
            binder.validate_create_proof(
                evidence / "isolated-native-create-intent.json",
                evidence / "isolated-native-create-result.json",
                evidence / "isolated-native-initial-drafts.json")

    def test_synthetic_wrong_orientation_create_receipt_is_rejected(self):
        request = binder.create_draft_request()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            intent = root / "create-intent.json"
            result = root / "create-result.json"
            drafts = root / "drafts.json"
            intent.write_text(json.dumps(request))
            resource = {
                "ready": True, "resourceId": "timeline:11111111-1111-4111-8111-111111111111",
                "name": binder.TIMELINE_NAME, "width": 1920, "height": 1080,
                "frameRate": 30, "clips": [], "tracks": [],
            }
            result.write_text(json.dumps(_native_response({
                "ok": True, "operation": "created", "projectFileCreated": True,
                "libraryRegistered": True, "navigationRevertible": False,
                "resource": resource,
            })))
            drafts.write_text(json.dumps(_native_response({
                "ok": True, "draftCount": 1, "unavailableRecordCount": 0,
                "drafts": [{"timelineId": "11111111-1111-4111-8111-111111111111",
                            "name": binder.TIMELINE_NAME}],
            })))
            with self.assertRaisesRegex(binder.BinderError, "exact portrait"):
                binder.validate_create_proof(intent, result, drafts)

    def test_empty_draft_rejects_any_clip(self):
        with self.assertRaisesRegex(binder.BinderError, "exactly zero"):
            binder._check_empty_clips([{"clipId": "synthetic"}], [], "synthetic negative fixture")

    def test_document_hash_mismatch_is_rejected(self):
        fake = _native_response({
            "ok": True, "ready": True,
            "resourceId": "timeline:11111111-1111-4111-8111-111111111111",
            "savedDocumentPath": "/tmp/native-documents/11111111-1111-4111-8111-111111111111.json",
            "width": 1080, "height": 1920, "frameRate": 30,
            "clips": [], "tracks": [], "documentHash": "bad",
        })
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "document.json"
            path.write_text(json.dumps(fake))
            with self.assertRaisesRegex(binder.BinderError, "canonical lowercase SHA"):
                binder.validate_document_proof(
                    path, "timeline:11111111-1111-4111-8111-111111111111",
                    "/tmp/native-documents/11111111-1111-4111-8111-111111111111.json")

    def test_admission_scope_mismatch_is_rejected(self):
        resource_id = "timeline:11111111-1111-4111-8111-111111111111"
        plan = {"assetPaths": ["/source/a.png", "/source/b.png"],
                "outputDirectory": "/output"}
        expected = binder.admission_request(plan, resource_id)["arguments"]
        intent = {"tool": "timeline.admit_task", "arguments": expected}
        response = _native_response({
            "ok": True, "active": True, "status": "running", "taskId": "22222222-2222-4222-8222-222222222222",
            "targetResourceId": resource_id,
            "nativeTimelineScope": {"targetIds": [resource_id],
                "assetPaths": ["/source/a.png"], "outputDirectory": "/output"},
            "resources": [{"id": resource_id, "pending": False,
                "beforeHash": "a" * 64, "currentHash": "a" * 64}],
        })
        with tempfile.TemporaryDirectory() as temporary:
            intent_path = Path(temporary) / "intent.json"
            arguments_path = Path(temporary) / "arguments.json"
            result_path = Path(temporary) / "result.json"
            intent_path.write_text(json.dumps(intent))
            arguments_path.write_text(json.dumps(expected))
            result_path.write_text(json.dumps(response))
            with self.assertRaisesRegex(binder.BinderError, "exact 104 portrait asset paths"):
                binder.validate_admission_proof(intent_path, arguments_path, result_path, plan,
                                                 resource_id, "a" * 64)

    def test_existing_native_output_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "looksy-native-full-portrait-v7.json"
            output.write_text("existing")
            with self.assertRaisesRegex(binder.BinderError, "already exists"):
                binder._assert_absent_output(output, temporary)

    def test_opening_corrections_change_only_the_three_reviewed_fields(self):
        _, parent = binder._read_parent(binder.PARENT_PLAN)
        corrected = deepcopy(parent)
        original = {
            step["id"]: deepcopy(step["values"])
            for step in parent["steps"]
            if step.get("id") in {
                "opening/desktop-handoff-label",
                "opening/desktop-handoff-headline",
                "opening/desktop-source-image-frame",
            }
        }

        corrections = binder._apply_three_opening_corrections(corrected)
        by_id = {step["id"]: step for step in corrected["steps"] if step.get("id") in original}

        self.assertEqual(len(corrections), 3)
        self.assertAlmostEqual(
            by_id["opening/desktop-handoff-label"]["values"]["values"]["positionX"],
            original["opening/desktop-handoff-label"]["values"]["positionX"] + 2.9 / 1920.0,
        )
        self.assertAlmostEqual(
            by_id["opening/desktop-handoff-headline"]["values"]["values"]["positionX"],
            original["opening/desktop-handoff-headline"]["values"]["positionX"] + 5.8 / 1920.0,
        )
        self.assertEqual(
            by_id["opening/desktop-source-image-frame"]["values"]["objectFit"], "stretch")
        self.assertEqual(
            by_id["opening/desktop-source-image-frame"]["values"]["offsetY"],
            original["opening/desktop-source-image-frame"]["offsetY"],
        )

    def test_bound_plan_must_stay_new_and_in_native_recreation_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            wrong_directory = Path(temporary) / binder.DEFAULT_BOUND_PLAN.name
            with self.assertRaisesRegex(binder.BinderError, "native-recreation-v4 directory"):
                binder._assert_new_plan_path(wrong_directory)

    def test_malformed_admission_resource_row_is_rejected_cleanly(self):
        resource_id = "timeline:11111111-1111-4111-8111-111111111111"
        plan = {"assetPaths": [], "outputDirectory": "/output"}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            intent_path = root / "intent.json"
            arguments_path = root / "arguments.json"
            result_path = root / "result.json"
            arguments = binder.admission_request(plan, resource_id)["arguments"]
            intent_path.write_text(json.dumps({
                "tool": "timeline.admit_task",
                "arguments": arguments,
            }))
            arguments_path.write_text(json.dumps(arguments))
            result_path.write_text(json.dumps(_native_response({
                "ok": True, "active": True, "status": "running",
                "taskId": "22222222-2222-4222-8222-222222222222",
                "targetResourceId": resource_id,
                "nativeTimelineScope": {"targetIds": [resource_id], "assetPaths": [],
                                        "outputDirectory": "/output"},
                "resources": [None],
            })))
            with self.assertRaisesRegex(binder.BinderError, "beforeHash/currentHash"):
                binder.validate_admission_proof(intent_path, arguments_path, result_path, plan,
                                                resource_id, "a" * 64)


if __name__ == "__main__":
    unittest.main()
