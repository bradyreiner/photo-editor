"""Bind the v15 landscape suffix to an exact native cached-batch replay.

This is an offline evidence binder. It never calls MCP and never edits the
original plan, receipt, runner event log, Timeline document, or task journal.
It accepts the timed-out runner batch only when the native journal and the
successful idempotent replay prove the exact original operations and current
checkpoint. The emitted plan contains only the still-unexecuted suffix.
"""

from pathlib import Path
import argparse
import copy
import hashlib
import json
import math
import os
import re
import sys


HERE = Path(__file__).resolve().parent
EVIDENCE = HERE / "evidence"
PROJECTS = HERE / "projects"
PLAN_SHA256 = "55018e9271ee16890b8f93bcbcddfe59fdc63096e70a4f51749857bee251bf45"
TASK_ID = "98081dfb-47f4-424b-9d46-aeb5fcb46ba3"
RESOURCE_ID = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
TIMEOUT_BATCH_ID = "d3489425-8a44-58a6-b597-b749d46f0774"
TIMEOUT_REQUEST_SHA256 = "b07c9e3967fa322b4d31c4440f262f4e666c4d3cd8de45181f899881b67da059"
COMPLETED_PREFIX_COUNT = 861
RECOVERED_STEP_COUNT = 2
SUFFIX_START = COMPLETED_PREFIX_COUNT + RECOVERED_STEP_COUNT
EXPECTED_CREATED_CLIP_ID = "5b851e48-dea1-41cd-b1a3-596a6f9b16f4"
EXPECTED_CREATED_TRACK_ID = "graphics-8d8ebd70-be41-4e46-9c3e-63e20dfbbccc"
RECEIPT_SCHEMA = "looksy.native-authoring-receipt/v1"
BINDER_SCHEMA = "looksy.native-timeout-replay-continuation/v1"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")

if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import author_native  # noqa: E402
import build_native_continuation as continuation  # noqa: E402


class TimeoutRecoveryError(RuntimeError):
    """Raised when native proofs do not establish one exact safe suffix."""


def _sha256(data):
    return hashlib.sha256(data).hexdigest()


def _canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False, allow_nan=False)


def _read_json(path, label):
    try:
        raw = Path(path).read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise TimeoutRecoveryError(f"Could not read {label}: {error}") from error
    if not isinstance(value, dict):
        raise TimeoutRecoveryError(f"{label} must be a JSON object.")
    return raw, value


def _structured(envelope, label):
    result = envelope.get("result")
    structured = result.get("structuredContent") if isinstance(result, dict) else None
    if (not isinstance(structured, dict) or structured.get("ok") is not True or
            result.get("isError") is True):
        raise TimeoutRecoveryError(f"{label} has no authoritative ok:true structuredContent.")
    return structured


def _sha_path(path, label):
    path = Path(path)
    try:
        digest = hashlib.sha256()
        size = 0
        with path.open("rb") as source:
            while True:
                chunk = source.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
                size += len(chunk)
    except OSError as error:
        raise TimeoutRecoveryError(f"Could not hash {label}: {error}") from error
    return size, digest.hexdigest()


def _resolve_prior_refs(value, completed_results, suffix_positions, current_index,
                        full_positions, replacements, path):
    """Resolve proven prefix results; retain only valid earlier suffix refs."""
    if isinstance(value, dict):
        if "result" in value or "field" in value:
            if set(value) != {"result", "field"}:
                raise TimeoutRecoveryError(f"Malformed stable reference at {path}.")
            source_id, field = value["result"], value["field"]
            if not isinstance(source_id, str) or not isinstance(field, str):
                raise TimeoutRecoveryError(f"Stable reference at {path} is not string-valued.")
            if source_id in completed_results:
                result = completed_results[source_id]
                if field not in result:
                    raise TimeoutRecoveryError(
                        f"Recovered result {source_id!r} has no field {field!r} referenced at {path}.")
                replacement = result[field]
                replacements.append({
                    "path": path,
                    "sourceStepId": source_id,
                    "field": field,
                    "resolvedValue": replacement,
                })
                return copy.deepcopy(replacement)
            position = suffix_positions.get(source_id)
            if position is None or position >= current_index:
                full_position = full_positions.get(source_id)
                if full_position is not None and full_position < SUFFIX_START:
                    raise TimeoutRecoveryError(
                        f"Completed reference {source_id!r} was not proven at {path}.")
                raise TimeoutRecoveryError(
                    f"Suffix reference at {path} points to an unknown or future step {source_id!r}.")
            return {"result": source_id, "field": field}
        return {
            key: _resolve_prior_refs(item, completed_results, suffix_positions,
                                     current_index, full_positions, replacements,
                                     f"{path}/{key}")
            for key, item in value.items()
        }
    if isinstance(value, list):
        return [
            _resolve_prior_refs(item, completed_results, suffix_positions,
                                current_index, full_positions, replacements,
                                f"{path}/{index}")
            for index, item in enumerate(value)
        ]
    if isinstance(value, float) and not math.isfinite(value):
        raise TimeoutRecoveryError(f"Non-finite number found at {path}.")
    return value


