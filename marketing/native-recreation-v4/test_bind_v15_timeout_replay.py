"""Focused offline checks for exact v15 timeout-replay suffix binding."""

from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import bind_v15_timeout_replay as binder
import author_native


class TimeoutReplayContinuationTest(unittest.TestCase):
    def _cached_batch_fixture(self):
        operations = [
            {"id": "op000", "commandId": "timeline.action.setTimelineClipFrame",
             "values": {"clipId": binder.EXPECTED_CREATED_CLIP_ID, "width": 0.55}},
            {"id": "op001", "commandId": "timeline.action.createTimelineTrack",
             "values": {"name": "waveform"}},
        ]
        rows = [
            {"id": "op000", "commandId": operations[0]["commandId"],
             "ok": True, "alreadySatisfied": False},
            {"id": "op001", "commandId": operations[1]["commandId"],
             "ok": True, "trackId": binder.EXPECTED_CREATED_TRACK_ID},
        ]
        native_row = {
            "command": author_native.BATCH_COMMAND_ID,
            "resourceId": binder.RESOURCE_ID,
            "status": "completed",
            "arguments": {
                "batchId": binder.TIMEOUT_BATCH_ID,
                "_batchId": binder.TIMEOUT_BATCH_ID,
                "_batchRequestSha256": binder.TIMEOUT_REQUEST_SHA256,
                "operations": operations,
            },
            "outcome": {
                "batchId": binder.TIMEOUT_BATCH_ID,
                "batchRequestSha256": binder.TIMEOUT_REQUEST_SHA256,
                "ok": True,
                "changed": True,
                "operationCount": 2,
                "batchResults": rows,
            },
        }
        replay = {
            "ok": True,
            "idempotentReplay": True,
            "commandId": author_native.BATCH_COMMAND_ID,
            "batchId": binder.TIMEOUT_BATCH_ID,
            "batchRequestSha256": binder.TIMEOUT_REQUEST_SHA256,
            "resourceId": binder.RESOURCE_ID,
            "taskStateHash": "a" * 64,
            "batchResults": rows,
        }
        return {"operations": operations}, native_row, replay

    def _autosave_fixture(self):
        baseline_clip = {"clipId": "baseline-clip"}
        created_clip = {
            "clipId": binder.EXPECTED_CREATED_CLIP_ID,
            "frameWidth": 0.55,
            "frameHeight": 0.0651851851851852,
            "frameOffsetX": 0.5,
            "frameOffsetY": 1.304444444444445,
            "frameScale": 1.0,
            "frameMode": "stretch",
            "frameCornerRadius": 0.0,
            "opacity": 1.0,
        }
        prefix_clip = {"clipId": "prefix-created-clip"}
        canonical = {
            "timelineId": binder.RESOURCE_ID.removeprefix("timeline:"),
            "tracks": [{"trackId": "video-1", "clips": [baseline_clip]}],
        }
        autosave = {
            "timelineId": binder.RESOURCE_ID.removeprefix("timeline:"),
            "sequenceFormat": {"width": 1920, "height": 1080, "frameRate": 30},
            "tracks": [
                {"trackId": "video-1", "clips": [baseline_clip]},
                {"trackId": "graphics-prefix", "clips": [prefix_clip]},
                {"trackId": binder.EXPECTED_CREATED_TRACK_ID,
                 "customName": "audio mixer waveform 1 / bars 113-128",
                 "role": "graphics", "clips": [created_clip]},
            ],
        }
        initial = {
            "clips": [baseline_clip],
            "tracks": [{"trackId": "video-1"}],
        }
        current = {
            "clips": [baseline_clip, prefix_clip, created_clip],
            "tracks": [
                {"trackId": "video-1"},
                {"trackId": "graphics-prefix"},
                {"trackId": binder.EXPECTED_CREATED_TRACK_ID},
            ],
        }
        prefix_results = {
            "prefix-clip-step": {"createdClipId": "prefix-created-clip"},
            "prefix-track-step": {"trackId": "graphics-prefix"},
        }
        native_rows = [{"id": "op001", "trackId": binder.EXPECTED_CREATED_TRACK_ID}]
        plan = {"composition": {"width": 1920, "height": 1080, "fps": 30}}
        return canonical, autosave, initial, current, plan, prefix_results, native_rows

    def test_native_replay_requires_exact_completed_journal_body(self):
        expected, native_row, replay = self._cached_batch_fixture()
        rows = binder._validate_cached_native_batch(
            expected, native_row, replay, "a" * 64)
        self.assertEqual(len(rows), 2)

        wrong_replay = dict(replay, idempotentReplay=False)
        with self.assertRaisesRegex(binder.TimeoutRecoveryError, "cached native receipt"):
            binder._validate_cached_native_batch(
                expected, native_row, wrong_replay, "a" * 64)

        changed_body = dict(native_row)
        changed_body["arguments"] = dict(native_row["arguments"])
        changed_body["arguments"]["operations"] = [dict(item) for item in expected["operations"]]
        changed_body["arguments"]["operations"][0] = dict(
            changed_body["arguments"]["operations"][0])
        changed_body["arguments"]["operations"][0]["values"] = {"width": 0.54}
        with self.assertRaisesRegex(binder.TimeoutRecoveryError, "exact successful timeout batch"):
            binder._validate_cached_native_batch(
                expected, changed_body, replay, "a" * 64)

    def test_autosave_drift_is_rejected_even_when_ids_are_present(self):
        fixtures = self._autosave_fixture()
        state = binder._validate_autosave_checkpoint(*fixtures)
        self.assertEqual(state["autosaveCounts"], {"clips": 3, "tracks": 3})
        self.assertEqual(len(state["createdClipIds"]), 1)
        self.assertEqual(len(state["createdTrackIds"]), 2)

        canonical, autosave, initial, current, plan, prefix, rows = self._autosave_fixture()
        autosave["tracks"][2]["clips"][0]["frameOffsetY"] = 1.2
        with self.assertRaisesRegex(binder.TimeoutRecoveryError, "recovered frame geometry"):
            binder._validate_autosave_checkpoint(
                canonical, autosave, initial, current, plan, prefix, rows)

    def test_recovered_prefix_reference_becomes_literal_and_suffix_ref_stays(self):
        completed = {"prior-track": {"trackId": "graphics-recovered"}}
        suffix_positions = {"suffix-track": 0, "suffix-clip": 1}
        full_positions = {"prior-track": 4, "suffix-track": 5, "suffix-clip": 6}
        replacements = []
        values = {
            "trackId": {"result": "prior-track", "field": "trackId"},
            "later": {"result": "suffix-track", "field": "trackId"},
        }
        rewritten = binder._resolve_prior_refs(
            values, completed, suffix_positions, 1, full_positions,
            replacements, "steps/suffix-clip/values")
        self.assertEqual(rewritten["trackId"], "graphics-recovered")
        self.assertEqual(rewritten["later"],
                         {"result": "suffix-track", "field": "trackId"})
        self.assertEqual(len(replacements), 1)
        self.assertEqual(replacements[0]["sourceStepId"], "prior-track")

    def test_unproven_prior_reference_is_rejected(self):
        with self.assertRaisesRegex(binder.TimeoutRecoveryError, "was not proven"):
            binder._resolve_prior_refs(
                {"trackId": {"result": "missing-prefix", "field": "trackId"}},
                {}, {}, 0, {"missing-prefix": 3}, [], "step/values")

    def test_unknown_or_future_suffix_reference_is_rejected(self):
        with self.assertRaisesRegex(binder.TimeoutRecoveryError, "unknown or future"):
            binder._resolve_prior_refs(
                {"trackId": {"result": "future", "field": "trackId"}},
                {}, {"future": 2}, 1, {"future": 1000}, [], "step/values")

    def test_missing_cached_result_field_is_rejected(self):
        with self.assertRaisesRegex(binder.TimeoutRecoveryError, "has no field"):
            binder._resolve_prior_refs(
                {"trackId": {"result": "prior", "field": "trackId"}},
                {"prior": {"createdClipId": "clip"}}, {}, 0,
                {"prior": 0}, [], "step/values")

    def test_plan_writer_is_idempotent_but_never_overwrites_different_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "suffix.json"
            plan = {"steps": [{"id": "remaining", "commandId": "typed", "values": {}}]}
            first = binder.write_new_plan(plan, output)
            self.assertEqual(first.read_bytes(), binder.serialize_plan(plan))
            self.assertEqual(binder.write_new_plan(plan, output), first)
            with self.assertRaisesRegex(binder.TimeoutRecoveryError, "refusing overwrite"):
                binder.write_new_plan({"different": True}, output)


if __name__ == "__main__":
    unittest.main()
