#!/usr/bin/env python3
"""Read-only audit of the saved P19 Canvas native edit and its author receipts."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from collections import Counter
from pathlib import Path
from typing import Any


HERE = Path(__file__).resolve().parent
POLISH = HERE.parent
ROOT = POLISH.parents[2]
DEFAULTS = {
    "plan": POLISH / "pass19-canvas-bound.json",
    "before": POLISH / "native-projects/looksy-polish-pass18-library-complete.json",
    "before_receipt": POLISH / "checkpoint-pass18-library-complete-result.json",
    "checkpoint": POLISH / "checkpoint-pass19-canvas-polish-result.json",
    "receipt": POLISH / "pass19-canvas.execution.ndjson",
    "readout_plan": HERE / "p19-brush-opacity-v3.commands-fragment.json",
    "admission": POLISH / "pass19-canvas-admission-result.json",
    "audit": HERE / "pass19-canvas-native-saved-audit.json",
    "after": POLISH / "native-projects/looksy-polish-pass19-canvas-polish.json",
}

RESOURCE_ID = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
EXPECTED_BASELINE_DOCUMENT_HASH = "672ed174fe45978327049e0561356933e84dc78ff6c9291244ed1cdc1bd9314f"
EXPECTED_BASELINE_FILE_SHA256 = "44ba03bd4a8ee78dcbe48cb2134de882322ccca16bfe03f865d035b2bc8862ae"
EXPECTED_EXISTING_TARGETS = 23
EXPECTED_CREATED_TITLES = 47

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
FPS_TOLERANCE = 1e-6
NUMERIC_TOLERANCE = 1e-8


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_json(path: Path) -> Any:
    require(path.is_file(), f"Required evidence is not present yet: {path}")
    return json.loads(path.read_text())


def sha256(path: Path) -> str:
    return hashlib.file_digest(path.open("rb"), "sha256").hexdigest()


def nearly_equal(left: Any, right: Any) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return left is right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return math.isfinite(float(left)) and math.isfinite(float(right)) and math.isclose(
            float(left), float(right), rel_tol=0.0, abs_tol=NUMERIC_TOLERANCE
        )
    if isinstance(left, dict) and isinstance(right, dict):
        return set(left) == set(right) and all(nearly_equal(left[k], right[k]) for k in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(nearly_equal(x, y) for x, y in zip(left, right))
    return left == right


def assert_equal(actual: Any, expected: Any, where: str) -> None:
    require(nearly_equal(actual, expected), f"{where}: expected {expected!r}, got {actual!r}")


def clip_indexes(document: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, str]]:
    clips: dict[str, dict[str, Any]] = {}
    parents: dict[str, str] = {}
    for track in document["tracks"]:
        for clip in track.get("clips", []):
            clip_id = clip["clipId"]
            require(clip_id not in clips, f"Duplicate clip ID in saved document: {clip_id}")
            clips[clip_id] = clip
            parents[clip_id] = track["trackId"]
    return clips, parents


def title_values_to_fields(values: dict[str, Any]) -> dict[str, Any]:
    unknown = set(values) - set(TITLE_FIELD_MAP)
    require(not unknown, f"Unexpected P19 title fields: {sorted(unknown)}")
    return {TITLE_FIELD_MAP[key]: value for key, value in values.items()}


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


def normalized_animation_channel(keys: list[dict[str, Any]], parameter: str) -> list[dict[str, Any]]:
    selected = sorted((key for key in keys if key.get("parameter") == parameter),
                      key=lambda key: float(key["seconds"]))
    return [
        {"localSeconds": key["seconds"], "value": key["value"],
         "interpolation": key["interpolation"]}
        for key in selected
    ]


def smoothstep5(value: float, start: float, end: float) -> float:
    if end <= start:
        return 1.0 if value >= end else 0.0
    u = min(1.0, max(0.0, (value - start) / (end - start)))
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def source_readout_runs(readout: dict[str, Any], fps: int) -> list[dict[str, Any]]:
    curve = readout["readoutCurve"]
    first = int(curve["firstFrame"])
    last = int(curve["lastFrameInclusive"])
    phase_start = float(curve["sourcePhaseStartGlobalSeconds"])
    require(last - first + 1 == 90, "P19 source readout is no longer a 90-frame run.")
    values: list[tuple[int, int]] = []
    for frame in range(first, last + 1):
        local_seconds = frame / fps - phase_start
        value = 1.0 - 0.38 * smoothstep5(local_seconds, 5.1, 6.1) * (
            1.0 - smoothstep5(local_seconds, 6.4, 7.5)
        )
        percent = math.floor(value * 100.0 + 0.5)
        if values and values[-1][1] == percent:
            values[-1] = (values[-1][0], percent)
        else:
            values.append((frame, percent))

    runs: list[dict[str, Any]] = []
    for index, (start_frame, percent) in enumerate(values):
        if index + 1 < len(values):
            end_frame = values[index + 1][0]
        else:
            end_frame = last + 1
        runs.append({
            "startFrame": start_frame,
            "endFrameExclusive": end_frame,
            "startSeconds": start_frame / fps,
            "durationFrames": end_frame - start_frame,
            "durationSeconds": (end_frame - start_frame) / fps,
            "valuePercent": percent,
            "text": f"PAINT OPACITY / {percent}%",
        })
    return runs


def assert_rle_matches_source(plan: dict[str, Any], readout: dict[str, Any], fps: int) -> list[dict[str, Any]]:
    expected = source_readout_runs(readout, fps)
    recorded = readout["readoutCurve"]["runs"]
    require(len(recorded) == len(expected), "The source RLE run count drifted from the formula.")
    for index, (got, want) in enumerate(zip(recorded, expected)):
        for field in ("startFrame", "endFrameExclusive", "durationFrames", "valuePercent", "text"):
            assert_equal(got[field], want[field], f"source RLE run {index} {field}")
        for field in ("startSeconds", "durationSeconds"):
            assert_equal(got[field], want[field], f"source RLE run {index} {field}")

    # The combined native authoring plan must have been assembled from this
    # exact RLE fragment; don't silently audit a newer/different readout.
    source_rel = str(readout["readoutCurve"]["source"])
    require(source_rel.endswith("CanvasPolish.tsx"), "Unexpected numeric readout source file.")
    source_path = ROOT / source_rel
    source_text = source_path.read_text()
    require("0.38 * smooth(s, 5.1, 6.1)" in source_text and
            "smooth(s, 6.4, 7.5)" in source_text and
            "Math.round(product * 100)" in source_text,
            "CanvasPolish source no longer contains the audited opacity formula/display.")

    source_fragments = plan.get("sourceFragments", [])
    readout_fragment = readout.get("_auditPath")
    require(readout_fragment is not None, "Internal readout fragment path was not supplied.")
    fragment_rel = str(readout_fragment.relative_to(ROOT))
    require(any(item["path"] == fragment_rel and item["sha256"] == sha256(readout_fragment)
                for item in source_fragments),
            "P19 bound plan does not bind the exact numeric readout source fragment hash.")
    return expected


def build_expected(plan: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, str], dict[str, dict[str, Any]]]:
    existing: dict[str, dict[str, Any]] = {}
    moves: dict[str, str] = {}
    created_steps: dict[str, dict[str, Any]] = {}
    for step in plan["steps"]:
        action = step["commandId"].split(".")[-1]
        values = step["values"]
        if action == "createTimelineTitle":
            created_steps[step["id"]] = step
            continue
        clip_id = values["clipId"]
        entry = existing.setdefault(clip_id, {
            "allowedFields": set(), "fields": {}, "variants": {},
            "channels": {}, "trackId": None, "timed": False,
            "titleStyleApplied": False,
        })
        if action == "setTimelineClipTitle":
            title_values = values["values"]
            mapped = title_values_to_fields(title_values)
            for field, expected in mapped.items():
                entry["allowedFields"].add(field)
                entry["fields"][field] = expected
            for key, expected in title_values.items():
                variant_field = VARIANT_FIELD_MAP.get(key)
                if variant_field is not None:
                    entry["variants"][variant_field] = expected
            entry["titleStyleApplied"] |= bool(CUSTOM_TITLE_KEYS.intersection(title_values))
            entry["allowedFields"].update({"displayLabel", "titleTemplateKey"})
        elif action == "setTimelineClipTiming":
            entry["trackId"] = values["trackId"]
            entry["timed"] = True
            entry["allowedFields"].update(TIMING_FIELDS)
            entry["fields"].update({
                "startSeconds": values["startSeconds"],
                "durationSeconds": values["durationSeconds"],
                "trimStartSeconds": 0.0,
                "trimEndSeconds": values["durationSeconds"],
            })
        elif action == "setTimelineClipAnimation":
            entry["allowedFields"].add("keyframes")
            for channel in values["channels"]:
                parameter = channel["parameter"]
                require(parameter not in entry["channels"],
                        f"P19 repeats an animation channel for {clip_id}: {parameter}")
                entry["channels"][parameter] = channel["keyframes"]
        else:
            raise AssertionError(f"Unexpected P19 command: {action}")
    require(len(existing) == EXPECTED_EXISTING_TARGETS,
            f"Expected {EXPECTED_EXISTING_TARGETS} existing clip targets, found {len(existing)}.")
    require(len(created_steps) == EXPECTED_CREATED_TITLES,
            f"Expected {EXPECTED_CREATED_TITLES} created title steps, found {len(created_steps)}.")
    for clip_id, entry in existing.items():
        if entry["trackId"] is not None:
            moves[clip_id] = entry["trackId"]
    return existing, moves, created_steps


def read_author_receipts(path: Path, plan: dict[str, Any], plan_sha: str) -> tuple[dict[str, dict[str, Any]], dict[str, str]]:
    expected = {step["id"]: step for step in plan["steps"]}
    started: dict[str, list[dict[str, Any]]] = {}
    completed: dict[str, list[dict[str, Any]]] = {}
    failures: list[dict[str, Any]] = []
    for line_number, line in enumerate(path.read_text().splitlines(), start=1):
        if not line.strip():
            continue
        row = json.loads(line)
        if row.get("event") == "invoke_failed":
            failures.append(row)
        step_id = row.get("stepId")
        if row.get("event") not in {"invoke_started", "invoke_completed"}:
            continue
        require(row.get("planSha256") == plan_sha,
                f"Receipt line {line_number} is bound to a different plan hash.")
        require(step_id in expected, f"Unexpected author receipt step ID: {step_id}")
        wanted = expected[step_id]
        require(row.get("commandId") == wanted["commandId"],
                f"Receipt command mismatch for {step_id}.")
        assert_equal(row.get("values"), wanted["values"], f"receipt values for {step_id}")
        table = started if row["event"] == "invoke_started" else completed
        table.setdefault(step_id, []).append(row)
    require(not failures, f"Native author receipt contains failed steps: {[r.get('stepId') for r in failures]}")
    require(set(started) == set(expected),
            f"Missing/extra started receipt steps: missing={sorted(set(expected)-set(started))}")
    require(set(completed) == set(expected),
            f"P19 authoring is not fully complete: missing={sorted(set(expected)-set(completed))}")

    created_ids: dict[str, str] = {}
    for step_id, step in expected.items():
        require(len(started[step_id]) == 1 and len(completed[step_id]) == 1,
                f"Step {step_id} has repeated/ambiguous invoke outcomes; inspect the task journal.")
        done = completed[step_id][0]
        result = done["result"]
        require(result.get("ok") is True, f"Native step did not succeed: {step_id}")
        if step["commandId"].endswith("createTimelineTitle"):
            clip_id = result.get("createdClipId")
            require(isinstance(clip_id, str) and clip_id,
                    f"Create receipt has no stable clip ID: {step_id}")
            require(clip_id not in created_ids.values(), f"Duplicate returned title ID: {clip_id}")
            created_ids[step_id] = clip_id
    return completed, created_ids


def verify_variant_overrides(before: dict[str, Any], after: dict[str, Any], expected: dict[str, Any], clip_id: str) -> None:
    old_variants = before.get("outputVariantOverrides", {})
    new_variants = after.get("outputVariantOverrides", {})
    variant_keys = set(old_variants) | set(new_variants)
    for variant in variant_keys:
        old = old_variants.get(variant, {})
        new = new_variants.get(variant, {})
        changed = {key for key in set(old) | set(new) if not nearly_equal(old.get(key), new.get(key))}
        if variant != "landscape":
            require(not changed, f"Non-landscape output variant changed for {clip_id}: {variant}/{sorted(changed)}")
            continue
        allowed = set(expected["variants"])
        require(changed <= allowed,
                f"Unexpected landscape override changes for {clip_id}: {sorted(changed - allowed)}")
    landscape = new_variants.get("landscape", {})
    for field, value in expected["variants"].items():
        assert_equal(landscape.get(field), value, f"{clip_id} landscape.{field}")


def verify_existing_targets(
    before_clips: dict[str, dict[str, Any]], before_parents: dict[str, str],
    after_clips: dict[str, dict[str, Any]], after_parents: dict[str, str],
    expected: dict[str, dict[str, Any]],
) -> dict[str, Any]:
    changed_rows: list[dict[str, Any]] = []
    for clip_id, spec in expected.items():
        require(clip_id in before_clips and clip_id in after_clips,
                f"An existing target disappeared: {clip_id}")
        old, new = before_clips[clip_id], after_clips[clip_id]
        fields = dict(spec["fields"])
        if "displayLabel" in spec["allowedFields"]:
            fields["displayLabel"] = display_label({**old, **fields})
        if "titleTemplateKey" in spec["allowedFields"] and spec["titleStyleApplied"]:
            fields["titleTemplateKey"] = "custom"

        diffs = {key for key in set(old) | set(new) if not nearly_equal(old.get(key), new.get(key))}
        allowed = set(spec["allowedFields"]) | {"outputVariantOverrides"}
        require(diffs <= allowed,
                f"Unexpected saved clip fields for {clip_id}: {sorted(diffs - allowed)}")
        for field, wanted in fields.items():
            assert_equal(new.get(field), wanted, f"{clip_id}.{field}")
        if spec["timed"]:
            require(after_parents[clip_id] == spec["trackId"],
                    f"{clip_id} moved to {after_parents[clip_id]}, expected {spec['trackId']}")
        else:
            require(after_parents[clip_id] == before_parents[clip_id],
                    f"Unrequested track move for existing target {clip_id}")

        old_keys = old.get("keyframes", [])
        new_keys = new.get("keyframes", [])
        for parameter, wanted in spec["channels"].items():
            assert_equal(normalized_animation_channel(new_keys, parameter), wanted,
                         f"{clip_id} animation channel {parameter}")
        touched_channels = set(spec["channels"])
        old_untouched = [key for key in old_keys if key.get("parameter") not in touched_channels]
        new_untouched = [key for key in new_keys if key.get("parameter") not in touched_channels]
        assert_equal(new_untouched, old_untouched, f"{clip_id} unrelated animation channels")
        if "keyframes" not in spec["allowedFields"]:
            assert_equal(new_keys, old_keys, f"{clip_id} preserved keyframes")

        verify_variant_overrides(old, new, spec, clip_id)
        changed_rows.append({
            "clipId": clip_id,
            "changedFields": sorted(diffs),
            "fromTrackId": before_parents[clip_id],
            "toTrackId": after_parents[clip_id],
            "requestedChannels": {p: len(k) for p, k in spec["channels"].items()},
        })
    return {"targetCount": len(changed_rows), "rows": changed_rows}


def verify_created_titles(
    plan: dict[str, Any], created_steps: dict[str, dict[str, Any]], created_ids: dict[str, str],
    after_clips: dict[str, dict[str, Any]], after_parents: dict[str, str], before_ids: set[str],
) -> dict[str, Any]:
    require(set(created_ids) == set(created_steps), "Create receipt IDs do not match the plan.")
    new_ids = set(after_clips) - before_ids
    created_clip_ids = set(created_ids.values())
    require(new_ids == created_clip_ids,
            f"Saved document has unexpected/missing new clip IDs: extra={sorted(new_ids-created_clip_ids)}, missing={sorted(created_clip_ids-new_ids)}")
    rows: list[dict[str, Any]] = []
    for step_id, step in created_steps.items():
        clip_id = created_ids[step_id]
        clip = after_clips[clip_id]
        values = step["values"]
        require(clip.get("mediaType") == "title", f"Created clip is not a native title: {clip_id}")
        require(after_parents[clip_id] == values["trackId"], f"Created title is on the wrong track: {clip_id}")
        assert_equal(clip.get("startSeconds"), values["startSeconds"], f"{clip_id}.startSeconds")
        assert_equal(clip.get("durationSeconds"), values["durationSeconds"], f"{clip_id}.durationSeconds")
        assert_equal(clip.get("trimStartSeconds"), 0.0, f"{clip_id}.trimStartSeconds")
        assert_equal(clip.get("trimEndSeconds"), values["durationSeconds"], f"{clip_id}.trimEndSeconds")
        requested = title_values_to_fields(values["values"])
        for field, wanted in requested.items():
            assert_equal(clip.get(field), wanted, f"created {clip_id}.{field}")
        expected_label = display_label({**clip, **requested})
        assert_equal(clip.get("displayLabel"), expected_label, f"created {clip_id}.displayLabel")
        if CUSTOM_TITLE_KEYS.intersection(values["values"]):
            assert_equal(clip.get("titleTemplateKey"), "custom", f"created {clip_id}.titleTemplateKey")
        landscape = clip.get("outputVariantOverrides", {}).get("landscape", {})
        require(landscape, f"Created title is missing its landscape output override: {clip_id}")
        for key, value in values["values"].items():
            variant_field = VARIANT_FIELD_MAP.get(key)
            if variant_field is not None:
                assert_equal(landscape.get(variant_field), value,
                             f"created {clip_id} landscape.{variant_field}")
        rows.append({"stepId": step_id, "clipId": clip_id,
                     "trackId": after_parents[clip_id], "startSeconds": clip["startSeconds"],
                     "durationSeconds": clip["durationSeconds"], "titleText": clip.get("titleText", "")})
    return {"createdCount": len(rows), "rows": rows}


def verify_readout_frames(
    plan: dict[str, Any], readout: dict[str, Any], runs: list[dict[str, Any]],
    after_clips: dict[str, dict[str, Any]], after_parents: dict[str, str], fps: int,
) -> dict[str, Any]:
    expected_existing_id = readout["targets"][0]["clipId"]
    track_id = readout["targets"][0]["trackId"]
    require(any(step["values"].get("clipId") == expected_existing_id for step in plan["steps"]),
            "Combined plan no longer includes the original first numeric-readout clip.")
    frame_first = int(readout["readoutCurve"]["firstFrame"])
    frame_end = int(readout["readoutCurve"]["lastFrameInclusive"]) + 1
    frame_runs: list[tuple[int, int, str, str]] = []
    for clip_id, clip in after_clips.items():
        if after_parents[clip_id] != track_id:
            continue
        start_frame_exact = float(clip["startSeconds"]) * fps
        end_frame_exact = (float(clip["startSeconds"]) + float(clip["durationSeconds"])) * fps
        start_frame = round(start_frame_exact)
        end_frame = round(end_frame_exact)
        if end_frame <= frame_first or start_frame >= frame_end:
            continue
        require(abs(start_frame_exact - start_frame) <= FPS_TOLERANCE and
                abs(end_frame_exact - end_frame) <= FPS_TOLERANCE,
                f"Readout title {clip_id} is not aligned to output frames.")
        frame_runs.append((start_frame, end_frame, str(clip.get("titleText", "")), clip_id))
    frame_runs.sort()
    require(len(frame_runs) == len(runs),
            f"Expected {len(runs)} gapless readout runs on {track_id}, found {len(frame_runs)}.")

    run_mismatches = []
    cursor = frame_first
    for index, (actual, wanted) in enumerate(zip(frame_runs, runs)):
        start, end, text, clip_id = actual
        if start != cursor or end != wanted["endFrameExclusive"] or \
                start != wanted["startFrame"] or text != wanted["text"]:
            run_mismatches.append({"index": index, "actual": actual, "expected": wanted})
        cursor = end
    require(not run_mismatches, f"Native PAINT OPACITY run labels/timings differ from source RLE: {run_mismatches[:3]}")
    require(cursor == frame_end, f"Native numeric readout does not cover final frame {frame_end - 1}.")

    per_frame = {}
    for frame in range(frame_first, frame_end):
        covering = [(start, end, text, clip_id) for start, end, text, clip_id in frame_runs
                    if start <= frame < end]
        require(len(covering) == 1, f"Output frame {frame} has {len(covering)} readout titles.")
        wanted = next(run for run in runs if run["startFrame"] <= frame < run["endFrameExclusive"])
        assert_equal(covering[0][2], wanted["text"], f"output frame {frame} PAINT OPACITY label")
        per_frame[str(frame)] = {"titleClipId": covering[0][3], "text": covering[0][2]}
    return {
        "trackId": track_id,
        "firstFrame": frame_first,
        "lastFrameInclusive": frame_end - 1,
        "frameCount": frame_end - frame_first,
        "runCount": len(frame_runs),
        "allFramesExactlyMatchRemotionRLE": True,
        "runs": [
            {"startFrame": start, "endFrameExclusive": end, "titleClipId": clip_id, "text": text}
            for start, end, text, clip_id in frame_runs
        ],
        "sourceRunsSha256": hashlib.sha256(json.dumps(runs, sort_keys=True, separators=(",", ":")).encode()).hexdigest(),
        "perFrameLabels": per_frame,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("plan", "before", "before_receipt", "checkpoint", "receipt", "readout_plan", "admission", "audit", "after"):
        parser.add_argument("--" + key.replace("_", "-"), type=Path, default=DEFAULTS[key])
    args = parser.parse_args()

    plan_path = args.plan.resolve()
    plan = read_json(plan_path)
    plan_sha = sha256(plan_path)
    require(plan.get("resourceId") == RESOURCE_ID, "P19 plan resource changed.")
    require(plan.get("taskId"), "P19 bound plan has no native task ID.")

    before_receipt = read_json(args.before_receipt)
    baseline_path = args.before.resolve()
    baseline_sha = sha256(baseline_path)
    require(before_receipt["readbackVerified"] is True and
            before_receipt["fileSha256"] == baseline_sha == EXPECTED_BASELINE_FILE_SHA256 and
            before_receipt["documentHash"] == EXPECTED_BASELINE_DOCUMENT_HASH and
            before_receipt["resourceId"] == RESOURCE_ID,
            "P18 complete baseline receipt/hash is not the expected saved snapshot.")
    before = read_json(baseline_path)
    require(before.get("timelineId") == RESOURCE_ID.split(":", 1)[1], "P18 timeline resource mismatch.")
    require(before.get("activeOutputVariant") == "landscape", "P18 baseline is not landscape.")
    require(plan.get("startingDocumentHash") == before_receipt["documentHash"],
            "P19 native task was not admitted from the verified P18 complete snapshot.")

    admission = read_json(args.admission)
    require(admission.get("taskId") == plan["taskId"], "P19 plan task differs from admission receipt.")
    require(admission.get("targetResourceId") == RESOURCE_ID, "P19 admission resource mismatch.")

    source_hashes = []
    for fragment in plan.get("sourceFragments", []):
        fragment_path = ROOT / fragment["path"]
        require(fragment_path.is_file(), f"Missing P19 source fragment: {fragment_path}")
        actual_sha = sha256(fragment_path)
        require(actual_sha == fragment["sha256"], f"P19 source fragment changed since binding: {fragment['path']}")
        source_hashes.append({"path": fragment["path"], "sha256": actual_sha})

    readout = read_json(args.readout_plan)
    readout["_auditPath"] = args.readout_plan.resolve()
    fps = int(before["sequenceFormat"]["frameRate"])
    require(fps == 30 and plan["composition"]["fps"] == fps,
            "P19 plan/readout fps changed; this auditor expects native 30fps.")
    source_runs = assert_rle_matches_source(plan, readout, fps)
    expected, _, created_steps = build_expected(plan)
    completed, created_ids = read_author_receipts(args.receipt.resolve(), plan, plan_sha)

    checkpoint = read_json(args.checkpoint)
    after_path = args.after.resolve()
    require(checkpoint.get("readbackVerified") is True and
            checkpoint.get("resourceId") == RESOURCE_ID and
            checkpoint.get("outputPath") == str(after_path),
            "P19 checkpoint does not prove the expected saved native copy.")
    require(after_path.is_file(), f"P19 saved native project is not present: {after_path}")
    after_sha = sha256(after_path)
    require(checkpoint.get("fileSha256") == after_sha,
            "Saved P19 project bytes differ from the native readback receipt.")
    after = read_json(after_path)
    require(after.get("timelineId") == before.get("timelineId") and
            after.get("activeOutputVariant") == "landscape" and
            after.get("sequenceFormat") == before.get("sequenceFormat"),
            "P19 save changed timeline identity, output variant, or sequence format.")

    before_clips, before_parents = clip_indexes(before)
    after_clips, after_parents = clip_indexes(after)
    require(len(before["tracks"]) == 196 and len(before_clips) == 1363,
            "Unexpected P18 baseline inventory.")
    require(len(after["tracks"]) == len(before["tracks"]), "P19 added or removed a timeline track.")
    old_track_metadata = [{k: v for k, v in track.items() if k != "clips"} for track in before["tracks"]]
    new_track_metadata = [{k: v for k, v in track.items() if k != "clips"} for track in after["tracks"]]
    assert_equal(new_track_metadata, old_track_metadata, "track metadata/order")
    require(set(before_clips) <= set(after_clips), "P19 removed an original clip.")
    require(len(after_clips) == len(before_clips) + EXPECTED_CREATED_TITLES,
            f"Saved P19 clip count is {len(after_clips)}, expected {len(before_clips)+EXPECTED_CREATED_TITLES}.")

    existing_summary = verify_existing_targets(
        before_clips, before_parents, after_clips, after_parents, expected)
    created_summary = verify_created_titles(
        plan, created_steps, created_ids, after_clips, after_parents, set(before_clips))
    readout_summary = verify_readout_frames(plan, readout, source_runs, after_clips, after_parents, fps)

    out = {
        "schema": "looksy.p19-canvas-native-saved-audit/v1",
        "auditKind": "read-only saved native project and receipt audit",
        "productionCallsMadeByAuditor": False,
        "planPath": str(plan_path),
        "planSha256": plan_sha,
        "taskId": plan["taskId"],
        "timelineResourceId": RESOURCE_ID,
        "baseline": {"path": str(baseline_path), "fileSha256": baseline_sha,
                     "documentHash": before_receipt["documentHash"],
                     "trackCount": len(before["tracks"]), "clipCount": len(before_clips)},
        "savedReadback": {"path": str(after_path), "fileSha256": after_sha,
                          "documentHash": checkpoint.get("documentHash"),
                          "readbackVerified": True,
                          "trackCount": len(after["tracks"]), "clipCount": len(after_clips)},
        "sourceFragments": source_hashes,
        "existingTargets": existing_summary,
        "createdNativeTitles": created_summary,
        "numericPaintOpacityReadout": readout_summary,
        "unchangedOutsideScope": len(before_clips) - existing_summary["targetCount"],
        "trackMetadataAndOrderUnchanged": True,
        "addedTracks": 0,
        "unsupportedClaims": [
            "The numeric native text titles match the source curve frame by frame; they are not a live engine-bound brush parameter expression.",
            "This audit verifies saved project structure and native author receipts. It does not establish final rendered pixels or human visual approval.",
        ],
    }
    audit_path = args.audit.resolve()
    audit_path.parent.mkdir(parents=True, exist_ok=True)
    with audit_path.open("x") as stream:
        json.dump(out, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print(json.dumps({
        "audit": str(audit_path),
        "existingTargets": existing_summary["targetCount"],
        "createdTitles": created_summary["createdCount"],
        "untouchedOriginalClips": out["unchangedOutsideScope"],
        "tracksUnchanged": True,
        "readoutFrames": readout_summary["frameCount"],
        "readoutRuns": readout_summary["runCount"],
        "nativeFileSha256": after_sha,
    }, indent=2))


if __name__ == "__main__":
    main()