def _validate_runner_events(path, plan_sha256, plan_steps, receipt_steps,
                            expected_batch, expected_request):
    events = []
    try:
        with Path(path).open("r", encoding="utf-8") as source:
            for line_number, line in enumerate(source, 1):
                if not line.strip():
                    continue
                try:
                    event = json.loads(line)
                except json.JSONDecodeError as error:
                    raise TimeoutRecoveryError(
                        f"Runner event log is invalid at line {line_number}.") from error
                if (not isinstance(event, dict) or
                        event.get("schema") != RECEIPT_SCHEMA or
                        event.get("planSha256") != plan_sha256):
                    raise TimeoutRecoveryError(
                        f"Runner event {line_number} does not belong to the frozen plan.")
                if event.get("event") not in continuation.ALLOWED_EVENTS:
                    raise TimeoutRecoveryError(
                        f"Runner event {line_number} has an unknown event kind.")
                if event.get("event") == "read_call_failed":
                    raise TimeoutRecoveryError("The original runner log contains a failed read call.")
                events.append(event)
    except OSError as error:
        raise TimeoutRecoveryError(f"Could not read the runner event log: {error}") from error

    opened = [event for event in events if event.get("event") == "plan_opened"]
    if len(opened) != 1 or opened[0].get("stepCount") != len(plan_steps):
        raise TimeoutRecoveryError("Runner log does not prove one opening of the exact parent plan.")
    starts = [event for event in events if event.get("event") == "invoke_started"]
    completed = [event for event in events if event.get("event") == "invoke_completed"]
    failed = [event for event in events if event.get("event") == "invoke_failed"]
    expected_ids = [step["id"] for step in plan_steps]
    if [event.get("stepId") for event in starts] != expected_ids[:SUFFIX_START]:
        raise TimeoutRecoveryError("Runner mutation intents are not the exact 863-step prefix.")
    if [event.get("stepId") for event in completed] != expected_ids[:COMPLETED_PREFIX_COUNT]:
        raise TimeoutRecoveryError("Runner success receipts are not the exact 861-step prefix.")
    if [event.get("stepId") for event in failed] != expected_ids[COMPLETED_PREFIX_COUNT:SUFFIX_START]:
        raise TimeoutRecoveryError("Runner failures are not exactly the two recovered batch operations.")
    if len(receipt_steps) != COMPLETED_PREFIX_COUNT:
        raise TimeoutRecoveryError("Runner JSON receipt does not contain exactly 861 successes.")

    for index, (event, recorded, planned) in enumerate(zip(
            completed, receipt_steps, plan_steps[:COMPLETED_PREFIX_COUNT])):
        if (event.get("stepId") != planned["id"] or
                recorded.get("id") != planned["id"] or
                event.get("commandId") != planned["commandId"] or
                recorded.get("commandId") != planned["commandId"] or
                not continuation._same_json_value(event.get("result"), recorded.get("result")) or
                not continuation._same_json_value(event.get("values"), recorded.get("values"))):
            raise TimeoutRecoveryError(f"Runner prefix receipt differs at plan index {index}.")

    for index, event in enumerate(starts):
        if event.get("commandId") != plan_steps[index]["commandId"]:
            raise TimeoutRecoveryError(f"Runner intent command differs at plan index {index}.")

    for offset, event in enumerate(failed):
        index = COMPLETED_PREFIX_COUNT + offset
        planned = plan_steps[index]
        operation = expected_request["operations"][offset]
        if (event.get("commandId") != planned["commandId"] or
                event.get("batchId") != expected_batch or
                event.get("batchOperationId") != operation["id"] or
                event.get("batchOperationCount") != RECOVERED_STEP_COUNT or
                event.get("error") != "timed out" or
                event.get("rolledBack") is not False or
                event.get("recoveryRequired") is not True):
            raise TimeoutRecoveryError(
                "The old runner failure does not match the exact timed-out native batch.")
        intent = starts[index]
        if (intent.get("batchId") != expected_batch or
                intent.get("batchOperationId") != operation["id"] or
                intent.get("batchOperationCount") != RECOVERED_STEP_COUNT or
                not continuation._same_json_value(intent.get("batchOperationValues"),
                                                  operation["values"]) or
                not continuation._same_json_value(intent.get("values"), planned["values"])):
            raise TimeoutRecoveryError(
                "Timed-out intent does not match the recovered operation body.")
    return events


def _clip_track_ids(document):
    tracks = document.get("tracks")
    if not isinstance(tracks, list):
        raise TimeoutRecoveryError("Managed Timeline document has no track list.")
    track_ids = [track.get("trackId") for track in tracks
                 if isinstance(track, dict) and isinstance(track.get("trackId"), str)]
    clips = [clip for track in tracks if isinstance(track, dict)
             for clip in track.get("clips", []) if isinstance(clip, dict)]
    clip_ids = [clip.get("clipId") for clip in clips
                if isinstance(clip.get("clipId"), str)]
    if len(set(track_ids)) != len(track_ids) or len(set(clip_ids)) != len(clip_ids):
        raise TimeoutRecoveryError("Managed Timeline autosave has duplicate stable IDs.")
    return set(clip_ids), set(track_ids), {clip["clipId"]: clip for clip in clips}, {
        track["trackId"]: track for track in tracks if isinstance(track, dict) and track.get("trackId")
    }


def _ids_from_inspection(document):
    clips = document.get("clips")
    tracks = document.get("tracks")
    if not isinstance(clips, list) or not isinstance(tracks, list):
        raise TimeoutRecoveryError("Fresh native document proof has no clip and track lists.")
    clip_ids = [item.get("clipId") for item in clips if isinstance(item, dict)]
    track_ids = [item.get("trackId") for item in tracks if isinstance(item, dict)]
    clip_ids = [item for item in clip_ids if isinstance(item, str)]
    track_ids = [item for item in track_ids if isinstance(item, str)]
    if len(set(clip_ids)) != len(clip_ids) or len(set(track_ids)) != len(track_ids):
        raise TimeoutRecoveryError("Fresh native document proof has duplicate stable IDs.")
    return set(clip_ids), set(track_ids)


