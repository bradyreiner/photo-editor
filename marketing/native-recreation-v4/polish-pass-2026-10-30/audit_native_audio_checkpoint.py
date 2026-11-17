#!/usr/bin/env python3
"""Read-only audit of the Pass 10 native audio authoring result against Pass 9.

This utility never connects to Looksy and never edits either project. It compares
the saved native JSON with the exact task plan and its durable NDJSON receipts.

Example after a native save-copy has been produced:
  python3 audit_native_audio_checkpoint.py \
    --candidate native-projects/looksy-polish-pass10.json \
    --report pass10-audio-postsave-audit.json
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Iterable


HERE = Path(__file__).resolve().parent
PLAN_DIR = HERE / "native-audio-plan-p9"
TASK_ID = "790a6af0-976a-43e3-ae54-15105e7e4bfc"
BASELINE = HERE / "native-projects/looksy-polish-pass9.json"
PLAN = PLAN_DIR / "receipts" / TASK_ID / "new-gods-native-authoring-plan.json"
RECEIPT = PLAN_DIR / "receipts" / TASK_ID / "receipt.ndjson"
MANIFEST = PLAN_DIR / "new-gods-native-audio-cue-manifest-p9.json"
BASELINE_SHA256 = "51531c69e912a50908cb99ce5a795d697c7fb22e24a2dcf90cbc886b9a35f0b4"
EXPECTED_BASE_CLIPS = 1222
EXPECTED_NEW_CLIPS = 55
EXPECTED_NEW_TRACKS = 4
EXPECTED_DURATION_SECONDS = 298.0
NUMERIC_EPSILON = 1e-9
FRAME_GRID_EPSILON = 1e-6
MAX_DIFF_SAMPLES = 100


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def value_text(value: Any, limit: int = 180) -> str:
    text = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
    return text if len(text) <= limit else text[: limit - 3] + "..."


def equal_value(left: Any, right: Any) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return type(left) is type(right) and left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return math.isfinite(float(left)) and math.isfinite(float(right)) and abs(float(left) - float(right)) <= NUMERIC_EPSILON
    return type(left) is type(right) and left == right


def walk_differences(expected: Any, actual: Any, path: str = "") -> Iterable[tuple[str, Any, Any]]:
    if isinstance(expected, dict) and isinstance(actual, dict):
        for key in sorted(set(expected) | set(actual)):
            child = f"{path}.{key}" if path else str(key)
            if key not in expected:
                yield child, "<missing>", actual[key]
            elif key not in actual:
                yield child, expected[key], "<missing>"
            else:
                yield from walk_differences(expected[key], actual[key], child)
        return
    if isinstance(expected, list) and isinstance(actual, list):
        if len(expected) != len(actual):
            yield f"{path}.length", len(expected), len(actual)
        for index, (left, right) in enumerate(zip(expected, actual)):
            yield from walk_differences(left, right, f"{path}[{index}]")
        return
    if not equal_value(expected, actual):
        yield path or "$", expected, actual


def is_timing_path(path: str) -> bool:
    last = path.rsplit(".", 1)[-1]
    if last.endswith(".length"):
        return False
    if last.startswith("keyframes["):
        return False
    return last in {
        "startSeconds",
        "trimStartSeconds",
        "trimEndSeconds",
        "durationSeconds",
        "seconds",
        "localSeconds",
    }


def timing_classification(expected: Any, actual: Any, fps: float) -> str:
    if not isinstance(expected, (int, float)) or isinstance(expected, bool):
        return "timing-mismatch"
    if not isinstance(actual, (int, float)) or isinstance(actual, bool) or fps <= 0:
        return "timing-mismatch"
    expected_value = float(expected)
    actual_value = float(actual)
    if not math.isfinite(expected_value) or not math.isfinite(actual_value):
        return "timing-mismatch"
    rounded_frame = round(actual_value * fps)
    candidate_on_frame = abs(actual_value - rounded_frame / fps) <= FRAME_GRID_EPSILON
    expected_on_frame = abs(expected_value * fps - round(expected_value * fps)) <= FRAME_GRID_EPSILON
    if candidate_on_frame and not expected_on_frame and abs(actual_value - expected_value) <= (1.0 / fps + FRAME_GRID_EPSILON):
        return "candidate-frame-rounded; FAIL"
    return "timing-mismatch; FAIL"


def append_check(report: dict[str, Any], name: str, passed: bool, details: Any = None) -> None:
    row: dict[str, Any] = {"name": name, "status": "passed" if passed else "failed"}
    if details is not None:
        row["details"] = details
    report["checks"].append(row)
    if not passed:
        report["failures"].append(name)


def flatten_document(document: dict[str, Any], label: str, report: dict[str, Any]) -> tuple[dict[str, dict[str, Any]], dict[str, dict[str, Any]], dict[str, str]]:
    tracks: dict[str, dict[str, Any]] = {}
    clips: dict[str, dict[str, Any]] = {}
    clip_owner: dict[str, str] = {}
    duplicate_track_ids: list[str] = []
    duplicate_clip_ids: list[str] = []
    for track in document.get("tracks", []):
        track_id = track.get("trackId")
        if not isinstance(track_id, str) or not track_id:
            duplicate_track_ids.append(f"{track_id!r} (missing/invalid)")
            continue
        if track_id in tracks:
            duplicate_track_ids.append(track_id)
        tracks[track_id] = track
        for clip in track.get("clips", []):
            clip_id = clip.get("clipId")
            if not isinstance(clip_id, str) or not clip_id:
                duplicate_clip_ids.append(f"{clip_id!r} (missing/invalid)")
                continue
            if clip_id in clips:
                duplicate_clip_ids.append(clip_id)
            clips[clip_id] = clip
            clip_owner[clip_id] = track_id
    append_check(
        report,
        f"{label} stable track and clip IDs are present and unique",
        not duplicate_track_ids and not duplicate_clip_ids,
        {"duplicateTrackIds": duplicate_track_ids[:20], "duplicateClipIds": duplicate_clip_ids[:20]},
    )
    return tracks, clips, clip_owner


def effective_end_seconds(clip: dict[str, Any]) -> float | None:
    try:
        start = float(clip.get("startSeconds", 0.0))
        trim_start = float(clip.get("trimStartSeconds", 0.0))
        trim_end = float(clip.get("trimEndSeconds", clip.get("durationSeconds", 0.0)))
        rate = float(clip.get("playbackRate", 1.0) or 1.0)
        if not all(math.isfinite(value) for value in (start, trim_start, trim_end, rate)) or rate <= 0:
            return None
        return start + max(0.0, trim_end - trim_start) / rate
    except (TypeError, ValueError):
        return None


def resolve_plan_refs(value: Any, completed: dict[str, dict[str, Any]]) -> Any:
    if isinstance(value, dict):
        if set(value) == {"result", "field"}:
            source = completed.get(value["result"], {}).get("result", {})
            return source.get(value["field"])
        return {key: resolve_plan_refs(item, completed) for key, item in value.items()}
    if isinstance(value, list):
        return [resolve_plan_refs(item, completed) for item in value]
    return value


def expected_clip_keyframes(plan_steps: list[dict[str, Any]], step_to_clip: dict[str, str], completed: dict[str, dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    expected: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for step in plan_steps:
        if step.get("commandId") != "timeline.action.setTimelineClipAnimation":
            continue
        step_id = step["id"]
        completed_record = completed.get(step_id)
        values = step.get("values", {})
        clip_ref = values.get("clipId", {})
        source_step = clip_ref.get("result") if isinstance(clip_ref, dict) else None
        clip_id = step_to_clip.get(source_step or "")
        if not clip_id:
            continue
        for channel in values.get("channels", []):
            if channel.get("parameter") == "audioGainDb":
                expected[clip_id] = channel.get("keyframes", [])
        if completed_record:
            actual_values = completed_record.get("values", {})
            for channel in actual_values.get("channels", []):
                if channel.get("parameter") == "audioGainDb" and clip_id not in expected:
                    expected[clip_id] = channel.get("keyframes", [])
    return dict(expected)


def normalized_gain_keyframes(clip: dict[str, Any]) -> list[dict[str, Any]]:
    rows = []
    for keyframe in clip.get("keyframes", []) or []:
        if keyframe.get("parameter") == "audioGainDb":
            rows.append({
                "localSeconds": keyframe.get("seconds"),
                "value": keyframe.get("value"),
                "interpolation": keyframe.get("interpolation", "linear"),
            })
    def numeric_key(row: dict[str, Any], name: str) -> float:
        try:
            value = float(row.get(name, 0.0))
            return value if math.isfinite(value) else float("inf")
        except (TypeError, ValueError):
            return float("inf")

    return sorted(rows, key=lambda row: (numeric_key(row, "localSeconds"), numeric_key(row, "value")))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--candidate", required=True, type=Path, help="Pass 10 saved native JSON checkpoint")
    parser.add_argument("--baseline", type=Path, default=BASELINE)
    parser.add_argument("--plan", type=Path, default=PLAN)
    parser.add_argument("--receipt", type=Path, default=RECEIPT)
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--report", type=Path, help="Optional JSON report path; omitted means stdout only")
    args = parser.parse_args()

    report: dict[str, Any] = {
        "schema": "looksy.native-audio-checkpoint-audit/v1",
        "status": "running",
        "checks": [],
        "failures": [],
        "warnings": [],
        "timingDifferences": [],
        "frameRoundedTimingDifferences": [],
        "originalClipDifferences": {"count": 0, "samples": []},
        "originalTrackDifferences": {"count": 0, "samples": []},
        "selectionMetadataDifferences": [],
        "newClipDifferences": [],
        "inputs": {},
        "metrics": {},
    }

    paths = {
        "baseline": args.baseline,
        "candidate": args.candidate,
        "plan": args.plan,
        "receipt": args.receipt,
        "manifest": args.manifest,
    }
    missing = [str(path) for path in paths.values() if not path.is_file()]
    if missing:
        parser.error("required audit input missing: " + ", ".join(missing))
    if args.report and args.report.exists():
        parser.error("refusing to overwrite an existing report: " + str(args.report))
    report["inputs"] = {
        key: {"path": str(path.resolve()), "sha256": sha256_file(path)}
        for key, path in paths.items() if key != "receipt"
    }
    report["inputs"]["receipt"] = {"path": str(args.receipt.resolve()), "sha256": sha256_file(args.receipt)}

    baseline = load_json(args.baseline)
    candidate = load_json(args.candidate)
    plan = load_json(args.plan)
    manifest = load_json(args.manifest)
    guard_by_path = {guard.get("path"): guard.get("sha256", "").lower() for guard in plan.get("assetGuards", [])}

    baseline_hash = report["inputs"]["baseline"]["sha256"]
    append_check(
        report,
        "Pass 9 baseline is the verified checkpoint",
        baseline_hash == BASELINE_SHA256,
        {"expectedSha256": BASELINE_SHA256, "actualSha256": baseline_hash},
    )
    p9_result_path = HERE / "checkpoint-pass9-result.json"
    if p9_result_path.exists():
        p9_receipt = load_json(p9_result_path)
        append_check(
            report,
            "Pass 9 checkpoint receipt matches baseline document and file hashes",
            p9_receipt.get("fileSha256") == baseline_hash
            and p9_receipt.get("documentHash") == plan.get("startingDocumentHash"),
            {"receiptFileSha256": p9_receipt.get("fileSha256"), "receiptDocumentHash": p9_receipt.get("documentHash"), "planStartingDocumentHash": plan.get("startingDocumentHash")},
        )
        manifest_guard = manifest.get("nativeTimelineGuard", {})
        append_check(
            report,
            "Audio authoring plan and cue manifest are anchored to Pass 9",
            plan.get("taskId") == TASK_ID
            and plan.get("startingDocumentHash") == p9_receipt.get("documentHash")
            and manifest_guard.get("documentHash") == p9_receipt.get("documentHash")
            and manifest_guard.get("clipCount") == EXPECTED_BASE_CLIPS,
            {"planTaskId": plan.get("taskId"), "expectedTaskId": TASK_ID, "planStartingDocumentHash": plan.get("startingDocumentHash"), "manifestDocumentHash": manifest_guard.get("documentHash"), "pass9DocumentHash": p9_receipt.get("documentHash"), "manifestClipCount": manifest_guard.get("clipCount")},
        )

    fps = float(baseline.get("sequenceFormat", {}).get("frameRate", 0.0) or 0.0)
    report["metrics"].update({
        "baselineClipCount": EXPECTED_BASE_CLIPS,
        "expectedNewClipCount": EXPECTED_NEW_CLIPS,
        "expectedNewTrackCount": EXPECTED_NEW_TRACKS,
        "expectedSequenceDurationSeconds": EXPECTED_DURATION_SECONDS,
        "frameRate": fps,
    })
    baseline_tracks, baseline_clips, baseline_owners = flatten_document(baseline, "Pass 9", report)
    candidate_tracks, candidate_clips, candidate_owners = flatten_document(candidate, "Pass 10", report)
    report["metrics"].update({
        "baselineTrackCount": len(baseline_tracks),
        "candidateTrackCount": len(candidate_tracks),
        "baselineClipCountActual": len(baseline_clips),
        "candidateClipCount": len(candidate_clips),
    })

    append_check(report, "Sequence identity is preserved", candidate.get("timelineId") == baseline.get("timelineId"), {"baseline": baseline.get("timelineId"), "candidate": candidate.get("timelineId")})
    expected_format = baseline.get("sequenceFormat", {})
    actual_format = candidate.get("sequenceFormat", {})
    format_diffs = list(walk_differences(expected_format, actual_format, "sequenceFormat"))
    append_check(report, "Sequence geometry and frame/audio rates are preserved", not format_diffs, [{"path": p, "expected": value_text(e), "actual": value_text(a)} for p, e, a in format_diffs[:20]])

    receipt_events: list[dict[str, Any]] = []
    with args.receipt.open("r", encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            if not line.strip():
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                report["failures"].append(f"receipt line {line_number} is invalid JSON: {exc}")
                continue
            receipt_events.append(event)
    plan_hash = sha256_file(args.plan)
    receipt_hashes = sorted({event.get("planSha256") for event in receipt_events if event.get("planSha256")})
    append_check(report, "Every durable action receipt belongs to the exact authoring plan", receipt_hashes == [plan_hash], {"planSha256": plan_hash, "receiptPlanSha256Values": receipt_hashes})
    completed_lists: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for event in receipt_events:
        if event.get("event") == "invoke_completed" and event.get("stepId"):
            completed_lists[event["stepId"]].append(event)
    completed = {step_id: records[-1] for step_id, records in completed_lists.items() if records}
    plan_steps = plan.get("steps", [])
    action_steps = [step for step in plan_steps if step.get("commandId", "").startswith("timeline.action.")]
    incomplete_steps = [step.get("id") for step in action_steps if len(completed_lists.get(step.get("id", ""), [])) != 1]
    unsuccessful_steps = [step.get("id") for step in action_steps if step.get("id") in completed and completed[step["id"]].get("result", {}).get("ok") is not True]
    append_check(report, "All planned native mutation steps completed exactly once and succeeded", not incomplete_steps and not unsuccessful_steps, {"plannedMutationSteps": len(action_steps), "completedStepCount": sum(bool(completed_lists.get(step.get("id", ""))) for step in action_steps), "missingOrRepeatedStepIds": incomplete_steps[:30], "unsuccessfulStepIds": unsuccessful_steps[:30]})

    receipt_value_mismatches: list[dict[str, Any]] = []
    for step in action_steps:
        step_id = step.get("id", "")
        event = completed.get(step_id)
        if not event:
            continue
        expected_values = resolve_plan_refs(step.get("values", {}), completed)
        actual_values = event.get("values", {})
        diffs = list(walk_differences(expected_values, actual_values, step_id + ".values"))
        if diffs:
            receipt_value_mismatches.extend({"stepId": step_id, "path": path, "expected": value_text(left), "actual": value_text(right)} for path, left, right in diffs[:10])
    append_check(report, "Executed mutation values match the immutable authoring plan", not receipt_value_mismatches, {"problemCount": len(receipt_value_mismatches), "samples": receipt_value_mismatches[:30]})

    track_create_steps = [step for step in action_steps if step.get("commandId") == "timeline.action.createTimelineTrack"]
    create_clip_steps = [step for step in action_steps if step.get("commandId") == "timeline.action.createTimelineClip"]
    animation_steps = [step for step in action_steps if step.get("commandId") == "timeline.action.setTimelineClipAnimation"]
    track_create_ids: dict[str, str] = {}
    for step in track_create_steps:
        result = completed.get(step.get("id", ""), {}).get("result", {})
        track_id = result.get("trackId")
        if isinstance(track_id, str) and track_id:
            track_create_ids[step["id"]] = track_id
    new_track_ids = set(track_create_ids.values())
    baseline_track_ids = set(baseline_tracks)
    candidate_track_ids = set(candidate_tracks)
    actual_new_track_ids = candidate_track_ids - baseline_track_ids
    append_check(report, "Exactly four new stable-ID audio lanes were added", len(actual_new_track_ids) == EXPECTED_NEW_TRACKS and actual_new_track_ids == new_track_ids, {"expectedNewTrackIdsFromReceipts": sorted(new_track_ids), "actualNewTrackIds": sorted(actual_new_track_ids)})
    missing_old_tracks = sorted(baseline_track_ids - candidate_track_ids)
    append_check(report, "Every Pass 9 track remains present", not missing_old_tracks, {"missingTrackIds": missing_old_tracks[:30]})

    track_role_by_id: dict[str, str] = {}
    expected_new_track_by_id: dict[str, dict[str, Any]] = {}
    for step in track_create_steps:
        track_id = track_create_ids.get(step.get("id", ""))
        if not track_id:
            continue
        expected_new_track_by_id[track_id] = dict(step.get("values", {}))
    role_steps = [step for step in action_steps if step.get("commandId") == "timeline.action.setTimelineTrackAudioRole"]
    role_values_by_id: dict[str, str] = {}
    for step in role_steps:
        values = resolve_plan_refs(step.get("values", {}), completed)
        if values.get("trackId"):
            role_values_by_id[str(values["trackId"])] = str(values.get("audioRole", ""))
    role_diffs: list[dict[str, Any]] = []
    expected_lane_counts: Counter[str] = Counter()
    role_bus_by_id: dict[str, str] = {}
    for step in track_create_steps:
        track_id = track_create_ids.get(step.get("id", ""))
        if not track_id or track_id not in candidate_tracks:
            role_diffs.append({"trackId": track_id, "error": "created track missing from candidate"})
            continue
        track = candidate_tracks[track_id]
        expected = step.get("values", {})
        expected_role = role_values_by_id.get(track_id, expected.get("audioRole", ""))
        expected_name = expected.get("name", "")
        actual_name = track.get("customName", track.get("name", ""))
        if track.get("kind") != "audio" or track.get("audioRole") != expected_role or actual_name != expected_name or track.get("muted") is not False:
            role_diffs.append({"trackId": track_id, "expected": {"kind": "audio", "audioRole": expected_role, "customName": expected_name, "muted": False}, "actual": {"kind": track.get("kind"), "audioRole": track.get("audioRole"), "customName": actual_name, "muted": track.get("muted")}})
        role_step = next((candidate for candidate in role_steps if resolve_plan_refs(candidate.get("values", {}), completed).get("trackId") == track_id), None)
        role_result = completed.get(role_step.get("id", ""), {}).get("result", {}) if role_step else {}
        expected_bus = role_result.get("audioBus")
        if expected_bus is not None:
            role_bus_by_id[track_id] = str(expected_bus)
            if track.get("audioBus") != expected_bus:
                role_diffs.append({"trackId": track_id, "field": "audioBus", "expectedFromRoleReceipt": expected_bus, "actual": track.get("audioBus")})
        track_role_by_id[track_id] = expected_role
    append_check(report, "New lanes have the planned names, audio roles, and unmuted final state", not role_diffs, role_diffs[:20])

    clip_create_step_to_id: dict[str, str] = {}
    new_clip_ids: set[str] = set()
    created_clip_by_id: dict[str, dict[str, Any]] = {}
    expected_clip_values: dict[str, dict[str, Any]] = {}
    clip_receipt_errors: list[dict[str, Any]] = []
    for step in create_clip_steps:
        step_id = step.get("id", "")
        event = completed.get(step_id)
        result = event.get("result", {}) if event else {}
        clip_id = result.get("createdClipId") or result.get("clip", {}).get("clipId")
        if not isinstance(clip_id, str) or not clip_id:
            clip_receipt_errors.append({"stepId": step_id, "error": "receipt has no created stable clip ID"})
            continue
        if clip_id in new_clip_ids:
            clip_receipt_errors.append({"stepId": step_id, "clipId": clip_id, "error": "duplicate created clip ID in receipts"})
        new_clip_ids.add(clip_id)
        clip_create_step_to_id[step_id] = clip_id
        created_clip_by_id[clip_id] = step
        expected_clip_values[clip_id] = resolve_plan_refs(step.get("values", {}), completed)
    append_check(report, "All 55 new clip IDs come from successful native receipts", not clip_receipt_errors and len(new_clip_ids) == EXPECTED_NEW_CLIPS, {"receiptCreatedClipCount": len(new_clip_ids), "errors": clip_receipt_errors[:20]})
    actual_new_clip_ids = set(candidate_clips) - set(baseline_clips)
    append_check(report, "Exactly the receipt-created 55 clips were added", len(actual_new_clip_ids) == EXPECTED_NEW_CLIPS and actual_new_clip_ids == new_clip_ids, {"receiptCreatedCount": len(new_clip_ids), "candidateAddedCount": len(actual_new_clip_ids), "receiptIdsMissingFromCandidate": sorted(new_clip_ids - actual_new_clip_ids)[:30], "unreceiptedCandidateIds": sorted(actual_new_clip_ids - new_clip_ids)[:30]})
    append_check(report, "All 1,222 original clips remain present", len(baseline_clips) == EXPECTED_BASE_CLIPS and set(baseline_clips).issubset(candidate_clips), {"baselineCount": len(baseline_clips), "preservedCount": len(set(baseline_clips) & set(candidate_clips)), "missingClipIds": sorted(set(baseline_clips) - set(candidate_clips))[:30]})

    original_clip_diff_samples: list[dict[str, Any]] = []
    original_clip_diff_count = 0
    for clip_id, before in baseline_clips.items():
        after = candidate_clips.get(clip_id)
        if after is None:
            continue
        for path, expected, actual in walk_differences(before, after, clip_id):
            original_clip_diff_count += 1
            sample = {"clipId": clip_id, "path": path, "expected": value_text(expected), "actual": value_text(actual)}
            if len(original_clip_diff_samples) < MAX_DIFF_SAMPLES:
                original_clip_diff_samples.append(sample)
            if is_timing_path(path):
                row = dict(sample)
                row["classification"] = timing_classification(expected, actual, fps)
                report["timingDifferences"].append(row)
                if row["classification"].startswith("candidate-frame-rounded"):
                    report["frameRoundedTimingDifferences"].append(row)
    report["originalClipDifferences"] = {"count": original_clip_diff_count, "samples": original_clip_diff_samples}
    append_check(report, "Every original clip field is preserved exactly (including media, trims, timing, and animation)", original_clip_diff_count == 0, {"differenceCount": original_clip_diff_count, "samples": original_clip_diff_samples[:20]})

    music_track_id = plan.get("expectedCurrentAudioState", {}).get("musicTrackId")
    original_track_diff_samples: list[dict[str, Any]] = []
    original_track_diff_count = 0
    selection_metadata_differences: list[dict[str, Any]] = []
    for track_id, before in baseline_tracks.items():
        after = candidate_tracks.get(track_id)
        if after is None:
            continue
        if before.get("targeted") != after.get("targeted"):
            selection_metadata_differences.append({
                "trackId": track_id,
                "baselineTargeted": before.get("targeted"),
                "candidateTargeted": after.get("targeted"),
                "classification": "transient native track targeting metadata",
            })
        before_without_clips = {k: v for k, v in before.items() if k != "clips"}
        after_without_clips = {k: v for k, v in after.items() if k != "clips"}
        before_without_clips.pop("targeted", None)
        after_without_clips.pop("targeted", None)
        if track_id == music_track_id:
            before_without_clips = {k: v for k, v in before_without_clips.items() if k != "muted"}
            after_without_clips = {k: v for k, v in after_without_clips.items() if k != "muted"}
        for path, expected, actual in walk_differences(before_without_clips, after_without_clips, track_id):
            original_track_diff_count += 1
            if len(original_track_diff_samples) < MAX_DIFF_SAMPLES:
                original_track_diff_samples.append({"trackId": track_id, "path": path, "expected": value_text(expected), "actual": value_text(actual)})
    report["originalTrackDifferences"] = {"count": original_track_diff_count, "samples": original_track_diff_samples}
    report["selectionMetadataDifferences"] = selection_metadata_differences
    expected_targeting_delta = []
    if baseline_tracks.get(music_track_id, {}).get("targeted") is True and candidate_tracks.get(music_track_id, {}).get("targeted") is False:
        expected_targeting_delta = [{
            "trackId": music_track_id,
            "baselineTargeted": True,
            "candidateTargeted": False,
            "classification": "transient native track targeting metadata",
        }]
    targeting_delta_is_expected = selection_metadata_differences == expected_targeting_delta
    append_check(report, "Original authored track fields are preserved (music mute is the only authored track-state change)", original_track_diff_count == 0, {"differenceCount": original_track_diff_count, "samples": original_track_diff_samples[:20], "transientTargetingFieldsExcludedAndReported": True})
    append_check(report, "Original track targeting metadata is reported as a separate transient delta", targeting_delta_is_expected, {"expectedObservedDelta": expected_targeting_delta, "actualDeltas": selection_metadata_differences})

    original_music_track = candidate_tracks.get(music_track_id, {}) if music_track_id else {}
    before_music_track = baseline_tracks.get(music_track_id, {}) if music_track_id else {}
    original_master_id = plan.get("expectedCurrentAudioState", {}).get("masterClipId")
    append_check(report, "Only the original music lane is newly muted", bool(music_track_id) and before_music_track.get("muted") is False and original_music_track.get("muted") is True, {"trackId": music_track_id, "baselineMuted": before_music_track.get("muted"), "candidateMuted": original_music_track.get("muted")})
    append_check(report, "Original reference mix clip remains unchanged on its original lane", original_master_id in candidate_clips and baseline_clips.get(original_master_id) == candidate_clips.get(original_master_id) and candidate_owners.get(original_master_id) == music_track_id, {"clipId": original_master_id, "ownerTrackId": candidate_owners.get(original_master_id)})
    unexpected_muted_original_lanes = sorted(track_id for track_id, track in candidate_tracks.items() if track_id in baseline_track_ids and track_id != music_track_id and track.get("muted") != baseline_tracks[track_id].get("muted"))
    append_check(report, "No other original lane mute state changed", not unexpected_muted_original_lanes, {"trackIds": unexpected_muted_original_lanes})

    keyframes_by_clip = expected_clip_keyframes(plan_steps, clip_create_step_to_id, completed)
    animation_clip_ids = set(keyframes_by_clip)
    append_check(report, "Every new audio clip has a planned gain-channel receipt", animation_clip_ids == new_clip_ids and len(animation_steps) == EXPECTED_NEW_CLIPS, {"newClipCount": len(new_clip_ids), "gainAnimationClipCount": len(animation_clip_ids), "animationStepCount": len(animation_steps)})
    new_clip_problems: list[dict[str, Any]] = []
    expected_lane_counts.clear()
    for clip_id in sorted(new_clip_ids):
        clip = candidate_clips.get(clip_id)
        step = created_clip_by_id.get(clip_id)
        expected = expected_clip_values.get(clip_id, {})
        owner_id = candidate_owners.get(clip_id)
        if clip is None or step is None:
            new_clip_problems.append({"clipId": clip_id, "error": "receipt clip is absent from native checkpoint"})
            continue
        expected_owner = expected.get("trackId")
        if owner_id != expected_owner:
            new_clip_problems.append({"clipId": clip_id, "field": "trackId", "expected": expected_owner, "actual": owner_id})
        if owner_id:
            expected_lane_counts[str(owner_id)] += 1
        fields = ["assetId", "filePath", "displayLabel", "startSeconds", "durationSeconds", "trimStartSeconds", "trimEndSeconds"]
        for field in fields:
            e = expected.get(field)
            a = clip.get(field)
            if not equal_value(e, a):
                detail = {"clipId": clip_id, "field": field, "expected": value_text(e), "actual": value_text(a)}
                if field in {"startSeconds", "durationSeconds", "trimStartSeconds", "trimEndSeconds"}:
                    detail["classification"] = timing_classification(e, a, fps)
                    report["timingDifferences"].append(detail)
                    if detail["classification"].startswith("candidate-frame-rounded"):
                        report["frameRoundedTimingDifferences"].append(detail)
                new_clip_problems.append(detail)
        if clip.get("mediaType") != "audio":
            new_clip_problems.append({"clipId": clip_id, "field": "mediaType", "expected": "audio", "actual": clip.get("mediaType")})
        if not equal_value(clip.get("playbackRate", 1.0), 1.0):
            new_clip_problems.append({"clipId": clip_id, "field": "playbackRate", "expected": 1.0, "actual": clip.get("playbackRate")})
        create_receipt = completed.get(step.get("id", ""), {}).get("result", {}).get("clip", {})
        for field in ("audioChannelMode", "audioSwapChannels", "audioInvertLeftPhase", "audioInvertRightPhase", "audioGainDb"):
            expected_default = create_receipt.get(field)
            if expected_default is not None and not equal_value(expected_default, clip.get(field)):
                new_clip_problems.append({"clipId": clip_id, "field": field, "expectedFromCreateReceipt": expected_default, "actual": clip.get(field)})
        if clip.get("audioMuted") is True or clip.get("audioPlaybackEnabled") is False:
            new_clip_problems.append({"clipId": clip_id, "field": "effectiveAudioState", "error": "explicit saved clip state is muted or playback-disabled", "audioMuted": clip.get("audioMuted"), "audioPlaybackEnabled": clip.get("audioPlaybackEnabled")})
        if clip.get("sourceContentSha256"):
            path = expected.get("filePath")
            expected_source_sha = guard_by_path.get(path)
            if expected_source_sha and str(clip["sourceContentSha256"]).lower() != expected_source_sha:
                new_clip_problems.append({"clipId": clip_id, "field": "sourceContentSha256", "expected": expected_source_sha, "actual": clip.get("sourceContentSha256")})
        planned_gain = keyframes_by_clip.get(clip_id, [])
        actual_gain = normalized_gain_keyframes(clip)
        gain_diffs = list(walk_differences(planned_gain, actual_gain, f"{clip_id}.audioGainDb.keyframes"))
        for path, e, a in gain_diffs[:20]:
            detail = {"clipId": clip_id, "path": path, "expected": value_text(e), "actual": value_text(a)}
            if path.endswith("localSeconds") or path.endswith("localSeconds.length"):
                detail["classification"] = timing_classification(e, a, fps)
                report["timingDifferences"].append(detail)
                if detail["classification"].startswith("candidate-frame-rounded"):
                    report["frameRoundedTimingDifferences"].append(detail)
            new_clip_problems.append(detail)
        asset_id = expected.get("assetId")
        if asset_id != clip.get("assetId"):
            continue
    report["newClipDifferences"] = (report["newClipDifferences"] + new_clip_problems)[:MAX_DIFF_SAMPLES]
    append_check(report, "New clips match receipt source, placement, trims, and gain keyframes", not new_clip_problems, {"problemCount": len(new_clip_problems), "samples": new_clip_problems[:30]})

    source_hash_results = []
    source_hash_problems = []
    for source_path, expected_sha in guard_by_path.items():
        path = Path(source_path)
        if not path.is_file():
            source_hash_problems.append({"path": source_path, "error": "guarded source file is missing"})
            continue
        actual_sha = sha256_file(path)
        source_hash_results.append({"path": source_path, "expectedSha256": expected_sha, "actualSha256": actual_sha, "match": actual_sha == expected_sha})
        if actual_sha != expected_sha:
            source_hash_problems.append({"path": source_path, "expectedSha256": expected_sha, "actualSha256": actual_sha})
    report["metrics"]["sourceAssetHashChecks"] = source_hash_results
    append_check(report, "Every planned source asset still matches its admitted SHA-256", not source_hash_problems and len(source_hash_results) == len(guard_by_path), {"problemCount": len(source_hash_problems), "problems": source_hash_problems[:20]})

    manifest_source_hashes: dict[str, str] = {}
    for asset in manifest.get("sourceAssets", {}).values():
        if isinstance(asset, dict) and asset.get("path") and asset.get("sha256"):
            manifest_source_hashes[asset["path"]] = str(asset["sha256"]).lower()
    for cue in manifest.get("cues", []):
        if cue.get("sourcePath") and cue.get("sourceSha256"):
            manifest_source_hashes[cue["sourcePath"]] = str(cue["sourceSha256"]).lower()
    for cue in manifest.get("ambienceClips", []):
        if cue.get("sourcePath") and cue.get("sourceSha256"):
            manifest_source_hashes[cue["sourcePath"]] = str(cue["sourceSha256"]).lower()
    manifest_guard_diffs = [
        {"path": path, "planGuardSha256": source_sha, "manifestSha256": manifest_source_hashes.get(path)}
        for path, source_sha in sorted(guard_by_path.items())
        if manifest_source_hashes.get(path) != source_sha
    ]
    append_check(report, "Cue manifest source hashes agree with admitted task asset guards", not manifest_guard_diffs, manifest_guard_diffs[:20])

    lane_distribution = {}
    for track_id, expected_track in expected_new_track_by_id.items():
        track = candidate_tracks.get(track_id, {})
        role = role_values_by_id.get(track_id, expected_track.get("audioRole", ""))
        lane_distribution[track_id] = {"name": expected_track.get("name"), "audioRole": role, "clipCount": expected_lane_counts.get(track_id, 0)}
    report["metrics"]["newLaneDistribution"] = lane_distribution
    append_check(report, "New audio clips are distributed only across the four created lanes", all(owner in new_track_ids for owner in expected_lane_counts) and sum(expected_lane_counts.values()) == EXPECTED_NEW_CLIPS, {"distribution": lane_distribution})
    expected_slot_counts: Counter[str] = Counter()
    expected_slot_counts["newGodsMusic"] = 1
    for cue in manifest.get("cues", []):
        expected_slot_counts[str(cue.get("trackSlot", ""))] += 1
    for cue in manifest.get("ambienceClips", []):
        expected_slot_counts[str(cue.get("trackSlot", ""))] += 1
    slot_specs = manifest.get("trackSlots", {})
    expected_counts_by_lane_name = {spec.get("name"): expected_slot_counts.get(slot, 0) for slot, spec in slot_specs.items()}
    actual_counts_by_lane_name = {entry["name"]: entry["clipCount"] for entry in lane_distribution.values()}
    slot_count_diffs = [
        {"name": name, "expectedFromManifest": expected_count, "actual": actual_counts_by_lane_name.get(name, 0)}
        for name, expected_count in expected_counts_by_lane_name.items()
        if actual_counts_by_lane_name.get(name, 0) != expected_count
    ]
    append_check(report, "Lane clip counts match the cue manifest slots", not slot_count_diffs and manifest.get("expectedNativeAudioClipCount") == EXPECTED_NEW_CLIPS, {"expectedNativeAudioClipCount": manifest.get("expectedNativeAudioClipCount"), "slotCounts": expected_counts_by_lane_name, "differences": slot_count_diffs})

    baseline_ends = [effective_end_seconds(clip) for clip in baseline_clips.values()]
    candidate_ends = [effective_end_seconds(clip) for clip in candidate_clips.values()]
    baseline_end = max((value for value in baseline_ends if value is not None), default=None)
    candidate_end = max((value for value in candidate_ends if value is not None), default=None)
    report["metrics"].update({"baselineTimelineEndSeconds": baseline_end, "candidateTimelineEndSeconds": candidate_end, "candidateFrameCountAtNominalRate": candidate_end * fps if candidate_end is not None and fps else None, "expectedDurationSeconds": EXPECTED_DURATION_SECONDS})
    duration_ok = baseline_end is not None and candidate_end is not None and abs(baseline_end - EXPECTED_DURATION_SECONDS) <= NUMERIC_EPSILON and abs(candidate_end - EXPECTED_DURATION_SECONDS) <= NUMERIC_EPSILON
    if candidate_end is not None and not duration_ok:
        detail = {"path": "timelineEndSeconds", "expected": EXPECTED_DURATION_SECONDS, "actual": candidate_end, "classification": timing_classification(EXPECTED_DURATION_SECONDS, candidate_end, fps)}
        report["timingDifferences"].append(detail)
        if detail["classification"].startswith("candidate-frame-rounded"):
            report["frameRoundedTimingDifferences"].append(detail)
    append_check(report, "Full timeline duration remains exactly 298 seconds", duration_ok, {"baselineEndSeconds": baseline_end, "candidateEndSeconds": candidate_end, "expectedSeconds": EXPECTED_DURATION_SECONDS})

    report["metrics"].update({
        "expectedCandidateTrackCount": len(baseline_tracks) + EXPECTED_NEW_TRACKS,
        "expectedCandidateClipCount": EXPECTED_BASE_CLIPS + EXPECTED_NEW_CLIPS,
        "receiptCompletedStepCount": sum(len(items) for items in completed_lists.values()),
        "plannedMutationStepCount": len(action_steps),
        "receiptCreatedNewClipCount": len(new_clip_ids),
        "frameRoundedTimingDifferenceCount": len(report["frameRoundedTimingDifferences"]),
        "timingDifferenceCount": len(report["timingDifferences"]),
    })
    append_check(report, "Final track and clip counts match the planned Pass 10 structure", len(candidate_tracks) == len(baseline_tracks) + EXPECTED_NEW_TRACKS and len(candidate_clips) == EXPECTED_BASE_CLIPS + EXPECTED_NEW_CLIPS, {"expectedTracks": len(baseline_tracks) + EXPECTED_NEW_TRACKS, "actualTracks": len(candidate_tracks), "expectedClips": EXPECTED_BASE_CLIPS + EXPECTED_NEW_CLIPS, "actualClips": len(candidate_clips)})
    report["status"] = "passed" if not report["failures"] else "failed"
    report["warnings"].append("This checkpoint audit verifies native project and receipt data; it does not establish that the rendered audio mix was heard or judged by a person.")
    rendered = json.dumps(report, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered, encoding="utf-8")
        print(json.dumps({"status": report["status"], "report": str(args.report.resolve()), "failureCount": len(report["failures"]), "frameRoundedTimingDifferenceCount": len(report["frameRoundedTimingDifferences"])}, indent=2))
    else:
        sys.stdout.write(rendered)
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
