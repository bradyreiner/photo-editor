"""Offline regression checks for the v14 typed-schema upgrade binder."""

import copy
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
CURRENT_PLAN = HERE / "full-landscape-v14-crop-continuation.commands.json"
CORRECTED_PLAN = HERE / "full-landscape-v15-radius-corrected-parent.commands.json"
PRIOR_PLAN = HERE / "full-landscape-v12-reviewed-suffix.commands.json"


class V14SchemaUpgradeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.current = json.loads(CURRENT_PLAN.read_text())
        cls.corrected = json.loads(CORRECTED_PLAN.read_text())

    def _validate(self, blocked_task=None, current_receipt=None):
        return binder.validate_v14_schema_blocked_attempt(
            CURRENT_PLAN,
            current_receipt or EVIDENCE / "native-full-landscape-v14-receipt.json",
            EVIDENCE / "native-full-landscape-v14-receipt.json.ndjson",
            PRIOR_PLAN,
            EVIDENCE / "native-full-landscape-v12-receipt.json",
            EVIDENCE / "native-full-landscape-v12-receipt.json.ndjson",
            blocked_task or EVIDENCE / "native-v14-schema-blocked-task-current-result.json",
            CORRECTED_PLAN,
            EVIDENCE / "native-v14-checkpoint-5-reviewed-document-result.json")

    def test_exact_native_receipts_prove_278_prefix_and_both_failures(self):
        proof = self._validate()
        self.assertEqual(proof["priorPrefix"]["completedCount"], 297)
        self.assertEqual(proof["prefix"]["completedCount"], 278)
        self.assertEqual(proof["priorJournalRows"], 44)
        self.assertEqual(proof["currentJournalRows"], 31)
        self.assertEqual(proof["upgradeEvidence"]["journalFailedStepIds"], [44, 76])
        self.assertEqual(
            proof["upgradeEvidence"]["currentFailure"]["batchId"],
            "78f66b80-dbcc-5c87-b1d6-d40bf96d6dc5")
        self.assertTrue(proof["upgradeEvidence"]["priorFailure"]["rolledBack"])
        self.assertTrue(proof["upgradeEvidence"]["currentFailure"]["mutationNotApplied"])

    def test_upgrade_provenance_roundtrips_and_stays_linked_to_prior_receipts(self):
        proof = self._validate()
        digest = "a" * 64
        source_plan = self.current
        output_directory = source_plan["outputDirectory"]
        prior = source_plan["continuationProvenance"]
        provenance = {
            "schema": binder.BINDER_SCHEMA,
            "parentPlanSha256": proof["upgradeEvidence"]["currentPlanSha256"],
            "parentReceiptSha256": proof["upgradeEvidence"]["currentReceiptSha256"],
            "parentEventLogSha256": proof["upgradeEvidence"]["currentEventLogSha256"],
            "stopReviewProofSha256": digest,
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
            "completedPrefixCount": 278,
            "lastCompletedStepId": proof["prefix"]["completedRecords"][-1]["id"],
            "completedPrefixResultSha256": digest,
            "rejectedStepIds": [step["id"] for step in proof["prefix"]["failedSteps"]],
            "rejectedBatchId": proof["prefix"]["failedBatchId"],
            "rejectedGuard": binder.SCHEMA_TYPED_REJECTION,
            "runnerClaimedRollback": False,
            "runnerClaimedRecoveryRequired": False,
            "nativePreDispatchRejectionVerified": True,
            "nativeTaskJournalHasNoRejectedBatchStep": False,
            "sourceResourceId": source_plan["resourceId"],
            "sourceTaskId": source_plan["taskId"],
            "sourceDocumentHash": proof["upgradeEvidence"]["currentFailure"]["taskStateHash"],
            "sourceSavedDocumentPath": str(
                HERE / "projects/looksy-native-landscape-in-progress-checkpoint-5.json"),
            "sourceFileSha256": digest,
            "sourceTrackIdSetSha256": digest,
            "sourceClipIdSetSha256": digest,
            "resourceId": "timeline:00000000-0000-4000-8000-000000000201",
            "taskId": "00000000-0000-4000-8000-000000000202",
            "startingDocumentHash": "c" * 64,
            "journalCheckpointHash": "c" * 64,
            "outputDirectory": output_directory,
            "outputPath": output_directory + "/upgrade-copy.json",
            "clipCount": 855,
            "trackCount": 642,
            "retargetedFields": [
                "steps[completed-prefix:]", "resourceId", "taskId", "startingDocumentHash",
                "outputPath", "steps[delivery/save-editable-project].values.outputPath",
                "continuationProvenance", "proofStatus",
            ],
            "schemaUpgradeEvidence": proof["upgradeEvidence"],
            "priorContinuationProvenance": prior,
            "priorContinuationProvenanceSha256": binder._sha256(
                binder._canonical_json(prior).encode("utf-8")),
        }
        binder._validate_parent_identity({
            "resourceId": provenance["resourceId"],
            "taskId": provenance["taskId"],
            "startingDocumentHash": provenance["startingDocumentHash"],
            "continuationProvenance": provenance,
        })
        tampered = copy.deepcopy(provenance)
        tampered["schemaUpgradeEvidence"]["priorFailure"]["rolledBack"] = False
        with self.assertRaises(binder.BinderError):
            binder._validate_parent_identity({
                "resourceId": tampered["resourceId"],
                "taskId": tampered["taskId"],
                "startingDocumentHash": tampered["startingDocumentHash"],
                "continuationProvenance": tampered,
            })

    def test_corrected_parent_keeps_completed_and_rejected_prefix_exact(self):
        mutated = copy.deepcopy(self.corrected)
        mutated["steps"][0]["values"]["offsetX"] += 0.01
        with self.assertRaises(binder.BinderError):
            binder._validate_radius_repaired_plan(
                self.current, mutated, binder._sha256(CURRENT_PLAN.read_bytes()), CURRENT_PLAN)

        mutated = copy.deepcopy(self.corrected)
        mutated["steps"][278]["values"]["name"] = "changed rejected operation"
        with self.assertRaises(binder.BinderError):
            binder._validate_radius_repaired_plan(
                self.current, mutated, binder._sha256(CURRENT_PLAN.read_bytes()), CURRENT_PLAN)

    def test_native_rollback_and_current_rejection_are_independently_required(self):
        task_envelope = json.loads(
            (EVIDENCE / "native-v14-schema-blocked-task-current-result.json").read_text())
        structured = task_envelope["result"]["structuredContent"]
        structured["steps"][44]["outcome"]["rolledBack"] = False
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tampered-task.json"
            path.write_text(json.dumps(task_envelope))
            with self.assertRaises(binder.BinderError):
                self._validate(blocked_task=path)

        task_envelope = json.loads(
            (EVIDENCE / "native-v14-schema-blocked-task-current-result.json").read_text())
        structured = task_envelope["result"]["structuredContent"]
        structured["steps"][76]["arguments"]["operations"][0]["values"]["name"] = "forged"
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tampered-task.json"
            path.write_text(json.dumps(task_envelope))
            with self.assertRaises(binder.BinderError):
                self._validate(blocked_task=path)

    def test_typed_failure_envelope_must_prove_no_dispatch_at_same_hash(self):
        receipt = json.loads(
            (EVIDENCE / "native-full-landscape-v14-receipt.json").read_text())
        receipt["failure"]["error"] = receipt["failure"]["error"].replace(
            '"mutationNotApplied": true', '"mutationNotApplied": false')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "tampered-receipt.json"
            path.write_text(json.dumps(receipt))
            with self.assertRaises(binder.BinderError):
                self._validate(current_receipt=path)

    def test_ready_source_proof_allows_only_verified_integrity_hydration(self):
        checkpoint = json.loads(
            (EVIDENCE / "native-v14-checkpoint-5-reviewed-document-result.json").read_text()
        )["result"]["structuredContent"]
        ready = json.loads(
            (EVIDENCE / "native-v14-checkpoint-5-ready-document-result.json").read_text()
        )["result"]["structuredContent"]
        binder._verify_derived_integrity_refresh(
            checkpoint, ready, self.current["assetPaths"])
        changed = copy.deepcopy(ready)
        changed["clips"][0]["opacity"] = 0.5
        with self.assertRaises(binder.BinderError):
            binder._verify_derived_integrity_refresh(
                checkpoint, changed, self.current["assetPaths"])

    def test_running_settled_pre_save_checkpoint_precedes_reviewed_save_row(self):
        proof = self._validate()
        plan_hash = binder._sha256(CURRENT_PLAN.read_bytes())
        stop = json.loads(
            (EVIDENCE / "native-v14-schema-blocked-task-current-result.json").read_text()
        )["result"]["structuredContent"]
        final_review = json.loads(
            (EVIDENCE / "native-v14-checkpoint-5-review-result.json").read_text()
        )["result"]["structuredContent"]
        release = json.loads(
            (EVIDENCE / "native-v14-checkpoint-5-release-result.json").read_text()
        )["result"]["structuredContent"]
        checkpoint = json.loads(
            (EVIDENCE / "native-v14-checkpoint-5-reviewed-document-result.json").read_text()
        )["result"]["structuredContent"]
        ready = json.loads(
            (EVIDENCE / "native-v14-checkpoint-5-ready-document-result.json").read_text()
        )["result"]["structuredContent"]
        save = json.loads(
            (EVIDENCE / "native-v14-checkpoint-5-save-result.json").read_text()
        )["result"]["structuredContent"]
        verified = binder._verify_source_checkpoint(
            self.current, proof["prefix"], stop, final_review, release, ready, save,
            self.current["taskId"], self.current["resourceId"], plan_hash,
            journal_history=proof["journalHistory"], checkpoint_document=checkpoint)
        self.assertEqual(len(verified["sourceGraphClipIds"]), 855)
        self.assertEqual(len(verified["sourceGraphTrackIds"]), 642)
        with self.assertRaises(binder.BinderError):
            binder._verify_source_checkpoint(
                self.current, proof["prefix"], final_review, final_review, release,
                ready, save, self.current["taskId"], self.current["resourceId"], plan_hash,
                journal_history=proof["journalHistory"], checkpoint_document=checkpoint)
        changed = copy.deepcopy(ready)
        target = next(item for item in changed["clips"]
                      if item.get("sourceContentSha256"))
        target["sourceContentSha256"] = "0" * 64
        with self.assertRaises(binder.BinderError):
            binder._verify_derived_integrity_refresh(
                checkpoint, changed, self.current["assetPaths"])


if __name__ == "__main__":
    unittest.main()