def _id_set_sha(ids):
    return _sha256(json.dumps(sorted(ids), separators=(",", ":")).encode("utf-8"))


def _resolve_and_validate_created_ids(prefix_results, replay_rows, clip_ids, track_ids):
    created_clips = set()
    created_tracks = set()
    for result in prefix_results.values():
        if isinstance(result.get("createdClipId"), str):
            created_clips.add(result["createdClipId"])
        ids = result.get("createdClipIds")
        if isinstance(ids, list):
            created_clips.update(item for item in ids if isinstance(item, str))
        if isinstance(result.get("trackId"), str):
            created_tracks.add(result["trackId"])
    for row in replay_rows:
        if isinstance(row.get("createdClipId"), str):
            created_clips.add(row["createdClipId"])
        if isinstance(row.get("trackId"), str):
            created_tracks.add(row["trackId"])
    missing_clips = created_clips - clip_ids
    missing_tracks = created_tracks - track_ids
    if missing_clips or missing_tracks:
        raise TimeoutRecoveryError(
            f"Durable autosave is missing created IDs ({len(missing_clips)} clips, "
            f"{len(missing_tracks)} tracks).")
    return created_clips, created_tracks


def _validate_cached_native_batch(expected_batch, native_row, replay, current_hash):
    """Check exact journal arguments and prove replay returned its cached result."""
    native_args = native_row.get("arguments", {})
    native_outcome = native_row.get("outcome", {})
    if (native_row.get("command") != author_native.BATCH_COMMAND_ID or
            native_row.get("resourceId") != RESOURCE_ID or
            native_row.get("status") != "completed" or
            native_args.get("_batchId") != TIMEOUT_BATCH_ID or
            native_args.get("batchId") != TIMEOUT_BATCH_ID or
            native_args.get("_batchRequestSha256") != TIMEOUT_REQUEST_SHA256 or
            native_outcome.get("batchId") != TIMEOUT_BATCH_ID or
            native_outcome.get("batchRequestSha256") != TIMEOUT_REQUEST_SHA256 or
            not continuation._same_json_value(native_args.get("operations"),
                                               expected_batch["operations"]) or
            native_outcome.get("ok") is not True or
            native_outcome.get("changed") is not True or
            native_outcome.get("operationCount") != RECOVERED_STEP_COUNT):
        raise TimeoutRecoveryError("Native journal does not prove the exact successful timeout batch.")
    native_rows = native_outcome.get("batchResults")
    if (not isinstance(native_rows, list) or len(native_rows) != RECOVERED_STEP_COUNT or
            [row.get("id") for row in native_rows] != ["op000", "op001"] or
            any(row.get("ok") is not True for row in native_rows) or
            [row.get("commandId") for row in native_rows] !=
            [op["commandId"] for op in expected_batch["operations"]] or
            native_rows[1].get("trackId") != EXPECTED_CREATED_TRACK_ID):
        raise TimeoutRecoveryError("Native journal operation receipts do not match the expected two results.")
    if (replay.get("ok") is not True or replay.get("idempotentReplay") is not True or
            replay.get("commandId") != author_native.BATCH_COMMAND_ID or
            replay.get("batchId") != TIMEOUT_BATCH_ID or
            replay.get("batchRequestSha256") != TIMEOUT_REQUEST_SHA256 or
            replay.get("resourceId") != RESOURCE_ID or
            replay.get("taskStateHash") != current_hash or
            not continuation._same_json_value(replay.get("batchResults"), native_rows)):
        raise TimeoutRecoveryError("Replay result is not the exact cached native receipt at the current checkpoint.")
    return native_rows


