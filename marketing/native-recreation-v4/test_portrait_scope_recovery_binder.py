"""Evidence tests for the offline portrait v8 scope-recovery binder."""

import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import bind_reviewed_continuation as binder


EVIDENCE = HERE / "evidence"
PARENT = HERE / "full-portrait-v8-crop-corrected.commands.json"
CORRECTED = HERE / "full-portrait-v9-schema-corrected-parent.commands.json"
RECEIPT = EVIDENCE / "native-full-portrait-v8-receipt.json"
EVENTS = EVIDENCE / "native-full-portrait-v8-receipt.json.ndjson"
REVIEW = EVIDENCE / "portrait-v8-upgrade-review-result.json"


class PortraitScopeRecoveryTest(unittest.TestCase):
    def _validate(self, parent=PARENT, receipt=RECEIPT, events=EVENTS,
                  review=REVIEW, corrected=CORRECTED):
        return binder.validate_portrait_v8_scope_blocked_attempt(
            parent, receipt, events, review, corrected)

    def test_exact_scope_rejection_has_1637_steps_and_no_native_journal_row(self):
        proof = self._validate()
        self.assertEqual(proof["prefix"]["completedCount"], 1637)
        self.assertEqual(proof["prefix"]["failedSteps"][0]["id"],
                         "library-carousel/p-lib-62-IMG_4788-image")
        self.assertEqual(proof["prefix"]["failedSteps"][0]["commandId"],
                         binder.SCOPE_REJECTION_COMMAND)
        self.assertEqual(len(proof["journalHistory"]["successfulRows"]), 236)
        self.assertEqual(proof["journalHistory"]["failedRows"], [])
        self.assertIs(proof["scopeFailureEvidence"]["nativeRejectedOperationRowPresent"], False)
        self.assertEqual(len(proof["futurePlan"]["steps"]), 3643)

    def test_scope_continuation_provenance_preserves_no_claim_and_native_lineage(self):
        proof = self._validate()
        parent = json.loads(PARENT.read_text())
        evidence = proof["scopeFailureEvidence"]
        digest = "a" * 64
        binding = parent["bindingProvenance"]
        provenance = {
            "schema": binder.SCOPE_REVIEW_SCHEMA,
            "parentPlanSha256": evidence["parentPlanSha256"],
            "parentReceiptSha256": evidence["parentReceiptSha256"],
            "parentEventLogSha256": evidence["parentEventLogSha256"],
            "stopReviewProofSha256": evidence["reviewTaskProofSha256"],
            "finalReviewProofSha256": digest,
            "releasedSourceTaskProofSha256": digest,
            "sourceDocumentProofSha256": digest,
            "sourceSaveProofSha256": digest,
            "sourceProjectSha256": digest,
            "sourceGraphSha256": digest,
            "copyGraphSha256": digest,
            "importReceiptSha256": digest,
            "freshAdmissionProofSha256": digest,
            "importedDocumentProofSha256": digest,
            "completedPrefixCount": 1637,
            "lastCompletedStepId": "library-carousel/lane-portrait-library-media-07",
            "completedPrefixResultSha256": evidence["completedPrefixResultSha256"],
            "rejectedStepIds": [evidence["stepId"]],
            "rejectedGuard": binder.SCOPE_REJECTION_ERROR,
            "nativePreDispatchRejectionVerified": True,
            "sourceResourceId": evidence["sourceResourceId"],
            "sourceTaskId": evidence["sourceTaskId"],
            "sourceDocumentHash": evidence["reviewTaskHash"],
            "sourceSavedDocumentPath": str((HERE / "projects/source.json").resolve()),
            "sourceFileSha256": digest,
            "sourceTrackIdSetSha256": digest,
            "sourceClipIdSetSha256": digest,
            "resourceId": "timeline:00000000-0000-4000-8000-000000000201",
            "taskId": "00000000-0000-4000-8000-000000000202",
            "startingDocumentHash": "b" * 64,
            "journalCheckpointHash": "b" * 64,
            "outputDirectory": str(HERE / "projects"),
            "outputPath": str(HERE / "projects/copy.json"),
            "clipCount": 518,
            "trackCount": 429,
            "retargetedFields": [
                "steps[completed-prefix:]", "resourceId", "taskId", "startingDocumentHash",
                "outputPath", "steps[delivery/save-editable-project].values.outputPath",
                "continuationProvenance", "proofStatus",
            ],
            "runnerFailureClaims": {"rollback": None, "recoveryRequired": None},
            "nativeJournalRejectedOperationPresent": False,
            "scopeFailureEvidence": evidence,
            "sourceBindingProvenance": binding,
            "sourceBindingProvenanceSha256": hashlib.sha256(
                binder._canonical_json(binding).encode("utf-8")).hexdigest(),
        }
        binder._validate_scope_continuation_provenance(provenance)
        changed = copy.deepcopy(provenance)
        changed["runnerFailureClaims"]["rollback"] = False
        with self.assertRaises(binder.BinderError):
            binder._validate_scope_continuation_provenance(changed)

    def test_scope_failure_text_and_marker_must_match_exactly(self):
        receipt = json.loads(RECEIPT.read_text())
        event_lines = [json.loads(line) for line in EVENTS.read_text().splitlines()]
        failure = json.loads(receipt["failure"]["error"])
        failure["result"]["structuredContent"]["blockedByScope"] = False
        receipt["failure"]["error"] = json.dumps(failure)
        for event in event_lines:
            if event.get("event") == "invoke_failed":
                event_failure = json.loads(event["error"])
                event_failure["result"]["structuredContent"]["blockedByScope"] = False
                event["error"] = json.dumps(event_failure)
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            receipt_path = directory / "receipt.json"
            event_path = directory / "events.ndjson"
            receipt_path.write_text(json.dumps(receipt))
            event_path.write_text("\n".join(json.dumps(item) for item in event_lines) + "\n")
            with self.assertRaises(binder.BinderError):
                self._validate(receipt=receipt_path, events=event_path)

    def test_native_review_journal_must_not_gain_a_failed_operation_row(self):
        review = json.loads(REVIEW.read_text())
        structured = review["result"]["structuredContent"]
        structured["steps"].append({
            "id": 236,
            "status": "failed",
            "resourceId": "timeline:3958ec69-3421-4831-9167-fd0c244272f4",
            "command": binder.SCOPE_REJECTION_COMMAND,
            "arguments": {"filePath": "forged"},
            "outcome": {"ok": False},
        })
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "review.json"
            path.write_text(json.dumps(review))
            with self.assertRaises(binder.BinderError):
                self._validate(review=path)

    def test_corrected_future_parent_cannot_change_failed_or_completed_intent(self):
        corrected = json.loads(CORRECTED.read_text())
        corrected["steps"][1637]["values"]["durationSeconds"] = 8
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "corrected.json"
            path.write_text(json.dumps(corrected))
            with self.assertRaises(binder.BinderError):
                self._validate(corrected=path)

    def test_checkpoint_ready_refresh_is_limited_to_79_verified_source_hashes(self):
        checkpoint = json.loads(
            (EVIDENCE / "portrait-v8-checkpoint-1-document-result.json").read_text()
        )["result"]["structuredContent"]
        ready = json.loads(
            (EVIDENCE / "portrait-v8-checkpoint-1-ready-document-2-result.json").read_text()
        )["result"]["structuredContent"]
        plan = json.loads(PARENT.read_text())
        binder._verify_derived_integrity_refresh(
            checkpoint, ready, plan["assetPaths"], (79, 79), allow_ready_checkpoint=True)
        wrong_ready = copy.deepcopy(ready)
        wrong_ready["clips"][0]["opacity"] = 0.25
        with self.assertRaises(binder.BinderError):
            binder._verify_derived_integrity_refresh(
                checkpoint, wrong_ready, plan["assetPaths"], (79, 79),
                allow_ready_checkpoint=True)
        with self.assertRaises(binder.BinderError):
            binder._verify_derived_integrity_refresh(
                checkpoint, ready, plan["assetPaths"], (78, 79),
                allow_ready_checkpoint=True)

    def test_native_save_review_release_checkpoint_matches_the_scope_prefix(self):
        proof = self._validate()
        plan = json.loads(PARENT.read_text())
        checkpoint = json.loads(
            (EVIDENCE / "portrait-v8-checkpoint-1-document-result.json").read_text()
        )["result"]["structuredContent"]
        ready = json.loads(
            (EVIDENCE / "portrait-v8-checkpoint-1-ready-document-2-result.json").read_text()
        )["result"]["structuredContent"]
        stop = json.loads(REVIEW.read_text())["result"]["structuredContent"]
        final_review = json.loads(
            (EVIDENCE / "portrait-v8-checkpoint-1-reviewed-task-result.json").read_text()
        )["result"]["structuredContent"]
        release = json.loads(
            (EVIDENCE / "portrait-v8-checkpoint-1-release-result.json").read_text()
        )["result"]["structuredContent"]
        save = json.loads(
            (EVIDENCE / "portrait-v8-checkpoint-1-save-result.json").read_text()
        )["result"]["structuredContent"]
        plan_hash = hashlib.sha256(PARENT.read_bytes()).hexdigest()
        verified = binder._verify_source_checkpoint(
            plan, proof["prefix"], stop, final_review, release, ready, save,
            plan["taskId"], plan["resourceId"], plan_hash,
            journal_history=proof["journalHistory"], checkpoint_document=checkpoint,
            integrity_change_counts=(79, 79), allow_ready_checkpoint=True)
        self.assertEqual(verified["sourceFileSha256"],
                         "5c5ac69035e789c4a9d5917594d718fcf035bcb619cbdd080f0c231c5364f564")
        self.assertEqual(len(verified["sourceGraphClipIds"]), 502)
        self.assertEqual(len(verified["sourceGraphTrackIds"]), 413)

    def test_scope_error_does_not_authorize_other_commands_or_steps(self):
        plan = json.loads(PARENT.read_text())
        plan["steps"][1637]["id"] = "different-failure"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "plan.json"
            path.write_text(json.dumps(plan))
            with self.assertRaises(binder.BinderError):
                self._validate(parent=path)


if __name__ == "__main__":
    unittest.main()
