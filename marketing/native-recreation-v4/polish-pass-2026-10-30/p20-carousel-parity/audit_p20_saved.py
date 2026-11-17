#!/usr/bin/env python3
"""Read-only saved-state audit for an executed and checkpointed P20 fragment."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path
from typing import Any


HERE = Path(__file__).resolve().parent
POLISH = HERE.parent
DEFAULT_PLAN = POLISH / "pass20-carousel-bound.json"
DEFAULT_BEFORE = POLISH / "native-projects/looksy-polish-pass22-photo-proof.json"
DEFAULT_RECEIPT = POLISH / "pass20-carousel.execution.json"
DEFAULT_CHECKPOINT = POLISH / "checkpoint-pass20-carousel-complete-result.json"
DEFAULT_AUDIT = HERE / "p20-carousel-native-saved-audit.json"
DEFAULT_HASH_TOOL = Path("/private/tmp/looksy-dense-native-hash")
NUMERIC_TOLERANCE = 1e-8

TITLE_FIELDS = {
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
FRAME_FIELDS = {
    "offsetX": "frameOffsetX",
    "offsetY": "frameOffsetY",
    "scale": "frameScale",
    "rotationDegrees": "frameRotationDegrees",
    "perspectiveX": "framePerspectiveX",
    "perspectiveY": "framePerspectiveY",
    "opacity": "opacity",
    "objectFit": "frameMode",
    "width": "frameWidth",
    "height": "frameHeight",
    "cornerRadius": "frameCornerRadius",
    "cropLeft": "sourceCropLeft",
    "cropTop": "sourceCropTop",
    "cropRight": "sourceCropRight",
    "cropBottom": "sourceCropBottom",
    "frameShadow": "frameShadow",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_json(path: Path) -> Any:
    require(path.is_file(), f"Required evidence is not present: {path}")
    return json.loads(path.read_text())


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def equal(left: Any, right: Any) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return left is right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return math.isfinite(float(left)) and math.isfinite(float(right)) and math.isclose(
            float(left), float(right), rel_tol=0.0, abs_tol=NUMERIC_TOLERANCE
        )
    if isinstance(left, dict) and isinstance(right, dict):
        return set(left) == set(right) and all(equal(left[key], right[key]) for key in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(equal(a, b) for a, b in zip(left, right))
    return left == right


def clip_index(document: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, str]]:
    clips: dict[str, dict[str, Any]] = {}
    parent: dict[str, str] = {}
    for track in document.get("tracks", []):
        track_id = track.get("trackId")
        require(isinstance(track_id, str), "Saved track is missing a stable trackId.")
        for clip in track.get("clips", []):
            clip_id = clip.get("clipId")
            require(isinstance(clip_id, str), f"Clip on {track_id} is missing clipId.")
            require(clip_id not in clips, f"Duplicate clipId in saved document: {clip_id}")
            clips[clip_id] = clip
            parent[clip_id] = track_id
    return clips, parent


def track_index(document: dict[str, Any]) -> dict[str, dict[str, Any]]:
    tracks = {track["trackId"]: track for track in document.get("tracks", [])}
    require(len(tracks) == len(document.get("tracks", [])), "Duplicate trackId in saved document.")
    return tracks


def resolve_refs(value: Any, prior_results: dict[str, dict[str, Any]]) -> Any:
    if isinstance(value, dict):
        if "result" in value or "field" in value:
            require(set(value) == {"result", "field"}, "Malformed stable-ID reference in plan.")
            step_id, field = value["result"], value["field"]
            require(step_id in prior_results, f"Reference points to unavailable earlier step {step_id}.")
            require(field in prior_results[step_id], f"Step {step_id} did not return {field}.")
            return prior_results[step_id][field]
        return {key: resolve_refs(item, prior_results) for key, item in value.items()}
    if isinstance(value, list):
        return [resolve_refs(item, prior_results) for item in value]
    return value


def normalized_channel(clip: dict[str, Any], parameter: str) -> list[dict[str, Any]]:
    keys = [key for key in clip.get("keyframes", []) if key.get("parameter") == parameter]
    keys.sort(key=lambda key: float(key["seconds"]))
    return [{"localSeconds": key["seconds"], "value": key["value"],
             "interpolation": key["interpolation"]} for key in keys]


def normalized_expected_channel(keyframes: list[dict[str, Any]]) -> list[dict[str, Any]]:
    return sorted(keyframes, key=lambda key: float(key["localSeconds"]))


def native_title_value(key: str, value: Any, width: int, height: int) -> Any:
    # updateSelectedTitle clamps normalized shape dimensions to at least one
    # canvas pixel so an authored shape remains representable in the UI.
    if key == "shapeWidth":
        return min(4.0, max(1.0 / max(1, width), float(value)))
    if key == "shapeHeight":
        return min(4.0, max(1.0 / max(1, height), float(value)))
    return value


def untouched_keyframes(clip: dict[str, Any], replaced: set[str]) -> list[dict[str, Any]]:
    return sorted((key for key in clip.get("keyframes", [])
                   if key.get("parameter") not in replaced),
                  key=lambda key: (key.get("parameter", ""), float(key.get("seconds", 0))))


def native_document_hash(tool: Path, document_path: Path) -> dict[str, Any]:
    require(tool.is_file(), f"Native canonical hash helper is unavailable: {tool}")
    proc = subprocess.run([str(tool), str(document_path)], check=True, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    result = json.loads(proc.stdout)
    require(result.get("path") == str(document_path.resolve()),
            "Canonical hash helper returned a different document path.")
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--plan", type=Path, default=DEFAULT_PLAN,
                        help="Bound P20 fragment used by author_native.py.")
    parser.add_argument("--before", type=Path, default=DEFAULT_BEFORE,
                        help="Exact native project copy matching plan.startingDocumentHash.")
    parser.add_argument("--receipt", type=Path, default=DEFAULT_RECEIPT,
                        help="author_native.py JSON summary receipt.")
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CHECKPOINT,
                        help="timeline.action.saveTimeline result JSON.")
    parser.add_argument("--after", type=Path,
                        help="Saved native readback; defaults to checkpoint.outputPath.")
    parser.add_argument("--hash-tool", type=Path, default=DEFAULT_HASH_TOOL)
    parser.add_argument("--audit", type=Path, default=DEFAULT_AUDIT)
    args = parser.parse_args()

    plan = read_json(args.plan)
    before = read_json(args.before)
    receipt = read_json(args.receipt)
    checkpoint = read_json(args.checkpoint)
    after_path = args.after or Path(checkpoint["outputPath"])
    after = read_json(after_path)

    plan_digest = sha256(args.plan)
    require(receipt.get("planSha256") == plan_digest,
            "Authoring receipt does not bind the supplied bound plan SHA-256.")
    require(receipt.get("status") == "authored-awaiting-native-review",
            f"Authoring receipt is not complete: {receipt.get('status')!r}")
    require(isinstance(plan.get("taskId"), str) and plan["taskId"],
            "P20 bound plan is missing its owned native taskId.")
    require(isinstance(plan.get("startingDocumentHash"), str),
            "P20 bound plan is missing its startingDocumentHash.")

    before_hash = native_document_hash(args.hash_tool, args.before)
    after_hash = native_document_hash(args.hash_tool, after_path)
    require(before_hash.get("documentHash") == plan["startingDocumentHash"],
            "Before project does not match the bound starting document hash.")
    require(before_hash.get("timelineId") == after_hash.get("timelineId") ==
            plan["resourceId"].removeprefix("timeline:"),
            "Before/after native timeline identity does not match the bound resource.")
    require(checkpoint.get("ok") is True and checkpoint.get("readbackVerified") is True,
            "Native save checkpoint is not successful/readback-verified.")
    require(checkpoint.get("resourceId") == plan["resourceId"],
            "Native save checkpoint resourceId does not match P20.")
    require(Path(checkpoint["outputPath"]).resolve() == after_path.resolve(),
            "--after path differs from the checkpoint's verified outputPath.")
    after_raw_sha = sha256(after_path)
    require(checkpoint.get("fileSha256") == after_raw_sha,
            "Checkpoint file SHA-256 does not match the saved native project.")
    require(checkpoint.get("documentHash") == after_hash.get("documentHash"),
            "Checkpoint canonical document hash does not match the native hash helper.")
    require(checkpoint.get("timelineId") == after_hash.get("timelineId"),
            "Checkpoint timelineId does not match saved state.")

    plan_steps = plan.get("steps", [])
    receipt_steps = receipt.get("steps", [])
    require(len(plan_steps) == len(receipt_steps),
            "Authoring receipt step count differs from the bound plan.")
    receipt_by_id = {step.get("id"): step for step in receipt_steps}
    require(len(receipt_by_id) == len(receipt_steps), "Duplicate step ID in authoring receipt.")
    results: dict[str, dict[str, Any]] = {}
    applied_values: dict[str, dict[str, Any]] = {}
    for index, step in enumerate(plan_steps):
        step_id = step["id"]
        recorded = receipt_by_id.get(step_id)
        require(recorded is not None, f"Missing completed receipt for plan step {step_id}.")
        require(recorded.get("commandId") == step.get("commandId"),
                f"Command identity mismatch for step {step_id}.")
        expected_values = resolve_refs(step.get("values", {}), results)
        require(equal(recorded.get("values"), expected_values),
                f"Applied native values differ from the reviewed plan at step {step_id}.")
        result = recorded.get("result", {})
        require(isinstance(result, dict) and result.get("ok", True) is not False,
                f"Native authoring step failed: {step_id}.")
        results[step_id] = result
        applied_values[step_id] = expected_values

    before_clips, before_parents = clip_index(before)
    after_clips, after_parents = clip_index(after)
    sequence_format = after.get("sequenceFormat", {})
    sequence_width = int(sequence_format.get("width", plan.get("composition", {}).get("width", 1920)))
    sequence_height = int(sequence_format.get("height", plan.get("composition", {}).get("height", 1080)))
    before_tracks = track_index(before)
    after_tracks = track_index(after)
    require(set(before_tracks) == set(after_tracks), "P20 unexpectedly created or removed a track.")
    for track_id in before_tracks:
        left = {key: value for key, value in before_tracks[track_id].items() if key != "clips"}
        right = {key: value for key, value in after_tracks[track_id].items() if key != "clips"}
        require(equal(left, right), f"Unplanned track metadata/order change: {track_id}.")

    create_steps = {
        "createTimelineTitle", "createTimelineGraphic", "createTimelineGenerator"
    }
    created_ids: dict[str, str] = {}
    for step in plan_steps:
        action = step["commandId"].rsplit(".", 1)[-1]
        if action not in create_steps:
            continue
        result = results[step["id"]]
        clip_id = result.get("createdClipId")
        require(isinstance(clip_id, str) and clip_id,
                f"Create step {step['id']} lacks a stable createdClipId receipt.")
        require(clip_id not in created_ids.values(), f"Duplicate createdClipId in receipts: {clip_id}")
        created_ids[step["id"]] = clip_id
    require(len(created_ids) == 32, f"Expected 32 P20-created clips, found {len(created_ids)}.")
    require(set(after_clips) - set(before_clips) == set(created_ids.values()),
            "Saved state's new clip IDs do not exactly match the native create receipts.")
    require(set(before_clips) - set(after_clips) == set(), "P20 removed an existing clip.")

    expected_fields: dict[str, dict[str, Any]] = {}
    expected_channels: dict[str, dict[str, list[dict[str, Any]]]] = {}
    requested_masks: dict[str, dict[str, Any]] = {}
    allowed_fields: dict[str, set[str]] = {}
    original_channels: dict[str, set[str]] = {}
    created_expected: dict[str, dict[str, Any]] = {}

    def require_existing_target(cid: str, step_id: str) -> None:
        require(cid in before_clips and cid in after_clips,
                f"Step {step_id} target clip is missing from before/after document.")

    for step in plan_steps:
        action = step["commandId"].rsplit(".", 1)[-1]
        values = applied_values[step["id"]]
        if action in create_steps:
            cid = created_ids[step["id"]]
            require(cid in after_clips, f"Created clip {cid} is absent from saved project.")
            expected = {"trackId": values["trackId"],
                        "startSeconds": values["startSeconds"],
                        "durationSeconds": values["durationSeconds"]}
            if action == "createTimelineTitle":
                expected["mediaType"] = "title"
                # createTimelineTitleForAutomation applies the requested
                # template, then updateSelectedTitle(values). Native title
                # fields other than text mark that authored style as custom.
                expected["titleTemplateKey"] = (
                    "custom" if set(values.get("values", {})) - {"text"}
                    else values["templateKey"]
                )
                expected.update({TITLE_FIELDS[key]: native_title_value(key, val, sequence_width, sequence_height)
                                 for key, val in values.get("values", {}).items()})
            elif action == "createTimelineGraphic":
                expected["mediaType"] = "title"
                expected["titleShapeType"] = values["shapeType"]
                expected["titleTemplateKey"] = "custom"
                expected.update({TITLE_FIELDS[key]: native_title_value(key, val, sequence_width, sequence_height)
                                 for key, val in values.get("values", {}).items()})
            else:
                expected["mediaType"] = "motion-generator"
                expected["generatorKind"] = values["generatorKind"]
                expected["generatorParameters"] = values["parameters"]
            created_expected[cid] = expected
            continue

        raw_cid = values.get("clipId")
        cid = raw_cid if isinstance(raw_cid, str) else None
        # The author runner resolves typed references in applied values; they
        # therefore arrive here as stable ID strings.
        require(isinstance(cid, str), f"Step {step['id']} has no resolved clipId.")
        if cid in before_clips:
            require_existing_target(cid, step["id"])
        else:
            require(cid in created_ids.values(), f"Step {step['id']} targets an unreceipted new clip.")
        fields = allowed_fields.setdefault(cid, set())
        if action == "setTimelineClipFrame":
            unknown = set(values) - {"clipId", *FRAME_FIELDS.keys()}
            require(not unknown, f"Unknown frame setter fields in {step['id']}: {sorted(unknown)}")
            for key, value in values.items():
                if key == "clipId":
                    continue
                field = FRAME_FIELDS[key]
                if key in {"cropLeft", "cropTop", "cropRight", "cropBottom"} and all(
                    values.get(crop_key, 0.0) == 0.0
                    for crop_key in ("cropLeft", "cropTop", "cropRight", "cropBottom")
                ):
                    # The native automation contract treats the all-zero crop
                    # tuple as its full-image sentinel (0, 0, 1, 1).
                    if key in {"cropRight", "cropBottom"}:
                        value = 1.0
                fields.add(field)
                expected_fields.setdefault(cid, {})[field] = value
        elif action == "setTimelineClipTitle":
            title_values = values.get("values", {})
            unknown = set(title_values) - set(TITLE_FIELDS)
            require(not unknown, f"Unknown title setter fields in {step['id']}: {sorted(unknown)}")
            for key, value in title_values.items():
                field = TITLE_FIELDS[key]
                fields.add(field)
                expected_fields.setdefault(cid, {})[field] = native_title_value(
                    key, value, sequence_width, sequence_height
                )
        elif action == "setTimelineClipMask":
            mask = {key: value for key, value in values.items() if key != "clipId"}
            require(set(mask) == {"shape", "left", "top", "right", "bottom", "cornerRadius"},
                    f"Unexpected mask schema in {step['id']}.")
            fields.add("clipMask")
            requested_masks[cid] = mask
        elif action == "setTimelineClipAnimation":
            fields.add("keyframes")
            channels = expected_channels.setdefault(cid, {})
            for channel in values["channels"]:
                parameter = channel["parameter"]
                require(parameter not in channels,
                        f"P20 replaces {parameter} more than once on {cid}; audit must be updated.")
                channels[parameter] = channel["keyframes"]
            original_channels.setdefault(cid, set()).update(
                channel["parameter"] for channel in values["channels"]
            )
        else:
            raise AssertionError(f"Unexpected P20 existing-clip mutation: {action}")

    # Existing clip identity and all unplanned fields must remain stable.
    changed_clip_summary: dict[str, dict[str, Any]] = {}
    for cid, original in before_clips.items():
        current = after_clips[cid]
        require(before_parents[cid] == after_parents[cid], f"Existing clip moved tracks: {cid}.")
        changed = {key for key in set(original) | set(current)
                   if not equal(original.get(key), current.get(key))}
        changed_without_variant = changed - {"outputVariantOverrides"}
        require(changed_without_variant <= allowed_fields.get(cid, set()),
                f"Unplanned saved clip field change on {cid}: "
                f"{sorted(changed_without_variant - allowed_fields.get(cid, set()))}")
        if "outputVariantOverrides" in changed:
            old_variants = original.get("outputVariantOverrides", {})
            new_variants = current.get("outputVariantOverrides", {})
            for variant in set(old_variants) | set(new_variants):
                old = old_variants.get(variant, {})
                new = new_variants.get(variant, {})
                variant_changed = {key for key in set(old) | set(new)
                                   if not equal(old.get(key), new.get(key))}
                if not variant_changed:
                    continue
                require(variant == "landscape", f"Unexpected non-landscape variant changed on {cid}.")
                variant_allowed = set(allowed_fields.get(cid, set())) | {"framingKeyframes"}
                require(variant_changed <= variant_allowed,
                        f"Unplanned landscape variant fields changed on {cid}: "
                        f"{sorted(variant_changed - variant_allowed)}")
                if "framingKeyframes" in variant_changed:
                    changed_channels = original_channels.get(cid, set())
                    old_keys = old.get("framingKeyframes", [])
                    new_keys = new.get("framingKeyframes", [])
                    old_unrequested = sorted((key for key in old_keys
                                              if key.get("parameter") not in changed_channels),
                                             key=lambda key: (key.get("parameter", ""),
                                                              float(key.get("seconds", 0))))
                    new_unrequested = sorted((key for key in new_keys
                                              if key.get("parameter") not in changed_channels),
                                             key=lambda key: (key.get("parameter", ""),
                                                              float(key.get("seconds", 0))))
                    require(equal(old_unrequested, new_unrequested),
                            f"Unrequested landscape keyframes changed on {cid}.")
                    by_time = {(key.get("parameter"), float(key.get("seconds", 0))): key
                               for key in current.get("keyframes", [])}
                    for key in new_keys:
                        identity = (key.get("parameter"), float(key.get("seconds", 0)))
                        require(identity in by_time and equal(key, by_time[identity]),
                                f"Landscape curve is not mirrored by the saved native curve on {cid}.")

        for field, wanted in expected_fields.get(cid, {}).items():
            require(field in current and equal(current[field], wanted),
                    f"Saved {field} does not match P20 request on {cid}.")
        if cid in requested_masks:
            old_mask = original.get("clipMask", {})
            expected_mask = dict(old_mask)
            expected_mask.update(requested_masks[cid])
            require(equal(current.get("clipMask", {}), expected_mask),
                    f"Saved clipMask does not match P20 request on {cid}.")

        replaced = original_channels.get(cid, set())
        if replaced:
            require(equal(untouched_keyframes(original, replaced),
                          untouched_keyframes(current, replaced)),
                    f"Unrequested animation channels changed on {cid}.")
            for parameter, expected in expected_channels[cid].items():
                require(equal(normalized_channel(current, parameter),
                              normalized_expected_channel(expected)),
                        f"Saved keyframes for {parameter} do not match P20 on {cid}.")
        if changed or before_parents[cid] != after_parents[cid]:
            changed_clip_summary[cid] = {
                "changedFields": sorted(changed),
                "trackId": after_parents[cid],
                "plannedFields": sorted(allowed_fields.get(cid, set())),
                "replacedAnimationChannels": sorted(replaced),
            }

    for cid, expected in created_expected.items():
        current = after_clips[cid]
        expected_track = expected.pop("trackId")
        require(after_parents[cid] == expected_track, f"Created clip placed on wrong track: {cid}.")
        for field, wanted in expected.items():
            require(field in current and equal(current[field], wanted),
                    f"Created clip {cid} has wrong {field}: expected {wanted!r}, got {current.get(field)!r}.")
        for field, wanted in expected_fields.get(cid, {}).items():
            require(field in current and equal(current[field], wanted),
                    f"Created clip {cid} has wrong post-create {field}: expected {wanted!r}, got {current.get(field)!r}.")
        if cid in requested_masks:
            mask = dict(current.get("clipMask", {}))
            require(all(equal(mask.get(key), value) for key, value in requested_masks[cid].items()),
                    f"Created clip {cid} mask does not match the requested values.")
        for parameter, expected_keys in expected_channels.get(cid, {}).items():
            require(equal(normalized_channel(current, parameter),
                          normalized_expected_channel(expected_keys)),
                    f"Created clip {cid} keyframes for {parameter} do not match P20.")

    changed_top = {key for key in set(before) | set(after)
                   if key not in {"tracks", "updatedAt"} and not equal(before.get(key), after.get(key))}
    require(not changed_top, f"Unplanned top-level document fields changed: {sorted(changed_top)}")
    track_order_before = [track["trackId"] for track in before["tracks"]]
    track_order_after = [track["trackId"] for track in after["tracks"]]
    require(track_order_before == track_order_after, "Global native track order changed during P20.")

    audit = {
        "schema": "looksy.p20-carousel-native-saved-audit/v1",
        "status": "passed-saved-native-state",
        "claimLimit": "Saved native structure/author receipt proof only; rendered-frame and UI review remain separate.",
        "planPath": str(args.plan.resolve()),
        "planSha256": plan_digest,
        "taskId": plan["taskId"],
        "resourceId": plan["resourceId"],
        "beforePath": str(args.before.resolve()),
        "beforeRawSha256": sha256(args.before),
        "beforeDocumentHash": before_hash["documentHash"],
        "savedPath": str(after_path.resolve()),
        "savedRawSha256": after_raw_sha,
        "savedDocumentHash": after_hash["documentHash"],
        "checkpointPath": str(args.checkpoint.resolve()),
        "checkpointReadbackVerified": checkpoint["readbackVerified"],
        "receiptPath": str(args.receipt.resolve()),
        "receiptStatus": receipt["status"],
        "completedSteps": len(receipt_steps),
        "existingClipCount": len(before_clips),
        "savedClipCount": len(after_clips),
        "createdClipCount": len(created_ids),
        "createdClipIdsByStep": created_ids,
        "changedExistingClipCount": len(changed_clip_summary),
        "changedExistingClips": changed_clip_summary,
        "newTrackCount": 0,
        "trackOrderUnchanged": True,
        "trackMetadataUnchanged": True,
        "unrequestedAnimationChannelsPreserved": True,
        "allRequestedValuesAndCurvesMatch": True,
        "topLevelFieldsUnchangedExceptUpdatedAt": True,
    }
    args.audit.parent.mkdir(parents=True, exist_ok=True)
    with args.audit.open("x") as stream:
        json.dump(audit, stream, indent=2, ensure_ascii=False)
        stream.write("\n")
    print(json.dumps({key: value for key, value in audit.items()
                      if key not in {"changedExistingClips", "createdClipIdsByStep"}}, indent=2))


if __name__ == "__main__":
    main()