def _validate_autosave_checkpoint(canonical_doc, autosave_doc, initial_document,
                                  document, plan, prefix_results, native_rows):
    """Verify durable clip/track graph and recovered values against inspection."""
    expected_timeline_id = RESOURCE_ID.removeprefix("timeline:")
    initial_clip_ids, initial_track_ids = _ids_from_inspection(initial_document)
    fresh_clip_ids, fresh_track_ids = _ids_from_inspection(document)
    canonical_clip_ids, canonical_track_ids, _, _ = _clip_track_ids(canonical_doc)
    autosave_clip_ids, autosave_track_ids, autosave_clips, autosave_tracks = _clip_track_ids(autosave_doc)
    if (canonical_doc.get("timelineId") != expected_timeline_id or
            canonical_clip_ids != initial_clip_ids or canonical_track_ids != initial_track_ids):
        raise TimeoutRecoveryError("Canonical managed project is not the exact original v15 baseline.")
    sequence_format = autosave_doc.get("sequenceFormat", {})
    if (autosave_doc.get("timelineId") != expected_timeline_id or
            autosave_clip_ids != fresh_clip_ids or autosave_track_ids != fresh_track_ids or
            sequence_format.get("width") != plan["composition"]["width"] or
            sequence_format.get("height") != plan["composition"]["height"] or
            not math.isclose(float(sequence_format.get("frameRate", 0)),
                             float(plan["composition"]["fps"]), rel_tol=0.0, abs_tol=1e-9)):
        raise TimeoutRecoveryError("Latest autosave does not have the exact fresh native clip/track graph.")

    created_clips, created_tracks = _resolve_and_validate_created_ids(
        prefix_results, native_rows, autosave_clip_ids, autosave_track_ids)
    target_clip = autosave_clips.get(EXPECTED_CREATED_CLIP_ID)
    expected_frame = {
        "frameWidth": 0.55,
        "frameHeight": 0.0651851851851852,
        "frameOffsetX": 0.5,
        "frameOffsetY": 1.304444444444445,
        "frameScale": 1.0,
        "frameMode": "stretch",
        "frameCornerRadius": 0.0,
        "opacity": 1.0,
    }
    if (not isinstance(target_clip, dict) or
            any(not continuation._same_json_value(target_clip.get(key), value)
                for key, value in expected_frame.items())):
        raise TimeoutRecoveryError("Autosave does not preserve the exact recovered frame geometry.")
    target_track = autosave_tracks.get(EXPECTED_CREATED_TRACK_ID)
    if (not isinstance(target_track, dict) or
            target_track.get("customName") != "audio mixer waveform 1 / bars 113-128" or
            target_track.get("role") != "graphics"):
        raise TimeoutRecoveryError("Autosave does not preserve the exact recovered track creation.")
    return {
        "initialClipIds": initial_clip_ids,
        "initialTrackIds": initial_track_ids,
        "freshClipIds": fresh_clip_ids,
        "freshTrackIds": fresh_track_ids,
        "autosaveClipIds": autosave_clip_ids,
        "autosaveTrackIds": autosave_track_ids,
        "createdClipIds": created_clips,
        "createdTrackIds": created_tracks,
        "canonicalCounts": {"clips": len(canonical_clip_ids), "tracks": len(canonical_track_ids)},
        "autosaveCounts": {"clips": len(autosave_clip_ids), "tracks": len(autosave_track_ids)},
        "documentCounts": {"clips": len(fresh_clip_ids), "tracks": len(fresh_track_ids)},
    }


def _validate_clone_file(manifest, source_path, source_hash, source_bytes):
    source_path = Path(source_path).resolve(strict=True)
    matching = [row for row in manifest.get("files", [])
                if Path(row.get("source", "")).resolve(strict=False) == source_path]
    if len(matching) != 1:
        raise TimeoutRecoveryError(f"Recovery clone does not uniquely cover {source_path}.")
    row = matching[0]
    if row.get("bytes") != source_bytes or row.get("sha256") != source_hash:
        raise TimeoutRecoveryError(f"Recovery clone manifest does not match {source_path}.")
    backup = Path(row.get("backup", "")).resolve(strict=True)
    backup_bytes, backup_hash = _sha_path(backup, "APFS recovery clone")
    if backup_bytes != source_bytes or backup_hash != source_hash:
        raise TimeoutRecoveryError(f"Recovery clone contents differ for {source_path}.")
    return str(backup)


