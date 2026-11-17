"""Offline checks for chaining reviewed native continuation provenance."""

import copy
from pathlib import Path
import sys
import unittest

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import bind_reviewed_continuation as binder


class ReviewedContinuationChainTest(unittest.TestCase):
    def _provenance(self, resource_uuid, task_uuid, document_hash):
        digest = "a" * 64
        source_resource = "timeline:00000000-0000-4000-8000-000000000101"
        source_task = "00000000-0000-4000-8000-000000000102"
        return {
            "schema": binder.BINDER_SCHEMA,
            "parentPlanSha256": digest,
            "parentReceiptSha256": digest,
            "parentEventLogSha256": digest,
            "stopReviewProofSha256": digest,
            "finalReviewProofSha256": digest,
            "releasedSourceTaskProofSha256": digest,
            "sourceDocumentProofSha256": digest,
            "sourceSaveProofSha256": digest,
            "sourceProjectSha256": digest,
            "sourceGraphSha256": "b" * 64,
            "copyGraphSha256": "b" * 64,
            "importReceiptSha256": digest,
            "freshAdmissionProofSha256": digest,
            "importedDocumentProofSha256": digest,
            "completedPrefixCount": 12,
            "lastCompletedStepId": "chapter/last-completed",
            "completedPrefixResultSha256": digest,
            "rejectedStepIds": ["chapter/rejected-a", "chapter/rejected-b"],
            "rejectedBatchId": "00000000-0000-4000-8000-000000000103",
            "rejectedGuard": binder.EXPECTED_GUARD_ERROR,
            "runnerClaimedRollback": False,
            "runnerClaimedRecoveryRequired": True,
            "nativePreDispatchRejectionVerified": True,
            "nativeTaskJournalHasNoRejectedBatchStep": True,
            "sourceResourceId": source_resource,
            "sourceTaskId": source_task,
            "sourceDocumentHash": "c" * 64,
            "sourceSavedDocumentPath": "/archive/source-project.json",
            "sourceFileSha256": digest,
            "sourceTrackIdSetSha256": digest,
            "sourceClipIdSetSha256": digest,
            "resourceId": "timeline:" + resource_uuid,
            "taskId": task_uuid,
            "startingDocumentHash": document_hash,
            "journalCheckpointHash": document_hash,
            "outputDirectory": "/workspace/projects",
            "outputPath": "/workspace/projects/native-copy.json",
            "clipCount": 547,
            "trackCount": 429,
            "retargetedFields": [
                "steps[completed-prefix:]", "resourceId", "taskId",
                "startingDocumentHash", "outputPath",
                "steps[delivery/save-editable-project].values.outputPath",
                "continuationProvenance", "proofStatus",
            ],
        }

    def _plan(self, provenance):
        return {
            "resourceId": provenance["resourceId"],
            "taskId": provenance["taskId"],
            "startingDocumentHash": provenance["startingDocumentHash"],
            "continuationProvenance": provenance,
        }

    def test_repeated_binder_upgrades_preserve_and_validate_the_full_chain(self):
        first_parent = self._provenance(
            "00000000-0000-4000-8000-000000000201",
            "00000000-0000-4000-8000-000000000202",
            "d" * 64)
        binder._validate_parent_identity(self._plan(first_parent))

        second_generation = self._provenance(
            "00000000-0000-4000-8000-000000000301",
            "00000000-0000-4000-8000-000000000302",
            "e" * 64)
        second_parent = binder._chain_parent_provenance(
            first_parent, second_generation)
        binder._validate_parent_identity(self._plan(second_parent))
        self.assertEqual(second_parent["priorContinuationProvenance"], first_parent)

        third_generation = self._provenance(
            "00000000-0000-4000-8000-000000000401",
            "00000000-0000-4000-8000-000000000402",
            "f" * 64)
        third_parent = binder._chain_parent_provenance(
            second_parent, third_generation)
        binder._validate_parent_identity(self._plan(third_parent))
        self.assertEqual(third_parent["priorContinuationProvenance"], second_parent)
        self.assertEqual(
            third_parent["priorContinuationProvenance"]["priorContinuationProvenance"],
            first_parent)

    def test_chain_rejects_tampered_payload_hash_or_graph_claims(self):
        original = self._provenance(
            "00000000-0000-4000-8000-000000000201",
            "00000000-0000-4000-8000-000000000202",
            "d" * 64)
        generated = self._provenance(
            "00000000-0000-4000-8000-000000000301",
            "00000000-0000-4000-8000-000000000302",
            "e" * 64)
        chained = binder._chain_parent_provenance(original, generated)

        wrong_hash = copy.deepcopy(chained)
        wrong_hash["priorContinuationProvenanceSha256"] = "0" * 64
        with self.assertRaises(binder.BinderError):
            binder._validate_parent_identity(self._plan(wrong_hash))

        changed_payload = copy.deepcopy(chained)
        changed_payload["priorContinuationProvenance"]["sourceProjectSha256"] = "0" * 64
        with self.assertRaises(binder.BinderError):
            binder._validate_parent_identity(self._plan(changed_payload))

        mismatched_graph = copy.deepcopy(chained)
        mismatched_graph["copyGraphSha256"] = "0" * 64
        with self.assertRaises(binder.BinderError):
            binder._validate_parent_identity(self._plan(mismatched_graph))


if __name__ == "__main__":
    unittest.main()
