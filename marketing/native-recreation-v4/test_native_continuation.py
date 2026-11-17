"""Offline safety tests for build_native_continuation.py."""

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import author_native
import build_native_continuation as continuation


RESOURCE_ID = "timeline:00000000-0000-4000-8000-000000000001"
TASK_ID = "00000000-0000-4000-8000-000000000002"
DOCUMENT_HASH = "a" * 64
OUTPUT_DIRECTORY = "/tmp/continuation-proof/projects"
SAFE = "timeline.action.setTimelineClipFrame"


def _write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def _tool_envelope(value, is_error=False):
    return {
        "jsonrpc": "2.0",
        "result": {"isError": is_error, "structuredContent": value},
    }


class ContinuationBuilderTest(unittest.TestCase):
    failed_count = 8

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.parent_path = self.root / "parent.json"
        self.corrected_path = self.root / "corrected.json"
        self.receipt_path = self.root / "receipt.json"
        self.log_path = self.root / "receipt.ndjson"
        self.task_path = self.root / "task.json"
        self.document_path = self.root / "document.json"

        self.parent = {
            "composition": {"width": 1920, "height": 1080, "fps": 30},
            "resourceId": RESOURCE_ID,
            "taskId": TASK_ID,
            "startingDocumentHash": DOCUMENT_HASH,
            "outputDirectory": OUTPUT_DIRECTORY,
            "outputPath": OUTPUT_DIRECTORY + "/landscape.mp4",
            "assetPaths": [],
            "steps": [
                {"id": "complete/track", "commandId": "timeline.action.createTimelineTrack",
                 "values": {"kind": "graphics", "name": "Track"}},
                {"id": "complete/title", "commandId": "timeline.action.createTimelineTitle",
                 "values": {"trackId": {"result": "complete/track", "field": "trackId"},
                            "text": "Done"}},
                {"id": "retry/title", "commandId": "timeline.action.createTimelineTitle",
                 "values": {"trackId": {"result": "complete/track", "field": "trackId"},
                            "text": "Retry"}},
                {"id": "retry/frame-from-title", "commandId": SAFE,
                 "values": {"clipId": {"result": "retry/title", "field": "createdClipId"},
                            "opacity": 0.9}},
            ] + [
                {"id": f"retry/frame-{index}", "commandId": SAFE,
                 "values": {"clipId": f"clip-{index}", "opacity": 1.0}}
                for index in range(2, self.failed_count)
            ] + [
                {"id": "later/final", "commandId": SAFE,
                 "values": {"clipId": "final", "opacity": 0.5}},
            ],
        }
        self.corrected = copy.deepcopy(self.parent)
        # An uncompleted action may be corrected; the completed prefix must not.
        self.corrected["steps"][8]["values"]["opacity"] = 0.75
        self.receipt = {"status": "incomplete", "steps": [], "failure": {}}

        self.prefix_receipts = [
            {
                "id": "complete/track",
                "commandId": "timeline.action.createTimelineTrack",
                "values": {"kind": "graphics", "name": "Track"},
                "result": {"ok": True, "commandId": "timeline.action.createTimelineTrack",
                           "trackId": "graphics-stable"},
            },
            {
                "id": "complete/title",
                "commandId": "timeline.action.createTimelineTitle",
                "values": {"trackId": "graphics-stable", "text": "Done"},
                "result": {"ok": True, "commandId": "timeline.action.createTimelineTitle",
                           "createdClipId": "clip-created-title",
                           "trackId": "graphics-stable"},
            },
        ]
        self.receipt["steps"] = copy.deepcopy(self.prefix_receipts)

        parent_bytes = continuation.serialize_plan(self.parent)
        self.parent_hash = hashlib.sha256(parent_bytes).hexdigest()
        positions = {step["id"]: i for i, step in enumerate(self.parent["steps"])}
        prefix_results = {row["id"]: row["result"] for row in self.prefix_receipts}
        self.failed_steps = self.parent["steps"][2:2 + self.failed_count]
        request = author_native._build_batch_request(
            self.parent_hash, self.failed_steps, prefix_results)
        self.batch_id = request["batchId"]
        self.request_sha = "b" * 64
        self.receipt["planSha256"] = self.parent_hash
        self.receipt["failure"] = {
            "stepId": self.failed_steps[0]["id"],
            "batchId": self.batch_id,
            # This was conservative in the real historical receipt. The builder
            # must rely on the native proof instead.
            "recoveryRequired": True,
            "rolledBack": False,
        }

        result_rows = []
        for index, operation in enumerate(request["operations"]):
            result_rows.append({
                "id": operation["id"],
                "commandId": operation["commandId"],
                "ok": index < len(request["operations"]) - 1,
            })
        native_result = {
            "commandId": continuation.BATCH_COMMAND_ID,
            "batchId": self.batch_id,
            "batchRequestSha256": self.request_sha,
            "taskStepId": 2,
            "resourceId": RESOURCE_ID,
            "ok": False,
            "rolledBack": True,
            "changed": False,
            "mutationNotApplied": True,
            "taskStateHash": DOCUMENT_HASH,
            "batchResults": result_rows,
        }
        self.native_result = native_result
        error_envelope = json.dumps(_tool_envelope(native_result, is_error=True))

        events = [{
            "schema": continuation.RECEIPT_SCHEMA,
            "planSha256": self.parent_hash,
            "event": "plan_opened",
            "stepCount": len(self.parent["steps"]),
        }]
        for index, row in enumerate(self.prefix_receipts):
            events.extend([
                {"schema": continuation.RECEIPT_SCHEMA, "planSha256": self.parent_hash,
                 "event": "invoke_started", "stepId": row["id"],
                 "commandId": row["commandId"], "values": row["values"]},
                {"schema": continuation.RECEIPT_SCHEMA, "planSha256": self.parent_hash,
                 "event": "invoke_completed", "stepId": row["id"],
                 "commandId": row["commandId"], "values": row["values"],
                 "result": row["result"]},
            ])
        for index, operation in enumerate(request["operations"]):
            step = self.failed_steps[index]
            events.append({
                "schema": continuation.RECEIPT_SCHEMA,
                "planSha256": self.parent_hash,
                "event": "invoke_started",
                "stepId": step["id"],
                "commandId": step["commandId"],
                "values": step["values"],
                "batchId": self.batch_id,
                "batchOperationId": operation["id"],
                "batchOperationValues": operation["values"],
                "batchOperationCount": self.failed_count,
            })
        for step in self.failed_steps:
            events.append({
                "schema": continuation.RECEIPT_SCHEMA,
                "planSha256": self.parent_hash,
                "event": "invoke_failed",
                "stepId": step["id"],
                "commandId": step["commandId"],
                "batchId": self.batch_id,
                "error": error_envelope,
                "rolledBack": False,
                "recoveryRequired": True,
            })
        self.events = events

        self.task = {
            "ok": True,
            "active": True,
            "status": "running",
            "taskId": TASK_ID,
            "targetResourceIds": [RESOURCE_ID],
            "nativeTimelineScope": {
                "targetIds": [RESOURCE_ID],
                "outputDirectory": OUTPUT_DIRECTORY,
                "assetPaths": [],
            },
            "resources": [{"id": RESOURCE_ID, "beforeHash": DOCUMENT_HASH, "currentHash": DOCUMENT_HASH,
                           "pending": False, "reverted": False}],
            "steps": [
                {"id": 0, "status": "completed", "resourceId": RESOURCE_ID,
                 "command": "timeline.action.createTimelineTrack", "arguments": {},
                 "outcome": {"ok": True}},
                {"id": 1, "status": "completed", "resourceId": RESOURCE_ID,
                 "command": "timeline.action.createTimelineTitle", "arguments": {},
                 "outcome": {"ok": True}},
                {"id": 2, "status": "failed", "resourceId": RESOURCE_ID,
                 "command": continuation.BATCH_COMMAND_ID,
                 "arguments": {
                     "batchId": self.batch_id,
                     "_batchId": self.batch_id,
                     "_batchRequestSha256": self.request_sha,
                     "operations": request["operations"],
                 },
                 "outcome": native_result},
            ],
        }
        self.document = {
            "ok": True,
            "ready": True,
            "resourceId": RESOURCE_ID,
            "documentHash": DOCUMENT_HASH,
            "width": 1920,
            "height": 1080,
            "frameRate": 30,
        }
        self._write_inputs()

    def tearDown(self):
        self.temporary.cleanup()

    def _write_inputs(self):
        _write_json(self.parent_path, self.parent)
        _write_json(self.corrected_path, self.corrected)
        _write_json(self.receipt_path, self.receipt)
        self.log_path.write_text("".join(json.dumps(event) + "\n" for event in self.events))
        _write_json(self.task_path, _tool_envelope(self.task))
        _write_json(self.document_path, _tool_envelope(self.document))

    def build(self):
        return continuation.build_continuation_plan(
            self.parent_path, self.corrected_path, self.receipt_path,
            self.log_path, self.task_path, self.document_path)

    def test_success_emits_only_suffix_and_resolves_completed_refs_without_mcp(self):
        with mock.patch.object(author_native, "call", side_effect=AssertionError("MCP call")):
            plan = self.build()
        self.assertEqual(len(plan["steps"]), self.failed_count + 1)
        self.assertEqual([step["id"] for step in plan["steps"][:self.failed_count]],
                         [step["id"] for step in self.failed_steps])
        self.assertEqual(plan["steps"][0]["values"]["trackId"], "graphics-stable")
        self.assertEqual(plan["steps"][1]["values"]["clipId"],
                         {"result": "retry/title", "field": "createdClipId"})
        self.assertEqual(plan["steps"][6]["values"]["opacity"], 0.75)
        self.assertNotIn("receipt", plan)
        self.assertNotIn("events", plan)
        provenance = plan["continuationProvenance"]
        self.assertEqual(provenance["completedPrefixCount"], 2)
        self.assertTrue(provenance["nativeRollbackVerified"])
        self.assertNotEqual(provenance["failedBatchId"],
                            author_native._build_batch_request(
                                hashlib.sha256(continuation.serialize_plan(plan)).hexdigest(),
                                plan["steps"][:self.failed_count],
                                {row["id"]: row["result"] for row in self.prefix_receipts})["batchId"])

    def assert_refused(self):
        with self.assertRaises(continuation.ContinuationError):
            self.build()

    def test_rejects_changed_completed_prefix(self):
        self.corrected["steps"][0]["values"]["name"] = "Changed"
        self._write_inputs()
        self.assert_refused()

    def test_rejects_unknown_intent(self):
        self.events.append({"schema": continuation.RECEIPT_SCHEMA,
                            "planSha256": self.parent_hash,
                            "event": "invoke_started", "stepId": "unknown/step",
                            "commandId": SAFE, "values": {}})
        self._write_inputs()
        self.assert_refused()

    def test_rejects_unproven_rollback(self):
        self.task["steps"][-1]["outcome"]["rolledBack"] = False
        self._write_inputs()
        self.assert_refused()

    def test_rejects_mismatched_task_or_document_hash(self):
        self.task["taskId"] = "00000000-0000-4000-8000-000000000099"
        self._write_inputs()
        self.assert_refused()
        self.task["taskId"] = TASK_ID
        self.document["documentHash"] = "c" * 64
        self.task["resources"][0]["currentHash"] = "c" * 64
        self._write_inputs()
        self.assert_refused()

    def test_rejects_missing_reference(self):
        self.corrected["steps"][2]["values"]["trackId"] = {
            "result": "missing/step", "field": "trackId"}
        self._write_inputs()
        self.assert_refused()

    def test_rejects_conflicting_native_operation_count(self):
        self.native_result["operationCount"] = self.failed_count - 1
        self._write_inputs()
        self.assert_refused()

    def test_completed_prefix_can_advance_from_the_initial_hash(self):
        current = "c" * 64
        self.document["documentHash"] = current
        self.task["resources"][0]["currentHash"] = current
        self.native_result["taskStateHash"] = current
        for event in self.events:
            if event.get("event") == "invoke_failed":
                event["error"] = json.dumps(_tool_envelope(self.native_result, is_error=True))
        self._write_inputs()
        self.assertEqual(self.build()["startingDocumentHash"], current)


class ThirteenOperationContinuationTest(ContinuationBuilderTest):
    failed_count = 13


if __name__ == "__main__":
    unittest.main()