def _validate_proofs(plan_path, receipt_path, event_path, task_path, replay_path,
                     document_path, initial_document_path, clone_manifest_path,
                     replay_arguments_path, replay_manifest_path):
    plan_bytes, plan = _read_json(plan_path, "v15 parent plan")
    plan_hash = _sha256(plan_bytes)
    if plan_hash != PLAN_SHA256:
        raise TimeoutRecoveryError("The v15 parent plan bytes do not match the frozen SHA-256.")
    plan_steps, positions = continuation._validate_plan(plan, "v15 parent plan")
    if len(plan_steps) != 1091:
        raise TimeoutRecoveryError("The frozen v15 parent plan is not 1,091 steps.")
    if (plan.get("taskId") != TASK_ID or plan.get("resourceId") != RESOURCE_ID or
            plan.get("startingDocumentHash") != "fd6eeeb7f444263a18d99fd34c5bcd51d6aac03edd4d93df0801dea6b1aa90f6"):
        raise TimeoutRecoveryError("The parent plan is not bound to the expected v15 source task.")
    if (plan_steps[COMPLETED_PREFIX_COUNT]["id"] !=
            "film-closing/audio-mixer-waveform-1-chunk-06-frame" or
            plan_steps[COMPLETED_PREFIX_COUNT + 1]["id"] !=
            "film-closing/audio-mixer-waveform-1-chunk-07-track"):
        raise TimeoutRecoveryError("The frozen timeout steps are not at the expected plan positions.")

    receipt_bytes, receipt = _read_json(receipt_path, "original runner receipt")
    if (receipt.get("planSha256") != plan_hash or receipt.get("status") != "incomplete" or
            not isinstance(receipt.get("steps"), list) or
            len(receipt["steps"]) != COMPLETED_PREFIX_COUNT):
        raise TimeoutRecoveryError("Original runner receipt does not prove exactly 861 successes.")
    prefix_results = continuation._resolve_prefix(
        plan_steps[:COMPLETED_PREFIX_COUNT], receipt["steps"], positions,
        "original 861-step runner receipt")
    failure = receipt.get("failure")
    if (not isinstance(failure, dict) or
            failure.get("stepId") != plan_steps[COMPLETED_PREFIX_COUNT]["id"] or
            failure.get("batchId") != TIMEOUT_BATCH_ID or
            failure.get("error") != "timed out" or
            failure.get("rolledBack") is not False or
            failure.get("recoveryRequired") is not True):
        raise TimeoutRecoveryError("Original receipt does not preserve the exact timeout claim.")

    _, task_envelope = _read_json(task_path, "fresh native task proof")
    task = _structured(task_envelope, "fresh native task proof")
    _, replay_envelope = _read_json(replay_path, "native idempotent replay result")
    replay = _structured(replay_envelope, "native idempotent replay result")
    _, document_envelope = _read_json(document_path, "fresh native document proof")
    document = _structured(document_envelope, "fresh native document proof")
    _, initial_envelope = _read_json(initial_document_path, "initial native document proof")
    initial_document = _structured(initial_envelope, "initial native document proof")
    clone_bytes, clone_manifest = _read_json(clone_manifest_path, "restart recovery clone manifest")
    if clone_manifest.get("method") != "APFS clonefile; no full-copy fallback":
        raise TimeoutRecoveryError("Recovery file preservation is not proven by the approved APFS clone.")

    if (task.get("taskId") != TASK_ID or task.get("status") != "running" or
            task.get("active") is not True):
        raise TimeoutRecoveryError("Native task proof is not the same active running task.")
    scope = task.get("nativeTimelineScope", {})
    if (scope.get("targetIds") != [RESOURCE_ID] or
            scope.get("assetPaths") != plan.get("assetPaths") or
            scope.get("outputDirectory") != plan.get("outputDirectory")):
        raise TimeoutRecoveryError("Native task scope differs from the frozen v15 plan.")
    task_resource_rows = [row for row in task.get("resources", [])
                          if row.get("id") == RESOURCE_ID]
    if len(task_resource_rows) != 1:
        raise TimeoutRecoveryError("Native task proof does not contain exactly one Timeline resource.")
    task_resource = task_resource_rows[0]
    current_hash = task_resource.get("currentHash")
    if (task_resource.get("pending") is not False or
            not isinstance(current_hash, str) or not SHA256_RE.fullmatch(current_hash)):
        raise TimeoutRecoveryError("Native task resource is pending or has no exact checkpoint hash.")
    task_steps = task.get("steps", [])
    if (len(task_steps) != 123 or any(row.get("status") != "completed" for row in task_steps) or
            task_steps[-1].get("id") != 122):
        raise TimeoutRecoveryError("Native journal contains an unexpected, pending, or failed operation.")
    native_row = task_steps[-1]
    if (native_row.get("command") != author_native.BATCH_COMMAND_ID or
            native_row.get("resourceId") != RESOURCE_ID or
            native_row.get("status") != "completed"):
        raise TimeoutRecoveryError("Latest native journal entry is not the recovered batch.")

    expected_batch = author_native._build_batch_request(
        plan_hash, plan_steps[COMPLETED_PREFIX_COUNT:SUFFIX_START], prefix_results)
    if (not expected_batch.get("fitsLimits") or
            expected_batch.get("batchId") != TIMEOUT_BATCH_ID or
            len(expected_batch.get("operations", [])) != RECOVERED_STEP_COUNT):
        raise TimeoutRecoveryError("Frozen plan does not reconstruct the exact bounded timeout batch.")
    native_rows = _validate_cached_native_batch(
        expected_batch, native_row, replay, current_hash)
    _validate_runner_events(event_path, plan_hash, plan_steps, receipt["steps"],
                            TIMEOUT_BATCH_ID, expected_batch)

    replay_args_bytes, replay_args = _read_json(
        replay_arguments_path, "exact native replay arguments")
    replay_manifest_bytes, replay_manifest = _read_json(
        replay_manifest_path, "native replay arguments manifest")
    expected_public_args = {
        "commandId": author_native.BATCH_COMMAND_ID,
        "values": expected_batch["values"],
    }
    if (not continuation._same_json_value(replay_args, expected_public_args) or
            replay_manifest.get("expectedNativeBatchId") != TIMEOUT_BATCH_ID or
            replay_manifest.get("expectedNativeRequestSha256") != TIMEOUT_REQUEST_SHA256 or
            replay_manifest.get("argumentsSha256") != _sha256(replay_args_bytes)):
        raise TimeoutRecoveryError("Replay arguments file or its pinned native request hash differs.")

    if (document.get("ready") is not True or document.get("resourceId") != RESOURCE_ID or
            document.get("documentHash") != current_hash or
            document.get("width") != plan["composition"]["width"] or
            document.get("height") != plan["composition"]["height"] or
            not math.isclose(float(document.get("frameRate", 0)),
                             float(plan["composition"]["fps"]), rel_tol=0.0, abs_tol=1e-9)):
        raise TimeoutRecoveryError("Fresh native document proof differs from the replay checkpoint or composition.")
    if (initial_document.get("documentHash") != plan.get("startingDocumentHash") or
            initial_document.get("resourceId") != RESOURCE_ID or
            initial_document.get("ready") is not True):
        raise TimeoutRecoveryError("Initial v15 document proof differs from the frozen source baseline.")
    if Path(initial_document.get("savedDocumentPath", "")).resolve(strict=False) != Path(
            document.get("savedDocumentPath", "")).resolve(strict=False):
        raise TimeoutRecoveryError("The v15 document path changed between initial and current inspect proofs.")

    canonical_path = Path(document.get("savedDocumentPath", ""))
    if not canonical_path.is_absolute() or not canonical_path.is_file():
        raise TimeoutRecoveryError("Fresh native document has no existing canonical managed path.")
    autosave_path = Path(str(canonical_path) + ".autosave")
    canonical_bytes, canonical_sha = _sha_path(canonical_path, "canonical managed Timeline")
    autosave_bytes, autosave_sha = _sha_path(autosave_path, "latest managed Timeline autosave")
    canonical_doc = json.loads(canonical_path.read_text())
    autosave_doc = json.loads(autosave_path.read_text())
    managed_state = _validate_autosave_checkpoint(
        canonical_doc, autosave_doc, initial_document, document, plan,
        prefix_results, native_rows)
    initial_clip_ids = managed_state["initialClipIds"]
    initial_track_ids = managed_state["initialTrackIds"]
    fresh_clip_ids = managed_state["freshClipIds"]
    fresh_track_ids = managed_state["freshTrackIds"]
    autosave_clip_ids = managed_state["autosaveClipIds"]
    autosave_track_ids = managed_state["autosaveTrackIds"]
    created_clips = managed_state["createdClipIds"]
    created_tracks = managed_state["createdTrackIds"]

    clone_manifest_path = Path(clone_manifest_path)
    clone_size, clone_sha = _sha_path(clone_manifest_path, "recovery clone manifest")
    clone_bindings = {}
    for path, digest, size, label in (
            (canonical_path, canonical_sha, canonical_bytes, "canonicalDocument"),
            (autosave_path, autosave_sha, autosave_bytes, "latestAutosave")):
        clone_bindings[label] = _validate_clone_file(
            clone_manifest, path, digest, size)
    journal_path = Path(task.get("journalPath", ""))
    durable_journal_path = journal_path
    if not durable_journal_path.is_file():
        # The native proof's journalPath may be a URL or omitted; derive only
        # from the exact task-owned data root containing the inspected draft.
        data_root = canonical_path.parents[2]
        durable_journal_path = data_root / "agent-tasks" / TASK_ID / "journal.json"
    if not durable_journal_path.is_file():
        raise TimeoutRecoveryError("Could not locate the exact durable task journal for the native proof.")
    durable_journal_bytes, durable_journal = _read_json(durable_journal_path, "durable task journal")
    if (durable_journal.get("taskId") != TASK_ID or durable_journal.get("status") != "running" or
            durable_journal.get("steps") != task_steps or
            durable_journal.get("resources") != task.get("resources") or
            durable_journal.get("nativeTimelineScope") != scope):
        raise TimeoutRecoveryError("Durable task journal differs from the fresh native task proof.")
    journal_size, journal_sha = _sha_path(durable_journal_path, "durable task journal")
    clone_bindings["taskJournal"] = _validate_clone_file(
        clone_manifest, durable_journal_path, journal_sha, journal_size)
    if (clone_manifest.get("backupRoot") is None or
            not Path(clone_manifest["backupRoot"]).is_dir()):
        raise TimeoutRecoveryError("APFS recovery clone root is missing.")

    replay_raw = Path(replay_path).read_bytes()
    task_raw = Path(task_path).read_bytes()
    document_raw = Path(document_path).read_bytes()
    receipt_raw = Path(receipt_path).read_bytes()
    event_bytes = Path(event_path).read_bytes()
    initial_raw = Path(initial_document_path).read_bytes()
    output_path = Path(plan.get("outputPath", ""))
    output_directory = Path(plan.get("outputDirectory", ""))
    try:
        if output_path.resolve(strict=False).parent != output_directory.resolve(strict=True):
            raise TimeoutRecoveryError("Final output path is outside the sealed task output directory.")
    except OSError as error:
        raise TimeoutRecoveryError(f"Could not verify sealed output directory: {error}") from error
    if output_path.exists():
        raise TimeoutRecoveryError("The planned final project output already exists; refusing to reuse it.")
    if scope.get("outputPaths"):
        raise TimeoutRecoveryError("Native task already recorded output files; this suffix assumes none.")

    input_hashes = {
        "parentPlanSha256": plan_hash,
        "originalReceiptSha256": _sha256(receipt_raw),
        "originalNdjsonSha256": _sha256(event_bytes),
        "nativeTaskProofSha256": _sha256(task_raw),
        "nativeReplayProofSha256": _sha256(replay_raw),
        "replayArgumentsSha256": _sha256(replay_args_bytes),
        "replayArgumentsManifestSha256": _sha256(replay_manifest_bytes),
        "nativeDocumentProofSha256": _sha256(document_raw),
        "initialDocumentProofSha256": _sha256(initial_raw),
        "cloneManifestSha256": clone_sha,
        "durableTaskJournalSha256": journal_sha,
        "canonicalManagedDocumentSha256": canonical_sha,
        "latestAutosaveSha256": autosave_sha,
    }
    return {
        "plan": plan,
        "planSteps": plan_steps,
        "prefixResults": prefix_results,
        "nativeRows": native_rows,
        "expectedBatch": expected_batch,
        "task": task,
        "document": document,
        "autosave": autosave_doc,
        "autosaveClipIds": autosave_clip_ids,
        "autosaveTrackIds": autosave_track_ids,
        "createdClipIds": created_clips,
        "createdTrackIds": created_tracks,
        "cloneBindings": clone_bindings,
        "cloneManifest": clone_manifest,
        "cloneManifestSize": clone_size,
        "inputHashes": input_hashes,
        "canonicalPath": str(canonical_path.resolve()),
        "autosavePath": str(autosave_path.resolve()),
        "canonicalCounts": managed_state["canonicalCounts"],
        "autosaveCounts": managed_state["autosaveCounts"],
        "documentCounts": managed_state["documentCounts"],
        "durableJournalPath": str(durable_journal_path.resolve()),
        "durableJournalSize": journal_size,
    }


