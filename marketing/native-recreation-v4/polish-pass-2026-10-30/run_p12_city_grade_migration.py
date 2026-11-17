#!/usr/bin/env python3
"""Guard, journal, and audit the P12 city-clip native-grade migration.

Read-only preflight is the default. `execute` requires a fresh task ID and
starting document hash, rechecks the P11 source/targets and task path scope,
then delegates the nine sequential mutations to the existing durable
author_native runner. It never starts/prepares a task or admits source paths.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import sys
import time
from typing import Any


HERE = Path(__file__).resolve().parent
NATIVE_ROOT = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(NATIVE_ROOT))
from native_review_session import WORKER, call  # noqa: E402

from author_native import execute as execute_native_plan  # noqa: E402


FRAGMENT_PATH = HERE / "p11-native-city-grade-repair-fragment.json"
PREFLIGHT_PATH = HERE / "p12-city-grade-readonly-preflight-20261031.json"
INSPECT_CLIP_COMMAND = "timeline.method.inspectTimelineClipById(QString)"
GRADE_COMMAND = "timeline.method.setClipColorGradeParameterForAutomation(QString,QString,double)"
EFFECT_COMMAND = "timeline.method.editClipEffectStackForAutomation(QString,QString,QString,double,bool)"
REPLACE_COMMAND = "timeline.action.replaceTimelineClipSource"
EXPECTED_SCHEMAS = {
    REPLACE_COMMAND: {
        "properties": {
            "clipId", "assetId", "filePath", "displayLabel", "sourceInSeconds",
            "sourceOutSeconds", "posterSource", "fitToFill",
        },
        "required": {"clipId", "assetId", "filePath", "displayLabel"},
    },
    GRADE_COMMAND: {
        "properties": {"clip_id", "parameter", "value"},
        "required": {"clip_id", "parameter", "value"},
    },
    EFFECT_COMMAND: {
        "properties": {"clip_id", "operation", "effect_name", "amount", "enabled"},
        "required": {"clip_id", "operation"},
    },
}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def json_bytes(value: Any) -> bytes:
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_file(path: Path) -> str:
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_once(path: Path, value: Any) -> None:
    """Create a durable JSON receipt without replacing an earlier receipt."""
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    with path.open("xb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    dirfd = os.open(path.parent, os.O_RDONLY)
    try:
        os.fsync(dirfd)
    finally:
        os.close(dirfd)


def read_fragment() -> dict[str, Any]:
    return json.loads(FRAGMENT_PATH.read_text(encoding="utf-8"))


def native_inspector(command_id: str, clip_id: str) -> dict[str, Any]:
    result = call("app.inspect_command", {
        "commandId": command_id,
        "values": {"clip_id": clip_id},
    })
    clip = result.get("clip")
    if result.get("ok") is not True or not isinstance(clip, dict):
        raise RuntimeError(f"Native clip inspection failed for {clip_id}.")
    return clip


def grade_readback(clip_id: str) -> dict[str, Any]:
    return call("timeline.inspect_clip_color_grade", {"clipId": clip_id})


def effect_readback(clip_id: str) -> dict[str, Any]:
    return call("timeline.inspect_clip_effects", {"clipId": clip_id})


def structure_readback(resource_id: str, document_hash: str, clip_id: str) -> dict[str, Any]:
    result = call("timeline.inspect_structure", {
        "scope": "clips",
        "resourceId": resource_id,
        "documentHash": document_hash,
        "clipId": clip_id,
        "maxItems": 2,
    })
    rows = result.get("items")
    if result.get("ok") is not True or not isinstance(rows, list) or len(rows) != 1:
        raise RuntimeError(f"Stable-ID structure readback failed for {clip_id}.")
    return rows[0]


def schema_readback(command_id: str) -> dict[str, Any]:
    result = call("app.command_schema", {"commandId": command_id})
    expected = EXPECTED_SCHEMAS.get(command_id)
    input_schema = result.get("inputSchema", {})
    props = set(input_schema.get("properties", {}))
    required = set(input_schema.get("required", []))
    if (result.get("ok") is not True or result.get("available") is not True or
            result.get("readOnly") is not False or result.get("invokeTool") != "app.invoke_command" or
            result.get("resource") != "timeline-document" or
            result.get("reversal") != "exact-task-baseline" or expected is None or
            props != expected["properties"] or required != expected["required"] or
            input_schema.get("additionalProperties") is not False):
        raise RuntimeError(f"Live command schema diverged from the reviewed contract: {command_id}.")
    return result


def keyframe_digest(keyframes: list[dict[str, Any]]) -> str:
    return sha256_bytes(json_bytes(keyframes))


def inspector_keyframe_digest(keyframes: list[dict[str, Any]]) -> str:
    # The native inspector adds sequenceSeconds as a computed projection field;
    # the serialized P11 keyframe data and mutation guard omit that value.
    authored = [{key: value for key, value in row.items() if key != "sequenceSeconds"}
                for row in keyframes]
    return keyframe_digest(authored)


def numeric_close(actual: Any, expected: Any) -> bool:
    return (isinstance(actual, (int, float)) and not isinstance(actual, bool) and
            math.isfinite(float(actual)) and math.isclose(float(actual), float(expected), rel_tol=0.0, abs_tol=1e-9))


def assert_equal(actual: Any, expected: Any, label: str) -> None:
    if actual != expected:
        raise RuntimeError(f"Guard mismatch for {label}: expected {expected!r}, got {actual!r}.")


def clip_summary(clip: dict[str, Any]) -> dict[str, Any]:
    keyframes = clip.get("keyframes", [])
    if not isinstance(keyframes, list):
        raise RuntimeError(f"Clip {clip.get('clipId')} returned malformed keyframes.")
    return {
        "clipId": clip.get("clipId"),
        "assetId": clip.get("assetId"),
        "trackId": clip.get("trackId"),
        "filePath": clip.get("filePath"),
        "displayLabel": clip.get("displayLabel"),
        "mediaType": clip.get("mediaType"),
        "mediaAnalysisStatus": clip.get("mediaAnalysisStatus"),
        "sourceIntegrityStatus": clip.get("sourceIntegrityStatus"),
        "sourceContentSha256": clip.get("sourceContentSha256"),
        "sourceFileSize": clip.get("sourceFileSize"),
        "sourceWidth": clip.get("sourceWidth"),
        "sourceHeight": clip.get("sourceHeight"),
        "hasAudio": clip.get("hasAudio"),
        "audioLaneIndex": clip.get("audioLaneIndex"),
        "audioDetached": clip.get("audioDetached"),
        "startSeconds": clip.get("startSeconds"),
        "durationSeconds": clip.get("durationSeconds"),
        "trimStartSeconds": clip.get("trimStartSeconds"),
        "trimEndSeconds": clip.get("trimEndSeconds"),
        "playbackRate": clip.get("playbackRate"),
        "frameMode": clip.get("frameMode"),
        "frameScale": clip.get("frameScale"),
        "frameOffsetX": clip.get("frameOffsetX"),
        "frameOffsetY": clip.get("frameOffsetY"),
        "frameRotationDegrees": clip.get("frameRotationDegrees"),
        "frameWidth": clip.get("frameWidth"),
        "frameHeight": clip.get("frameHeight"),
        "frameCornerRadius": clip.get("frameCornerRadius"),
        "clipMask": clip.get("clipMask"),
        "opacity": clip.get("opacity"),
        "effectsEnabled": clip.get("effectsEnabled"),
        "effectStack": clip.get("effectStack"),
        "colorLutPath": clip.get("colorLutPath"),
        "keyframeCount": len(keyframes),
        "keyframeChannels": dict(sorted(Counter(str(item.get("parameter", "")) for item in keyframes).items())),
        "keyframeCanonicalSha256": inspector_keyframe_digest(keyframes),
    }


def validate_target(clip: dict[str, Any], structure: dict[str, Any], guard: dict[str, Any]) -> None:
    assert_equal(clip.get("clipId"), guard["clipId"], f"clip {guard['clipId']} identity")
    assert_equal(clip.get("trackId"), guard["trackId"], f"clip {guard['clipId']} track")
    assert_equal(structure.get("trackId"), guard["trackId"], f"clip {guard['clipId']} structure track")
    assert_equal(structure.get("trackKind"), guard["trackKind"], f"clip {guard['clipId']} track kind")
    assert_equal(clip.get("mediaType"), "video", f"clip {guard['clipId']} media type")
    for field in ("filePath", "displayLabel", "assetId"):
        expected_field = {
            "filePath": "expectedCurrentPath",
            "displayLabel": "label",
            "assetId": "expectedCurrentAssetId",
        }.get(field)
        if expected_field and expected_field in guard:
            assert_equal(clip.get(field), guard[expected_field], f"clip {guard['clipId']} {field}")
        elif field == "filePath":
            assert_equal(clip.get(field), guard["expectedCurrentPath"], f"clip {guard['clipId']} path")
        elif field == "displayLabel":
            assert_equal(clip.get(field), guard["label"], f"clip {guard['clipId']} label")
    for field, expected in (
        ("startSeconds", guard["sequenceStartSeconds"]),
        ("durationSeconds", guard["durationSeconds"]),
        ("trimStartSeconds", guard["trimStartSeconds"]),
        ("trimEndSeconds", guard["trimEndSeconds"]),
    ):
        if not numeric_close(clip.get(field), expected):
            raise RuntimeError(f"Guard mismatch for {guard['clipId']} {field}.")
    frame_fields = {
        "mode": "frameMode", "scale": "frameScale", "offsetX": "frameOffsetX",
        "offsetY": "frameOffsetY", "rotationDegrees": "frameRotationDegrees",
        "width": "frameWidth", "height": "frameHeight", "cornerRadius": "frameCornerRadius",
    }
    for expected_field, actual_field in frame_fields.items():
        expected = guard["frame"][expected_field]
        actual = clip.get(actual_field)
        if isinstance(expected, str):
            assert_equal(actual, expected, f"clip {guard['clipId']} frame.{expected_field}")
        elif not numeric_close(actual, expected):
            raise RuntimeError(f"Guard mismatch for {guard['clipId']} frame.{expected_field}.")
    actual_mask = dict(clip.get("clipMask", {}))
    mask_enabled = actual_mask.pop("enabled", None)
    assert_equal(actual_mask, guard["clipMask"], f"clip {guard['clipId']} mask")
    if mask_enabled is not True:
        raise RuntimeError(f"Guard mismatch for {guard['clipId']} mask enabled state.")
    # The live clip inspector reports evaluated opacity at the current
    # sequence time; P11's serialized project carries the authored base value.
    # Validate base opacity against that guarded saved native baseline below.
    keyframes = clip.get("keyframes", [])
    channels = dict(sorted(Counter(str(item.get("parameter", "")) for item in keyframes).items()))
    assert_equal(len(keyframes), guard["keyframeCount"], f"clip {guard['clipId']} keyframe count")
    assert_equal(channels, guard["keyframeChannels"], f"clip {guard['clipId']} keyframe channels")
    assert_equal(inspector_keyframe_digest(keyframes), guard["keyframeCanonicalSha256"],
                 f"clip {guard['clipId']} keyframe digest")
    assert_equal(clip.get("effectStack", []), guard["effectStack"], f"clip {guard['clipId']} effect stack")
    assert_equal(clip.get("colorLutPath", ""), "", f"clip {guard['clipId']} LUT path")


def make_snapshot(require_task: bool = False) -> dict[str, Any]:
    fragment = read_fragment()
    guard = fragment["guard"]
    doc_before = call("timeline.inspect_document")
    task = call("timeline.inspect_task")
    schemas = {command_id: schema_readback(command_id)
               for command_id in (REPLACE_COMMAND, GRADE_COMMAND, EFFECT_COMMAND)}
    clip_schema = call("app.command_schema", {"commandId": INSPECT_CLIP_COMMAND})
    if (clip_schema.get("ok") is not True or clip_schema.get("available") is not True or
            clip_schema.get("readOnly") is not True or clip_schema.get("invokeTool") != "app.inspect_command" or
            clip_schema.get("inputSchema", {}).get("required") != ["clip_id"]):
        raise RuntimeError("Stable-ID native clip inspector schema changed; refusing the plan.")

    source_clip_id = guard["sourceAnalysisPreflight"]["existingSourceClipId"]
    clip_ids = [source_clip_id] + [item["clipId"] for item in guard["targetClips"]]
    clips: dict[str, dict[str, Any]] = {}
    structures: dict[str, dict[str, Any]] = {}
    for clip_id in clip_ids:
        clips[clip_id] = native_inspector(INSPECT_CLIP_COMMAND, clip_id)
        structures[clip_id] = structure_readback(doc_before["resourceId"], doc_before["documentHash"], clip_id)

    grades = {item["clipId"]: grade_readback(item["clipId"]) for item in guard["targetClips"]}
    effects = {item["clipId"]: effect_readback(item["clipId"]) for item in guard["targetClips"]}
    doc_after = call("timeline.inspect_document")
    task_after = call("timeline.inspect_task")

    assert_equal(doc_before.get("resourceId"), guard["resourceId"], "active Timeline resource")
    assert_equal(doc_before.get("documentHash"), guard["expectedNativeDocumentHash"], "P11 native document hash")
    assert_equal(doc_after.get("documentHash"), doc_before.get("documentHash"), "document hash during read-only preflight")
    assert_equal(doc_after.get("resourceId"), doc_before.get("resourceId"), "resource during read-only preflight")
    assert_equal(doc_before.get("width"), 1920, "sequence width")
    assert_equal(doc_before.get("height"), 1080, "sequence height")
    assert_equal(doc_before.get("frameRate"), 30, "sequence frame rate")

    source = clips[source_clip_id]
    source_guard = guard["sourceAnalysisPreflight"]
    for field, expected in (
        ("filePath", source_guard["expectedCanonicalPath"]),
        ("assetId", "native-trailer:creative-polish-v2/city-10.mp4"),
        ("mediaAnalysisStatus", "ready"),
        ("sourceIntegrityStatus", "verified"),
        ("sourceContentSha256", source_guard["expectedSourceSha256"]),
        ("sourceFileSize", 35971545),
        ("sourceWidth", 1600),
        ("sourceHeight", 900),
    ):
        assert_equal(source.get(field), expected, f"original source {field}")
    source_path = Path(source_guard["expectedCanonicalPath"])
    assert_equal(sha256_file(source_path), source_guard["expectedSourceSha256"], "original source file SHA-256")

    project_path = Path(guard["savedProjectPath"])
    assert_equal(sha256_file(project_path), guard["savedProjectFileSha256"], "P11 saved project file SHA-256")
    baseline_project = json.loads(project_path.read_text(encoding="utf-8"))
    assert_equal(baseline_project.get("timelineId"), guard["timelineId"], "P11 saved timeline ID")
    baseline_map, _ = clip_map(baseline_project)

    target_summaries = {}
    for target_guard in guard["targetClips"]:
        clip_id = target_guard["clipId"]
        validate_target(clips[clip_id], structures[clip_id], target_guard)
        authored_base_opacity = baseline_map[clip_id][1].get("opacity")
        if not numeric_close(authored_base_opacity, target_guard["baseOpacity"]):
            raise RuntimeError(f"Guard mismatch for {clip_id} serialized base opacity.")
        assert_equal(grades[clip_id].get("enabled"), False, f"baseline grade enabled {clip_id}")
        assert_equal(grades[clip_id].get("recipe"), {}, f"baseline grade recipe {clip_id}")
        assert_equal(effects[clip_id].get("effectStack"), [], f"baseline effect stack {clip_id}")
        target_summaries[clip_id] = {
            "native": clip_summary(clips[clip_id]),
            "structure": structures[clip_id],
            "grade": grades[clip_id],
            "effects": effects[clip_id],
        }

    return {
        "schema": "looksy.p12.city-grade-readonly-preflight/v1",
        "status": "preflight-readonly-complete",
        "mutationSent": False,
        "capturedAtUtc": utc_now(),
        "fragmentPath": str(FRAGMENT_PATH),
        "fragmentSha256": sha256_file(FRAGMENT_PATH),
        "worker": {
            "pid": WORKER.get("pid"),
            "binarySha256": WORKER.get("binarySha256"),
            "sourceSha256": WORKER.get("sourceSha256"),
            "dataDirectory": WORKER.get("data"),
        },
        "document": {key: doc_before.get(key) for key in (
            "resourceId", "documentHash", "ready", "width", "height", "frameRate",
            "durationSeconds", "trackCount", "saved", "dirty")},
        "task": {
            "active": task.get("active"),
            "status": task.get("status"),
            "taskId": task.get("taskId"),
            "targetResourceId": task.get("targetResourceId"),
            "targetResourceIds": task.get("targetResourceIds"),
            "nativeTimelineScope": task.get("nativeTimelineScope"),
        },
        "sourceAnalysisPreflight": {
            "clipId": source_clip_id,
            "canonicalPath": source.get("filePath"),
            "assetId": source.get("assetId"),
            "mediaAnalysisStatus": source.get("mediaAnalysisStatus"),
            "sourceIntegrityStatus": source.get("sourceIntegrityStatus"),
            "sourceContentSha256": source.get("sourceContentSha256"),
            "sourceFileSize": source.get("sourceFileSize"),
            "width": source.get("sourceWidth"),
            "height": source.get("sourceHeight"),
            "hasAudio": source.get("hasAudio"),
            "audioLaneIndex": source.get("audioLaneIndex"),
            "audioDetached": source.get("audioDetached"),
            "timelineReady": doc_before.get("ready"),
            "readyAtEndOfRead": doc_after.get("ready"),
        },
        "schemas": schemas,
        "clipInspectorSchema": clip_schema,
        "targets": target_summaries,
        "documentHashAfterRead": doc_after.get("documentHash"),
        "taskAfterRead": {"active": task_after.get("active"), "taskId": task_after.get("taskId"), "status": task_after.get("status")},
    }


def assert_execution_ready(snapshot: dict[str, Any], task_id: str, starting_hash: str) -> None:
    fragment = read_fragment()
    guard = fragment["guard"]
    doc = snapshot["document"]
    task = snapshot["task"]
    assert_equal(doc.get("ready"), True, "Timeline readiness before execution")
    assert_equal(doc.get("documentHash"), starting_hash, "fresh caller-supplied document hash")
    assert_equal(starting_hash, guard["expectedNativeDocumentHash"], "P11 baseline document hash")
    assert_equal(task.get("active"), True, "active native task")
    assert_equal(task.get("status"), "running", "running native task status")
    assert_equal(task.get("taskId"), task_id, "fresh caller-supplied task ID")
    scope = task.get("nativeTimelineScope", {})
    assert_equal(scope.get("targetIds"), [guard["resourceId"]], "task Timeline target scope")
    asset_paths = scope.get("assetPaths", [])
    assert guard["replacementSource"]["canonicalPath"] in asset_paths, (
        "Task path scope does not grant import_media for the original city source; no mutation was sent."
    )
    assert_equal(snapshot["sourceAnalysisPreflight"].get("mediaAnalysisStatus"), "ready", "source analysis readiness")
    assert_equal(snapshot["sourceAnalysisPreflight"].get("sourceIntegrityStatus"), "verified", "source integrity readiness")


def plan_for_task(task_id: str, starting_hash: str) -> dict[str, Any]:
    fragment = read_fragment()
    steps = []
    for action in fragment["actions"]:
        args = action.get("arguments", {})
        if action.get("tool") != "app.invoke_command":
            raise RuntimeError(f"Step {action.get('step')} is not an app.invoke_command action.")
        steps.append({
            "id": f"p12-city-grade-{action['step']:02d}",
            "commandId": args["commandId"],
            "values": args["values"],
        })
    if len(steps) != 9:
        raise RuntimeError("The reviewed P12 migration must contain exactly nine sequential calls.")
    return {
        "schema": "looksy.native-authoring-plan/v1",
        "name": "P12 city source native grade migration",
        "resourceId": fragment["guard"]["resourceId"],
        "taskId": task_id,
        "startingDocumentHash": starting_hash,
        "composition": {"width": 1920, "height": 1080, "fps": 30},
        "steps": steps,
        "sourceFragmentPath": str(FRAGMENT_PATH),
        "sourceFragmentSha256": sha256_file(FRAGMENT_PATH),
    }


def capture_readback(task_id: str, expected_document_hash: str) -> dict[str, Any]:
    fragment = read_fragment()
    guard = fragment["guard"]
    doc = call("timeline.inspect_document")
    task = call("timeline.inspect_task")
    assert_equal(doc.get("resourceId"), guard["resourceId"], "post-edit Timeline resource")
    assert_equal(doc.get("documentHash"), expected_document_hash, "post-edit document hash")
    assert_equal(doc.get("ready"), True, "post-edit Timeline readiness")
    assert_equal(task.get("active"), True, "post-edit active task")
    assert_equal(task.get("taskId"), task_id, "post-edit task ID")
    targets = {}
    for item in guard["targetClips"]:
        clip_id = item["clipId"]
        clip = native_inspector(INSPECT_CLIP_COMMAND, clip_id)
        targets[clip_id] = {
            "native": clip_summary(clip),
            "grade": grade_readback(clip_id),
            "effects": effect_readback(clip_id),
        }
    source_id = guard["sourceAnalysisPreflight"]["existingSourceClipId"]
    source = native_inspector(INSPECT_CLIP_COMMAND, source_id)
    return {
        "schema": "looksy.p12.city-grade-readback/v1",
        "capturedAtUtc": utc_now(),
        "resourceId": doc["resourceId"],
        "documentHash": doc["documentHash"],
        "taskId": task_id,
        "sourceAnalysisPreflight": {
            "clipId": source_id,
            "filePath": source.get("filePath"),
            "mediaAnalysisStatus": source.get("mediaAnalysisStatus"),
            "sourceIntegrityStatus": source.get("sourceIntegrityStatus"),
            "sourceContentSha256": source.get("sourceContentSha256"),
            "sourceWidth": source.get("sourceWidth"),
            "sourceHeight": source.get("sourceHeight"),
        },
        "targets": targets,
    }


def clip_map(document: dict[str, Any]) -> tuple[dict[str, tuple[str, dict[str, Any]]], list[tuple[str, list[str]]]]:
    found: dict[str, tuple[str, dict[str, Any]]] = {}
    tracks = document.get("tracks", [])
    signature: list[tuple[str, list[str]]] = []
    for track in tracks:
        track_id = track.get("trackId", "")
        ids = []
        for clip in track.get("clips", []):
            clip_id = clip.get("clipId")
            if not isinstance(clip_id, str) or clip_id in found:
                raise RuntimeError("Saved native project has missing or duplicate stable clip IDs.")
            found[clip_id] = (track_id, clip)
            ids.append(clip_id)
        signature.append((track_id, ids))
    return found, signature


PROTECTED_TARGET_FIELDS = (
    "clipId", "startSeconds", "durationSeconds", "trimStartSeconds", "trimEndSeconds",
    "playbackRate", "frameMode", "frameScale", "frameOffsetX", "frameOffsetY",
    "frameRotationDegrees", "framePerspectiveX", "framePerspectiveY", "framePerspectiveQuad",
    "frameWidth", "frameHeight", "frameCornerRadius", "frameShadow", "clipMask",
    "sourceMatte", "blendMode", "sourceCropLeft", "sourceCropTop", "sourceCropRight",
    "sourceCropBottom", "opacity", "audioGainDb", "fadeInSeconds", "fadeOutSeconds",
    "effectsEnabled", "keyframes", "transitionInName", "transitionInSeconds",
    "transitionOutName", "transitionOutSeconds", "outputVariantOverrides",
    "colorLutPath", "colorLutEnabled", "videoInputTransformSource",
)
ALLOWED_SOURCE_OR_RECIPE_FIELDS = {
    "assetId", "filePath", "displayLabel", "sourceContentSha256", "sourceIdentitySignature",
    "sourceFileSize", "sourceWidth", "sourceHeight", "hasAudio", "audioLaneIndex",
    "audioDetached", "sourceAudioChannels", "sourceAudioChannelLayout", "sourceColorMatrix",
    "sourceColorRange", "sourceColorTransfer", "sourceColorPrimaries", "videoInputTransform",
    "posterSource", "durationProvisional", "audioPresenceProvisional", "mediaAnalysisStatus",
    "colorGradeRecipe", "colorGradeRecipeJson", "effectStack", "effectSettings", "effectStates",
    "stabilizationAnalysisState", "stabilizationAnalysisSourceSignature",
    "stabilizationAnalysisTrimStartSeconds", "stabilizationAnalysisTrimEndSeconds",
    "stabilizationTransformPath", "stabilizationRenderPath", "stabilizationPreviewPath",
    "compoundError", "compoundStatus", "compoundDocumentPath", "compoundRenderPath",
    "proxyPath", "waveformSource",
}


def audit_native_copy(baseline_path: Path, output_path: Path, readback_path: Path) -> dict[str, Any]:
    fragment = read_fragment()
    guard = fragment["guard"]
    baseline_path = baseline_path.resolve(strict=True)
    output_path = output_path.resolve(strict=True)
    baseline_sha = sha256_file(baseline_path)
    assert_equal(baseline_sha, guard["savedProjectFileSha256"], "P11 baseline file SHA-256")
    before = json.loads(baseline_path.read_text(encoding="utf-8"))
    after = json.loads(output_path.read_text(encoding="utf-8"))
    assert_equal(after.get("timelineId"), guard["timelineId"], "saved copy Timeline ID")
    assert_equal(after.get("sequenceFormat"), before.get("sequenceFormat"), "sequence format")

    before_map, before_signature = clip_map(before)
    after_map, after_signature = clip_map(after)
    assert_equal(after_signature, before_signature, "track order and per-track clip order")
    assert_equal(set(after_map), set(before_map), "stable clip ID set")

    target_ids = {item["clipId"] for item in guard["targetClips"]}
    non_target_changes = []
    for clip_id in sorted(set(before_map) - target_ids):
        if before_map[clip_id] != after_map[clip_id]:
            non_target_changes.append(clip_id)
    if non_target_changes:
        raise RuntimeError("Unexpected non-target clip changes: " + ", ".join(non_target_changes[:8]))

    target_changes: dict[str, list[str]] = {}
    for target_guard in guard["targetClips"]:
        clip_id = target_guard["clipId"]
        before_track, prior = before_map[clip_id]
        after_track, current = after_map[clip_id]
        assert_equal(after_track, before_track, f"clip {clip_id} track membership")
        for field in PROTECTED_TARGET_FIELDS:
            assert_equal(current.get(field), prior.get(field), f"clip {clip_id} protected field {field}")
        assert_equal(current.get("assetId"), "native-trailer:creative-polish-v2/city-10.mp4", f"clip {clip_id} source asset")
        assert_equal(current.get("filePath"), guard["replacementSource"]["canonicalPath"], f"clip {clip_id} source path")
        saved_source_sha = current.get("sourceContentSha256")
        if saved_source_sha not in ("", guard["replacementSource"]["sha256"]):
            raise RuntimeError(f"Saved native copy has an unexpected derived source hash for {clip_id}.")
        assert_equal(current.get("trimStartSeconds"), 0, f"clip {clip_id} source in")
        assert_equal(current.get("trimEndSeconds"), 8, f"clip {clip_id} source out")
        changed = sorted(key for key in set(prior) | set(current) if prior.get(key) != current.get(key))
        unexpected = sorted(set(changed) - ALLOWED_SOURCE_OR_RECIPE_FIELDS)
        target_changes[clip_id] = changed
        if unexpected:
            raise RuntimeError(f"Clip {clip_id} changed fields outside the reviewed source/recipe allowlist: {unexpected}")

    if len(before.get("tracks", [])) != len(after.get("tracks", [])):
        raise RuntimeError("Track count changed during the source/grade migration.")
    for before_track, after_track in zip(before["tracks"], after["tracks"]):
        before_meta = {k: v for k, v in before_track.items() if k != "clips"}
        after_meta = {k: v for k, v in after_track.items() if k != "clips"}
        assert_equal(after_meta, before_meta, f"track metadata {before_track.get('trackId')}")

    readback = json.loads(readback_path.read_text(encoding="utf-8"))
    assert_equal(readback.get("schema"), "looksy.p12.city-grade-readback/v1", "native readback receipt schema")
    assert_equal(readback.get("resourceId"), guard["resourceId"], "native readback resource")
    source_readback = readback["sourceAnalysisPreflight"]
    assert_equal(source_readback.get("filePath"), guard["replacementSource"]["canonicalPath"], "live source readback path")
    assert_equal(source_readback.get("sourceContentSha256"), guard["replacementSource"]["sha256"], "live source readback hash")
    assert_equal(source_readback.get("mediaAnalysisStatus"), "ready", "live source media analysis")
    assert_equal(source_readback.get("sourceIntegrityStatus"), "verified", "live source integrity")

    # The task pauses background integrity scans. A successful source
    # replacement can therefore save the canonical path and metadata while its
    # per-clip derived content hash is empty and the live integrity state is
    # pending. Verify the actual source bytes independently; never promote that
    # physical-file check into a claim that the native clip is verified.
    replacement = guard["replacementSource"]
    physical_source_path = Path(replacement["canonicalPath"]).resolve(strict=True)
    if not physical_source_path.is_file():
        raise RuntimeError(f"Replacement source is not a regular file: {physical_source_path}")
    assert_equal(str(physical_source_path), replacement["canonicalPath"], "canonical physical source path")
    physical_source_sha = sha256_file(physical_source_path)
    assert_equal(physical_source_sha, replacement["sha256"], "physical replacement source SHA-256")
    physical_source_size = physical_source_path.stat().st_size
    expected_source_size = source_readback.get("sourceFileSize")
    if expected_source_size is not None:
        assert_equal(physical_source_size, expected_source_size, "physical replacement source size")
    for target_guard in guard["targetClips"]:
        clip_id = target_guard["clipId"]
        native_clip = after_map[clip_id][1]
        assert_equal(native_clip.get("sourceFileSize"), physical_source_size, f"saved native source size {clip_id}")
        assert_equal(native_clip.get("sourceWidth"), replacement["width"], f"saved native source width {clip_id}")
        assert_equal(native_clip.get("sourceHeight"), replacement["height"], f"saved native source height {clip_id}")

    integrity_pending_targets: list[str] = []
    integrity_verified_targets: list[str] = []

    for target_guard in guard["targetClips"]:
        clip_id = target_guard["clipId"]
        record = readback["targets"][clip_id]
        clip = record["native"]
        assert_equal(clip.get("filePath"), guard["replacementSource"]["canonicalPath"], f"readback source path {clip_id}")
        saved_source_sha = after_map[clip_id][1].get("sourceContentSha256")
        assert_equal(saved_source_sha, clip.get("sourceContentSha256"), f"saved/readback source hash {clip_id}")
        target_source_sha = clip.get("sourceContentSha256")
        target_integrity = clip.get("sourceIntegrityStatus")
        if target_source_sha == "" and target_integrity == "pending":
            integrity_pending_targets.append(clip_id)
        elif target_source_sha == replacement["sha256"] and target_integrity == "verified":
            integrity_verified_targets.append(clip_id)
        else:
            raise RuntimeError(
                f"Target source integrity state is inconsistent for {clip_id}: "
                f"sourceContentSha256={target_source_sha!r}, sourceIntegrityStatus={target_integrity!r}."
            )
        if saved_source_sha not in ("", replacement["sha256"]):
            raise RuntimeError(f"Saved native copy has an unexpected derived source hash for {clip_id}.")
        assert_equal(clip.get("mediaAnalysisStatus"), "ready", f"readback media analysis {clip_id}")
        assert_equal(clip.get("sourceFileSize"), physical_source_size, f"native source size {clip_id}")
        assert_equal(clip.get("sourceWidth"), replacement["width"], f"native source width {clip_id}")
        assert_equal(clip.get("sourceHeight"), replacement["height"], f"native source height {clip_id}")
        grade = record["grade"]
        assert_equal(grade.get("enabled"), True, f"grade enabled {clip_id}")
        recipe = grade.get("recipe", {})
        global_adjustments = recipe.get("globalAdjustments", {})
        for parameter, value in (("exposure", 0.25), ("contrast", 14), ("vibrance", 12)):
            actual = global_adjustments.get(parameter)
            if not isinstance(actual, (int, float)) or isinstance(actual, bool) or not math.isclose(
                float(actual), value, rel_tol=0.0, abs_tol=1e-9
            ):
                raise RuntimeError(f"Grade readback mismatch for {clip_id} {parameter}: {actual!r}.")
        effect_stack = record["effects"].get("effectStack", [])
        expected_effects = ["Black & White"] if clip_id == "d8d47522-2f58-4a32-8e8b-402b091fe657" else []
        assert_equal(effect_stack, expected_effects, f"effect stack readback {clip_id}")
        if expected_effects:
            details = record["effects"].get("effectDetails", [])
            row = next((item for item in details if item.get("effectName", item.get("name")) == "Black & White"), None)
            if row is None or row.get("enabled") is not True or not numeric_close(row.get("amount"), 1.0):
                raise RuntimeError("Native Black & White effect readback is not enabled at amount 1.0.")

    top_level_changes = sorted(key for key in set(before) | set(after)
                               if key != "updatedAt" and before.get(key) != after.get(key)
                               and key not in {"tracks", "clips"})
    if top_level_changes:
        raise RuntimeError(f"Unexpected top-level native project changes: {top_level_changes}")

    return {
        "schema": "looksy.p12.city-grade-native-copy-audit/v2",
        "status": "passed-with-target-source-integrity-pending" if integrity_pending_targets else "passed",
        "auditedAtUtc": utc_now(),
        "baselinePath": str(baseline_path),
        "baselineFileSha256": baseline_sha,
        "outputPath": str(output_path),
        "outputFileSha256": sha256_file(output_path),
        "timelineId": after["timelineId"],
        "readbackPath": str(readback_path.resolve(strict=True)),
        "readbackDocumentHash": readback.get("documentHash"),
        "trackCount": len(after["tracks"]),
        "clipCount": len(after_map),
        "nonTargetClipsChanged": [],
        "targetChangedFields": target_changes,
        "protectedTargetFieldsVerified": list(PROTECTED_TARGET_FIELDS),
        "physicalReplacementSource": {
            "path": str(physical_source_path),
            "sha256": physical_source_sha,
            "sha256MatchesGuard": True,
            "sizeBytes": physical_source_size,
        },
        "sourceIntegrityPendingOnTargets": integrity_pending_targets,
        "sourceIntegrityVerifiedOnTargets": integrity_verified_targets,
        "nativeTargetSourceIntegrityFullyVerified": not integrity_pending_targets,
        "sharedNativeGradeVerifiedOnBothTargets": {"exposure": 0.25, "contrast": 14, "vibrance": 12},
        "monochromeNativeEffectVerified": {"name": "Black & White", "enabled": True, "amount": 1.0},
    }


def command_preflight(args: argparse.Namespace) -> None:
    result = make_snapshot()
    result["preflightGate"] = {
        "documentReady": result["document"].get("ready") is True,
        "taskInactive": result["task"].get("active") is False,
        "sourceAnalysisReady": result["sourceAnalysisPreflight"].get("mediaAnalysisStatus") == "ready",
        "sourceIntegrityVerified": result["sourceAnalysisPreflight"].get("sourceIntegrityStatus") == "verified",
        "targetGuardsMatched": True,
    }
    if args.output:
        write_once(Path(args.output), result)
    print(json.dumps({
        "status": result["status"],
        "mutationSent": False,
        "resourceId": result["document"]["resourceId"],
        "documentHash": result["document"]["documentHash"],
        "documentReady": result["document"]["ready"],
        "taskActive": result["task"]["active"],
        "sourceAnalysis": result["sourceAnalysisPreflight"],
        "schemas": {key: {"available": value["available"], "invokeTool": value["invokeTool"],
                           "resource": value["resource"], "reversal": value["reversal"],
                           "properties": sorted(value["inputSchema"]["properties"]),
                           "required": value["inputSchema"]["required"]}
                    for key, value in result["schemas"].items()},
        "output": args.output,
    }, ensure_ascii=False, indent=2))


def command_execute(args: argparse.Namespace) -> None:
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", args.task_id):
        raise RuntimeError("Supply the exact current root-owned task ID.")
    if not re.fullmatch(r"[0-9a-f]{64}", args.starting_document_hash):
        raise RuntimeError("Supply the exact lowercase current document hash.")
    snapshot = make_snapshot(require_task=True)
    assert_execution_ready(snapshot, args.task_id, args.starting_document_hash)
    plan = plan_for_task(args.task_id, args.starting_document_hash)
    suffix = args.task_id
    plan_path = HERE / f"p12-city-grade-plan-{suffix}.json"
    intent_path = HERE / f"p12-city-grade-intent-{suffix}.json"
    receipt_path = HERE / f"p12-city-grade-receipt-{suffix}.json"
    ndjson_path = HERE / f"p12-city-grade-receipt-{suffix}.ndjson"
    readback_path = HERE / f"p12-city-grade-readback-{suffix}.json"
    for path in (plan_path, intent_path, receipt_path, ndjson_path, readback_path):
        if path.exists():
            raise RuntimeError(f"Preserve existing P12 execution artifact {path}; inspect it before any retry.")
    plan_bytes = (json.dumps(plan, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    write_once(plan_path, plan)
    intent = {
        "schema": "looksy.p12.city-grade-intent/v1",
        "createdAtUtc": utc_now(),
        "status": "prepared-no-mutation-yet",
        "planPath": str(plan_path),
        "planSha256": sha256_bytes(plan_bytes),
        "fragmentPath": str(FRAGMENT_PATH),
        "fragmentSha256": sha256_file(FRAGMENT_PATH),
        "taskId": args.task_id,
        "resourceId": snapshot["document"]["resourceId"],
        "startingDocumentHash": args.starting_document_hash,
        "sourcePathGrantVerified": True,
        "preflight": snapshot,
        "receiptPath": str(receipt_path),
        "ndjsonPath": str(ndjson_path),
        "readbackPath": str(readback_path),
    }
    write_once(intent_path, intent)
    print(f"P12 plan and intent are durable. task={args.task_id} hash={args.starting_document_hash}", flush=True)
    # author_native writes a fsynced invoke_started before each call and an
    # invoke_completed result after it. It refuses to replay an unresolved call.
    execute_native_plan(plan_path, receipt_path, ndjson_path, compact_inspection=True)
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt.get("status") != "authored-awaiting-native-review" or len(receipt.get("steps", [])) != 9:
        raise RuntimeError("The durable authoring receipt is incomplete; stop and reconcile the native task.")
    doc_after = call("timeline.inspect_document")
    readback = capture_readback(args.task_id, doc_after.get("documentHash", ""))
    write_once(readback_path, readback)
    intent["status"] = "authored-readback-captured"
    intent["postMutationDocumentHash"] = doc_after.get("documentHash")
    # Preserve the original intent file as append-only evidence; status is in
    # the terminal receipt instead of rewriting this pre-mutation record.
    summary_path = HERE / f"p12-city-grade-run-summary-{suffix}.json"
    write_once(summary_path, {
        "schema": "looksy.p12.city-grade-run-summary/v1",
        "status": "authored-awaiting-native-copy-and-diff-audit",
        "taskId": args.task_id,
        "startingDocumentHash": args.starting_document_hash,
        "postMutationDocumentHash": doc_after.get("documentHash"),
        "intentPath": str(intent_path),
        "planPath": str(plan_path),
        "receiptPath": str(receipt_path),
        "ndjsonPath": str(ndjson_path),
        "readbackPath": str(readback_path),
        "steps": 9,
    })
    print(f"P12 actions complete; inspect and checkpoint before audit. Readback: {readback_path}", flush=True)


def command_readback(args: argparse.Namespace) -> None:
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", args.task_id):
        raise RuntimeError("Supply the exact current root-owned task ID.")
    if not re.fullmatch(r"[0-9a-f]{64}", args.current_document_hash):
        raise RuntimeError("Supply the exact current lowercase document hash.")
    result = capture_readback(args.task_id, args.current_document_hash)
    path = Path(args.output)
    write_once(path, result)
    print(json.dumps({"status": "readback-captured", "documentHash": result["documentHash"], "path": str(path)}))


def command_audit(args: argparse.Namespace) -> None:
    result = audit_native_copy(Path(args.baseline), Path(args.native_copy), Path(args.readback))
    write_once(Path(args.output), result)
    print(json.dumps(result, ensure_ascii=False, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    preflight = commands.add_parser("preflight", help="Read schemas and guarded source/target state only.")
    preflight.add_argument("--output", default=str(PREFLIGHT_PATH))
    preflight.set_defaults(func=command_preflight)
    execute_parser = commands.add_parser("execute", help="Run the nine journaled mutations under a fresh task.")
    execute_parser.add_argument("--task-id", required=True)
    execute_parser.add_argument("--starting-document-hash", required=True)
    execute_parser.set_defaults(func=command_execute)
    readback = commands.add_parser("readback", help="Capture guarded native state after authoring/checkpoint.")
    readback.add_argument("--task-id", required=True)
    readback.add_argument("--current-document-hash", required=True)
    readback.add_argument("--output", required=True)
    readback.set_defaults(func=command_readback)
    audit = commands.add_parser("audit", help="Compare the saved native copy with P11 and readback receipts.")
    audit.add_argument("--baseline", default=str(read_fragment()["guard"]["savedProjectPath"]))
    audit.add_argument("--native-copy", required=True)
    audit.add_argument("--readback", required=True)
    audit.add_argument("--output", required=True)
    audit.set_defaults(func=command_audit)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
