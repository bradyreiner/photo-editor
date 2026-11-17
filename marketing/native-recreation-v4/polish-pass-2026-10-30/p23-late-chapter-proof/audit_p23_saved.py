#!/usr/bin/env python3
"""Read-only P23 saved-native-state audit against the P20 baseline and receipts."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
POLISH = HERE.parent
P20 = POLISH / "native-projects/looksy-polish-pass20-carousel-complete.json"
P20_SHA = "2728aaec37df78003811f0aab81ac54ffc4142b986c0fa503826eb79b2dc23b1"
P20_HASH = "801fea1560cd6ecd77a940d7926e63948d367598d8ec81b79eef1ffa319ae799"
RESOURCE = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"

SETTER_TO_FIELD = {
    "text": "titleText", "fontFamily": "titleFontFamily", "fontSize": "titleFontSize",
    "fontWeight": "titleFontWeight", "letterSpacing": "titleLetterSpacing",
    "lineHeight": "titleLineHeight", "color": "titleColor", "positionX": "titlePositionX",
    "positionY": "titlePositionY", "alignment": "titleAlignment",
    "backgroundColor": "titleBackgroundColor", "backgroundPadding": "captionBackgroundPadding",
    "backgroundRadius": "captionBackgroundRadius", "shapeType": "titleShapeType",
    "shapeWidth": "titleShapeWidth", "shapeHeight": "titleShapeHeight",
    "shapeCornerRadius": "titleShapeCornerRadius", "shapeStrokeColor": "titleShapeStrokeColor",
    "shapeStrokeWidth": "titleShapeStrokeWidth",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(value: Any) -> str:
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


def index(project: dict[str, Any]) -> tuple[dict[str, tuple[dict, dict]], dict[str, dict]]:
    clips: dict[str, tuple[dict, dict]] = {}
    tracks = {}
    for track in project["tracks"]:
        tracks[track["trackId"]] = track
        for clip in track.get("clips", []):
            clips[clip["clipId"]] = (track, clip)
    return clips, tracks


def close(actual: Any, expected: Any, *, tolerance: float = 1e-9) -> bool:
    if isinstance(expected, (int, float)) and not isinstance(expected, bool):
        return isinstance(actual, (int, float)) and math.isclose(
            float(actual), float(expected), rel_tol=0.0, abs_tol=tolerance
        )
    return actual == expected


def field_tolerance(field: str, expected: Any) -> float:
    if field in {"titleShapeWidth", "shapeWidth"}:
        return 0.35 / 1920.0
    if field in {"titleShapeHeight", "shapeHeight"}:
        # Native graphics can normalize a subpixel source rule to a one-pixel
        # minimum. The 1.1px source divider permits at most a 0.35px delta.
        return 0.35 / 1080.0 if float(expected) > 0.002 else 0.35 / 1080.0
    if field in {"titlePositionX", "positionX"}:
        return 0.25 / 1920.0
    if field in {"titlePositionY", "positionY"}:
        return 0.25 / 1080.0
    if field in {"titleFontSize", "fontSize"}:
        return 0.01
    return 1e-9


def expect_value(actual: Any, expected: Any, label: str, field: str) -> None:
    require(close(actual, expected, tolerance=field_tolerance(field, expected)),
            f"{label}: {field} expected {expected!r}, got {actual!r}")


def normalized_keyframes(rows: list[dict[str, Any]], parameter: str) -> list[dict[str, Any]]:
    return [
        {"localSeconds": float(row.get("localSeconds", row.get("seconds"))),
         "value": float(row["value"]), "interpolation": row.get("interpolation", "linear")}
        for row in rows if row.get("parameter") in (None, parameter)
    ]


def resolve_receipt_refs(value: Any, created_ids: dict[str, str]) -> Any:
    if isinstance(value, list):
        return [resolve_receipt_refs(item, created_ids) for item in value]
    if isinstance(value, dict):
        if set(value) == {"result", "field"} and value.get("field") == "createdClipId":
            creator = value.get("result")
            require(creator in created_ids, f"Unresolved create receipt reference: {creator}")
            return created_ids[creator]
        return {key: resolve_receipt_refs(item, created_ids) for key, item in value.items()}
    return value


def compare_channels(actual_clip: dict, channels: list[dict], label: str) -> None:
    keyframes = actual_clip.get("keyframes", [])
    requested_parameters = {channel["parameter"] for channel in channels}
    for channel in channels:
        parameter = channel["parameter"]
        actual = normalized_keyframes(keyframes, parameter)
        expected = channel["keyframes"]
        require(len(actual) == len(expected),
                f"{label}/{parameter}: keyframe count {len(actual)} != {len(expected)}")
        for i, (got, wanted) in enumerate(zip(actual, expected)):
            require(math.isclose(got["localSeconds"], wanted["localSeconds"], abs_tol=1e-9),
                    f"{label}/{parameter}[{i}]: local time differs")
            require(math.isclose(got["value"], wanted["value"], abs_tol=1e-8),
                    f"{label}/{parameter}[{i}]: value differs")
            require(got["interpolation"] == wanted.get("interpolation", "linear"),
                    f"{label}/{parameter}[{i}]: interpolation differs")
    unexpected = sorted({row.get("parameter") for row in keyframes} - requested_parameters)
    require(not unexpected, f"{label}: unexpected animation channels {unexpected}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bound-plan", type=Path,
                        default=POLISH / "pass23-late-bound.json")
    parser.add_argument("--saved-project", type=Path,
                        default=POLISH / "native-projects/looksy-polish-pass23-late-proof.json")
    parser.add_argument("--checkpoint", type=Path,
                        default=POLISH / "checkpoint-pass23-late-proof-result.json")
    parser.add_argument("--execution-receipt", type=Path,
                        default=POLISH / "pass23-late.execution.json")
    parser.add_argument("--output", type=Path,
                        default=HERE / "p23-late-native-saved-audit.json")
    args = parser.parse_args()

    plan_bytes = args.bound_plan.read_bytes()
    plan = json.loads(plan_bytes)
    before_bytes = P20.read_bytes()
    require(hashlib.sha256(before_bytes).hexdigest() == P20_SHA,
            "Pinned P20 baseline changed; regenerate this auditor after a fresh review.")
    before = json.loads(before_bytes)
    saved_bytes = args.saved_project.read_bytes()
    saved = json.loads(saved_bytes)
    checkpoint = json.loads(args.checkpoint.read_text())
    execution = json.loads(args.execution_receipt.read_text())

    saved_sha = hashlib.sha256(saved_bytes).hexdigest()
    require(checkpoint.get("fileSha256") == saved_sha,
            "Saved project bytes do not match the native save checkpoint receipt.")
    require(checkpoint.get("documentHash") and checkpoint.get("documentHash") ==
            checkpoint.get("taskStateHash", checkpoint.get("documentHash")),
            "Native checkpoint has no consistent document/task-state hash.")
    require(plan.get("resourceId") == RESOURCE, "P23 resource ID mismatch")
    require(plan.get("planningSnapshot", {}).get("sha256") ==
            "b6fc44862237506bb661ce69f3b5cb82e67806fe573d4274dfaa1952b0d4f37b",
            "Bound plan must retain the reviewed P22 planning snapshot.")
    bound_doc_hash = plan.get("startingDocumentHash")
    require(isinstance(bound_doc_hash, str) and len(bound_doc_hash) == 64,
            "Bound plan must carry the exact task document hash.")
    require(bound_doc_hash == P20_HASH,
            "P23 task must be bound to the current P20 saved document hash.")
    require(plan.get("planningSnapshot", {}).get("path") == str(
        (POLISH / "native-projects/looksy-polish-pass22-photo-proof.json").resolve()),
        "Bound plan planningSnapshot.path is not the exact P22 baseline path.")
    require(plan.get("planningSnapshot", {}).get("documentHash") ==
            "c2fb601cf1808116a7aaaa87aaecfb22c340b4f4bf719876cb9b8be932918b0c",
            "Bound plan P22 native document hash changed.")
    if checkpoint.get("taskId") and plan.get("taskId"):
        require(checkpoint["taskId"] == plan["taskId"], "Native checkpoint task ID differs from bound plan.")
    plan_sha = hashlib.sha256(plan_bytes).hexdigest()
    require(execution.get("planSha256") == plan_sha,
            "Execution receipt is not bound to the supplied exact plan bytes.")
    expected_steps = {step["id"]: step for step in plan["steps"]}
    rows = execution.get("steps")
    require(isinstance(rows, list) and len(rows) == len(expected_steps),
            "Execution receipt step count differs from the bound plan.")
    receipt_steps = {}
    created_ids: dict[str, str] = {}
    for row in rows:
        sid = row.get("id")
        require(sid in expected_steps and sid not in receipt_steps,
                f"Unexpected or duplicate receipt step: {sid}")
        receipt_steps[sid] = row
        require(row.get("result", {}).get("ok") is True,
                f"Native operation did not complete successfully: {sid}")
        expected = expected_steps[sid]
        require(row.get("commandId") == expected.get("commandId"),
                f"Command identity differs in receipt for {sid}")
        if expected["commandId"] in {
            "timeline.action.createTimelineTitle", "timeline.action.createTimelineGraphic"
        }:
            created = row.get("result", {}).get("createdClipId")
            require(isinstance(created, str) and created,
                    f"Create operation lacks a durable createdClipId result: {sid}")
            created_ids[sid] = created
    require(set(receipt_steps) == set(expected_steps), "Execution receipt is missing P23 steps.")
    require(len(created_ids) == 12 and len(set(created_ids.values())) == 12,
            "Expected 12 unique receipt-created P23 clips.")
    for sid, step in expected_steps.items():
        resolved = resolve_receipt_refs(step["values"], created_ids)
        require(canonical(receipt_steps[sid].get("values")) == canonical(resolved),
                f"Execution receipt command arguments differ from bound plan: {sid}")

    before_clips, before_tracks = index(before)
    after_clips, after_tracks = index(saved)
    original_ids = set(before_clips)
    target_ids = set(plan.get("placementGuards", {}).get("existingTargetClipIds", []))
    require(len(target_ids) == 24, f"Expected 24 existing targets, found {len(target_ids)}")
    created_set = set(created_ids.values())
    require(len(before_clips) == 1453 and len(after_clips) == 1465,
            f"P23 clip count {len(after_clips)} != expected P20 1453 + 12")
    require(original_ids <= set(after_clips), "P23 removed an original P20 clip.")
    require(created_set.isdisjoint(original_ids) and created_set <= set(after_clips),
            "Receipt-created IDs are duplicated or absent from the saved project.")
    require(list(before_tracks) == list(after_tracks), "P23 changed global track order.")
    for tid in before_tracks:
        left, right = before_tracks[tid], after_tracks[tid]
        require({k: v for k, v in left.items() if k != "clips"} ==
                {k: v for k, v in right.items() if k != "clips"},
                f"P23 changed track metadata/order fields: {tid}")
        old_order = [c["clipId"] for c in left["clips"]]
        new_order = [c["clipId"] for c in right["clips"] if c["clipId"] not in created_set]
        require(new_order == old_order, f"P23 changed existing per-track clip order: {tid}")

    expected_changes: dict[str, dict[str, Any]] = {}
    expected_landscape: dict[str, dict[str, Any]] = {}
    for step in plan["steps"]:
        if step["commandId"] != "timeline.action.setTimelineClipTitle":
            continue
        values = step["values"]
        cid = values["clipId"]
        require(cid in target_ids, f"Unexpected title setter target {cid}")
        mapped = {SETTER_TO_FIELD[key]: value for key, value in values["values"].items()
                  if key in SETTER_TO_FIELD}
        expected_changes.setdefault(cid, {}).update(mapped)
        expected_landscape.setdefault(cid, {}).update(mapped)
    require(set(expected_changes) == target_ids,
            f"Plan target list does not match title setters: {len(expected_changes)}")

    for cid in target_ids:
        btrack, bclip = before_clips[cid]
        atrack, aclip = after_clips[cid]
        require(btrack["trackId"] == atrack["trackId"], f"Existing target moved tracks: {cid}")
        for field in ("startSeconds", "durationSeconds", "mediaType", "assetId", "filePath",
                      "keyframes", "effectStack", "effectSettings", "colorGrade", "clipMask"):
            require(aclip.get(field) == bclip.get(field),
                    f"Existing target changed protected source/timing/motion field {cid}/{field}")
        allowed = set(expected_changes[cid])
        all_fields = set(bclip) | set(aclip)
        for field in all_fields - allowed - {"outputVariantOverrides"}:
            require(aclip.get(field) == bclip.get(field),
                    f"Unexpected existing target change {cid}/{field}")
        for field, expected in expected_changes[cid].items():
            expect_value(aclip.get(field), expected, cid, field)
        b_overrides = bclip.get("outputVariantOverrides", {})
        a_overrides = aclip.get("outputVariantOverrides", {})
        require(set(a_overrides) == set(b_overrides), f"Output variant set changed on {cid}")
        override_changed = set(expected_changes[cid]) & {
            "titleFontFamily", "titleFontSize", "titleFontWeight", "titleLetterSpacing",
            "titleLineHeight", "titleColor", "titlePositionX", "titlePositionY",
            "titleAlignment", "titleBackgroundColor", "captionBackgroundPadding",
            "captionBackgroundRadius",
        }
        for variant in b_overrides:
            if variant != "landscape":
                require(a_overrides[variant] == b_overrides[variant],
                        f"Non-landscape override changed on {cid}/{variant}")
                continue
            old, new = b_overrides[variant], a_overrides[variant]
            for field in set(old) | set(new):
                if field not in allowed:
                    require(new.get(field) == old.get(field),
                            f"Unexpected landscape override change {cid}/{field}")
            for field, expected in expected_landscape[cid].items():
                # The native title contract stores text and shape dimensions in
                # top-level fields. The active variant carries only these style
                # properties; requiring absent variant keys would be incorrect.
                if field not in override_changed:
                    continue
                expect_value(new.get(field), expected, cid + "/landscape", field)

    creates = {step["id"]: step for step in plan["steps"] if step["commandId"] in {
        "timeline.action.createTimelineTitle", "timeline.action.createTimelineGraphic"
    }}
    for sid, step in creates.items():
        cid = created_ids[sid]
        track, clip = after_clips[cid]
        values = step["values"]
        style = values["values"]
        require(track["trackId"] == values["trackId"], f"Created clip track mismatch: {sid}")
        require(clip.get("startSeconds") == values["startSeconds"] and
                clip.get("durationSeconds") == values["durationSeconds"],
                f"Created clip timing mismatch: {sid}")
        require(clip.get("mediaType") == "title", f"Created native title/graphic is not editable title: {sid}")
        require(not clip.get("filePath"), f"P23 created an external media dependency: {sid}")
        if step["commandId"] == "timeline.action.createTimelineTitle":
            require(clip.get("titleText") == style.get("text"), f"Created title text mismatch: {sid}")
            fields = {k: SETTER_TO_FIELD[k] for k in style if k in SETTER_TO_FIELD}
        else:
            require(clip.get("titleText", "") == "", f"Native divider acquired unexpected copy: {sid}")
            require(clip.get("titleShapeType") == values["shapeType"], f"Graphic shape mismatch: {sid}")
            fields = {k: SETTER_TO_FIELD[k] for k in style if k in SETTER_TO_FIELD}
        for input_field, output_field in fields.items():
            expect_value(clip.get(output_field), style[input_field], sid, output_field)
        # Geometry is serialized as normalized sequence coordinates. Check the
        # active landscape contract too where the native model stores it there.
        variant = clip.get("outputVariantOverrides", {}).get("landscape", {})
        for input_field, output_field in fields.items():
            if output_field in {
                "titleFontFamily", "titleFontSize", "titleFontWeight", "titleLetterSpacing",
                "titleLineHeight", "titleColor", "titlePositionX", "titlePositionY",
                "titleAlignment", "titleBackgroundColor", "captionBackgroundPadding",
                "captionBackgroundRadius",
            }:
                expect_value(variant.get(output_field), style[input_field],
                             sid + "/landscape", output_field)
        expected_channels = []
        for anim in plan["steps"]:
            if anim["commandId"] != "timeline.action.setTimelineClipAnimation":
                continue
            ref = anim["values"]["clipId"]
            if isinstance(ref, dict) and ref.get("result") == sid:
                expected_channels.extend(anim["values"]["channels"])
        compare_channels(clip, expected_channels, sid)

    untouched_ids = original_ids - target_ids
    for cid in untouched_ids:
        require(after_clips[cid][1] == before_clips[cid][1],
                f"Untouched P20 clip changed: {cid}")
    for key in set(before) | set(saved):
        if key in {"tracks", "updatedAt"}:
            continue
        require(saved.get(key) == before.get(key), f"P23 changed project-level field {key}")

    audit = {
        "schema": "looksy.p23-late-chapter-saved-audit/v1",
        "auditKind": "read-only saved native project, exact P20 baseline, bound plan and execution receipt",
        "productionCallsMadeByAuditor": False,
        "passed": True,
        "boundPlan": {"path": str(args.bound_plan.resolve()), "sha256": plan_sha,
                      "taskId": plan.get("taskId"), "documentHash": bound_doc_hash,
                      "operationCount": len(plan["steps"])},
        "baseline": {"path": str(P20.resolve()), "fileSha256": P20_SHA,
                     "documentHash": "801fea1560cd6ecd77a940d7926e63948d367598d8ec81b79eef1ffa319ae799",
                     "trackCount": len(before["tracks"]), "clipCount": len(before_clips)},
        "saved": {"path": str(args.saved_project.resolve()), "fileSha256": saved_sha,
                  "documentHash": checkpoint["documentHash"], "checkpoint": str(args.checkpoint.resolve()),
                  "checkpointSha256": sha(args.checkpoint), "readbackBytesMatch": True},
        "executionReceipt": {"path": str(args.execution_receipt.resolve()),
                             "sha256": sha(args.execution_receipt), "planSha256": execution["planSha256"],
                             "allStepsUniqueSuccessful": True, "receiptCreatedIds": created_ids},
        "nativeInventory": {"trackCount": len(saved["tracks"]), "baselineClipCount": len(before_clips),
                            "savedClipCount": len(after_clips), "existingTargets": len(target_ids),
                            "createdClips": len(created_set), "untouchedOriginalClips": len(untouched_ids),
                            "expectedUntouchedOriginalCount": 1429,
                            "trackOrderAndMetadataUnchanged": True,
                            "allUntouchedOriginalClipsUnchanged": True,
                            "allExistingTargetKeyframesAndTimingUnchanged": True,
                            "allExistingTargetChangesMatchPlan": True,
                            "nonLandscapeOverridesUnchanged": True,
                            "allCreatedClipsReceiptBoundAndEditable": True,
                            "audioAndOriginalMediaInventoryUnchanged": True},
        "normalizationPolicy": {
            "sourceDimensions": "geometry setters are compared in normalized full-frame coordinates; position tolerance <=0.25px, ordinary dimensions <=0.35px, font size <=0.01pt",
            "dividerMinimum": "1.1px source rules allow at most 0.35px native size normalization; curve/time keys remain exact within 1e-8",
            "text": "all requested title strings/newlines and colors are exact",
        },
        "visualProofLimit": "This verifies the saved editable project and authoring receipts only. A fresh native render and pixel review are still required for visual qualification.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(audit, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({"passed": True, "audit": str(args.output.resolve()),
                      "savedClipCount": len(after_clips), "createdClipCount": len(created_set),
                      "existingTargetCount": len(target_ids), "planSha256": plan_sha}, indent=2))


if __name__ == "__main__":
    main()
