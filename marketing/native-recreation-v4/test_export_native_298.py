import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import export_native_298 as exporter


class NativeExportBackupReceiptTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name).resolve()
        self.project = root / "native-save-copy.json"
        self.project_bytes = b'{"timelineId":"d941214f-27f2-4687-86b7-916d5cf4e110"}\n'
        self.project.write_bytes(self.project_bytes)
        self.document_hash = "a" * 64
        self.resource_id = "timeline:d941214f-27f2-4687-86b7-916d5cf4e110"
        self.receipt_path = root / "native-save-copy-receipt.json"
        self.receipt = {
            "ok": True,
            "commandId": "timeline.action.saveTimeline",
            "resourceId": self.resource_id,
            "timelineId": self.resource_id.removeprefix("timeline:"),
            "documentHash": self.document_hash,
            "taskStateHash": self.document_hash,
            "taskStepId": 123,
            "readbackVerified": True,
            "outputPath": str(self.project),
            "fileSha256": hashlib.sha256(self.project_bytes).hexdigest(),
        }
        self.write_receipt()
        self.inspection = {
            "ok": True,
            "active": True,
            "ready": True,
            "saved": False,
            "dirty": True,
            "resourceId": self.resource_id,
            "documentHash": self.document_hash,
            "width": 1920,
            "height": 1080,
            "frameRate": 30.0,
            "durationSeconds": 298.0,
        }
        self.preflight = {
            "ok": True,
            "preset": "youtube",
            "width": 1920,
            "height": 1080,
            "frameRate": 30.0,
            "estimatedOutputBytes": 2 * exporter.GIB,
            "requiredDiskBytes": 4 * exporter.GIB,
            "freeDiskBytes": 12 * exporter.GIB,
        }
        self.jobs = {"resourceId": self.resource_id, "jobs": [], "jobCount": 0}

    def write_receipt(self):
        self.receipt_path.write_text(json.dumps({
            "jsonrpc": "2.0",
            "result": {"structuredContent": self.receipt},
        }))

    def stubbed_native_preflight(self):
        values = {
            "timeline.inspect_document": self.inspection,
            "timeline.inspect_task": {"ok": True, "active": False},
            "timeline.inspect_delivery_preflight": self.preflight,
            "timeline.inspect_render_jobs": self.jobs,
        }
        return patch.object(exporter, "call_body",
                            side_effect=lambda name, arguments=None: values[name])

    def test_dirty_document_is_accepted_with_exact_readback_verified_copy(self):
        with self.stubbed_native_preflight():
            inspection, preflight, backup = exporter.check_document(
                "landscape", self.resource_id, self.document_hash,
                str(self.receipt_path))
        self.assertTrue(inspection["dirty"])
        self.assertFalse(inspection["saved"])
        self.assertEqual(preflight["preset"], "youtube")
        self.assertEqual(backup["fileSha256"], self.receipt["fileSha256"])
        self.assertEqual(backup["documentHash"], self.document_hash)

    def test_queue_snapshot_from_other_document_is_rejected(self):
        self.jobs["resourceId"] = "timeline:another-project"
        with self.stubbed_native_preflight():
            with self.assertRaisesRegex(RuntimeError, "render queue"):
                exporter.check_document("landscape", self.resource_id,
                                        self.document_hash, str(self.receipt_path))

    def test_active_native_job_prevents_duplicate_export(self):
        self.jobs["jobs"] = [{"status": "Running", "jobId": "owned-native-render"}]
        with self.stubbed_native_preflight():
            with self.assertRaisesRegex(RuntimeError, "already queued or running"):
                exporter.check_document("landscape", self.resource_id,
                                        self.document_hash, str(self.receipt_path))

    def test_missing_queue_rows_are_not_an_empty_queue(self):
        del self.jobs["jobs"]
        with self.stubbed_native_preflight():
            with self.assertRaisesRegex(RuntimeError, "render queue"):
                exporter.check_document("landscape", self.resource_id,
                                        self.document_hash, str(self.receipt_path))

    def test_stale_receipt_hash_is_rejected(self):
        self.receipt["documentHash"] = "b" * 64
        self.receipt["taskStateHash"] = "b" * 64
        self.write_receipt()
        with self.assertRaisesRegex(RuntimeError, "current Timeline hash"):
            exporter.verified_backup_receipt(
                str(self.receipt_path), self.resource_id, self.document_hash)

    def test_modified_project_bytes_are_rejected(self):
        self.project.write_bytes(self.project_bytes + b"changed")
        with self.assertRaisesRegex(RuntimeError, "project bytes"):
            exporter.verified_backup_receipt(
                str(self.receipt_path), self.resource_id, self.document_hash)

    def test_missing_receipt_is_rejected_even_for_clean_document(self):
        self.inspection["dirty"] = False
        self.inspection["saved"] = True
        with self.stubbed_native_preflight():
            with self.assertRaisesRegex(RuntimeError, "receipt is required"):
                exporter.check_document(
                    "landscape", self.resource_id, self.document_hash, None)


class NativeExportSourceRejectionTest(unittest.TestCase):
    def progress(self, structured=None):
        response = {"result": {"isError": True, "structuredContent": structured or {
            "error": "All Timeline source files must be online and fully checksum-verified before export."}}}
        return {"status": "enqueue-uncertain", "requestId": "retained-id",
                "error": "timeline.export_h264 failed: " + json.dumps(response)}

    def test_recorded_source_rejection_is_terminal_without_native_replay(self):
        progress = self.progress()
        with patch.object(exporter, "call", side_effect=AssertionError("must not enqueue")):
            result = exporter.resolve_explicit_source_rejection(progress)
        self.assertEqual(result["status"], "Rejected")
        self.assertEqual(result["error"], progress["error"])
        self.assertEqual(result["receipt"]["requestId"], "retained-id")

    def test_transport_or_unknown_native_errors_remain_uncertain(self):
        for error in ("connection reset after enqueue", "timeline.export_h264 failed: malformed"):
            progress = dict(self.progress(), error=error)
            self.assertEqual(exporter.resolve_explicit_source_rejection(progress), progress)
        progress = self.progress({"error": "An unknown error after publication."})
        self.assertEqual(exporter.resolve_explicit_source_rejection(progress), progress)

    def test_response_with_job_evidence_cannot_be_reclassified(self):
        for key, value in (("jobId", "job-id"), ("accepted", True), ("status", "Running")):
            structured = {"error": "All Timeline source files must be online and fully checksum-verified before export.", key: value}
            progress = self.progress(structured)
            self.assertEqual(exporter.resolve_explicit_source_rejection(progress), progress)


if __name__ == "__main__":
    unittest.main()