def _clip_track_ids(document):
    tracks = document.get("tracks")
    if not isinstance(tracks, list):
        raise TimeoutRecoveryError("Managed Timeline document has no track list.")
    track_ids = [track.get("trackId") for track in tracks
                 if isinstance(track, dict) and isinstance(track.get("trackId"), str)]
    clips = [clip for track in tracks if isinstance(track, dict)
             for clip in track.get("clips", []) if isinstance(clip, dict)]
    clip_ids = [clip.get("clipId") for clip in clips
                if isinstance(clip.get("clipId"), str)]
    if len(set(track_ids)) != len(track_ids) or len(set(clip_ids)) != len(clip_ids):
        raise TimeoutRecoveryError("Managed Timeline document has duplicate stable IDs.")
    return set(clip_ids), set(track_ids), {clip["clipId"]: clip for clip in clips}, {
        track["trackId"]: track for track in tracks if isinstance(track, dict) and track.get("trackId")
    }


def build_timeout_reconciled_suffix(proofs):
    plan = copy.deepcopy(proofs["plan"])
    all_steps = proofs["planSteps"]
    prefix_results = dict(proofs["prefixResults"])
    expected_batch = proofs["expectedBatch"]
    native_rows = proofs["nativeRows"]
    recovered_bindings = []
    for offset, row in enumerate(native_rows):
        step = all_steps[COMPLETED_PREFIX_COUNT + offset]
        result = author_native._compact_batch_step_result(
            row, step["commandId"], TIMEOUT_BATCH_ID, True)
        prefix_results[step["id"]] = result
        recovered_bindings.append({
            "planStepId": step["id"],
            "planCommandId": step["commandId"],
            "nativeOperationId": row["id"],
            "nativeResult": row,
            "runnerResultForContinuation": result,
        })

    suffix = copy.deepcopy(all_steps[SUFFIX_START:])
    suffix_positions = {step["id"]: index for index, step in enumerate(suffix)}
    full_positions = {step["id"]: index for index, step in enumerate(all_steps)}
    replacements = []
    for index, step in enumerate(suffix):
        step["values"] = _resolve_prior_refs(
            step["values"], prefix_results, suffix_positions, index,
            full_positions, replacements, f"steps/{step['id']}/values")
    plan["steps"] = suffix
    plan["resourceId"] = proofs["document"]["resourceId"]
    plan["taskId"] = proofs["task"]["taskId"]
    plan["startingDocumentHash"] = proofs["document"]["documentHash"]
    plan["proofStatus"] = (
        "The exact cached timeout batch and full authored graph are pinned to native journal, "
        "replay, inspect, and APFS autosave evidence. This 228-step suffix is unexecuted; "
        "native recovery must reopen the Timeline at the pinned document hash before it can run. "
        "Visual review remains pending.")
    replacement_bytes = json.dumps(replacements, sort_keys=True, separators=(",", ":"),
                                   ensure_ascii=False, allow_nan=False).encode("utf-8")
    plan["timeoutRecoveryProvenance"] = {
        "schema": BINDER_SCHEMA,
        "parentPlanSha256": proofs["inputHashes"]["parentPlanSha256"],
        "parentPlanPath": str(HERE / "full-landscape-v15-reviewed-suffix.commands.json"),
        "originalReceiptPath": str(EVIDENCE / "native-full-landscape-v15-receipt.json"),
        "originalEventLogPath": str(EVIDENCE / "native-full-landscape-v15-receipt.json.ndjson"),
        "inputHashes": proofs["inputHashes"],
        "taskId": plan["taskId"],
        "resourceId": plan["resourceId"],
        "startingDocumentHash": plan["startingDocumentHash"],
        "nativeTaskJournalStepId": 122,
        "nativeTaskJournalStepCount": 123,
        "nativeTaskJournalStatus": "running",
        "nativeTaskJournalAllStepsCompleted": True,
        "nativeTaskResourcePending": False,
        "nativeCheckpointHash": plan["startingDocumentHash"],
        "timedOutPlanStepIds": [step["id"] for step in all_steps[
            COMPLETED_PREFIX_COUNT:SUFFIX_START]],
        "nativeReplayBatchId": TIMEOUT_BATCH_ID,
        "nativeReplayRequestSha256": TIMEOUT_REQUEST_SHA256,
        "nativeReplayWasCachedIdempotentResult": True,
        "nativeReplayOperationCount": RECOVERED_STEP_COUNT,
        "recoveredPlanStepBindings": recovered_bindings,
        "runnerReportedCompletedPrefixCount": COMPLETED_PREFIX_COUNT,
        "nativeReconciledCompletedPrefixCount": SUFFIX_START,
        "remainingStepCount": len(suffix),
        "firstRemainingStepId": suffix[0]["id"] if suffix else None,
        "resolvedPriorReferenceCount": len(replacements),
        "resolvedPriorReferencesSha256": _sha256(replacement_bytes),
        "durableManagedDocument": {
            "canonicalPath": proofs["canonicalPath"],
            "canonicalStateAtOriginalBaseline": proofs["canonicalCounts"],
            "latestAutosavePath": proofs["autosavePath"],
            "latestAutosaveCounts": proofs["autosaveCounts"],
            "freshNativeDocumentCounts": proofs["documentCounts"],
            "latestAutosaveSha256": proofs["inputHashes"]["latestAutosaveSha256"],
            "autosaveClipIdSetSha256": _id_set_sha(proofs["autosaveClipIds"]),
            "autosaveTrackIdSetSha256": _id_set_sha(proofs["autosaveTrackIds"]),
            "createdClipIdsProvenPresent": len(proofs["createdClipIds"]),
            "createdTrackIdsProvenPresent": len(proofs["createdTrackIds"]),
            "allCreatedIdsPresent": True,
            "recoveryClonePaths": proofs["cloneBindings"],
            "recoveryRequiredForResume": True,
            "reopenGate": "Run through native recovery and inspect the exact resource/documentHash before executing; require the pinned 99e383... hash. The canonical project JSON is the older baseline; use the app-managed autosave recovery path.",
        },
    }
    return plan


