#!/usr/bin/env python3
"""Read-only audit for the saved P22 native photo-proof edit.

The auditor never edits a native project or calls the editor. It validates the
frozen P21 checkpoint, bound P22 plan, append-only operation receipts, saved
project copy, and the narrowly allowed title/opacity changes. It writes an
audit JSON only after all checks pass.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any


HERE = Path(__file__).resolve().parent
POLISH = HERE.parent
ROOT = HERE.parents[3]
RESOURCE_ID = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
EXPECTED_BASELINE_DOCUMENT_HASH = "8c125de95668c44c2a578721ae5ece46bad59b155391f9bf7655efca677a3c86"
EXPECTED_BASELINE_FILE_SHA256 = "7a99aa8d2fe790a58a20ebced6ff04f0e912dc584e2f34b1bdcfef7a1cbc0ef9"
EXPECTED_FRAGMENT_SHA256 = "e875fcf713166fd192e172d950dd1144351dad488e8467333575da0698def0e2"
EXPECTED_BASELINE_TRACKS = 196
EXPECTED_BASELINE_CLIPS = 1414
EXPECTED_EXISTING_TARGETS = 8
EXPECTED_CREATED_TITLES = 7
EXPECTED_TOTAL_STEPS = 22
EXPECTED_RAIL_OPACITY_KEYS = 68
NUMERIC_TOLERANCE = 1e-8
FPS_TOLERANCE = 1e-6

DEFAULT_BEFORE = POLISH / "native-projects/looksy-polish-pass21-film-polish.json"
DEFAULT_BEFORE_CHECKPOINT = POLISH / "checkpoint-pass21-film-polish-result.json"
DEFAULT_BEFORE_AUDIT = POLISH / "p21-film-layout/pass21-film-polish-native-saved-audit.json"
DEFAULT_FRAGMENT = HERE / "p22-photo-proof.fragment.json"
DEFAULT_AUDIT = HERE / "p22-photo-proof-native-saved-audit.json"

TITLE_FIELD_MAP = {
    "text": "titleText",
    "alignment": "titleAlignment",
    "fontFamily": "titleFontFamily",
    "fontSize": "titleFontSize",
    "fontWeight": "titleFontWeight",
    "italic": "titleItalic",
    "letterSpacing": "titleLetterSpacing",
    "lineHeight": "titleLineHeight",
    "color": "titleColor",
    "positionX": "titlePositionX",
    "positionY": "titlePositionY",
    "backgroundColor": "titleBackgroundColor",
    "shapeType": "titleShapeType",
    "shapeWidth": "titleShapeWidth",
    "shapeHeight": "titleShapeHeight",
    "shapeCornerRadius": "titleShapeCornerRadius",
    "shapeStrokeColor": "titleShapeStrokeColor",
    "shapeStrokeWidth": "titleShapeStrokeWidth",
    "casing": "captionCasing",
    "strokeColor": "captionStrokeColor",
    "strokeWidth": "captionStrokeWidth",
    "shadowEnabled": "captionShadowEnabled",
    "shadowColor": "captionShadowColor",
    "shadowOffsetX": "captionShadowOffsetX",
    "shadowOffsetY": "captionShadowOffsetY",
    "backgroundPadding": "captionBackgroundPadding",
    "backgroundRadius": "captionBackgroundRadius",
    "safeMarginX": "captionSafeMarginX",
    "safeMarginY": "captionSafeMarginY",
}
VARIANT_FIELD_MAP = {
    "alignment": "titleAlignment",
    "fontFamily": "titleFontFamily",
    "fontSize": "titleFontSize",
    "fontWeight": "titleFontWeight",
    "italic": "titleItalic",
    "letterSpacing": "titleLetterSpacing",
    "lineHeight": "titleLineHeight",
    "color": "titleColor",
    "positionX": "titlePositionX",
    "positionY": "titlePositionY",
    "backgroundColor": "titleBackgroundColor",
    "casing": "captionCasing",
    "strokeColor": "captionStrokeColor",
    "strokeWidth": "captionStrokeWidth",
    "shadowEnabled": "captionShadowEnabled",
    "shadowColor": "captionShadowColor",
    "shadowOffsetX": "captionShadowOffsetX",
    "shadowOffsetY": "captionShadowOffsetY",
    "backgroundPadding": "captionBackgroundPadding",
    "backgroundRadius": "captionBackgroundRadius",
    "safeMarginX": "captionSafeMarginX",
    "safeMarginY": "captionSafeMarginY",
}
CUSTOM_TITLE_KEYS = set(TITLE_FIELD_MAP) - {"text"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_json(path: Path) -> Any:
    require(path.is_file(), f"Required evidence is not present yet: {path}")
    return json.loads(path.read_text())


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def canonical_hash(value: Any) -> str:
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
    return hashlib.sha256(payload).hexdigest()


def nearly_equal(left: Any, right: Any) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return left is right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return math.isfinite(float(left)) and math.isfinite(float(right)) and math.isclose(
            float(left), float(right), rel_tol=0.0, abs_tol=NUMERIC_TOLERANCE
        )
    if isinstance(left, dict) and isinstance(right, dict):
        return set(left) == set(right) and all(nearly_equal(left[key], right[key]) for key in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(nearly_equal(a, b) for a, b in zip(left, right))
    return left == right


def assert_equal(actual: Any, expected: Any, where: str) -> None:
    require(nearly_equal(actual, expected), f"{where}: expected {expected!r}, got {actual!r}")


def clip_indexes(document: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, str]]:
    clips: dict[str, dict[str, Any]] = {}
    parents: dict[str, str] = {}
    for track in document["tracks"]:
        for clip in track.get("clips", []):
            clip_id = clip["clipId"]
            require(clip_id not in clips, f"Duplicate native clip ID: {clip_id}")
            clips[clip_id] = clip
            parents[clip_id] = track["trackId"]
    return clips, parents


def track_index(document: dict[str, Any]) -> dict[str, dict[str, Any]]:
    tracks = {track["trackId"]: track for track in document["tracks"]}
    require(len(tracks) == len(document["tracks"]), "Duplicate track IDs in native project.")
    return tracks


def display_label(clip: dict[str, Any]) -> str:
    shape = clip.get("titleShapeType", "")
    if shape == "rectangle":
        return "Rectangle"
    if shape == "rounded-rectangle":
        return "Rounded Rectangle"
    if shape == "ellipse":
        return "Ellipse"
    label = " ".join(str(clip.get("titleText", "")).split())[:48]
    return label or "Title"


def mapped_title_fields(values: dict[str, Any]) -> dict[str, Any]:
    unknown = set(values) - set(TITLE_FIELD_MAP)
    require(not unknown, f"Unexpected title fields in P22 plan: {sorted(unknown)}")
    return {TITLE_FIELD_MAP[key]: value for key, value in values.items()}


def normalized_channel(keys: list[dict[str, Any]], parameter: str) -> list[dict[str, Any]]:
    selected = sorted((key for key in keys if key.get("parameter") == parameter),
                      key=lambda key: float(key["seconds"]))
    return [{"localSeconds": key["seconds"], "value": key["value"],
             "interpolation": key["interpolation"]} for key in selected]


def resolve_refs(value: Any, created_ids: dict[str, str]) -> Any:
    if isinstance(value, dict):
        if set(value) == {"result", "field"}:
            creator, field = value["result"], value["field"]
            require(field == "createdClipId" and creator in created_ids,
                    f"Unresolved created-clip reference: {value!r}")
            return created_ids[creator]
        return {key: resolve_refs(nested, created_ids) for key, nested in value.items()}
    if isinstance(value, list):
        return [resolve_refs(item, created_ids) for item in value]
    return value


def contains_result_reference(value: Any) -> bool:
    if isinstance(value, dict):
        return set(value) == {"result", "field"} or any(contains_result_reference(item) for item in value.values())
    if isinstance(value, list):
        return any(contains_result_reference(item) for item in value)
    return False


def step_maps(plan: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    existing: dict[str, dict[str, Any]] = {}
    creates: dict[str, dict[str, Any]] = {}
    animations: dict[str, dict[str, Any]] = {}
    for step in plan["steps"]:
        command = step["commandId"].split(".")[-1]
        values = step["values"]
        if command == "createTimelineTitle":
            require(step["id"] not in creates, f"Duplicate create step ID: {step['id']}")
            creates[step["id"]] = step
        elif command == "setTimelineClipTitle":
            clip_ref = values["clipId"]
            require(isinstance(clip_ref, str), f"P22 title setter has nonstable target: {step['id']}")
            entry = existing.setdefault(clip_ref, {"fields": {}, "variantFields": {}, "steps": []})
            native_fields = mapped_title_fields(values["values"])
            entry["fields"].update(native_fields)
            entry["steps"].append(step["id"])
            for key, value in values["values"].items():
                variant_field = VARIANT_FIELD_MAP.get(key)
                if variant_field:
                    entry["variantFields"][variant_field] = value
            if CUSTOM_TITLE_KEYS.intersection(values["values"]):
                entry["fields"]["titleTemplateKey"] = "custom"
        elif command == "setTimelineClipAnimation":
            require(step["id"] not in animations, f"Duplicate animation step ID: {step['id']}")
            animations[step["id"]] = step
        else:
            raise AssertionError(f"Unexpected P22 operation: {command}")
    require(len(existing) == EXPECTED_EXISTING_TARGETS,
            f"Expected eight existing badge targets, found {len(existing)}.")
    require(len(creates) == EXPECTED_CREATED_TITLES,
            f"Expected seven title creates, found {len(creates)}.")
    require(len(animations) == EXPECTED_CREATED_TITLES,
            f"Expected seven opacity actions, found {len(animations)}.")
    require(len(plan["steps"]) == EXPECTED_TOTAL_STEPS,
            f"Expected {EXPECTED_TOTAL_STEPS} native steps, found {len(plan['steps'])}.")
    return existing, creates, animations


def verify_baseline(plan: dict[str, Any], fragment: dict[str, Any], fragment_path: Path,
                    before_path: Path, before_checkpoint_path: Path,
                    before_audit_path: Path) -> tuple[dict[str, Any], dict[str, dict[str, Any]], dict[str, str], dict[str, dict[str, Any]]]:
    require(sha256(fragment_path) == EXPECTED_FRAGMENT_SHA256,
            "The frozen P22 proposal fragment changed; refuse to audit against a moving plan.")
    require(plan.get("resourceId") == RESOURCE_ID, "P22 bound plan resource mismatch.")
    require(isinstance(plan.get("taskId"), str) and plan["taskId"], "P22 bound plan has no task ID.")
    require(plan.get("startingDocumentHash") == EXPECTED_BASELINE_DOCUMENT_HASH,
            "P22 plan did not bind to the exact saved P21 native hash.")
    binding = plan.get("bindingEvidence", {})
    require(binding.get("nativeAutosaveHash") == EXPECTED_BASELINE_DOCUMENT_HASH,
            "P22 native bind evidence does not name the exact P21 native document hash.")
    assert_equal(plan.get("steps"), fragment.get("steps"), "P22 bound operations vs frozen proposal fragment")
    require(fragment.get("resourceId") == RESOURCE_ID, "P22 proposal resource mismatch.")
    require(fragment.get("startingDocumentHash") == EXPECTED_BASELINE_DOCUMENT_HASH,
            "P22 fragment planning hash mismatch.")

    baseline_sha = sha256(before_path)
    require(baseline_sha == EXPECTED_BASELINE_FILE_SHA256,
            f"Frozen P21 project hash changed: {baseline_sha}")
    checkpoint = read_json(before_checkpoint_path)
    require(checkpoint.get("readbackVerified") is True and checkpoint.get("ok") is True,
            "P21 baseline checkpoint is not a verified native readback.")
    require(checkpoint.get("resourceId") == RESOURCE_ID and
            checkpoint.get("documentHash") == EXPECTED_BASELINE_DOCUMENT_HASH and
            checkpoint.get("fileSha256") == EXPECTED_BASELINE_FILE_SHA256 and
            checkpoint.get("outputPath") == str(before_path.resolve()),
            "P21 project bytes/checkpoint/resource/hash do not form the frozen baseline.")
    before = read_json(before_path)
    require(before.get("timelineId") == RESOURCE_ID.split(":", 1)[1], "P21 timeline ID mismatch.")
    require(before.get("activeOutputVariant") == "landscape", "P21 project is not landscape-active.")
    require(before.get("sequenceFormat", {}).get("frameRate") == 30,
            "P22 rail source-curve audit expects a 30fps sequence.")
    require(len(before.get("tracks", [])) == EXPECTED_BASELINE_TRACKS,
            "Unexpected P21 baseline track inventory.")
    before_clips, before_parents = clip_indexes(before)
    require(len(before_clips) == EXPECTED_BASELINE_CLIPS, "Unexpected P21 baseline clip inventory.")

    saved_audit = read_json(before_audit_path)
    require(sha256(before_audit_path) == fragment["planningSnapshot"]["savedAudit"]["sha256"],
            "The P21 saved audit used by P22 planning changed.")
    require(saved_audit.get("saved", {}).get("fileSha256") == EXPECTED_BASELINE_FILE_SHA256 and
            saved_audit.get("saved", {}).get("documentHash") == EXPECTED_BASELINE_DOCUMENT_HASH and
            saved_audit.get("nativeInventory", {}).get("savedClipCount") == EXPECTED_BASELINE_CLIPS and
            saved_audit.get("nativeInventory", {}).get("trackCount") == EXPECTED_BASELINE_TRACKS,
            "P21 saved audit does not describe the exact frozen P21 checkpoint.")
    snapshot = fragment.get("planningSnapshot", {})
    require(snapshot.get("path") == str(before_path.resolve()) and
            snapshot.get("sha256") == EXPECTED_BASELINE_FILE_SHA256 and
            snapshot.get("documentHash") == EXPECTED_BASELINE_DOCUMENT_HASH and
            snapshot.get("trackCount") == EXPECTED_BASELINE_TRACKS and
            snapshot.get("clipCount") == EXPECTED_BASELINE_CLIPS and
            snapshot.get("activeOutputVariant") == "landscape",
            "P22 planning snapshot no longer identifies the frozen P21 native project.")
    verify_target_preconditions(fragment, before, before_clips, before_parents)
    verify_destination_tracks(fragment, before)
    return before, before_clips, before_parents, track_index(before)


def verify_target_preconditions(fragment: dict[str, Any], before: dict[str, Any],
                                clips: dict[str, dict[str, Any]], parents: dict[str, str]) -> None:
    snapshots = fragment.get("executionPreconditions", {}).get("targetPreconditions", [])
    require(len(snapshots) == EXPECTED_EXISTING_TARGETS,
            f"Expected eight frozen badge snapshots, got {len(snapshots)}.")
    for snapshot in snapshots:
        clip_id = snapshot["clipId"]
        require(clip_id in clips, f"P21 target snapshot is missing from baseline: {clip_id}")
        clip = clips[clip_id]
        require(parents[clip_id] == snapshot["trackId"], f"Baseline target track mismatch: {clip_id}")
        require(before.get("activeOutputVariant") == "landscape", "Baseline variant changed during guard check.")
        for key, expected in snapshot.items():
            if key in {"clipId", "trackId", "displayLabel", "opacityKeyframeCount", "keyframesSha256",
                       "landscapeTitleStyle", "landscapeNonTitleGeometrySha256"}:
                continue
            assert_equal(clip.get(key), expected, f"P22 exact P21 target snapshot {clip_id}.{key}")
        track = next(track for track in before["tracks"] if track["trackId"] == parents[clip_id])
        assert_equal(track.get("displayLabel"), snapshot["displayLabel"], f"P22 {clip_id} display label")
        keys = clip.get("keyframes", [])
        assert_equal(sum(key.get("parameter") == "opacity" for key in keys),
                     snapshot["opacityKeyframeCount"], f"P22 {clip_id} baseline opacity key count")
        assert_equal(canonical_hash(keys), snapshot["keyframesSha256"],
                     f"P22 {clip_id} complete baseline keyframe hash")
        landscape = clip.get("outputVariantOverrides", {}).get("landscape", {})
        assert_equal({key: landscape.get(key) for key in snapshot["landscapeTitleStyle"]},
                     snapshot["landscapeTitleStyle"], f"P22 {clip_id} P21 landscape title style")
        non_title = {key: value for key, value in landscape.items()
                     if not key.startswith("title") and key not in {"captionBackgroundPadding", "captionBackgroundRadius"}}
        assert_equal(canonical_hash(non_title), snapshot["landscapeNonTitleGeometrySha256"],
                     f"P22 {clip_id} P21 landscape non-title geometry")


def verify_destination_tracks(fragment: dict[str, Any], before: dict[str, Any]) -> None:
    tracks = track_index(before)
    guards = fragment.get("executionPreconditions", {}).get("destinationTrackPreconditions", [])
    require(len(guards) == EXPECTED_CREATED_TITLES,
            f"Expected seven destination track guards, found {len(guards)}.")
    expected_interval = (27.0, 32.0)
    for guard in guards:
        track_id = guard["trackId"]
        require(track_id in tracks, f"P22 destination track is absent from P21: {track_id}")
        track = tracks[track_id]
        for key in ("displayLabel", "laneIndex", "kind", "role", "visible", "locked"):
            assert_equal(track.get(key), guard[key], f"P22 destination track guard {track_id}.{key}")
        require(len(track.get("clips", [])) == guard["existingClipCount"],
                f"P22 destination lane clip count changed before editing: {track_id}")
        require(track.get("locked") is False and track.get("visible") is True,
                f"P22 destination lane is not visible/editable: {track_id}")
        interval = guard.get("requiredEmptyHalfOpenIntervalSeconds")
        assert_equal(interval, list(expected_interval), f"P22 destination interval for {track_id}")
        overlaps = []
        for clip in track.get("clips", []):
            start = float(clip.get("startSeconds", 0.0))
            duration = float(clip.get("durationSeconds", 0.0))
            if start < expected_interval[1] and start + duration > expected_interval[0]:
                overlaps.append(clip["clipId"])
        assert_equal(overlaps, guard["overlappingClipIds"], f"P22 destination overlap guard {track_id}")
        require(not overlaps, f"P22 planned lane interval is not empty: {track_id}/{overlaps}")


def verify_admission(plan: dict[str, Any], admission_path: Path) -> dict[str, Any]:
    admission = read_json(admission_path)
    task_id = plan["taskId"]
    require(admission.get("taskId") == task_id and admission.get("targetResourceId") == RESOURCE_ID,
            "P22 admission receipt task/resource mismatch.")
    hashes = {resource.get("id"): resource.get("beforeHash") for resource in admission.get("resources", [])}
    require(hashes.get(RESOURCE_ID) == EXPECTED_BASELINE_DOCUMENT_HASH,
            "P22 task was not admitted against the exact P21 document hash.")
    require(admission.get("resources"), "P22 admission receipt has no resource grant.")
    return {"path": str(admission_path.resolve()), "taskId": task_id,
            "resourceId": RESOURCE_ID, "beforeDocumentHash": hashes[RESOURCE_ID]}


def verify_source_files(fragment: dict[str, Any]) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
    rows = []
    drift = []
    for source in fragment.get("creativeReference", {}).get("sourceFiles", []):
        path = Path(source["path"]).resolve()
        require(path.is_file(), f"Missing P22 creative source: {path}")
        actual = sha256(path)
        row = {"path": str(path), "plannedSha256": source["sha256"], "actualSha256": actual}
        if actual != source["sha256"]:
            # Concurrent timeline UI work landed after the proposal was frozen.
            # It is not used to claim pixel parity; retain the drift visibly,
            # while still requiring the creative source and command schema to
            # match exactly.
            require(path == ROOT / "apps/desktop/qml/workspaces/TimelineView.qml",
                    f"P22 creative source changed since plan: {path}")
            drift.append(row)
        else:
            rows.append(row)
    require(rows, "P22 plan has no source code provenance hashes.")
    return rows, drift


def verify_receipts(plan: dict[str, Any], plan_path: Path, receipt_path: Path,
                    creates: dict[str, dict[str, Any]]) -> tuple[
                        dict[str, dict[str, Any]], dict[str, str], list[str], list[dict[str, Any]]
                    ]:
    plan_sha = sha256(plan_path)
    expected = {step["id"]: step for step in plan["steps"]}
    started: dict[str, list[dict[str, Any]]] = {}
    completed: dict[str, list[dict[str, Any]]] = {}
    failures: list[dict[str, Any]] = []
    read_failures: list[dict[str, Any]] = []
    first_invoke_line: int | None = None
    plan_opened = []
    for line_number, line in enumerate(receipt_path.read_text().splitlines(), start=1):
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("planSha256") is not None:
            require(row.get("planSha256") == plan_sha,
                    f"P22 receipt line {line_number} is bound to a different plan SHA.")
        if row.get("event") == "plan_opened":
            plan_opened.append(row)
            continue
        if row.get("event") == "read_call_failed":
            read_failures.append({"line": line_number, "tool": row.get("tool"),
                                  "arguments": row.get("arguments"), "error": row.get("error")})
            continue
        if row.get("event") == "invoke_failed":
            failures.append(row)
        if row.get("event") not in {"invoke_started", "invoke_completed"}:
            continue
        if first_invoke_line is None:
            first_invoke_line = line_number
        require(row.get("planSha256") == plan_sha,
                f"P22 receipt line {line_number} has a different bound-plan SHA.")
        step_id = row.get("stepId")
        require(step_id in expected, f"Unexpected P22 receipt step at line {line_number}: {step_id}")
        require(row.get("commandId") == expected[step_id]["commandId"],
                f"P22 receipt command mismatch for {step_id}")
        (started if row["event"] == "invoke_started" else completed).setdefault(step_id, []).append(row)

    require(len(plan_opened) == 1, "P22 receipt must contain one plan_opened record.")
    if first_invoke_line is not None:
        require(all(item["line"] < first_invoke_line for item in read_failures),
                "P22 receipt contains a failed read-only preflight after native mutation began.")
    require(plan_opened[0].get("planSha256") == plan_sha and
            plan_opened[0].get("stepCount") == EXPECTED_TOTAL_STEPS,
            "P22 plan_opened receipt does not match the bound plan and step count.")
    require(not failures, f"P22 receipt contains failed invocations: {[row.get('stepId') for row in failures]}")
    require(set(started) == set(expected),
            f"P22 invocation starts mismatch: missing={sorted(set(expected)-set(started))}; extra={sorted(set(started)-set(expected))}")
    require(set(completed) == set(expected),
            f"P22 invocation completions mismatch: missing={sorted(set(expected)-set(completed))}; extra={sorted(set(completed)-set(expected))}")
    for step_id in expected:
        require(len(started[step_id]) == len(completed[step_id]) == 1,
                f"P22 step has repeated or ambiguous outcomes: {step_id}")

    created_ids: dict[str, str] = {}
    for step_id, step in creates.items():
        result = completed[step_id][0].get("result", {})
        require(result.get("ok") is True, f"P22 native title creation did not succeed: {step_id}")
        clip_id = result.get("createdClipId")
        require(isinstance(clip_id, str) and clip_id, f"P22 create receipt lacks createdClipId: {step_id}")
        require(clip_id not in created_ids.values(), f"P22 create receipt reused a stable ID: {clip_id}")
        require(result.get("trackId") == step["values"]["trackId"],
                f"P22 created title receipt returned the wrong lane: {step_id}")
        created_ids[step_id] = clip_id

    symbolic_steps: list[str] = []
    for step_id, step in expected.items():
        wanted = resolve_refs(step["values"], created_ids)
        for kind, rows in (("started", started), ("completed", completed)):
            actual = rows[step_id][0].get("values")
            if nearly_equal(actual, wanted):
                continue
            if contains_result_reference(step["values"]):
                assert_equal(actual, step["values"], f"P22 {kind} symbolic clip reference {step_id}")
                if step_id not in symbolic_steps:
                    symbolic_steps.append(step_id)
            else:
                assert_equal(actual, wanted, f"P22 {kind} arguments {step_id}")
        result = completed[step_id][0].get("result", {})
        require(result.get("ok") is True, f"P22 native step did not succeed: {step_id}")
        require(result.get("commandId") == step["commandId"], f"P22 result command mismatch: {step_id}")
    return completed, created_ids, symbolic_steps, read_failures


def verify_variant_changes(old: dict[str, Any], new: dict[str, Any],
                           expected_fields: dict[str, Any], clip_id: str) -> None:
    old_variants = old.get("outputVariantOverrides", {})
    new_variants = new.get("outputVariantOverrides", {})
    require(set(old_variants) == set(new_variants), f"P22 changed output variants for {clip_id}")
    for variant in old_variants:
        before = old_variants[variant]
        after = new_variants[variant]
        changed = {key for key in set(before) | set(after)
                   if not nearly_equal(before.get(key), after.get(key))}
        if variant != "landscape":
            require(not changed, f"P22 changed non-landscape override {clip_id}/{variant}: {sorted(changed)}")
            continue
        require(changed <= set(expected_fields),
                f"P22 changed unrelated landscape override fields for {clip_id}: {sorted(changed-set(expected_fields))}")
        for key, wanted in expected_fields.items():
            assert_equal(after.get(key), wanted, f"P22 {clip_id} landscape.{key}")


def verify_existing_targets(before_clips: dict[str, dict[str, Any]], before_parents: dict[str, str],
                            after_clips: dict[str, dict[str, Any]], after_parents: dict[str, str],
                            expected: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    rows = []
    for clip_id, spec in expected.items():
        require(clip_id in before_clips and clip_id in after_clips,
                f"P22 existing badge disappeared: {clip_id}")
        old, new = before_clips[clip_id], after_clips[clip_id]
        expected_fields = dict(spec["fields"])
        if "titleText" in expected_fields:
            expected_fields["displayLabel"] = display_label({**old, **expected_fields})
        allowed = set(expected_fields) | {"outputVariantOverrides"}
        changed = {key for key in set(old) | set(new)
                   if not nearly_equal(old.get(key), new.get(key))}
        require(changed <= allowed,
                f"P22 changed unapproved serialized fields on badge {clip_id}: {sorted(changed-allowed)}")
        for field, wanted in expected_fields.items():
            assert_equal(new.get(field), wanted, f"P22 badge {clip_id}.{field}")
        assert_equal(after_parents[clip_id], before_parents[clip_id], f"P22 badge track {clip_id}")
        assert_equal(new.get("keyframes", []), old.get("keyframes", []),
                     f"P22 badge keyframes must remain byte-equivalent {clip_id}")
        verify_variant_changes(old, new, spec["variantFields"], clip_id)
        rows.append({"clipId": clip_id, "trackId": before_parents[clip_id],
                     "changedFields": sorted(changed), "keyframesUnchanged": True,
                     "landscapeTitleFields": spec["variantFields"]})
    return rows


def smoothstep5(value: float, start: float, end: float) -> float:
    if end <= start:
        return 1.0 if value >= end else 0.0
    u = min(1.0, max(0.0, (value - start) / (end - start)))
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def out4(value: float, start: float, end: float) -> float:
    if end <= start:
        return 1.0 if value >= end else 0.0
    u = min(1.0, max(0.0, (value - start) / (end - start)))
    return 1.0 - pow(1.0 - u, 4.0)


def rail_child_index(step_id: str) -> int:
    # Each source flex child owns one label and, except the final child, its
    # following arrow. Paired label/arrow opacity uses the same stagger index.
    if step_id in {"create-rail-camera", "create-rail-arrow-raw-linear"}:
        return 0
    if step_id in {"create-rail-linear", "create-rail-arrow-linear-looksy"}:
        return 1
    if step_id in {"create-rail-looksy", "create-rail-arrow-looksy-srgb"}:
        return 2
    if step_id == "create-rail-srgb":
        return 3
    raise AssertionError(f"Unknown P22 rail child: {step_id}")


def verify_created_titles(plan: dict[str, Any], creates: dict[str, dict[str, Any]],
                          animations: dict[str, dict[str, Any]], created_ids: dict[str, str],
                          after_clips: dict[str, dict[str, Any]], after_parents: dict[str, str],
                          baseline_ids: set[str], fps: int) -> list[dict[str, Any]]:
    require(set(created_ids) == set(creates), "P22 receipt-created ID set differs from create steps.")
    new_ids = set(after_clips) - baseline_ids
    require(new_ids == set(created_ids.values()),
            f"P22 unexpected new clips: extra={sorted(new_ids-set(created_ids.values()))}, missing={sorted(set(created_ids.values())-new_ids)}")
    animation_by_creator = {}
    for step_id, step in animations.items():
        ref = step["values"]["clipId"]
        require(isinstance(ref, dict) and ref.get("field") == "createdClipId",
                f"P22 curve is not receipt-bound to a native created ID: {step_id}")
        creator = ref.get("result")
        require(creator in creates and creator not in animation_by_creator,
                f"P22 opacity curve has unknown/duplicate create reference: {step_id}")
        animation_by_creator[creator] = (step_id, step)
    require(set(animation_by_creator) == set(creates), "Each P22 created title must have one opacity setter.")

    rows = []
    for create_id, step in creates.items():
        clip_id = created_ids[create_id]
        clip = after_clips[clip_id]
        values = step["values"]
        require(clip.get("mediaType") == "title", f"P22 created non-title clip: {clip_id}")
        assert_equal(after_parents[clip_id], values["trackId"], f"P22 created title lane {clip_id}")
        assert_equal(clip.get("startSeconds"), values["startSeconds"], f"P22 created title start {clip_id}")
        assert_equal(clip.get("durationSeconds"), values["durationSeconds"], f"P22 created title duration {clip_id}")
        assert_equal(clip.get("trimStartSeconds"), 0.0, f"P22 created title trim start {clip_id}")
        assert_equal(clip.get("trimEndSeconds"), values["durationSeconds"], f"P22 created title trim end {clip_id}")
        title_values = dict(values.get("values", {}))
        native_fields = mapped_title_fields(title_values)
        for key, wanted in native_fields.items():
            assert_equal(clip.get(key), wanted, f"P22 created title {clip_id}.{key}")
        assert_equal(clip.get("displayLabel"), display_label({**clip, **native_fields}),
                     f"P22 created title display label {clip_id}")
        if CUSTOM_TITLE_KEYS.intersection(title_values):
            assert_equal(clip.get("titleTemplateKey"), "custom", f"P22 created title template {clip_id}")
        landscape = clip.get("outputVariantOverrides", {}).get("landscape", {})
        require(bool(landscape), f"P22 created title lacks landscape variant state: {clip_id}")
        for key, wanted in title_values.items():
            variant_field = VARIANT_FIELD_MAP.get(key)
            if variant_field:
                assert_equal(landscape.get(variant_field), wanted,
                             f"P22 created title {clip_id} landscape.{variant_field}")
        require(not set(clip.get("outputVariantOverrides", {})) - {"landscape"},
                f"P22 created title has unexpected non-landscape variants: {clip_id}")

        animation_id, animation = animation_by_creator[create_id]
        channel = animation["values"]["channels"][0]
        require(len(animation["values"]["channels"]) == 1 and channel.get("parameter") == "opacity",
                f"P22 created title has unexpected animation channel: {clip_id}")
        expected_keys = channel["keyframes"]
        actual_keys = normalized_channel(clip.get("keyframes", []), "opacity")
        assert_equal(actual_keys, expected_keys, f"P22 created title opacity curve {clip_id}")
        unrelated = [key for key in clip.get("keyframes", []) if key.get("parameter") != "opacity"]
        require(not unrelated, f"P22 title has unplanned non-opacity animation: {clip_id}")

        child = rail_child_index(create_id)
        require(len(actual_keys) == len(expected_keys),
                f"P22 rail opacity curve changed key count for {create_id}")
        expected_holds = {round(0.5 + child * 0.2, 8), 5.0}
        for index, key in enumerate(actual_keys):
            local_time = float(key["localSeconds"])
            require(math.isclose(local_time * fps, round(local_time * fps),
                                 rel_tol=0.0, abs_tol=FPS_TOLERANCE),
                    f"P22 rail key {index} is not frame-aligned for {create_id}: {local_time}")
            expected_interpolation = "hold" if round(local_time, 8) in expected_holds else "linear"
            assert_equal(key["interpolation"], expected_interpolation,
                         f"P22 rail interpolation {create_id}/{index}")
            expected_value = (out4(local_time, 0.0, 0.45) *
                              out4(local_time, child * 0.2, 0.5 + child * 0.2) *
                              (1.0 - smoothstep5(local_time, 4.65, 5.0)))
            assert_equal(key["value"], expected_value,
                         f"P22 source-matched opacity knot {create_id}/{index}")

        rows.append({"createStepId": create_id, "animationStepId": animation_id,
                     "clipId": clip_id, "trackId": after_parents[clip_id],
                     "titleText": clip.get("titleText"), "startSeconds": clip["startSeconds"],
                     "durationSeconds": clip["durationSeconds"], "opacityKeyCount": len(actual_keys),
                     "opacityKnotsMatchSourceFormula": True})
    return rows


def verify_project_structure(before: dict[str, Any], after: dict[str, Any],
                              before_clips: dict[str, dict[str, Any]], before_parents: dict[str, str],
                              after_clips: dict[str, dict[str, Any]], after_parents: dict[str, str],
                              existing_targets: dict[str, dict[str, Any]],
                              created_ids: set[str]) -> dict[str, Any]:
    require(after.get("timelineId") == before.get("timelineId") == RESOURCE_ID.split(":", 1)[1],
            "P22 changed timeline identity.")
    require(after.get("activeOutputVariant") == before.get("activeOutputVariant") == "landscape",
            "P22 changed the active output variant.")
    assert_equal(after.get("sequenceFormat"), before.get("sequenceFormat"), "P22 sequence format")
    require(len(after["tracks"]) == len(before["tracks"]) == EXPECTED_BASELINE_TRACKS,
            "P22 added, removed, or reordered the number of timeline tracks.")
    old_meta = [{key: value for key, value in track.items() if key != "clips"} for track in before["tracks"]]
    new_meta = [{key: value for key, value in track.items() if key != "clips"} for track in after["tracks"]]
    assert_equal(new_meta, old_meta, "P22 track metadata/order")
    require(set(before_clips) <= set(after_clips), "P22 removed an existing timeline clip.")
    require(len(after_clips) == len(before_clips) + EXPECTED_CREATED_TITLES,
            f"P22 saved clip count {len(after_clips)} differs from expected {len(before_clips)+EXPECTED_CREATED_TITLES}.")

    targets = set(existing_targets)
    untouched = set(before_clips) - targets
    for clip_id in untouched:
        assert_equal(after_clips[clip_id], before_clips[clip_id], f"P22 untouched original clip {clip_id}")
        assert_equal(after_parents[clip_id], before_parents[clip_id], f"P22 untouched original parent {clip_id}")
    for old_track, new_track in zip(before["tracks"], after["tracks"]):
        track_id = old_track["trackId"]
        old_ids = [clip["clipId"] for clip in old_track.get("clips", [])]
        new_ids = [clip["clipId"] for clip in new_track.get("clips", [])
                   if clip["clipId"] not in created_ids]
        assert_equal(new_ids, old_ids, f"P22 preserved existing clip order in {track_id}")

    require(set(before) == set(after), "P22 changed top-level project schema keys.")
    for key in set(before) - {"tracks", "updatedAt"}:
        assert_equal(after[key], before[key], f"P22 top-level project field {key}")
    return {
        "trackCount": len(after["tracks"]),
        "baselineClipCount": len(before_clips),
        "savedClipCount": len(after_clips),
        "existingBadgeTargets": len(targets),
        "untouchedOriginalClipCount": len(untouched),
        "createdTitleCount": len(created_ids),
        "noTrackAddedRemovedOrReordered": True,
        "allUntouchedOriginalClipsAndPerTrackOrderUnchanged": True,
        "allTopLevelFieldsUnchangedExceptTracksAndUpdatedAt": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True, help="Bound P22 native-authoring plan JSON")
    parser.add_argument("--admission", type=Path, required=True, help="P22 task admission receipt JSON")
    parser.add_argument("--receipt", type=Path, required=True, help="Append-only P22 execution receipt NDJSON")
    parser.add_argument("--checkpoint", type=Path, required=True, help="Verified P22 native save readback JSON")
    parser.add_argument("--after", type=Path, required=True, help="Saved P22 native project JSON")
    parser.add_argument("--before", type=Path, default=DEFAULT_BEFORE)
    parser.add_argument("--before-checkpoint", type=Path, default=DEFAULT_BEFORE_CHECKPOINT)
    parser.add_argument("--before-audit", type=Path, default=DEFAULT_BEFORE_AUDIT)
    parser.add_argument("--fragment", type=Path, default=DEFAULT_FRAGMENT)
    parser.add_argument("--audit", type=Path, default=DEFAULT_AUDIT)
    args = parser.parse_args()

    plan_path = args.plan.resolve()
    admission_path = args.admission.resolve()
    receipt_path = args.receipt.resolve()
    checkpoint_path = args.checkpoint.resolve()
    after_path = args.after.resolve()
    before_path = args.before.resolve()
    before_checkpoint_path = args.before_checkpoint.resolve()
    before_audit_path = args.before_audit.resolve()
    fragment_path = args.fragment.resolve()
    audit_path = args.audit.resolve()

    plan = read_json(plan_path)
    fragment = read_json(fragment_path)
    before, before_clips, before_parents, before_tracks = verify_baseline(
        plan, fragment, fragment_path, before_path, before_checkpoint_path, before_audit_path
    )
    admission_summary = verify_admission(plan, admission_path)
    source_hashes, source_drift = verify_source_files(fragment)
    existing, creates, animations = step_maps(plan)
    completed, created_ids, symbolic_steps, read_failures = verify_receipts(
        plan, plan_path, receipt_path, creates
    )

    checkpoint = read_json(checkpoint_path)
    require(checkpoint.get("ok") is True and checkpoint.get("readbackVerified") is True,
            "P22 save checkpoint is not a verified native readback.")
    require(checkpoint.get("resourceId") == RESOURCE_ID and
            checkpoint.get("outputPath") == str(after_path),
            "P22 checkpoint output path/resource differs from the audited project.")
    require(checkpoint.get("fileSha256") == sha256(after_path),
            "Saved P22 project bytes differ from native readback checkpoint.")
    require(checkpoint.get("clipCount") == EXPECTED_BASELINE_CLIPS + EXPECTED_CREATED_TITLES,
            "P22 checkpoint clip count does not include exactly seven new titles.")
    after = read_json(after_path)
    after_clips, after_parents = clip_indexes(after)
    fps = int(before["sequenceFormat"]["frameRate"])

    existing_rows = verify_existing_targets(before_clips, before_parents, after_clips, after_parents, existing)
    created_rows = verify_created_titles(plan, creates, animations, created_ids,
                                         after_clips, after_parents, set(before_clips), fps)
    assert_equal(sum(row["opacityKeyCount"] for row in created_rows), EXPECTED_RAIL_OPACITY_KEYS,
                 "P22 total source-matched rail opacity key count")
    structure = verify_project_structure(before, after, before_clips, before_parents,
                                         after_clips, after_parents, existing, set(created_ids.values()))
    for create_id, step in creates.items():
        track_id = step["values"]["trackId"]
        require(track_id in before_tracks and track_id in track_index(after),
                f"P22 title used a new/unavailable track: {track_id}")
        assert_equal(after_parents[created_ids[create_id]], track_id,
                     f"P22 created title parent from receipt {create_id}")

    require(not audit_path.exists(), f"Refusing to overwrite existing P22 audit: {audit_path}")
    result = {
        "schema": "looksy.p22-photo-native-saved-audit/v1",
        "auditKind": "read-only saved native state, exact P21 baseline, P22 task receipt and bound-plan audit",
        "productionCallsMadeByAuditor": False,
        "plan": {"path": str(plan_path), "sha256": sha256(plan_path), "taskId": plan["taskId"],
                 "resourceId": RESOURCE_ID, "stepCount": len(plan["steps"]),
                 "fragmentPath": str(fragment_path), "fragmentSha256": sha256(fragment_path)},
        "admission": admission_summary,
        "baseline": {"path": str(before_path), "fileSha256": sha256(before_path),
                     "documentHash": EXPECTED_BASELINE_DOCUMENT_HASH,
                     "checkpointPath": str(before_checkpoint_path),
                     "savedAuditPath": str(before_audit_path)},
        "saved": {"path": str(after_path), "fileSha256": sha256(after_path),
                  "documentHash": checkpoint.get("documentHash"),
                  "taskStateHash": checkpoint.get("taskStateHash"),
                  "readbackVerified": True},
        "creativeSourceHashes": source_hashes,
        "creativeSourceHashDrift": source_drift,
        "nativeInventory": structure,
        "existingComparisonBadges": {"count": len(existing_rows), "rows": existing_rows,
                                      "allKeyframesUnchanged": True},
        "createdPhotoRailTitles": {"count": len(created_rows), "rows": created_rows,
                                   "allCreatedIdsReceiptBound": True,
                                   "allCurvesMatchAuthoredSparseSourceKnots": True},
        "receipt": {"path": str(receipt_path), "sha256": sha256(receipt_path),
                    "completedStepCount": len(completed), "createdStableIds": created_ids,
                    "symbolicCreatedClipReferenceSteps": symbolic_steps,
                    "readOnlyPreflightFailuresBeforeAnyMutation": read_failures,
                    "invocationFailures": 0},
        "proofLimits": [
            "This audit proves saved editable title and opacity state plus the native operation receipt chain. It does not prove rendered pixel layout, crossfade behavior, or native preview/export parity; root must review the native render separately.",
            "The seven-step CAMERA RAW → LINEAR LIGHT → LOOKSY COLOR → sRGB rail is authored explanatory typography. Its labels and source-matched opacity knots do not prove that the photo engine executed those processing stages.",
            "The BEFORE/AFTER badges are comparison labels. The existing image plates remain the assets authored in P21; this audit does not establish their source-image provenance or that a live RAW recipe produced the comparison.",
            "Opacity curves are checked at their sparse authored local-time knots against the source formula. Native linear interpolation between knots is not asserted to be frame-exact with Remotion at every 30fps output frame.",
            "TimelineView.qml changed after the P22 proposal snapshot. This readback audit does not use that drifted file to claim native preview pixels or rendering parity; the frozen Remotion creative sources and setTimelineClipTitle schema still match their planned hashes.",
        ],
    }
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    audit_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    print(f"P22 saved native audit passed: {len(existing_rows)} badge styles, "
          f"{len(created_rows)} receipt-bound rail titles, "
          f"{structure['untouchedOriginalClipCount']} untouched original clips.")
    print(f"Audit: {audit_path}")


if __name__ == "__main__":
    main()
