"""Offline safety tests for the native Timeline acceptance runner."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import batch_acceptance as acceptance  # noqa: E402


class SimulatedProcessCrash(BaseException):
    """Model process death after a native intent is durable."""


class BatchAcceptanceSafetyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.plan_path = self.root / "plan.json"
        self.plan_path.write_text(json.dumps({
            "schema": acceptance.PLAN_SCHEMA,
            "taskId": "task-test",
            "resourceId": "timeline:test",
        }, separators=(",", ":")))
        self.plan, self.plan_sha = acceptance.load_plan(self.plan_path)

    def configure_plan(self, **values):
        plan = json.loads(self.plan_path.read_text())
        plan.update(values)
        self.plan_path.write_text(json.dumps(plan, separators=(",", ":")))
        self.plan, self.plan_sha = acceptance.load_plan(self.plan_path)
        return self.plan

    def completed_mutation(self, event_id, tool, arguments, result):
        acceptance.record(self.plan_path, self.plan_sha, {
            "event": "mutation_started", "eventId": event_id,
            "tool": tool, "arguments": arguments,
        })
        acceptance.record(self.plan_path, self.plan_sha, {
            "event": "mutation_completed", "eventId": event_id,
            "tool": tool, "arguments": arguments, "result": result,
        })

    def prepare_review_release(self):
        output_path = self.root / "owned-project.json"
        output_path.write_text("{}")
        plan = self.configure_plan(outputPath=str(output_path))
        acceptance.save_receipt(self.plan_path, self.plan_sha,
                                "native-undo-redo-passed", {
            "outputPath": str(output_path),
        })
        args = argparse.Namespace(
            execute=True, confirm_task_id=plan["taskId"],
            confirm_resource_id=plan["resourceId"],
        )
        return plan, args

    def test_mcp_result_envelope_error_is_detected(self):
        raw = {"jsonrpc": "2.0", "id": 1, "result": {
            "isError": True, "structuredContent": {"error": "task scope denied"}}}
        body = acceptance.safe_unpack(raw)
        self.assertEqual(body["error"], "task scope denied")
        self.assertTrue(acceptance.is_error(raw, body))

    def test_recovery_requires_the_exact_canonical_saved_document_path(self):
        canonical = self.root / "canonical.timeline.json"
        duplicate = self.root / "duplicate.timeline.json"
        canonical.write_text("{}")
        duplicate.write_text("{}")
        plan = self.configure_plan(canonicalDocumentPath=str(canonical.resolve()))
        acceptance.require_canonical_document_path(
            {"savedDocumentPath": str(canonical.resolve())}, plan)
        with self.assertRaisesRegex(acceptance.AcceptanceError,
                                    "exact canonical saved project path"):
            acceptance.require_canonical_document_path(
                {"savedDocumentPath": str(duplicate.resolve())}, plan)

    def test_expected_native_rejection_is_recorded_as_settled(self):
        raw = {"jsonrpc": "2.0", "id": 1, "result": {
            "isError": True, "structuredContent": {"error": "changed batch body"}}}
        with patch.object(acceptance, "call", return_value=raw):
            body = acceptance.timed_rpc(
                self.plan_path, self.plan_sha, "changed-body", "app.invoke_command",
                {"commandId": "batch", "values": {}}, mutating=True,
                expected_rejection=True)
        self.assertEqual(body["error"], "changed batch body")
        events = acceptance.read_events(self.plan_path, self.plan_sha)
        self.assertEqual([item["event"] for item in events],
                         ["mutation_started", "mutation_completed"])
        acceptance.verify_no_unknown_mutations(events)

    def test_unexpected_mcp_rejection_fails_and_is_not_replayed(self):
        raw = {"jsonrpc": "2.0", "id": 1, "result": {
            "isError": True, "structuredContent": {"error": "scope denied"}}}
        args = {"commandId": "batch", "values": {}}
        with patch.object(acceptance, "call", return_value=raw):
            with self.assertRaisesRegex(acceptance.AcceptanceError, "scope denied"):
                acceptance.timed_rpc(self.plan_path, self.plan_sha,
                                     "unsafe-mutation", "app.invoke_command",
                                     args, mutating=True)
        with self.assertRaisesRegex(acceptance.AcceptanceError, "previously failed"):
            acceptance.prior_mutation(self.plan_path, self.plan_sha,
                                      "unsafe-mutation", "app.invoke_command", args)

    def test_lost_response_after_intent_is_never_replayed(self):
        args = {"commandId": "batch", "values": {"batchId": "once"}}
        with patch.object(acceptance, "call", side_effect=SimulatedProcessCrash()):
            with self.assertRaises(SimulatedProcessCrash):
                acceptance.timed_rpc(self.plan_path, self.plan_sha,
                                     "lost-response", "app.invoke_command",
                                     args, mutating=True)
        with self.assertRaisesRegex(acceptance.AcceptanceError, "no durable response receipt"):
            acceptance.prior_mutation(self.plan_path, self.plan_sha,
                                      "lost-response", "app.invoke_command", args)

    def test_changed_payload_cannot_reuse_completed_step_id(self):
        args = {"commandId": "batch", "values": {"text": "original"}}
        acceptance.record(self.plan_path, self.plan_sha, {
            "event": "mutation_started", "eventId": "completed",
            "tool": "app.invoke_command", "arguments": args,
        })
        acceptance.record(self.plan_path, self.plan_sha, {
            "event": "mutation_completed", "eventId": "completed",
            "tool": "app.invoke_command", "arguments": args,
            "result": {"ok": True},
        })
        with self.assertRaisesRegex(acceptance.AcceptanceError, "does not match"):
            acceptance.prior_mutation(
                self.plan_path, self.plan_sha, "completed", "app.invoke_command",
                {"commandId": "batch", "values": {"text": "changed"}})

    def test_receipt_updates_preserve_stable_ids_and_source_fingerprint(self):
        acceptance.save_receipt(self.plan_path, self.plan_sha, "authored", {
            "importedClipId": "clip-stable", "sourceFingerprintAtImport": {"sourceContentSha256": "abc"},
        })
        acceptance.save_receipt(self.plan_path, self.plan_sha, "replayed", {
            "sameBatchIdReplay": "passed",
        })
        receipt = json.loads(acceptance.receipt_path_for(self.plan_path).read_text())
        self.assertEqual(receipt["status"], "replayed")
        self.assertEqual(receipt["importedClipId"], "clip-stable")
        self.assertEqual(receipt["sourceFingerprintAtImport"]["sourceContentSha256"], "abc")
        self.assertEqual(receipt["sameBatchIdReplay"], "passed")

    def test_review_release_resumes_from_durable_finish_receipt(self):
        plan, args = self.prepare_review_release()
        finish_args = {"taskId": plan["taskId"], "action": "review"}
        self.completed_mutation("task.finish.review", "timeline.finish_task",
                                finish_args, {"status": "review"})
        timed_calls = []

        def fake_timed_rpc(plan_path, plan_sha, step_id, tool, arguments, **kwargs):
            timed_calls.append((step_id, tool, arguments))
            return {"status": "released", "active": False}

        with patch.object(acceptance, "inspect_task",
                          return_value={"active": True, "status": "review"}), \
             patch.object(acceptance, "timed_rpc", side_effect=fake_timed_rpc):
            acceptance.phase_review_release(self.plan_path, self.plan_sha, plan, args)

        self.assertEqual([item[0] for item in timed_calls], ["task.release.reviewed"])
        receipt = json.loads(acceptance.receipt_path_for(self.plan_path).read_text())
        self.assertEqual(receipt["status"], "released-awaiting-derived-drain")
        self.assertEqual(receipt["finishReceipt"]["status"], "review")

    def test_review_release_rejects_review_without_its_finish_receipt(self):
        plan, args = self.prepare_review_release()
        with patch.object(acceptance, "inspect_task",
                          return_value={"active": True, "status": "review"}), \
             patch.object(acceptance, "timed_rpc",
                          side_effect=AssertionError("must not replay an unreceipted finish")):
            with self.assertRaisesRegex(acceptance.AcceptanceError,
                                        "durable successful finish receipt"):
                acceptance.phase_review_release(self.plan_path, self.plan_sha, plan, args)

    def test_review_release_recognizes_completed_release_after_receipt_write_crash(self):
        plan, args = self.prepare_review_release()
        finish_args = {"taskId": plan["taskId"], "action": "review"}
        release_args = {"taskId": plan["taskId"], "confirmed": True}
        self.completed_mutation("task.finish.review", "timeline.finish_task",
                                finish_args, {"status": "review"})
        self.completed_mutation("task.release.reviewed", "timeline.release_reviewed_task",
                                release_args, {"status": "released", "active": False})

        with patch.object(acceptance, "inspect_task",
                          return_value={"active": False, "status": "released"}), \
             patch.object(acceptance, "timed_rpc",
                          side_effect=AssertionError("completed lifecycle actions must not replay")):
            acceptance.phase_review_release(self.plan_path, self.plan_sha, plan, args)

        receipt = json.loads(acceptance.receipt_path_for(self.plan_path).read_text())
        self.assertEqual(receipt["status"], "released-awaiting-derived-drain")
        self.assertEqual(receipt["releaseReceipt"]["status"], "released")

    def prepare_revert(self, document_hash, clip_count=0):
        output_path = self.root / "owned-project.json"
        plan = self.configure_plan(outputPath=str(output_path))
        acceptance.save_receipt(self.plan_path, self.plan_sha,
                                "recovered-awaiting-confirmed-revert", {
            "baselineDocumentHash": document_hash,
            "baselineClipCount": clip_count,
        })
        args = argparse.Namespace(
            execute=True, confirm_revert=True,
            confirm_task_id=plan["taskId"],
            confirm_resource_id=plan["resourceId"],
        )
        return plan, args

    def run_fake_revert(self, document):
        def fake_timed_rpc(plan_path, plan_sha, step_id, tool, arguments, **kwargs):
            if step_id == "task.inspect.before-revert":
                return {"active": True, "status": "running"}
            if step_id == "task.revert.confirmed":
                return {"ok": True, "reverted": True}
            raise AssertionError(f"unexpected native call {step_id}")

        with patch.object(acceptance, "timed_rpc", side_effect=fake_timed_rpc), \
             patch.object(acceptance, "inspect_document", return_value=document), \
             patch.object(acceptance, "list_commands", return_value={}):
            return acceptance.phase_revert(
                self.plan_path, self.plan_sha, self.plan,
                argparse.Namespace(
                    execute=True, confirm_revert=True,
                    confirm_task_id=self.plan["taskId"],
                    confirm_resource_id=self.plan["resourceId"],
                ))

    def test_revert_rejects_a_nonbaseline_document_hash(self):
        plan, _ = self.prepare_revert("baseline-hash")
        self.plan = plan
        with self.assertRaisesRegex(acceptance.AcceptanceError,
                                    "exact saved empty Timeline baseline"):
            self.run_fake_revert({"documentHash": "changed-hash", "clips": []})
        receipt = json.loads(acceptance.receipt_path_for(self.plan_path).read_text())
        self.assertEqual(receipt["status"], "revert-conflict")
        self.assertFalse(receipt["baselineRestored"])

    def test_revert_rejects_any_remaining_clip(self):
        plan, _ = self.prepare_revert("baseline-hash")
        self.plan = plan
        with self.assertRaisesRegex(acceptance.AcceptanceError,
                                    "exact saved empty Timeline baseline"):
            self.run_fake_revert({"documentHash": "baseline-hash", "clips": [{"clipId": "leftover"}]})
        receipt = json.loads(acceptance.receipt_path_for(self.plan_path).read_text())
        self.assertEqual(receipt["status"], "revert-conflict")
        self.assertFalse(receipt["baselineRestored"])

    def test_revert_passes_only_on_exact_empty_baseline(self):
        plan, _ = self.prepare_revert("baseline-hash")
        self.plan = plan
        self.run_fake_revert({"documentHash": "baseline-hash", "clips": []})
        receipt = json.loads(acceptance.receipt_path_for(self.plan_path).read_text())
        self.assertEqual(receipt["status"], "revert-passed")
        self.assertTrue(receipt["baselineRestored"])
        self.assertEqual(receipt["postRevertClipCount"], 0)

    def test_prepare_creates_local_plan_without_mcp_calls(self):
        output = self.root / "outputs"
        output.mkdir()
        original = self.root / "baseline.json"
        original.write_text("{}")
        plan_path = self.root / "acceptance-plan.json"
        source_copy_args = argparse.Namespace(copy_id="demo-input-unique")
        args = argparse.Namespace(
            task_id="disposable-task", resource_id="timeline:disposable",
            output_directory=str(output.resolve()), canonical_document=str(original.resolve()),
            source_copy="", plan=plan_path,
        )
        with patch.object(acceptance, "ROOT", self.root.resolve()), \
             patch.object(acceptance, "call", side_effect=AssertionError("prepare must not call MCP")):
            acceptance.prepare_source(source_copy_args)
            source_copy = self.root / "acceptance-input" / source_copy_args.copy_id / "detail-0.png"
            args.source_copy = str(source_copy.resolve())
            acceptance.prepare(args)
        plan = json.loads(plan_path.read_text())
        self.assertEqual(plan["taskId"], "disposable-task")
        self.assertEqual(plan["resourceId"], "timeline:disposable")
        self.assertEqual(plan["outputDirectory"], str(output.resolve()))
        self.assertTrue(plan["sourceOrigin"]["path"].endswith("creative-polish-v2/detail/detail-0.png"))
        self.assertTrue(plan["source"]["path"].endswith(
            "acceptance-input/demo-input-unique/detail-0.png"))
        self.assertEqual(plan["source"]["bytes"], plan["sourceOrigin"]["bytes"])
        self.assertEqual(plan["source"]["sha256"], plan["sourceOrigin"]["sha256"])
        self.assertTrue(Path(plan["source"]["path"]).is_file())
        self.assertFalse(Path(plan["outputPath"]).exists())
        copy_receipt = json.loads(Path(plan["sourceCopyReceiptPath"]).read_text())
        self.assertEqual(copy_receipt["taskId"], "disposable-task")
        self.assertEqual(copy_receipt["resourceId"], "timeline:disposable")
        second_args = argparse.Namespace(
            task_id="second-task", resource_id="timeline:second",
            output_directory=str(output.resolve()), canonical_document=str(original.resolve()),
            source_copy=str(source_copy.resolve()), plan=self.root / "second-plan.json",
        )
        with patch.object(acceptance, "ROOT", self.root.resolve()):
            with self.assertRaisesRegex(acceptance.AcceptanceError, "already claimed"):
                acceptance.prepare(second_args)

    def test_source_copy_never_overwrites_an_existing_input(self):
        origin = acceptance.source_manifest_entry()
        with patch.object(acceptance, "ROOT", self.root.resolve()):
            copied = acceptance.copy_novel_task_source("never-overwrite", origin)
            with self.assertRaisesRegex(acceptance.AcceptanceError, "already has a novel acceptance input copy"):
                acceptance.copy_novel_task_source("never-overwrite", origin)
        self.assertEqual(acceptance.source_identity_for(copied["path"]), copied)


if __name__ == "__main__":
    unittest.main()