def serialize_plan(plan):
    return (json.dumps(plan, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")


def write_new_plan(plan, output_path):
    output = Path(output_path)
    payload = serialize_plan(plan)
    if output.exists():
        if output.read_bytes() == payload:
            return output
        raise TimeoutRecoveryError("Continuation path exists with different contents; refusing overwrite.")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + ".tmp")
    if temporary.exists():
        raise TimeoutRecoveryError("A stale temporary continuation file exists; refusing overwrite.")
    try:
        with temporary.open("xb") as target:
            target.write(payload)
            target.flush()
            os.fsync(target.fileno())
        os.replace(temporary, output)
        directory_fd = os.open(output.parent, os.O_RDONLY)
        try:
            os.fsync(directory_fd)
        finally:
            os.close(directory_fd)
    finally:
        if temporary.exists():
            temporary.unlink()
    return output


def bind_from_paths(plan_path, receipt_path, event_path, task_path, replay_path,
                    document_path, initial_document_path, clone_manifest_path,
                    replay_arguments_path, replay_manifest_path):
    proof_paths = [plan_path, receipt_path, event_path, task_path, replay_path,
                   document_path, initial_document_path, clone_manifest_path,
                   replay_arguments_path, replay_manifest_path]
    proofs = _validate_proofs(*proof_paths)
    plan = build_timeout_reconciled_suffix(proofs)
    return plan, proofs


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--parent-plan", type=Path,
                        default=HERE / "full-landscape-v15-reviewed-suffix.commands.json")
    parser.add_argument("--receipt", type=Path,
                        default=EVIDENCE / "native-full-landscape-v15-receipt.json")
    parser.add_argument("--event-log", type=Path,
                        default=EVIDENCE / "native-full-landscape-v15-receipt.json.ndjson")
    parser.add_argument("--task-proof", type=Path,
                        default=EVIDENCE / "native-v15-timeout-replayed-task-result.json")
    parser.add_argument("--replay-proof", type=Path,
                        default=EVIDENCE / "native-v15-timeout-idempotent-replay-result.json")
    parser.add_argument("--document-proof", type=Path,
                        default=EVIDENCE / "native-v15-timeout-replayed-document-result.json")
    parser.add_argument("--initial-document-proof", type=Path,
                        default=EVIDENCE / "native-v15-document-result.json")
    parser.add_argument("--clone-manifest", type=Path,
                        default=EVIDENCE / "native-v15-restart-recovery-clone.json")
    parser.add_argument("--replay-arguments", type=Path,
                        default=EVIDENCE / "native-v15-timeout-idempotent-replay-arguments.json")
    parser.add_argument("--replay-arguments-manifest", type=Path,
                        default=EVIDENCE / "native-v15-timeout-idempotent-replay-manifest.json")
    parser.add_argument("--output", type=Path,
                        default=HERE / "full-landscape-v15-timeout-reconciled-suffix.commands.json")
    parser.add_argument("--output-manifest", type=Path,
                        default=EVIDENCE / "native-v15-timeout-reconciled-suffix-manifest.json")
    args = parser.parse_args(argv)
    try:
        plan, proofs = bind_from_paths(
            args.parent_plan, args.receipt, args.event_log, args.task_proof,
            args.replay_proof, args.document_proof, args.initial_document_proof,
            args.clone_manifest, args.replay_arguments,
            args.replay_arguments_manifest)
        output = write_new_plan(plan, args.output)
        output_bytes = serialize_plan(plan)
        provenance = plan["timeoutRecoveryProvenance"]
        output_manifest = {
            "schema": BINDER_SCHEMA,
            "outputPlanPath": str(output.resolve()),
            "outputPlanSha256": _sha256(output_bytes),
            "remainingStepCount": len(plan["steps"]),
            "completedPrefixCount": provenance["nativeReconciledCompletedPrefixCount"],
            "taskId": plan["taskId"],
            "resourceId": plan["resourceId"],
            "startingDocumentHash": plan["startingDocumentHash"],
            "sourceHashes": provenance["inputHashes"],
            "latestAutosavePath": provenance["durableManagedDocument"]["latestAutosavePath"],
            "latestAutosaveSha256": provenance["durableManagedDocument"]["latestAutosaveSha256"],
            "resumeGate": provenance["durableManagedDocument"]["reopenGate"],
        }
        manifest_path = write_new_plan(output_manifest, args.output_manifest)
    except (TimeoutRecoveryError, continuation.ContinuationError,
            OSError, ValueError) as error:
        print(f"v15 timeout continuation refused: {error}", file=sys.stderr)
        return 2
    provenance = plan["timeoutRecoveryProvenance"]
    print(json.dumps({
        "ok": True,
        "output": str(output),
        "outputSha256": _sha256(output_bytes),
        "manifest": str(manifest_path),
        "remainingSteps": len(plan["steps"]),
        "completedPrefix": provenance["nativeReconciledCompletedPrefixCount"],
        "resourceId": plan["resourceId"],
        "taskId": plan["taskId"],
        "startingDocumentHash": plan["startingDocumentHash"],
        "autosavePath": provenance["durableManagedDocument"]["latestAutosavePath"],
        "autosaveSha256": provenance["durableManagedDocument"]["latestAutosaveSha256"],
        "resumeGate": provenance["durableManagedDocument"]["reopenGate"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
