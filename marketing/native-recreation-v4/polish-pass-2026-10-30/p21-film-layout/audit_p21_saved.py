#!/usr/bin/env python3
"""Read-only audit of the saved P21 Film edit, bound plan, and native receipts.

The script never edits the native project. It writes its audit JSON only after
all checks pass. The expected P21 checkpoint/project/receipt files are produced
by the native authoring workflow, not by this auditor.
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
EXPECTED_TASK_ID = "90532fb6-f84b-4b58-9d20-461aa4a31709"
EXPECTED_BASELINE_DOCUMENT_HASH = "cca3b9d7dff74e4190be0fd8faf7c8eefdfcbbd567515b956b14d98a2d74f149"
EXPECTED_BASELINE_FILE_SHA256 = "c23cd2936aeb971e8c183d7435059d24bdf89719c8722bd23f2bac6487839fc9"
EXPECTED_BASELINE_CLIPS = 1410
EXPECTED_EXISTING_TARGETS = 12
EXPECTED_CREATED_CLIPS = 4
FPS_TOLERANCE = 1e-6
NUMERIC_TOLERANCE = 1e-8

DEFAULTS = {
    "plan": POLISH / "pass21-film-v3-bound.json",
    "fragment": HERE / "p21-film-caption-comparison-v3.fragment.json",
    "before": POLISH / "native-projects/looksy-polish-pass19-canvas-polish.json",
    "before_checkpoint": POLISH / "checkpoint-pass19-canvas-polish-result.json",
    "admission": POLISH / "pass21-film-admission-result.json",
    "checkpoint": POLISH / "checkpoint-pass21-film-polish-result.json",
    "receipt": POLISH / "pass21-film-v3.execution.ndjson",
    "after": POLISH / "native-projects/looksy-polish-pass21-film-polish.json",
    "audit": HERE / "pass21-film-polish-native-saved-audit.json",
}

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
TIMING_FIELDS = {"startSeconds", "durationSeconds", "trimStartSeconds", "trimEndSeconds"}


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
            require(clip_id not in clips, f"Duplicate clip ID in saved project: {clip_id}")
            clips[clip_id] = clip
            parents[clip_id] = track["trackId"]
    return clips, parents


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


def normalized_channel(keys: list[dict[str, Any]], parameter: str) -> list[dict[str, Any]]:
    selected = sorted((key for key in keys if key.get("parameter") == parameter),
                      key=lambda key: float(key["seconds"]))
    return [
        {"localSeconds": key["seconds"], "value": key["value"],
         "interpolation": key["interpolation"]}
        for key in selected
    ]


def mapped_title_fields(values: dict[str, Any]) -> dict[str, Any]:
    unknown = set(values) - set(TITLE_FIELD_MAP)
    require(not unknown, f"Unexpected P21 title properties: {sorted(unknown)}")
    return {TITLE_FIELD_MAP[key]: value for key, value in values.items()}


def resolve_refs(value: Any, created_ids: dict[str, str]) -> Any:
    if isinstance(value, dict):
        if set(value) == {"result", "field"}:
            creator = value["result"]
            field = value["field"]
            require(field == "createdClipId" and creator in created_ids,
                    f"Unresolved created-clip reference: {value!r}")
            return created_ids[creator]
        return {key: resolve_refs(nested, created_ids) for key, nested in value.items()}
    if isinstance(value, list):
        return [resolve_refs(item, created_ids) for item in value]
    return value


def verify_target_preconditions(plan: dict[str, Any], before: dict[str, Any],
                                before_parents: dict[str, str]) -> None:
    clips, _ = clip_indexes(before)
    snapshots = plan.get("targetPreconditions", [])
    require(len(snapshots) == 10, f"Expected ten hashed caption snapshots, found {len(snapshots)}.")
    for target in snapshots:
        clip_id = target["clipId"]
        require(clip_id in clips, f"P21 precondition clip is missing from P19: {clip_id}")
        clip = clips[clip_id]
        snapshot = target["preconditionSnapshot"]
        require(before_parents[clip_id] == snapshot["trackId"], f"P21 bound source track differs for {clip_id}")
        for key, expected in snapshot.items():
            if key in {"trackId", "keyframesSha256", "landscapeOverrideSha256"}:
                continue
            assert_equal(clip.get(key), expected, f"P21 baseline guard {clip_id}.{key}")
        assert_equal(canonical_hash(clip.get("keyframes", [])), snapshot["keyframesSha256"],
                     f"P21 baseline guard {clip_id}.keyframesSha256")
        landscape = clip.get("outputVariantOverrides", {}).get("landscape", {})
        assert_equal(canonical_hash(landscape), snapshot["landscapeOverrideSha256"],
                     f"P21 baseline guard {clip_id}.landscapeOverrideSha256")


def expected_step_maps(plan: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]]]:
    existing: dict[str, dict[str, Any]] = {}
    created: dict[str, dict[str, Any]] = {}
    for step in plan["steps"]:
        action = step["commandId"].split(".")[-1]
        values = step["values"]
        if action in {"createTimelineTitle", "createTimelineGraphic"}:
            created[step["id"]] = step
            continue
        clip_ref = values.get("clipId")
        if isinstance(clip_ref, dict):
            continue
        clip_id = clip_ref
        entry = existing.setdefault(clip_id, {
            "allowedFields": set(), "fields": {}, "variantFields": {},
            "channels": {}, "trackId": None, "titleStyleApplied": False,
        })
        if action == "setTimelineClipTitle":
            title_values = values["values"]
            fields = mapped_title_fields(title_values)
            entry["fields"].update(fields)
            entry["allowedFields"].update(fields)
            if "text" in title_values:
                entry["allowedFields"].add("displayLabel")
            for key, expected in title_values.items():
                variant_field = VARIANT_FIELD_MAP.get(key)
                if variant_field:
                    entry["variantFields"][variant_field] = expected
            entry["titleStyleApplied"] |= bool(CUSTOM_TITLE_KEYS.intersection(title_values))
            if entry["titleStyleApplied"]:
                entry["allowedFields"].add("titleTemplateKey")
        elif action == "setTimelineClipTiming":
            entry["trackId"] = values["trackId"]
            entry["fields"].update({
                "startSeconds": values["startSeconds"],
                "durationSeconds": values["durationSeconds"],
            })
            entry["allowedFields"].update({"startSeconds", "durationSeconds"})
        elif action == "setTimelineClipAnimation":
            entry["allowedFields"].add("keyframes")
            for channel in values["channels"]:
                parameter = channel["parameter"]
                require(parameter not in entry["channels"],
                        f"Repeated animation channel in bound plan: {clip_id}/{parameter}")
                entry["channels"][parameter] = channel["keyframes"]
        else:
            raise AssertionError(f"Unexpected P21 action: {action}")
    require(len(existing) == EXPECTED_EXISTING_TARGETS,
            f"Expected {EXPECTED_EXISTING_TARGETS} existing targets, found {len(existing)}.")
    require(len(created) == EXPECTED_CREATED_CLIPS,
            f"Expected {EXPECTED_CREATED_CLIPS} created clips, found {len(created)}.")
    return existing, created


def contains_result_reference(value: Any) -> bool:
    if isinstance(value, dict):
        return set(value) == {"result", "field"} or any(contains_result_reference(item) for item in value.values())
    if isinstance(value, list):
        return any(contains_result_reference(item) for item in value)
    return False


def verify_receipts(path: Path, plan: dict[str, Any], plan_sha: str
                    ) -> tuple[dict[str, dict[str, Any]], dict[str, str], list[str]]:
    expected = {step["id"]: step for step in plan["steps"]}
    started: dict[str, list[dict[str, Any]]] = {}
    completed: dict[str, list[dict[str, Any]]] = {}
    failures: list[dict[str, Any]] = []
    parsed_rows = 0
    for line_number, line in enumerate(path.read_text().splitlines(), start=1):
        if not line.strip():
            continue
        row = json.loads(line)
        parsed_rows += 1
        if row.get("event") == "invoke_failed":
            failures.append(row)
        if row.get("event") not in {"invoke_started", "invoke_completed"}:
            continue
        step_id = row.get("stepId")
        require(row.get("planSha256") == plan_sha,
                f"Receipt line {line_number} is bound to a different plan hash.")
        require(step_id in expected, f"Unexpected P21 receipt step ID at line {line_number}: {step_id}")
        step = expected[step_id]
        require(row.get("commandId") == step["commandId"], f"P21 receipt command mismatch for {step_id}")
        table = started if row["event"] == "invoke_started" else completed
        table.setdefault(step_id, []).append(row)

    require(not failures, f"P21 native receipt reports failed operations: {[r.get('stepId') for r in failures]}")
    require(set(started) == set(expected),
            f"P21 receipt starts mismatch: missing={sorted(set(expected)-set(started))}, extra={sorted(set(started)-set(expected))}")
    require(set(completed) == set(expected),
            f"P21 receipt completion mismatch: missing={sorted(set(expected)-set(completed))}, extra={sorted(set(completed)-set(expected))}")
    for step_id in expected:
        require(len(started[step_id]) == 1 and len(completed[step_id]) == 1,
                f"P21 step has repeated or ambiguous outcomes: {step_id}")

    created_ids: dict[str, str] = {}
    # Resolve create IDs first so the subsequent animation step is checked against
    # the exact ID returned by the native authoring operation.
    for step_id, step in expected.items():
        if step["commandId"].endswith(("createTimelineTitle", "createTimelineGraphic")):
            result = completed[step_id][0].get("result", {})
            require(result.get("ok") is True, f"P21 create did not succeed: {step_id}")
            clip_id = result.get("createdClipId")
            require(isinstance(clip_id, str) and clip_id,
                    f"Native create receipt has no stable createdClipId: {step_id}")
            require(clip_id not in created_ids.values(), f"Duplicate create ID in P21 receipt: {clip_id}")
            created_ids[step_id] = clip_id

    symbolic_reference_steps: list[str] = []
    for step_id, step in expected.items():
        wanted_values = resolve_refs(step["values"], created_ids)
        for kind, rows in (("started", started), ("completed", completed)):
            actual_values = rows[step_id][0].get("values")
            if nearly_equal(actual_values, wanted_values):
                continue
            # The authoring receipt records the symbolic bound operation for
            # this one reference while the executor resolves it using the
            # creator receipt. The saved-project curve audit below verifies the
            # exact created clip ID received the requested animation.
            if contains_result_reference(step["values"]):
                assert_equal(actual_values, step["values"],
                             f"P21 {kind} symbolic receipt values for {step_id}")
                if step_id not in symbolic_reference_steps:
                    symbolic_reference_steps.append(step_id)
            else:
                assert_equal(actual_values, wanted_values, f"P21 {kind} receipt values for {step_id}")
        result = completed[step_id][0].get("result", {})
        require(result.get("ok") is True, f"P21 operation did not succeed: {step_id}")
        require(result.get("commandId") == step["commandId"], f"P21 receipt result command mismatch: {step_id}")
    return completed, created_ids, symbolic_reference_steps


def verify_variant_changes(old: dict[str, Any], new: dict[str, Any],
                           expected_fields: dict[str, Any], clip_id: str) -> None:
    old_variants = old.get("outputVariantOverrides", {})
    new_variants = new.get("outputVariantOverrides", {})
    require(set(old_variants) == set(new_variants), f"P21 changed output-variant set for {clip_id}")
    for variant in old_variants:
        old_values = old_variants[variant]
        new_values = new_variants[variant]
        changed = {key for key in set(old_values) | set(new_values)
                   if not nearly_equal(old_values.get(key), new_values.get(key))}
        if variant != "landscape":
            require(not changed, f"P21 changed non-landscape overrides for {clip_id}: {variant}/{sorted(changed)}")
            continue
        require(changed <= set(expected_fields),
                f"Unexpected landscape override edits for {clip_id}: {sorted(changed-set(expected_fields))}")
        for field, wanted in expected_fields.items():
            assert_equal(new_values.get(field), wanted, f"{clip_id} landscape.{field}")


def verify_existing_targets(before_clips: dict[str, dict[str, Any]], before_parents: dict[str, str],
                            after_clips: dict[str, dict[str, Any]], after_parents: dict[str, str],
                            expected: dict[str, dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for clip_id, spec in expected.items():
        require(clip_id in before_clips and clip_id in after_clips,
                f"P21 existing target is missing after save: {clip_id}")
        old, new = before_clips[clip_id], after_clips[clip_id]
        fields = dict(spec["fields"])
        if "displayLabel" in spec["allowedFields"]:
            fields["displayLabel"] = display_label({**old, **fields})
        if "titleTemplateKey" in spec["allowedFields"]:
            fields["titleTemplateKey"] = "custom"

        diffs = {key for key in set(old) | set(new) if not nearly_equal(old.get(key), new.get(key))}
        allowed = set(spec["allowedFields"]) | {"outputVariantOverrides"}
        require(diffs <= allowed,
                f"Unexpected P21 existing-target fields for {clip_id}: {sorted(diffs-allowed)}")
        for field, wanted in fields.items():
            assert_equal(new.get(field), wanted, f"P21 {clip_id}.{field}")

        expected_parent = spec["trackId"] or before_parents[clip_id]
        assert_equal(after_parents[clip_id], expected_parent, f"P21 {clip_id} parent track")
        old_keys = old.get("keyframes", [])
        new_keys = new.get("keyframes", [])
        for parameter, wanted in spec["channels"].items():
            assert_equal(normalized_channel(new_keys, parameter), wanted,
                         f"P21 {clip_id} {parameter} keyframes")
        touched = set(spec["channels"])
        old_unrelated = [key for key in old_keys if key.get("parameter") not in touched]
        new_unrelated = [key for key in new_keys if key.get("parameter") not in touched]
        assert_equal(new_unrelated, old_unrelated, f"P21 {clip_id} unrelated animation channels")
        if "keyframes" not in spec["allowedFields"]:
            assert_equal(new_keys, old_keys, f"P21 {clip_id} preserved keyframes")

        verify_variant_changes(old, new, spec["variantFields"], clip_id)
        rows.append({
            "clipId": clip_id,
            "changedFields": sorted(diffs),
            "fromTrackId": before_parents[clip_id],
            "toTrackId": after_parents[clip_id],
            "requestedChannels": {parameter: len(keys) for parameter, keys in spec["channels"].items()},
        })
    return rows


def verify_created_clips(plan: dict[str, Any], created_steps: dict[str, dict[str, Any]],
                         created_ids: dict[str, str], after_clips: dict[str, dict[str, Any]],
                         after_parents: dict[str, str], before_ids: set[str]) -> list[dict[str, Any]]:
    require(set(created_ids) == set(created_steps), "P21 created IDs differ from planned create steps")
    new_ids = set(after_clips) - before_ids
    require(new_ids == set(created_ids.values()),
            f"Unexpected new clip IDs: extra={sorted(new_ids-set(created_ids.values()))}, missing={sorted(set(created_ids.values())-new_ids)}")
    rows: list[dict[str, Any]] = []
    for step_id, step in created_steps.items():
        clip_id = created_ids[step_id]
        clip = after_clips[clip_id]
        values = step["values"]
        require(clip.get("mediaType") == "title", f"P21 created non-title clip: {clip_id}")
        assert_equal(after_parents[clip_id], values["trackId"], f"P21 created {clip_id} track")
        assert_equal(clip.get("startSeconds"), values["startSeconds"], f"P21 created {clip_id} start")
        assert_equal(clip.get("durationSeconds"), values["durationSeconds"], f"P21 created {clip_id} duration")
        assert_equal(clip.get("trimStartSeconds"), 0.0, f"P21 created {clip_id} trimStart")
        assert_equal(clip.get("trimEndSeconds"), values["durationSeconds"], f"P21 created {clip_id} trimEnd")

        title_values = dict(values.get("values", {}))
        if step["commandId"].endswith("createTimelineGraphic"):
            title_values["shapeType"] = values["shapeType"]
            title_values["text"] = ""
        mapped = mapped_title_fields(title_values)
        for field, wanted in mapped.items():
            assert_equal(clip.get(field), wanted, f"P21 created {clip_id}.{field}")
        assert_equal(clip.get("displayLabel"), display_label({**clip, **mapped}),
                     f"P21 created {clip_id}.displayLabel")
        if CUSTOM_TITLE_KEYS.intersection(title_values):
            assert_equal(clip.get("titleTemplateKey"), "custom", f"P21 created {clip_id}.titleTemplateKey")

        landscape = clip.get("outputVariantOverrides", {}).get("landscape", {})
        require(landscape, f"P21 created clip is missing landscape overrides: {clip_id}")
        for key, wanted in title_values.items():
            variant_field = VARIANT_FIELD_MAP.get(key)
            if variant_field:
                assert_equal(landscape.get(variant_field), wanted,
                             f"P21 created {clip_id} landscape.{variant_field}")
        require(not set(clip.get("outputVariantOverrides", {})) - {"landscape"},
                f"P21 created clip has unexpected non-landscape variants: {clip_id}")
        rows.append({
            "stepId": step_id,
            "clipId": clip_id,
            "trackId": after_parents[clip_id],
            "mediaType": clip["mediaType"],
            "shapeType": clip.get("titleShapeType", ""),
            "startSeconds": clip["startSeconds"],
            "durationSeconds": clip["durationSeconds"],
            "titleText": clip.get("titleText", ""),
        })
    return rows


def smoothstep5(value: float, start: float, end: float) -> float:
    if end <= start:
        return 1.0 if value >= end else 0.0
    u = min(1.0, max(0.0, (value - start) / (end - start)))
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def verify_before_fade_source(plan: dict[str, Any], expected: dict[str, dict[str, Any]]) -> dict[str, Any]:
    source = ROOT / "marketing/animated-trailer/src/creative-launch/FilmPolish.tsx"
    text = source.read_text()
    require(text.count("opacity: 1 - smooth(s, 6.5, 7.3)") == 2,
            "P21 source no longer has two 6.5–7.3s Before fade expressions.")
    channel_targets = [(clip_id, keys)
                       for clip_id, spec in expected.items()
                       for parameter, keys in spec["channels"].items()
                       if parameter == "opacity"]
    require(len(channel_targets) == 1,
            f"Expected one existing BEFORE opacity target, found {len(channel_targets)}.")
    copy_id, _ = channel_targets[0]
    # The second fade is on the created BEFORE plate. Its exact ID is resolved
    # from the create receipt; the plan expresses both fade curves identically.
    animation_steps = [step for step in plan["steps"]
                       if step["commandId"].endswith("setTimelineClipAnimation")]
    require(len(animation_steps) == 2, "Expected separate BEFORE text and plate opacity actions.")
    first_curve = animation_steps[0]["values"]["channels"][0]["keyframes"]
    second_curve = animation_steps[1]["values"]["channels"][0]["keyframes"]
    assert_equal(first_curve, second_curve, "P21 BEFORE copy/plate source opacity curve")
    require(len(first_curve) == 28, f"Expected 28 source-sampled opacity keys, found {len(first_curve)}")
    expected_times = [0.0, 6.5] + [frame / 30.0 for frame in range(196, 221)] + [8.0]
    assert_equal([key["localSeconds"] for key in first_curve], expected_times,
                 "P21 BEFORE opacity keyframe timing coverage")
    for index, key in enumerate(first_curve):
        seconds = float(key["localSeconds"])
        require(math.isclose(seconds * 30.0, round(seconds * 30.0), abs_tol=FPS_TOLERANCE),
                f"P21 opacity key {index} is not on a 30fps local frame: {seconds}")
        assert_equal(key["interpolation"], "linear", f"P21 opacity key {index} interpolation")
        expected_value = 1.0 - smoothstep5(seconds, 6.5, 7.3)
        assert_equal(key["value"], expected_value, f"P21 source opacity key {index}")
    return {
        "sourceFile": str(source),
        "sourceSha256": sha256(source),
        "sourceExpressionCount": 2,
        "existingCopyClipId": copy_id,
        "localCurveStartSeconds": 6.5,
        "localCurveEndSeconds": 7.3,
        "frameRate": 30,
        "keyCount": len(first_curve),
        "matchesRemotionQuinticAtEveryAuthoredKey": True,
        "linearBetweenAuthoredSamples": True,
    }


def verify_created_plate_fade(plan: dict[str, Any], created_ids: dict[str, str],
                              after_clips: dict[str, dict[str, Any]]) -> dict[str, Any]:
    plate_step = next((step for step in plan["steps"]
                       if step["id"] == "comparison-badge-fade-before-plate"), None)
    require(plate_step is not None, "P21 plan is missing the native BEFORE plate fade step.")
    clip_ref = plate_step["values"]["clipId"]
    require(isinstance(clip_ref, dict) and clip_ref.get("result") == "comparison-badge-plate-before",
            "P21 plate fade is no longer receipt-bound to the created BEFORE plate.")
    plate_id = created_ids.get(clip_ref["result"])
    require(plate_id in after_clips, f"P21 created BEFORE plate is missing from saved project: {plate_id}")
    channel = plate_step["values"]["channels"][0]
    actual = normalized_channel(after_clips[plate_id].get("keyframes", []), channel["parameter"])
    assert_equal(actual, channel["keyframes"], "P21 created BEFORE plate fade curve")
    unrelated = [key for key in after_clips[plate_id].get("keyframes", [])
                 if key.get("parameter") != channel["parameter"]]
    require(not unrelated, f"P21 created BEFORE plate has unexpected animation channels: {plate_id}")
    return {"createdPlateClipId": plate_id, "parameter": channel["parameter"],
            "keyCount": len(actual), "matchesReceiptAndBoundCurve": True}


def verify_document_structure(before: dict[str, Any], after: dict[str, Any],
                              before_clips: dict[str, dict[str, Any]], before_parents: dict[str, str],
                              after_clips: dict[str, dict[str, Any]], after_parents: dict[str, str],
                              created_ids: set[str], expected: dict[str, dict[str, Any]]) -> dict[str, Any]:
    require(after.get("timelineId") == before.get("timelineId") == RESOURCE_ID.split(":", 1)[1],
            "P21 changed timeline identity.")
    require(after.get("activeOutputVariant") == before.get("activeOutputVariant") == "landscape",
            "P21 changed the active output variant.")
    require(after.get("sequenceFormat") == before.get("sequenceFormat"),
            "P21 changed sequence format.")
    require(len(after["tracks"]) == len(before["tracks"]), "P21 added or removed timeline tracks.")
    old_track_metadata = [{key: value for key, value in track.items() if key != "clips"}
                          for track in before["tracks"]]
    new_track_metadata = [{key: value for key, value in track.items() if key != "clips"}
                          for track in after["tracks"]]
    assert_equal(new_track_metadata, old_track_metadata, "P21 track metadata and global order")
    require(set(before_clips) <= set(after_clips), "P21 removed an original clip.")
    require(len(after_clips) == len(before_clips) + EXPECTED_CREATED_CLIPS,
            f"P21 clip count {len(after_clips)} != expected {len(before_clips)+EXPECTED_CREATED_CLIPS}.")

    target_ids = set(expected)
    untouched_ids = set(before_clips) - target_ids
    for clip_id in untouched_ids:
        assert_equal(after_clips[clip_id], before_clips[clip_id], f"P21 untouched clip {clip_id}")
        assert_equal(after_parents[clip_id], before_parents[clip_id], f"P21 untouched clip track {clip_id}")
    for track_before, track_after in zip(before["tracks"], after["tracks"]):
        track_id = track_before["trackId"]
        old_ids = [clip["clipId"] for clip in track_before.get("clips", []) if clip["clipId"] not in target_ids]
        new_ids = [clip["clipId"] for clip in track_after.get("clips", [])
                   if clip["clipId"] not in target_ids and clip["clipId"] not in created_ids]
        assert_equal(new_ids, old_ids, f"P21 preserved non-target clip order in {track_id}")

    top_keys = set(before) | set(after)
    require(set(before) == set(after), "P21 changed top-level project schema keys.")
    for key in top_keys - {"tracks", "updatedAt"}:
        assert_equal(after[key], before[key], f"P21 top-level project field {key}")
    return {
        "trackCount": len(after["tracks"]),
        "baselineClipCount": len(before_clips),
        "savedClipCount": len(after_clips),
        "existingTargetCount": len(target_ids),
        "untouchedOriginalClipCount": len(untouched_ids),
        "createdClipCount": len(created_ids),
        "trackMetadataAndOrderUnchanged": True,
        "untouchedOriginalClipsAndPerTrackOrderUnchanged": True,
        "allTopLevelProjectFieldsUnchangedExceptTracksAndUpdatedAt": True,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for key, default in DEFAULTS.items():
        parser.add_argument("--" + key.replace("_", "-"), type=Path, default=default)
    args = parser.parse_args()

    plan_path = args.plan.resolve()
    fragment_path = args.fragment.resolve()
    before_path = args.before.resolve()
    plan = read_json(plan_path)
    fragment = read_json(fragment_path)
    plan_sha = sha256(plan_path)
    require(plan.get("resourceId") == RESOURCE_ID, "P21 bound plan resource changed.")
    require(plan.get("taskId") == EXPECTED_TASK_ID, "P21 bound task ID changed.")
    require(plan.get("startingDocumentHash") == EXPECTED_BASELINE_DOCUMENT_HASH,
            "P21 bound starting hash differs from the verified P19 snapshot.")
    require(plan.get("revision") == 3, "Expected P21 v3 bound plan.")
    require(plan.get("bindingEvidence", {}).get("nativeAutosaveHash") == EXPECTED_BASELINE_DOCUMENT_HASH,
            "P21 native bind evidence does not name the P19 live document hash.")
    assert_equal(plan.get("steps"), fragment.get("steps"), "P21 bound steps vs v3 proposal fragment")

    # The root bound P21 to the P19 live document after applying P19. The
    # copied proposal still carries its earlier P18 authoring snapshot fields;
    # record that provenance discrepancy instead of using stale counts/hashes.
    warnings: list[str] = []
    postconditions = plan.get("postconditions", {})
    if postconditions.get("existingTargets") != EXPECTED_EXISTING_TARGETS:
        warnings.append("Bound-plan postconditions retain the v2 existing-target count; actual v3 actions are independently checked as 12 existing targets.")

    require(len(plan["steps"]) == 20, "P21 v3 bound plan no longer contains 20 operations.")
    if plan.get("nativeActionContract", {}).get("stepCount") != len(plan["steps"]):
        warnings.append("Bound nativeActionContract.stepCount remains 12 from v2; v3 bound plan and receipt are checked as 20 steps.")
    parent_path = Path(plan["amendment"]["parent"]).resolve()
    require(parent_path.is_file(), f"Missing P21 v2 parent fragment: {parent_path}")
    parent_sha = sha256(parent_path)
    require(parent_sha == plan["amendment"]["parentSha256"], "P21 v3 parent fragment changed since v3 composition.")

    before_receipt = read_json(args.before_checkpoint)
    before_sha = sha256(before_path)
    require(before_receipt.get("readbackVerified") is True and
            before_receipt.get("fileSha256") == before_sha == EXPECTED_BASELINE_FILE_SHA256 and
            before_receipt.get("documentHash") == EXPECTED_BASELINE_DOCUMENT_HASH and
            before_receipt.get("resourceId") == RESOURCE_ID and
            before_receipt.get("outputPath") == str(before_path),
            "P19 saved baseline does not match its verified native checkpoint.")
    before = read_json(before_path)
    require(before.get("activeOutputVariant") == "landscape", "P19 baseline is not landscape.")
    require(before.get("timelineId") == RESOURCE_ID.split(":", 1)[1], "P19 timeline ID mismatch.")
    fps = int(before["sequenceFormat"]["frameRate"])
    require(fps == 30, "P21 opacity source validation expects 30fps.")

    admission = read_json(args.admission)
    require(admission.get("taskId") == EXPECTED_TASK_ID and admission.get("targetResourceId") == RESOURCE_ID,
            "P21 admission receipt task/resource mismatch.")
    admission_hashes = {row.get("id"): row.get("beforeHash") for row in admission.get("resources", [])}
    require(admission_hashes.get(RESOURCE_ID) == EXPECTED_BASELINE_DOCUMENT_HASH,
            "P21 admission was not prepared from the verified P19 document hash.")
    if admission.get("approvedSnapshot", {}).get("targetIds") != [RESOURCE_ID]:
        warnings.append("Admission targetIds encoding differs from the compact resource-only grant; task/resource identities still match.")

    source_hashes = []
    for source in plan.get("creativeReference", {}).get("sourceFiles", []):
        source_path = Path(source["path"]).resolve()
        require(source_path.is_file(), f"Missing P21 creative source: {source_path}")
        actual_sha = sha256(source_path)
        require(actual_sha == source["sha256"], f"P21 creative source hash changed: {source_path}")
        source_hashes.append({"path": str(source_path), "sha256": actual_sha})
    smooth_path = ROOT / "marketing/animated-trailer/src/rebuild/Kit.tsx"
    smooth_text = smooth_path.read_text()
    require("u * u * u * (10 + u * (-15 + 6 * u))" in smooth_text,
            "P21 source quintic smoothstep implementation changed.")
    smooth_hash = sha256(smooth_path)

    before_clips, before_parents = clip_indexes(before)
    require(len(before["tracks"]) == 196 and len(before_clips) == EXPECTED_BASELINE_CLIPS,
            "Unexpected P19 native baseline inventory.")
    verify_target_preconditions(plan, before, before_parents)
    expected, created_steps = expected_step_maps(plan)
    expected_target_ids = set(expected)
    guard_ids = set(plan.get("applyGuard", {}).get("existingTargetIds", []))
    evidence_ids = set(plan.get("bindingEvidence", {}).get("targetIds", []))
    require(expected_target_ids == guard_ids == evidence_ids,
            "P21 applyGuard/binding evidence/actual existing target ID sets differ.")

    completed, created_ids, symbolic_reference_steps = verify_receipts(args.receipt.resolve(), plan, plan_sha)
    fade_summary = verify_before_fade_source(plan, expected)

    checkpoint = read_json(args.checkpoint)
    after_path = args.after.resolve()
    require(checkpoint.get("readbackVerified") is True and checkpoint.get("resourceId") == RESOURCE_ID,
            "P21 native save checkpoint is not a verified readback.")
    require(checkpoint.get("outputPath") == str(after_path), "P21 checkpoint output path differs from audited file.")
    require(after_path.is_file(), f"P21 saved native project is not present yet: {after_path}")
    after_sha = sha256(after_path)
    require(checkpoint.get("fileSha256") == after_sha, "Saved P21 project bytes differ from native readback checkpoint.")
    after = read_json(after_path)
    after_clips, after_parents = clip_indexes(after)

    existing_rows = verify_existing_targets(before_clips, before_parents, after_clips, after_parents, expected)
    created_rows = verify_created_clips(plan, created_steps, created_ids, after_clips, after_parents,
                                        set(before_clips))
    plate_fade = verify_created_plate_fade(plan, created_ids, after_clips)
    structure = verify_document_structure(before, after, before_clips, before_parents,
                                          after_clips, after_parents, set(created_ids.values()), expected)

    audit_path = args.audit.resolve()
    require(not audit_path.exists(), f"Refusing to overwrite existing P21 audit output: {audit_path}")
    result = {
        "schema": "looksy.p21-film-native-saved-audit/v1",
        "auditKind": "read-only saved native project, source, binding, and author-receipt audit",
        "productionCallsMadeByAuditor": False,
        "plan": {"path": str(plan_path), "sha256": plan_sha, "taskId": EXPECTED_TASK_ID,
                 "stepCount": len(plan["steps"]), "proposalFragmentPath": str(fragment_path),
                 "proposalFragmentSha256": sha256(fragment_path),
                 "amendmentParentPath": str(parent_path), "amendmentParentSha256": parent_sha},
        "baseline": {"path": str(before_path), "fileSha256": before_sha,
                     "documentHash": EXPECTED_BASELINE_DOCUMENT_HASH,
                     "checkpointPath": str(args.before_checkpoint.resolve())},
        "admission": {"path": str(args.admission.resolve()), "taskId": EXPECTED_TASK_ID,
                      "resourceId": RESOURCE_ID, "beforeDocumentHash": EXPECTED_BASELINE_DOCUMENT_HASH},
        "saved": {"path": str(after_path), "fileSha256": after_sha,
                  "documentHash": checkpoint.get("documentHash"),
                  "taskStateHash": checkpoint.get("taskStateHash"),
                  "readbackVerified": True},
        "creativeSourceHashes": source_hashes + [{"path": str(smooth_path), "sha256": smooth_hash,
                                                   "boundByPlan": False,
                                                   "reason": "Source FilmPolish.tsx imports smooth through creative-launch/Kit.tsx; this is the concrete implementation audited for the fade formula."}],
        "nativeInventory": structure,
        "existingTargets": {"count": len(existing_rows), "rows": existing_rows},
        "createdClips": {"count": len(created_rows), "rows": created_rows},
        "beforeComparisonFade": fade_summary,
        "createdBeforePlateFade": plate_fade,
        "receipt": {"path": str(args.receipt.resolve()), "sha256": sha256(args.receipt.resolve()),
                    "parsedLineCount": sum(1 for line in args.receipt.read_text().splitlines() if line.strip()),
                    "completedStepCount": len(completed), "createdStableIds": created_ids,
                    "symbolicCreatedClipReferenceSteps": symbolic_reference_steps},
        "planMetadataWarnings": warnings,
        "limitations": [
            "This audit proves serialized native state and the authored receipt chain; it does not qualify rendered pixel parity or visual quality.",
            "The P18 planning snapshot is the intentional target-comparison baseline used by the binder; the live P19 checkpoint, P21 starting/admission hash, target snapshots, receipts, and saved project are checked independently.",
            "The bound plan's nativeActionContract.stepCount (12) and postconditions.existingTargets (10) remain from v2, while v3 has 20 steps and 12 existing targets. Actual v3 steps/receipts and saved clip differences are counted directly; the two created titles and two created shape plates are checked separately.",
        ],
    }
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    audit_path.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n")
    print(f"P21 saved native audit passed: {len(existing_rows)} existing targets, "
          f"{len(created_rows)} created clips, {structure['untouchedOriginalClipCount']} untouched originals.")
    print(f"Audit: {audit_path}")


if __name__ == "__main__":
    main()
