#!/usr/bin/env python3
"""Verify the fixed P13 native preview plan against its immutable inputs."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]
SNAPSHOT = REPO_ROOT / "marketing/native-recreation-v4/polish-pass-2026-10-30/native-projects/looksy-polish-pass13-audio-lanes.json"
EXPECTED_SNAPSHOT_SHA = "a35826ab1806e0161a830435501ba2af7c8beb7923b6a9b1861ffa60855dbd78"
PLAN_PATH = HERE / "preview-sync-p13.plan.template.json"
EXPECTED_PLAN_SHA = "0af4b594aab966750a369fbc415c0321b4d85dbf0153d2f4d6698c2b32afa077"
STORYBOARD_PATH = HERE / "preview-sync-p13.storyboard.json"
EXPECTED_STORYBOARD_SHA = "9eab8789f5b8c34d4d512aad993e49e3dfcbf0e458439f93f39ecbc86b04f2ab"
FPS = 30
SEGMENT_FRAMES = [(0, 36), (36, 49), (49, 54), (54, 60),
                  (60, 105), (105, 204), (204, 269), (269, 360)]


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fail(message: str) -> None:
    raise RuntimeError(message)


def smooth(t: float, a: float, b: float) -> float:
    u = max(0.0, min(1.0, (t - a) / (b - a)))
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def source_at(local_seconds: float) -> tuple[str, float]:
    trim = smooth(local_seconds, 3.5, 6.5)
    shots = [
        ("arrival", 1.0, 0.0, 3.7),
        ("ridge", 0.0, 3.7, 3.2),
        ("city", 0.0, 6.9, 2.7 - 0.7 * trim),
        ("night", 2.6, 9.6 - 0.7 * trim, 2.4),
    ]
    city_end = shots[2][2] + shots[2][3]
    if local_seconds < 1.2:
        clock = 11.95
    elif local_seconds < 3.5:
        clock = 11.95 * (1.0 - smooth(local_seconds, 1.2, 2.5))
    elif local_seconds < 6.8:
        clock = city_end - 0.04
    else:
        clock = 7.45 + (11.25 - 7.45) * smooth(local_seconds, 7.0, 11.5)
    shot = next((candidate for candidate in shots
                 if clock < candidate[2] + candidate[3]), shots[-1])
    name, offset, start, duration = shot
    return name, offset + max(0.0, min(duration - 0.04, clock - start))


def clip_index(snapshot: dict) -> tuple[dict[str, dict], dict[str, str]]:
    clips = {}
    owners = {}
    for track in snapshot.get("tracks", []):
        for clip in track.get("clips", []):
            clips[clip["clipId"]] = clip
            owners[clip["clipId"]] = track["trackId"]
    return clips, owners


def verify_plan(plan: dict | None = None) -> dict:
    if sha256_file(SNAPSHOT) != EXPECTED_SNAPSHOT_SHA:
        fail("The protected P13 checkpoint changed; stop and rebind a freshly audited snapshot.")
    if sha256_file(PLAN_PATH) != EXPECTED_PLAN_SHA:
        fail("The reviewed P13 plan template changed; stop and prepare a newly reviewed plan.")
    if sha256_file(STORYBOARD_PATH) != EXPECTED_STORYBOARD_SHA:
        fail("The reviewed preview storyboard changed; stop and re-review its frame map.")
    frozen_plan = json.loads(PLAN_PATH.read_text())
    if plan is None:
        plan = frozen_plan
    elif plan != frozen_plan:
        fail("The supplied plan object differs from the frozen P13 template.")
    snapshot = json.loads(SNAPSHOT.read_text())
    if (plan.get("schema") != "looksy.native-authoring-plan/v1" or
            plan.get("productionCallsMade") is not False or
            plan.get("planningSnapshot", {}).get("sha256") != EXPECTED_SNAPSHOT_SHA or
            plan.get("resourceId") != "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"):
        fail("The static authoring template lost its P13/resource guard.")
    if (len(snapshot["tracks"]) != 196 or
            sum(len(track.get("clips", [])) for track in snapshot["tracks"]) != 1277 or
            plan.get("planningSnapshot", {}).get("trackCount") != 196 or
            plan.get("planningSnapshot", {}).get("clipCount") != 1277):
        fail("P13 track/clip counts changed; review all changed clip identity and timing fields.")
    if snapshot.get("sequenceFormat") != {"audioSampleRate": 48000, "frameRate": 30,
                                          "height": 1080, "width": 1920}:
        fail("The P13 sequence format no longer matches this landscape plan.")
    if snapshot.get("activeOutputVariant") != "landscape":
        fail("P13 no longer has the reviewed landscape output variant active.")

    tracks = {track["trackId"]: track for track in snapshot["tracks"]}
    clips, owners = clip_index(snapshot)
    expected_tracks = {
        "G12Preview": ("graphics-6d4b5418-13f9-4584-b229-52acb981d052", 100011),
        "G13ShotLabel": ("graphics-446a8070-3cb9-4410-add8-a5f1a1ec521e", 100012),
        "G14Headline": ("graphics-e2ee0b7d-590a-4380-bb37-a36bdf70ba66", 100013),
        "G10ProgramCounter": ("graphics-94e59cf1-6bfe-44c4-a2d7-67d0a5c79b20", 100009),
    }
    for key, track_id in plan["preflightGuards"]["expectedTrackIds"].items():
        expected_id, expected_lane = expected_tracks.get(key, (None, None))
        track = tracks.get(track_id)
        if (track_id != expected_id or track is None or
                track.get("laneIndex") != expected_lane or
                track.get("locked") is not False or track.get("kind") != "video" or
                track.get("role") != "graphics"):
            fail(f"Reviewed native graphics track changed: {track_id}.")

    for guard in plan.get("assetGuards", []):
        path = Path(guard["path"]).resolve(strict=True)
        if sha256_file(path) != guard["sha256"]:
            fail(f"A native preview source file changed: {path}.")
    for guard in plan.get("creativeReference", {}).get("sourceFiles", []):
        path = Path(guard["path"]).resolve(strict=True)
        if sha256_file(path) != guard["sha256"]:
            fail(f"A creative reference source changed: {path}.")

    for guard in plan.get("existingMediaClipGuard", []):
        clip_id = guard["clipId"]
        clip = clips.get(clip_id)
        if clip is None or owners.get(clip_id) != guard["trackId"]:
            fail(f"Reviewed G12 media clip moved or disappeared: {clip_id}.")
        for key in ("startSeconds", "durationSeconds", "trimStartSeconds",
                    "trimEndSeconds", "assetId", "sourceContentSha256", "hasAudio",
                    "frameWidth", "frameHeight", "frameOffsetX", "frameOffsetY", "frameMode"):
            if clip.get(key) != guard.get(key):
                fail(f"P13 source clip field {key} changed for {clip_id}.")
        if sha256_file(Path(clip["filePath"])) != guard["sourceContentSha256"]:
            fail(f"P13 file bytes do not match native source metadata for {clip_id}.")

    preview_track_id = plan["preflightGuards"]["expectedTrackIds"]["G12Preview"]
    frame_step = next(step for step in plan["steps"]
                      if step["commandId"] == "timeline.action.setTimelineClipFrame")
    frame_values = frame_step["values"]
    frame_mapping = {
        "offsetX": "frameOffsetX", "offsetY": "frameOffsetY", "scale": "frameScale",
        "rotationDegrees": "frameRotationDegrees", "perspectiveX": "framePerspectiveX",
        "perspectiveY": "framePerspectiveY", "opacity": "opacity", "objectFit": "frameMode",
        "width": "frameWidth", "height": "frameHeight", "cornerRadius": "frameCornerRadius",
        "cropLeft": "sourceCropLeft", "cropTop": "sourceCropTop",
        "cropRight": "sourceCropRight", "cropBottom": "sourceCropBottom",
    }
    first_geometry = None
    for clip_guard in plan["existingMediaClipGuard"]:
        clip = clips[clip_guard["clipId"]]
        if owners[clip_guard["clipId"]] != preview_track_id:
            fail("A P13 source clip left the G12 preview lane.")
        geometry = {native: clip[field] for native, field in frame_mapping.items()}
        if first_geometry is None:
            first_geometry = geometry
        elif geometry != first_geometry:
            fail("The four P13 source clips no longer share the reviewed G12 geometry.")
        landscape = clip.get("outputVariantOverrides", {}).get("landscape", {})
        for field in ("frameWidth", "frameHeight", "frameOffsetX", "frameOffsetY",
                      "frameScale", "frameRotationDegrees", "framePerspectiveX",
                      "framePerspectiveY", "frameMode", "frameCornerRadius"):
            if landscape.get(field) != clip.get(field):
                fail(f"P13 landscape framing override {field} differs from the accepted G12 frame.")
    if frame_values != {"clipId": frame_values["clipId"], **first_geometry}:
        fail("New preview clips do not inherit the exact P13 G12 geometry.")

    for guard in plan["titleStyleGuard"]["existing"]:
        clip = clips.get(guard["clipId"])
        if clip is None or owners.get(guard["clipId"]) != guard["trackId"]:
            fail(f"Reviewed title/counter clip moved or disappeared: {guard['clipId']}.")
        if (clip.get("titleText") != guard["expectedText"] or
                clip.get("startSeconds") != guard["expectedStartSeconds"] or
                clip["trimEndSeconds"] - clip["trimStartSeconds"] != guard["expectedDurationSeconds"]):
            fail(f"Reviewed title/counter text or timing changed: {guard['clipId']}.")
    for guard in plan.get("nextChapterClipGuards", []):
        clip = clips.get(guard["clipId"])
        if clip is None or owners.get(guard["clipId"]) != guard["trackId"]:
            fail(f"Following chapter clip moved or disappeared: {guard['clipId']}.")
        for key, value in (("startSeconds", guard["expectedStartSeconds"]),
                           ("mediaType", guard["expectedMediaType"]),
                           ("assetId", guard["expectedAssetId"])):
            if clip.get(key) != value:
                fail(f"Following chapter clip field {key} changed: {guard['clipId']}.")
        if clip["trimEndSeconds"] - clip["trimStartSeconds"] != guard["expectedDurationSeconds"]:
            fail(f"Following chapter duration changed: {guard['clipId']}.")

    title_style_fields = {
        "fontFamily": "titleFontFamily", "fontSize": "titleFontSize",
        "fontWeight": "titleFontWeight", "italic": "titleItalic",
        "letterSpacing": "titleLetterSpacing", "lineHeight": "titleLineHeight",
        "color": "titleColor", "alignment": "titleAlignment",
        "positionX": "titlePositionX", "positionY": "titlePositionY",
        "backgroundColor": "titleBackgroundColor",
        "shadowEnabled": "captionShadowEnabled", "shadowColor": "captionShadowColor",
        "shadowOffsetX": "captionShadowOffsetX", "shadowOffsetY": "captionShadowOffsetY",
        "backgroundPadding": "captionBackgroundPadding",
        "backgroundRadius": "captionBackgroundRadius", "safeMarginX": "captionSafeMarginX",
        "safeMarginY": "captionSafeMarginY",
    }
    for group, shot_id in (("label", "night"), ("headline", "night")):
        source_title_id = plan["titleStyleGuard"]["existing"]
        source_id = next(item["clipId"] for item in source_title_id
                         if item["trackId"] == (plan["preflightGuards"]["expectedTrackIds"][
                             "G13ShotLabel" if group == "label" else "G14Headline"])
                         and item["expectedText"] == clips[item["clipId"]]["titleText"])
        baseline = clips[source_id]
        for item in plan["titleStyleGuard"]["existing"]:
            if item["trackId"] != owners.get(item["clipId"]):
                continue
            if item["trackId"] != plan["preflightGuards"]["expectedTrackIds"][
                    "G13ShotLabel" if group == "label" else "G14Headline"]:
                continue
            current = clips[item["clipId"]]
            if any(current.get(field) != baseline.get(field)
                   for field in title_style_fields.values()):
                fail(f"P13 {group} title styles are not consistent across native clips.")
        for step in plan["steps"]:
            if step["commandId"] != "timeline.action.createTimelineTitle":
                continue
            if step["id"].startswith(f"create-{group}-"):
                values = step["values"]["values"]
                if any(values.get(native) != baseline.get(source_field)
                       for native, source_field in title_style_fields.items()):
                    fail(f"New P13 {group} title style diverges from the existing native overlay.")

    steps = plan.get("steps", [])
    if len(steps) != 49 or len(plan.get("segments", [])) != 8:
        fail("The bounded eight-span/49-action plan changed size.")
    created = set()
    step_by_id = {}
    for step in steps:
        values = step.get("values", {})

        def inspect_refs(value):
            if isinstance(value, dict):
                if "result" in value or "field" in value:
                    if (set(value) != {"result", "field"} or
                            value["result"] not in created or
                            value["field"] != "createdClipId"):
                        fail(f"Unresolved or unsafe createdClipId reference in {step.get('id')}.")
                else:
                    for nested in value.values():
                        inspect_refs(nested)
            elif isinstance(value, list):
                for nested in value:
                    inspect_refs(nested)

        inspect_refs(values)
        if step["commandId"] in {
                "timeline.action.createTimelineClip",
                "timeline.action.createTimelineTitle"}:
            created.add(step["id"])
        step_by_id[step["id"]] = step
    if (sum(step["commandId"] == "timeline.action.createTimelineClip" for step in steps) != 4 or
            sum(step["commandId"] == "timeline.action.createTimelineTitle" for step in steps) != 4):
        fail("The plan must add exactly four video clips and four native titles.")

    expected_point_total = 0
    for segment, (first, stop) in zip(plan["segments"], SEGMENT_FRAMES):
        if segment["frames"] != [first, stop] or segment["frameCount"] != stop - first:
            fail(f"The reviewed 30fps boundary changed for {segment['key']}.")
        step = step_by_id[segment["retimePointsStepId"]]
        values = step["values"]
        points = values["points"]
        expected_count = stop - first + 1
        if len(points) != expected_count or len(points) != segment["retimePointCount"]:
            fail(f"Retime point count changed for {segment['key']}.")
        expected_source_in = values["expectedSourceInSeconds"]
        expected_source_out = values["expectedSourceOutSeconds"]
        expected_duration = (stop - first) / FPS
        if (values["ripplePolicy"] != "preserveOtherClipStarts" or
                values["targetPlayDurationSeconds"] != expected_duration or
                points[0]["localSeconds"] != 0.0 or
                points[-1]["localSeconds"] != expected_duration):
            fail(f"Retime window or ripple policy changed for {segment['key']}.")
        for offset, point in enumerate(points[:-1]):
            local_frame = first + offset
            shot, source_seconds = source_at(local_frame / FPS)
            if shot != segment["shot"]:
                fail(f"Remotion source identity changed at local frame {local_frame}.")
            if (point["localSeconds"] != offset / FPS or
                    point["sourceSeconds"] != source_seconds or
                    point["interpolation"] != "linear" or
                    not expected_source_in <= point["sourceSeconds"] <= expected_source_out):
                fail(f"The exact per-output-frame source time changed in {segment['key']} at offset {offset}.")
        endpoint = points[-1]
        last_source = points[-2]["sourceSeconds"]
        if endpoint["sourceSeconds"] != min(expected_source_out, max(expected_source_in, last_source)):
            fail(f"Non-rendered endpoint is not clamped to source marks in {segment['key']}.")
        expected_point_total += len(points)
    if expected_point_total != 368:
        fail("The eight retime curves no longer contain exactly 360 rendered-frame samples plus endpoints.")
    if plan.get("approvedMutationSummary", {}).get("outsideChapterChanges") != 0:
        fail("The plan declares an out-of-chapter mutation.")
    return plan


if __name__ == "__main__":
    value = verify_plan()
    print(json.dumps({
        "status": "plan-and-source-guards-passed",
        "nativeCallsMade": False,
        "productionProjectWrites": 0,
        "snapshotSha256": EXPECTED_SNAPSHOT_SHA,
        "steps": len(value["steps"]),
        "segments": len(value["segments"]),
        "sourceFramePoints": sum(item["retimePointCount"] for item in value["segments"]),
        "maxPointsPerClip": max(item["retimePointCount"] for item in value["segments"]),
    }, indent=2))
