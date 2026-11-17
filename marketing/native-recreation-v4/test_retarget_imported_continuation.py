"""Offline safety tests for imported-project continuation retargeting."""

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import retarget_imported_continuation as retarget


def _envelope(value):
    return {"jsonrpc": "2.0", "result": {
        "isError": False, "structuredContent": value}}


class ImportedContinuationRetargetTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name).resolve()
        self.output_directory = self.root / "projects"
        self.output_directory.mkdir()
        self.input_directory = self.root / "inputs"
        self.input_directory.mkdir()
        self.receipt_directory = self.root / "receipts"
        self.receipt_directory.mkdir()

        self.old_timeline = "00000000-0000-4000-8000-000000000001"
        self.new_timeline = "00000000-0000-4000-8000-000000000011"
        self.old_resource_id = "timeline:" + self.old_timeline
        self.new_resource_id = "timeline:" + self.new_timeline
        self.old_task_id = "00000000-0000-4000-8000-000000000002"
        self.new_task_id = "00000000-0000-4000-8000-000000000012"
        self.document_hash = "a" * 64
        self.source_graph_hash = "b" * 64
        self.source_sha = retarget.DEFAULT_SOURCE_SHA256
        self.track_ids = {"track-0001", "track-0002"}
        self.clip_ids = {"clip-0001", "clip-0002"}
        self.asset_paths = [
            "/canonical/assets/photo-a.jpg",
            "/canonical/assets/photo-b.jpg",
        ]

        old_output = self.output_directory / "looksy-native-full-landscape-v6.json"
        self.new_output = self.output_directory / "looksy-native-full-landscape-v7.json"
        self.plan = {
            "reference": "landscape.mp4",
            "durationSeconds": 298,
            "composition": {"width": 1920, "height": 1080, "fps": 30},
            "proofStatus": "Offline assembled only; native execution is unverified.",
            "assetPaths": list(self.asset_paths),
            "outputDirectory": str(self.output_directory),
            "outputPath": str(old_output),
            "resourceId": self.old_resource_id,
            "taskId": self.old_task_id,
            "startingDocumentHash": "c" * 64,
            "continuationProvenance": {
                "schema": "looksy.native-authoring-continuation/v1",
                "resourceId": self.old_resource_id,
                "taskId": self.old_task_id,
                "startingDocumentHash": "c" * 64,
                "prefixCount": 1690,
            },
            "steps": [
                {"id": "remaining/create", "commandId": "timeline.action.createTimelineGraphic",
                 "values": {"trackId": "track-0001", "label": "Generated graphic"}},
                {"id": "remaining/frame", "commandId": "timeline.action.setTimelineClipFrame",
                 "values": {"clipId": {"result": "remaining/create", "field": "createdClipId"},
                            "trackId": "track-0002", "offsetX": 0.125}},
                {"id": "delivery/save-editable-project", "commandId": retarget.SAVE_COMMAND_ID,
                 "values": {"outputPath": str(old_output)}},
            ],
        }

        self.copy_path = self.input_directory / (self.new_timeline + ".json")
        copy_tracks = [
            {"trackId": "track-0001", "clips": [{"clipId": "clip-0001"}]},
            {"trackId": "track-0002", "clips": [{"clipId": "clip-0002"}]},
        ]
        self.copy_path.write_text(json.dumps({
            "timelineId": self.new_timeline,
            "name": "Imported Copy",
            "tracks": copy_tracks,
        }))
        self.source_document = {
            "ok": True,
            "resourceId": self.old_resource_id,
            "documentHash": "d" * 64,
            "width": 1920,
            "height": 1080,
            "frameRate": 30,
            "clips": [{"clipId": value} for value in sorted(self.clip_ids)],
            "tracks": [{"trackId": value} for value in sorted(self.track_ids)],
        }
        self.import_receipt = {
            "ok": True,
            "operation": "imported_copy",
            "sourceSha256": self.source_sha,
            "sourceGraphSha256": self.source_graph_hash,
            "copyGraphSha256": self.source_graph_hash,
            "graphMatches": True,
            "readbackVerified": True,
            "libraryRegistered": True,
            "resourceId": self.new_resource_id,
            "timelineId": self.new_timeline,
            "savedDocumentPath": str(self.copy_path),
            "clipIds": sorted(self.clip_ids),
            "clipCount": len(self.clip_ids),
            "trackCount": len(self.track_ids),
            "resource": {
                "ok": True,
                "ready": True,
                "resourceId": self.new_resource_id,
                "savedDocumentPath": str(self.copy_path),
            },
        }
        self.document_proof = {
            "ok": True,
            "ready": True,
            "resourceId": self.new_resource_id,
            "savedDocumentPath": str(self.copy_path),
            "documentHash": self.document_hash,
            "width": 1920,
            "height": 1080,
            "frameRate": 30,
            "clips": [{"clipId": value} for value in sorted(self.clip_ids)],
            "tracks": [{"trackId": value} for value in sorted(self.track_ids)],
        }
        self.admission = {
            "ok": True,
            "active": True,
            "status": "running",
            "taskId": self.new_task_id,
            "targetResourceId": self.new_resource_id,
            "targetResourceIds": [self.new_resource_id],
            "nativeTimelineScope": {
                "targetIds": [self.new_resource_id],
                "assetPaths": list(self.asset_paths),
                "outputDirectory": str(self.output_directory),
            },
            "resources": [{
                "id": self.new_resource_id,
                "beforeHash": self.document_hash,
                "currentHash": self.document_hash,
                "pending": False,
                "reverted": False,
            }],
        }

    def tearDown(self):
        self.temporary.cleanup()

    def _write_evidence(self, *, plan=None, imported=None, admission=None, document=None,
                        source_document=None):
        inputs = {
            "plan.json": plan if plan is not None else self.plan,
            "import.json": _envelope(imported if imported is not None else self.import_receipt),
            "admission.json": _envelope(admission if admission is not None else self.admission),
            "document.json": _envelope(document if document is not None else self.document_proof),
            "source.json": _envelope(source_document if source_document is not None else self.source_document),
        }
        paths = {}
        for name, value in inputs.items():
            path = self.receipt_directory / name
            path.write_text(json.dumps(value, indent=2))
            paths[name] = path
        return paths

    def _retarget(self, *, plan=None, imported=None, admission=None, document=None,
                  source_document=None, output=None):
        paths = self._write_evidence(
            plan=plan, imported=imported, admission=admission, document=document,
            source_document=source_document)
        return retarget.retarget_continuation_plan(
            paths["plan.json"], paths["import.json"], paths["admission.json"],
            paths["document.json"], paths["source.json"],
            output or self.new_output,
        )

    def test_verified_import_retargets_only_identity_hash_and_final_save_path(self):
        original_steps = copy.deepcopy(self.plan["steps"])
        result = self._retarget()
        self.assertEqual(result["resourceId"], self.new_resource_id)
        self.assertEqual(result["taskId"], self.new_task_id)
        self.assertEqual(result["startingDocumentHash"], self.document_hash)
        self.assertEqual(result["outputPath"], str(self.new_output))
        self.assertEqual(result["steps"][:-1], original_steps[:-1])
        self.assertEqual(
            {**result["steps"][-1], "values": original_steps[-1]["values"]},
            original_steps[-1],
        )
        self.assertEqual(result["steps"][-1]["values"]["outputPath"], str(self.new_output))
        provenance = result["continuationProvenance"]
        self.assertEqual(provenance["schema"], retarget.PROVENANCE_SCHEMA)
        self.assertEqual(provenance["clipCount"], 2)
        self.assertEqual(provenance["trackCount"], 2)
        self.assertEqual(provenance["sourceProjectSha256"], self.source_sha)
        self.assertEqual(self.plan["steps"], original_steps)

    def test_pending_copy_is_accepted_only_after_same_path_ready_checkpoint(self):
        pending_import = copy.deepcopy(self.import_receipt)
        pending_import["resource"]["ready"] = False
        result = self._retarget(imported=pending_import)
        self.assertFalse(result["continuationProvenance"]["copyReadyAtImport"])
        self.assertEqual(result["startingDocumentHash"], self.document_hash)

        not_ready_document = copy.deepcopy(self.document_proof)
        not_ready_document["ready"] = False
        with self.assertRaises(retarget.RetargetError):
            self._retarget(imported=pending_import, document=not_ready_document)

        mismatched_path_document = copy.deepcopy(self.document_proof)
        mismatched_path_document["savedDocumentPath"] = str(
            self.input_directory / "another-project.json")
        with self.assertRaises(retarget.RetargetError):
            self._retarget(imported=pending_import, document=mismatched_path_document)

        no_checkpoint = copy.deepcopy(self.admission)
        no_checkpoint["resources"][0]["currentHash"] = "e" * 64
        with self.assertRaises(retarget.RetargetError):
            self._retarget(imported=pending_import, admission=no_checkpoint)

    def test_wrong_source_hash_or_graph_mismatch_is_refused(self):
        wrong_source = copy.deepcopy(self.import_receipt)
        wrong_source["sourceSha256"] = "f" * 64
        with self.assertRaises(retarget.RetargetError):
            self._retarget(imported=wrong_source)
        wrong_graph = copy.deepcopy(self.import_receipt)
        wrong_graph["copyGraphSha256"] = "e" * 64
        with self.assertRaises(retarget.RetargetError):
            self._retarget(imported=wrong_graph)

    def test_copy_and_inspect_must_retain_exact_parent_clip_track_ids(self):
        wrong_import = copy.deepcopy(self.import_receipt)
        wrong_import["clipIds"] = ["clip-0001", "clip-missing"]
        with self.assertRaises(retarget.RetargetError):
            self._retarget(imported=wrong_import)
        wrong_document = copy.deepcopy(self.document_proof)
        wrong_document["tracks"][0]["trackId"] = "track-missing"
        with self.assertRaises(retarget.RetargetError):
            self._retarget(document=wrong_document)

    def test_admission_requires_same_target_assets_output_and_checkpoint_hash(self):
        mutations = []
        wrong_target = copy.deepcopy(self.admission)
        wrong_target["nativeTimelineScope"]["targetIds"] = [self.old_resource_id]
        mutations.append(wrong_target)
        wrong_assets = copy.deepcopy(self.admission)
        wrong_assets["nativeTimelineScope"]["assetPaths"].reverse()
        mutations.append(wrong_assets)
        wrong_output = copy.deepcopy(self.admission)
        wrong_output["nativeTimelineScope"]["outputDirectory"] = str(self.input_directory)
        mutations.append(wrong_output)
        wrong_checkpoint = copy.deepcopy(self.admission)
        wrong_checkpoint["resources"][0]["currentHash"] = "e" * 64
        mutations.append(wrong_checkpoint)
        for mutation in mutations:
            with self.subTest(admission=mutation):
                with self.assertRaises(retarget.RetargetError):
                    self._retarget(admission=mutation)

    def test_unknown_literal_ids_and_dangling_completed_prefix_refs_are_refused(self):
        wrong_literal = copy.deepcopy(self.plan)
        wrong_literal["steps"][1]["values"]["trackId"] = "track-absent"
        with self.assertRaises(retarget.RetargetError):
            self._retarget(plan=wrong_literal)
        dangling = copy.deepcopy(self.plan)
        dangling["steps"][1]["values"]["clipId"] = {
            "result": "completed-prefix/created-clip", "field": "createdClipId"}
        with self.assertRaises(retarget.RetargetError):
            self._retarget(plan=dangling)
        unknown_list = copy.deepcopy(self.plan)
        unknown_list["steps"][0]["values"]["clipIds"] = ["clip-absent"]
        with self.assertRaises(retarget.RetargetError):
            self._retarget(plan=unknown_list)

    def test_existing_or_noncanonical_output_is_refused(self):
        self.new_output.write_text("keep")
        with self.assertRaises(retarget.RetargetError):
            self._retarget()
        self.new_output.unlink()
        noncanonical = str(self.output_directory / ".." / "projects" / "other.json")
        with self.assertRaises(retarget.RetargetError):
            self._retarget(output=noncanonical)

    def test_new_plan_publication_is_atomic_no_clobber(self):
        result = self._retarget()
        output = self.output_directory / "retargeted-plan.json"
        retarget.write_new_plan(result, output, input_paths=(
            self.receipt_directory / "plan.json",
        ))
        first_bytes = output.read_bytes()
        self.assertEqual(json.loads(first_bytes)["resourceId"], self.new_resource_id)
        with self.assertRaises(retarget.RetargetError):
            retarget.write_new_plan(result, output)
        self.assertEqual(output.read_bytes(), first_bytes)

    def test_cli_defaults_never_write_plan_over_native_timeline_output(self):
        defaults = retarget._default_paths()
        self.assertNotEqual(defaults["plan_output_path"], defaults["native_output_path"])
        self.assertEqual(defaults["native_output_path"],
                         HERE / "projects" / "looksy-native-full-landscape-v7.json")
        self.assertEqual(defaults["plan_output_path"],
                         HERE / "full-landscape-v7-continuation.commands.json")

    def test_real_v6_continuation_ids_and_references_match_parent_proof(self):
        plan = json.loads((HERE / "full-landscape-v6-continuation.commands.json").read_text())
        source_envelope = json.loads(
            (HERE / "evidence" / "native-full-blocked-document.json").read_text())
        source = source_envelope["result"]["structuredContent"]
        tracks, clips = retarget._inspect_ids(source, "Parent native document proof")
        steps, positions = retarget._validate_plan_steps(plan)
        retarget._validate_refs_and_ids(steps, positions, clips, tracks)
        self.assertEqual(len(steps), 1934)
        self.assertEqual(len(clips), 518)
        self.assertEqual(len(tracks), 429)

    def test_full_real_plan_retargets_against_synthetic_518_clip_import_receipts(self):
        source_envelope = json.loads(
            (HERE / "evidence" / "native-full-blocked-document.json").read_text())
        source = source_envelope["result"]["structuredContent"]
        real_plan = json.loads((HERE / "full-landscape-v6-continuation.commands.json").read_text())
        track_ids, clip_ids = retarget._inspect_ids(source, "Parent native document proof")

        copy_tracks = []
        for track in source["tracks"]:
            copy_tracks.append({
                "trackId": track["trackId"],
                "clips": [{"clipId": clip["clipId"]} for clip in track["clips"]],
            })
        copy_path = self.input_directory / (self.new_timeline + ".json")
        copy_path.write_text(json.dumps({"timelineId": self.new_timeline,
                                         "tracks": copy_tracks}))
        self.assertEqual(retarget._project_ids(
            json.loads(copy_path.read_text()), "Synthetic imported project"),
            (track_ids, clip_ids))

        plan = copy.deepcopy(real_plan)
        plan["outputDirectory"] = str(self.output_directory)
        plan["outputPath"] = str(self.output_directory / "landscape-v6-output.json")
        plan["steps"][-1]["values"]["outputPath"] = plan["outputPath"]
        document_hash = "d" * 64
        document = {
            "ok": True,
            "ready": True,
            "resourceId": self.new_resource_id,
            "savedDocumentPath": str(copy_path),
            "documentHash": document_hash,
            "width": plan["composition"]["width"],
            "height": plan["composition"]["height"],
            "frameRate": plan["composition"]["fps"],
            "clips": source["clips"],
            "tracks": source["tracks"],
        }
        imported = {
            "ok": True,
            "sourceSha256": retarget.DEFAULT_SOURCE_SHA256,
            "sourceGraphSha256": "e" * 64,
            "copyGraphSha256": "e" * 64,
            "graphMatches": True,
            "readbackVerified": True,
            "libraryRegistered": True,
            "resourceId": self.new_resource_id,
            "timelineId": self.new_timeline,
            "savedDocumentPath": str(copy_path),
            "clipIds": sorted(clip_ids),
            "clipCount": len(clip_ids),
            "trackCount": len(track_ids),
            "resource": {"ok": True, "ready": True, "resourceId": self.new_resource_id,
                         "savedDocumentPath": str(copy_path)},
        }
        admission = {
            "ok": True,
            "active": True,
            "status": "running",
            "taskId": self.new_task_id,
            "targetResourceId": self.new_resource_id,
            "targetResourceIds": [self.new_resource_id],
            "nativeTimelineScope": {
                "targetIds": [self.new_resource_id],
                "assetPaths": plan["assetPaths"],
                "outputDirectory": str(self.output_directory),
            },
            "resources": [{"id": self.new_resource_id, "beforeHash": document_hash,
                           "currentHash": document_hash, "pending": False,
                           "reverted": False}],
        }
        source_envelope = {"ok": True, **source}
        result = self._retarget(
            plan=plan, imported=imported, admission=admission, document=document,
            source_document=source_envelope,
            output=self.output_directory / "looksy-native-full-landscape-v7.json")
        self.assertEqual(len(result["steps"]), 1934)
        self.assertEqual(result["continuationProvenance"]["clipCount"], 518)
        self.assertEqual(result["continuationProvenance"]["trackCount"], 429)
        self.assertEqual(result["steps"][-1]["values"]["outputPath"],
                         str(self.output_directory / "looksy-native-full-landscape-v7.json"))


if __name__ == "__main__":
    unittest.main()
