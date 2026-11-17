"""Crash-safety tests for the native authoring runner using a fake MCP call."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import author_native  # noqa: E402


class SimulatedProcessCrash(BaseException):
    """Model abrupt process death without letting runner exception handlers log it."""


def write_plan(path, label="first"):
    plan = {
        "steps": [
            {
                "id": "track",
                "commandId": "timeline.action.createTimelineTrack",
                "values": {"label": label},
            }
        ]
    }
    path.write_text(json.dumps(plan, separators=(",", ":")) + "\n")


class FakeMcp:
    def __init__(self, invoke, document=None, task=None):
        self.invoke = invoke
        self.invocations = 0
        self.calls = []
        self.tool_calls = []
        self.document = document or {
            "ok": True, "ready": True, "resourceId": "timeline:test",
            "width": 1920, "height": 1080, "frameRate": 30,
        }
        self.task = task or {
            "ok": True, "active": True, "status": "running", "taskId": "task:test",
            "targetResourceIds": ["timeline:test"],
        }

    def __call__(self, name, arguments):
        self.tool_calls.append((name, arguments))
        if name == "app.command_schema":
            command_id = arguments.get("commandId")
            if command_id == "timeline.method.inspectAgentDocument()":
                return {"result": {"structuredContent": {
                    "available": True, "readOnly": True, "commandId": command_id,
                }}}
            return {"result": {"structuredContent": {"available": True, "readOnly": False}}}
        if name == "app.inspect_command":
            return {"result": {"structuredContent": dict(self.document)}}
        if name == "timeline.inspect_document":
            return {"result": {"structuredContent": dict(self.document)}}
        if name == "timeline.inspect_task":
            return {"result": {"structuredContent": dict(self.task)}}
        if name == "app.invoke_command":
            self.invocations += 1
            self.calls.append(arguments)
            raw = self.invoke(arguments)
            if arguments.get("commandId") != author_native.BATCH_COMMAND_ID:
                return raw
            structured = raw.get("result", {}).get("structuredContent", {})
            if not structured.get("ok") or "batchResults" in structured:
                return raw
            rows = []
            for operation in arguments["values"]["operations"]:
                row = {"id": operation["id"], "commandId": operation["commandId"], "ok": True}
                if operation["commandId"] == "timeline.action.createTimelineTrack":
                    row["trackId"] = structured.get("trackId", "track-" + operation["id"])
                elif operation["commandId"] in {
                    "timeline.action.createTimelineTitle",
                    "timeline.action.createTimelineGraphic",
                    "timeline.action.createTimelineGenerator",
                }:
                    row["createdClipId"] = "clip-" + operation["id"]
                    track_id = operation["values"].get("trackId", "track-" + operation["id"])
                    if isinstance(track_id, dict) and set(track_id) == {"result", "field"}:
                        track_id = next((prior.get(track_id["field"]) for prior in rows
                                         if prior.get("id") == track_id["result"]), track_id)
                    row["trackId"] = track_id
                elif operation["commandId"] == "timeline.action.moveTimelineGraphicsTrack":
                    track_id = operation["values"].get("trackId", "track-" + operation["id"])
                    if isinstance(track_id, dict) and set(track_id) == {"result", "field"}:
                        track_id = next((prior.get(track_id["field"]) for prior in rows
                                         if prior.get("id") == track_id["result"]), track_id)
                    row["trackId"] = track_id
                rows.append(row)
            structured = dict(structured)
            structured["batchResults"] = rows
            structured.setdefault("operationCount", len(rows))
            return {"result": {"structuredContent": structured}}
        raise AssertionError(f"Unexpected MCP tool call: {name}")


class AuthorNativeResumeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.plan = root / "plan.json"
        self.receipt = root / "receipt.json"
        self.log = root / "receipt.ndjson"
        write_plan(self.plan)

    def execute(self):
        author_native.execute(self.plan, self.receipt, self.log)

    def test_lost_response_after_started_intent_is_never_replayed(self):
        mutation_count = 0

        def mutate_then_crash(_arguments):
            nonlocal mutation_count
            mutation_count += 1
            raise SimulatedProcessCrash()

        fake = FakeMcp(mutate_then_crash)
        with patch.object(author_native, "call", fake):
            with self.assertRaises(SimulatedProcessCrash):
                self.execute()
            with self.assertRaisesRegex(RuntimeError, "will not replay it"):
                self.execute()

        self.assertEqual(mutation_count, 1)
        events = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual([event["event"] for event in events if event.get("stepId") == "track"],
                         ["invoke_started"])

    def test_completed_ndjson_recovers_when_json_summary_was_not_written(self):
        fake = FakeMcp(lambda _arguments: {
            "result": {"structuredContent": {"ok": True, "trackId": "track-stable"}}
        })
        with patch.object(author_native, "call", fake):
            with patch.object(author_native, "_append_summary", side_effect=SimulatedProcessCrash()):
                with self.assertRaises(SimulatedProcessCrash):
                    self.execute()
            self.assertFalse(self.receipt.exists())
            self.execute()

        self.assertEqual(fake.invocations, 1)
        summary = json.loads(self.receipt.read_text())
        self.assertEqual(summary["steps"][0]["result"]["trackId"], "track-stable")
        self.assertEqual(summary["status"], "authored-awaiting-native-review")

    def test_changed_plan_digest_is_rejected(self):
        fake = FakeMcp(lambda _arguments: {
            "result": {"structuredContent": {"ok": True, "trackId": "track-stable"}}
        })
        with patch.object(author_native, "call", fake):
            self.execute()
            write_plan(self.plan, label="changed")
            with self.assertRaisesRegex(RuntimeError, "Plan changed"):
                self.execute()
        self.assertEqual(fake.invocations, 1)

    def test_structured_native_error_is_receipted_and_stops_resume(self):
        fake = FakeMcp(lambda _arguments: {
            "result": {"structuredContent": {"ok": False, "error": "task scope denied"}}
        })
        with patch.object(author_native, "call", fake):
            with self.assertRaisesRegex(RuntimeError, "task scope denied"):
                self.execute()
            with self.assertRaisesRegex(RuntimeError, "failed native call"):
                self.execute()

        self.assertEqual(fake.invocations, 1)
        events = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertIn("invoke_failed", [event["event"] for event in events])
        summary = json.loads(self.receipt.read_text())
        self.assertEqual(summary["status"], "incomplete")
        self.assertEqual(summary["failure"]["stepId"], "track")

    def test_batch_maps_local_and_prior_stable_id_references(self):
        plan = {
            "steps": [
                {"id": "track / first", "commandId": "timeline.action.createTimelineTrack",
                 "values": {"kind": "graphics", "name": "Visual"}},
                {"id": "title with spaces", "commandId": "timeline.action.createTimelineTitle",
                 "values": {"trackId": {"result": "track / first", "field": "trackId"},
                            "templateKey": "headline", "startSeconds": 0, "durationSeconds": 2,
                            "values": {"text": "Native title"}}},
                {"id": "frame/title", "commandId": "timeline.action.setTimelineClipFrame",
                 "values": {"clipId": {"result": "title with spaces", "field": "createdClipId"},
                            "offsetX": 0, "offsetY": 0, "scale": 1}},
                {"id": "separate import", "commandId": "timeline.action.createTimelineClip",
                 "values": {"trackId": {"result": "track / first", "field": "trackId"},
                            "filePath": "/admitted/source.png", "startSeconds": 0,
                            "durationSeconds": 2}},
                {"id": "separate save", "commandId": "timeline.action.saveTimeline",
                 "values": {"outputPath": "/task-owned/output.json"}},
            ]
        }
        self.plan.write_text(json.dumps(plan, separators=(",", ":")) + "\n")

        def invoke(arguments):
            if arguments["commandId"] == "timeline.action.createTimelineClip":
                return {"result": {"structuredContent": {"ok": True, "createdClipId": "imported-stable"}}}
            return {"result": {"structuredContent": {"ok": True}}}

        fake = FakeMcp(invoke)
        with patch.object(author_native, "call", fake):
            self.execute()

        batch_calls = [call for call in fake.calls
                       if call["commandId"] == author_native.BATCH_COMMAND_ID]
        import_calls = [call for call in fake.calls
                        if call["commandId"] == "timeline.action.createTimelineClip"]
        save_calls = [call for call in fake.calls
                      if call["commandId"] == "timeline.action.saveTimeline"]
        self.assertEqual(len(batch_calls), 1)
        self.assertEqual(len(import_calls), 1)
        self.assertEqual(len(save_calls), 1)
        operations = batch_calls[0]["values"]["operations"]
        self.assertEqual([operation["id"] for operation in operations], ["op000", "op001", "op002"])
        self.assertEqual(operations[1]["values"]["trackId"],
                         {"result": "op000", "field": "trackId"})
        self.assertEqual(operations[2]["values"]["clipId"],
                         {"result": "op001", "field": "createdClipId"})
        self.assertEqual(import_calls[0]["values"]["trackId"], "track-op000")
        self.assertEqual([operation["commandId"] for operation in operations], [
            "timeline.action.createTimelineTrack",
            "timeline.action.createTimelineTitle",
            "timeline.action.setTimelineClipFrame",
        ])

        receipt = json.loads(self.receipt.read_text())
        completed = {step["id"]: step for step in receipt["steps"]}
        self.assertEqual(completed["title with spaces"]["values"]["trackId"], "track-op000")
        self.assertEqual(completed["frame/title"]["values"]["clipId"], "clip-op001")
        self.assertEqual(completed["separate import"]["result"]["createdClipId"], "imported-stable")

    def test_graphics_reorder_batches_with_stable_created_track_ids(self):
        plan = {"steps": [
            {"id": "grain-track", "commandId": "timeline.action.createTimelineTrack",
             "values": {"kind": "graphics", "name": "Paper grain"}},
            {"id": "footer-track", "commandId": "timeline.action.createTimelineTrack",
             "values": {"kind": "graphics", "name": "Footer"}},
            {"id": "grain-source", "commandId": "timeline.action.createTimelineGenerator",
             "values": {"trackId": {"result": "grain-track", "field": "trackId"},
                        "generatorKind": "fractal-noise", "startSeconds": 0,
                        "durationSeconds": 18,
                        "parameters": {"baseFrequencyX": 0.78,
                                       "baseFrequencyY": 0.78,
                                       "numOctaves": 3, "seed": 19,
                                       "stitchTiles": True, "opacity": 0.12}}},
            {"id": "grain-stack-position",
             "commandId": "timeline.action.moveTimelineGraphicsTrack",
             "values": {"trackId": {"result": "grain-track", "field": "trackId"},
                        "beforeTrackId": {"result": "footer-track", "field": "trackId"}}},
        ]}
        self.plan.write_text(json.dumps(plan, separators=(",", ":")) + "\n")
        fake = FakeMcp(lambda _arguments: {
            "result": {"structuredContent": {"ok": True}}
        })
        with patch.object(author_native, "call", fake):
            self.execute()

        batch = next(call["values"] for call in fake.calls
                     if call["commandId"] == author_native.BATCH_COMMAND_ID)
        self.assertEqual([operation["commandId"] for operation in batch["operations"]], [
            "timeline.action.createTimelineTrack",
            "timeline.action.createTimelineTrack",
            "timeline.action.createTimelineGenerator",
            "timeline.action.moveTimelineGraphicsTrack",
        ])
        self.assertEqual(batch["operations"][2]["values"]["trackId"],
                         {"result": "op000", "field": "trackId"})
        self.assertEqual(batch["operations"][3]["values"]["trackId"],
                         {"result": "op000", "field": "trackId"})
        self.assertEqual(batch["operations"][3]["values"]["beforeTrackId"],
                         {"result": "op001", "field": "trackId"})
        self.assertTrue(any(call["commandId"] == author_native.BATCH_COMMAND_ID
                            for call in fake.calls))
        receipt = json.loads(self.receipt.read_text())
        completed = {step["id"]: step for step in receipt["steps"]}
        self.assertEqual(completed["grain-stack-position"]["result"]["trackId"],
                         "track-op000")

    def test_batch_splits_at_native_16_operation_limit(self):
        plan = {"steps": [
            {"id": f"track-{index}", "commandId": "timeline.action.createTimelineTrack",
             "values": {"kind": "graphics", "name": f"Track {index}"}}
            for index in range(17)
        ]}
        self.plan.write_text(json.dumps(plan, separators=(",", ":")) + "\n")
        fake = FakeMcp(lambda _arguments: {"result": {"structuredContent": {"ok": True}}})
        with patch.object(author_native, "call", fake):
            self.execute()
            first_batch_ids = [call["values"]["batchId"] for call in fake.calls
                               if call["commandId"] == author_native.BATCH_COMMAND_ID]
            self.execute()

        batch_sizes = [len(call["values"]["operations"]) for call in fake.calls
                       if call["commandId"] == author_native.BATCH_COMMAND_ID]
        self.assertEqual(batch_sizes, [16, 1])
        self.assertTrue(all(size <= 16 for size in batch_sizes))
        self.assertEqual(len(set(first_batch_ids)), 2)
        self.assertEqual(fake.invocations, 2)

    def test_failed_rolled_back_batch_fails_closed_for_every_member(self):
        plan = {"steps": [
            {"id": "lane", "commandId": "timeline.action.createTimelineTrack",
             "values": {"kind": "graphics", "name": "Lane"}},
            {"id": "title", "commandId": "timeline.action.createTimelineTitle",
             "values": {"trackId": {"result": "lane", "field": "trackId"},
                        "templateKey": "headline", "startSeconds": 0,
                        "durationSeconds": 2, "values": {"text": "Denied"}}},
        ]}
        self.plan.write_text(json.dumps(plan, separators=(",", ":")) + "\n")
        fake = FakeMcp(lambda _arguments: {"result": {"isError": True, "structuredContent": {
            "ok": False, "rolledBack": True, "error": "child action denied",
            "mutationNotApplied": True,
        }}})
        with patch.object(author_native, "call", fake):
            with self.assertRaisesRegex(RuntimeError, "child action denied"):
                self.execute()
            with self.assertRaisesRegex(RuntimeError, "failed native call"):
                self.execute()

        self.assertEqual(fake.invocations, 1)
        events = [json.loads(line) for line in self.log.read_text().splitlines()]
        starts = [event for event in events if event.get("event") == "invoke_started"]
        failures = [event for event in events if event.get("event") == "invoke_failed"]
        self.assertEqual({event["stepId"] for event in starts}, {"lane", "title"})
        self.assertEqual({event["stepId"] for event in failures}, {"lane", "title"})
        self.assertTrue(all(event["rolledBack"] for event in failures))
        self.assertTrue(all(not event["recoveryRequired"] for event in failures))
        summary = json.loads(self.receipt.read_text())
        self.assertEqual(summary["status"], "incomplete")
        self.assertTrue(summary["failure"]["rolledBack"])

    def test_uncertain_batch_response_is_never_replayed(self):
        plan = {"steps": [
            {"id": "lane", "commandId": "timeline.action.createTimelineTrack",
             "values": {"kind": "graphics", "name": "Lane"}},
            {"id": "graphic", "commandId": "timeline.action.createTimelineGraphic",
             "values": {"trackId": {"result": "lane", "field": "trackId"},
                        "shapeType": "rectangle", "startSeconds": 0,
                        "durationSeconds": 2, "values": {"shapeWidth": 1, "shapeHeight": 1}}},
        ]}
        self.plan.write_text(json.dumps(plan, separators=(",", ":")) + "\n")

        def mutate_then_crash(_arguments):
            raise SimulatedProcessCrash()

        fake = FakeMcp(mutate_then_crash)
        with patch.object(author_native, "call", fake):
            with self.assertRaises(SimulatedProcessCrash):
                self.execute()
            with self.assertRaisesRegex(RuntimeError, "will not replay it"):
                self.execute()

        self.assertEqual(fake.invocations, 1)
        events = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertEqual({event["stepId"] for event in events
                          if event.get("event") == "invoke_started"}, {"lane", "graphic"})

    def test_idempotent_batch_result_resumes_from_receipts_without_replay(self):
        plan = {"steps": [
            {"id": "lane", "commandId": "timeline.action.createTimelineTrack",
             "values": {"kind": "graphics", "name": "Lane"}},
            {"id": "title", "commandId": "timeline.action.createTimelineTitle",
             "values": {"trackId": {"result": "lane", "field": "trackId"},
                        "templateKey": "headline", "startSeconds": 0,
                        "durationSeconds": 2, "values": {"text": "Replay"}}},
        ]}
        self.plan.write_text(json.dumps(plan, separators=(",", ":")) + "\n")

        def idempotent_replay(_arguments):
            return {"result": {"structuredContent": {
                "ok": True, "idempotentReplay": True,
            }}}

        fake = FakeMcp(idempotent_replay)
        with patch.object(author_native, "call", fake):
            with patch.object(author_native, "_append_summary", side_effect=SimulatedProcessCrash()):
                with self.assertRaises(SimulatedProcessCrash):
                    self.execute()
            batch_request = next(call["values"] for call in fake.calls
                                 if call["commandId"] == author_native.BATCH_COMMAND_ID)
            self.execute()

        self.assertEqual(fake.invocations, 1)
        plan_digest = author_native.hashlib.sha256(self.plan.read_bytes()).hexdigest()
        rebuilt_request = author_native._build_batch_request(plan_digest, plan["steps"], {})
        self.assertEqual(rebuilt_request["batchId"], batch_request["batchId"])
        receipt = json.loads(self.receipt.read_text())
        result_by_step = {step["id"]: step["result"] for step in receipt["steps"]}
        self.assertTrue(result_by_step["lane"]["idempotentReplay"])
        self.assertEqual(result_by_step["title"]["createdClipId"], "clip-op001")

    def test_legacy_completed_per_step_receipt_remains_resumable(self):
        plan = {"steps": [
            {"id": "track", "commandId": "timeline.action.createTimelineTrack",
             "values": {"kind": "graphics", "name": "Legacy track"}},
            {"id": "title", "commandId": "timeline.action.createTimelineTitle",
             "values": {"trackId": {"result": "track", "field": "trackId"},
                        "templateKey": "headline", "startSeconds": 0,
                        "durationSeconds": 2, "values": {"text": "Legacy result"}}},
        ]}
        self.plan.write_text(json.dumps(plan, separators=(",", ":")) + "\n")
        digest = author_native.hashlib.sha256(self.plan.read_bytes()).hexdigest()
        events = [
            {"schema": author_native.RECEIPT_SCHEMA, "planSha256": digest,
             "event": "plan_opened", "stepCount": 2},
            {"schema": author_native.RECEIPT_SCHEMA, "planSha256": digest,
             "event": "invoke_started", "stepId": "track",
             "commandId": "timeline.action.createTimelineTrack",
             "values": plan["steps"][0]["values"]},
            {"schema": author_native.RECEIPT_SCHEMA, "planSha256": digest,
             "event": "invoke_completed", "stepId": "track",
             "commandId": "timeline.action.createTimelineTrack",
             "values": plan["steps"][0]["values"],
             "result": {"ok": True, "trackId": "track-from-legacy-call"}},
            {"schema": author_native.RECEIPT_SCHEMA, "planSha256": digest,
             "event": "step_timing", "stepId": "track", "timing": {"rpcMs": 4.0}},
            {"schema": author_native.RECEIPT_SCHEMA, "planSha256": digest,
             "event": "invoke_started", "stepId": "title",
             "commandId": "timeline.action.createTimelineTitle",
             "values": {"trackId": "track-from-legacy-call"}},
            {"schema": author_native.RECEIPT_SCHEMA, "planSha256": digest,
             "event": "invoke_completed", "stepId": "title",
             "commandId": "timeline.action.createTimelineTitle",
             "values": {"trackId": "track-from-legacy-call", "templateKey": "headline",
                         "startSeconds": 0, "durationSeconds": 2,
                         "values": {"text": "Legacy result"}},
             "result": {"ok": True, "createdClipId": "clip-from-legacy-call"}},
            {"schema": author_native.RECEIPT_SCHEMA, "planSha256": digest,
             "event": "step_timing", "stepId": "title", "timing": {"rpcMs": 5.0}},
        ]
        self.log.write_text("".join(json.dumps(event, separators=(",", ":")) + "\n"
                                  for event in events))

        fake = FakeMcp(lambda _arguments: self.fail("completed legacy steps must not be invoked again"))
        with patch.object(author_native, "call", fake):
            self.execute()

        self.assertEqual(fake.invocations, 0)
        summary = json.loads(self.receipt.read_text())
        self.assertEqual(summary["steps"][0]["result"]["trackId"], "track-from-legacy-call")
        self.assertEqual(summary["steps"][1]["result"]["createdClipId"], "clip-from-legacy-call")


class AuthorNativeGeometryPreflightTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.plan = root / "plan.json"
        self.receipt = root / "receipt.json"
        self.log = root / "receipt.ndjson"
        self.write_geometry_plan()

    def write_geometry_plan(self, **extra):
        self.receipt.unlink(missing_ok=True)
        self.log.unlink(missing_ok=True)
        plan = {
            "composition": {"width": 1920, "height": 1080, "fps": 30},
            "steps": [{
                "id": "track",
                "commandId": "timeline.action.createTimelineTrack",
                "values": {"kind": "graphics", "name": "Visual"},
            }],
        }
        plan.update(extra)
        self.plan.write_text(json.dumps(plan, separators=(",", ":")) + "\n")

    def execute(self, fake):
        with patch.object(author_native, "call", fake):
            author_native.execute(self.plan, self.receipt, self.log)

    def assert_no_mutation(self, fake):
        self.assertEqual(fake.invocations, 0)
        if self.log.exists():
            events = [json.loads(line) for line in self.log.read_text().splitlines()]
            self.assertFalse(any(event.get("event") in {"invoke_started", "invoke_completed"}
                                 for event in events))

    def test_compact_inspector_preserves_destination_and_task_guards(self):
        self.write_geometry_plan(resourceId="timeline:test", taskId="task:test",
                                 startingDocumentHash="a" * 64)
        fake = FakeMcp(lambda _arguments: self.fail("preflight only"), document={
            "ok": True, "ready": True, "resourceId": "timeline:test",
            "documentHash": "a" * 64, "width": 1920, "height": 1080,
            "frameRate": 30, "contract": "looksy.native-timeline-inspection/v1",
        })
        with patch.object(author_native, "call", fake):
            author_native._preflight_timeline_destination(
                json.loads(self.plan.read_text()), self.log, "digest",
                compact_inspection=True)
        self.assertEqual([name for name, _ in fake.tool_calls],
                         ["timeline.inspect_document", "timeline.inspect_task"])
        self.assert_no_mutation(fake)

    def test_compact_inspector_rejects_changed_hash_and_unknown_contract(self):
        for changed in ({"documentHash": "b" * 64}, {"contract": "unknown"}):
            with self.subTest(changed=changed):
                self.write_geometry_plan(resourceId="timeline:test", taskId="task:test",
                                         startingDocumentHash="a" * 64)
                document = {"ok": True, "ready": True, "resourceId": "timeline:test",
                            "documentHash": "a" * 64, "width": 1920, "height": 1080,
                            "frameRate": 30,
                            "contract": "looksy.native-timeline-inspection/v1"}
                document.update(changed)
                fake = FakeMcp(lambda _arguments: self.fail("must refuse"), document=document)
                with patch.object(author_native, "call", fake):
                    with self.assertRaises(RuntimeError):
                        author_native._preflight_timeline_destination(
                            json.loads(self.plan.read_text()), self.log, "digest",
                            compact_inspection=True)
                self.assert_no_mutation(fake)

    def test_wrong_orientation_fails_before_any_mutation(self):
        fake = FakeMcp(
            lambda _arguments: {"result": {"structuredContent": {"ok": True}}},
            document={"ok": True, "ready": True, "resourceId": "timeline:test",
                      "width": 1080, "height": 1920, "frameRate": 30},
        )
        with self.assertRaisesRegex(RuntimeError, "Plan width 1920 does not match"):
            self.execute(fake)
        self.assert_no_mutation(fake)
        self.assertTrue(any(name == "app.inspect_command" for name, _ in fake.tool_calls))

    def test_continuation_rejects_changed_starting_document_before_mutation(self):
        self.write_geometry_plan(startingDocumentHash="a" * 64)
        fake = FakeMcp(lambda _arguments: self.fail("stale continuation must not invoke"))
        fake.document["documentHash"] = "b" * 64
        with self.assertRaisesRegex(RuntimeError, "changed after continuation planning"):
            self.execute(fake)
        self.assert_no_mutation(fake)

    def test_continuation_starting_hash_is_not_reused_after_receipted_progress(self):
        plan = json.loads(self.plan.read_text())
        plan["startingDocumentHash"] = "a" * 64
        fake = FakeMcp(lambda _arguments: self.fail("preflight only"))
        fake.document["documentHash"] = "b" * 64
        with patch.object(author_native, "call", fake):
            author_native._preflight_timeline_destination(
                plan, self.log, "plan:test", require_starting_hash=False)
        self.assert_no_mutation(fake)

    def test_unready_timeline_fails_before_any_mutation(self):
        fake = FakeMcp(
            lambda _arguments: {"result": {"structuredContent": {"ok": True}}},
            document={"ok": True, "ready": False, "resourceId": "timeline:test",
                      "width": 1920, "height": 1080, "frameRate": 30},
        )
        with self.assertRaisesRegex(RuntimeError, "Timeline is not ready"):
            self.execute(fake)
        self.assert_no_mutation(fake)
        self.assertFalse(any(name == "timeline.inspect_task" for name, _ in fake.tool_calls))

    def test_optional_resource_and_task_ids_and_task_target_are_enforced(self):
        self.write_geometry_plan(resourceId="timeline:other", taskId="task:other")
        fake = FakeMcp(
            lambda _arguments: {"result": {"structuredContent": {"ok": True}}},
        )
        with self.assertRaisesRegex(RuntimeError, "does not match the open Timeline"):
            self.execute(fake)
        self.assert_no_mutation(fake)

        self.write_geometry_plan(resourceId="timeline:test", taskId="task:other")
        fake = FakeMcp(
            lambda _arguments: {"result": {"structuredContent": {"ok": True}}},
        )
        with self.assertRaisesRegex(RuntimeError, "does not match the active native task"):
            self.execute(fake)
        self.assert_no_mutation(fake)

        self.write_geometry_plan(resourceId="timeline:test")
        fake = FakeMcp(
            lambda _arguments: {"result": {"structuredContent": {"ok": True}}},
            task={"ok": True, "active": True, "status": "running", "taskId": "task:test",
                  "targetResourceIds": ["timeline:other"]},
        )
        with self.assertRaisesRegex(RuntimeError, "does not target the inspected Timeline"):
            self.execute(fake)
        self.assert_no_mutation(fake)

    def test_matching_geometry_and_active_target_proceed(self):
        fake = FakeMcp(
            lambda _arguments: {"result": {"structuredContent": {"ok": True}}},
        )
        self.execute(fake)
        self.assertEqual(fake.invocations, 1)
        tool_names = [name for name, _ in fake.tool_calls]
        self.assertIn("app.inspect_command", tool_names)
        self.assertIn("timeline.inspect_task", tool_names)
        events = [json.loads(line) for line in self.log.read_text().splitlines()]
        self.assertLess(
            next(i for i, event in enumerate(events) if event.get("event") == "read_call_completed" and
                 event.get("tool") == "timeline.inspect_task"),
            next(i for i, event in enumerate(events) if event.get("event") == "invoke_started"),
        )

    def test_declared_frame_rate_is_checked_exactly(self):
        fake = FakeMcp(
            lambda _arguments: {"result": {"structuredContent": {"ok": True}}},
            document={"ok": True, "ready": True, "resourceId": "timeline:test",
                      "width": 1920, "height": 1080, "frameRate": 24},
        )
        with self.assertRaisesRegex(RuntimeError, "Plan fps 30 does not match"):
            self.execute(fake)
        self.assert_no_mutation(fake)
        self.assertFalse(any(name == "timeline.inspect_task" for name, _ in fake.tool_calls))

    def test_metadata_free_legacy_plan_skips_native_preflight(self):
        self.receipt.unlink(missing_ok=True)
        self.log.unlink(missing_ok=True)
        write_plan(self.plan)
        fake = FakeMcp(lambda _arguments: {
            "result": {"structuredContent": {"ok": True, "trackId": "track:test"}}
        })
        self.execute(fake)
        self.assertFalse(any(name in {"app.inspect_command", "timeline.inspect_task"}
                             for name, _ in fake.tool_calls))

    def test_completed_geometry_plan_resume_skips_native_preflight(self):
        self.write_geometry_plan()
        first_fake = FakeMcp(lambda _arguments: {"result": {"structuredContent": {"ok": True}}})
        self.execute(first_fake)
        completed_fake = FakeMcp(lambda _arguments: self.fail("completed step must not be replayed"))
        with patch.object(author_native, "call", completed_fake):
            author_native.execute(self.plan, self.receipt, self.log)
        self.assertFalse(any(name in {"app.inspect_command", "timeline.inspect_task"}
                             for name, _ in completed_fake.tool_calls))


class AuthorNativeTransportLimitTests(unittest.TestCase):
    def test_oversized_later_operation_rejects_before_any_rpc_or_intent(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            plan = root / "plan.json"
            receipt = root / "receipt.json"
            log = root / "events.ndjson"
            plan.write_text(json.dumps({"steps": [
                {"id": "track", "commandId": "timeline.action.createTimelineTrack",
                 "values": {"label": "must not be created"}},
                {"id": "too-large", "commandId": "timeline.action.createTimelineGenerator",
                 "values": {"payload": "x" * (1024 * 1024)}}]}))
            with patch.object(author_native, "call", side_effect=AssertionError("No RPC before validation")):
                with self.assertRaisesRegex(ValueError, "at most 1048576"):
                    author_native.execute(plan, receipt, log)
            self.assertFalse(log.exists())
            self.assertFalse(receipt.exists())

    def test_transport_counts_full_envelope_and_unicode_escaping(self):
        from native_mcp import encode_request, validate_tool_request_size
        arguments = {"text": "光😀" * 20}
        body = encode_request("tools/call", {"name": "example", "arguments": arguments})
        self.assertEqual(validate_tool_request_size("example", arguments), len(body))
        self.assertEqual(json.loads(body)["params"]["arguments"], arguments)
        self.assertGreater(len(body), len(author_native.compact_json_bytes(arguments)))


if __name__ == "__main__":
    unittest.main()
