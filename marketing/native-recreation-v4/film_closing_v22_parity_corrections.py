"""Prepare current-checkpoint FilmPolish quad/shadow repairs for native authoring.

This is an offline planner. It never calls the app/MCP, changes a native
project, or overwrites an earlier plan or receipt. Every generated action is
bound to the v22 SaveCopy receipt, native document hash, and stable clip IDs.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

import film_closing_perspective_quad_corrections as quad_source


ROOT = Path(__file__).resolve().parent
REPO_ROOT = ROOT.parents[1]
PROJECT = ROOT / "projects/looksy-native-landscape-reveal-v22.json"
SAVE_RECEIPT = ROOT / "evidence/native-v22-safety-save-result.json"
OUTPUT = ROOT / "film-closing-v22-parity-corrections-landscape-r2.commands-fragment.json"

RESOURCE_ID = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
TIMELINE_ID = "3db4e4fb-6737-415c-b6ee-dde766e7c237"
TASK_ID = "98081dfb-47f4-424b-9d46-aeb5fcb46ba3"
DOCUMENT_HASH = "39080497d158c3edf2ded9e2572e6c53f06968892f8fa19f263ead65c90eb11a"
PROJECT_SHA256 = "48e918f5a3c2f0979cede906707f6a8f99ceb366e1fd7940cc0022d6c1d260fc"
SAVE_RECEIPT_SHA256 = "d67ffac2707e5d980e0437efbc73f3a7b01fe31b0fcfae4c270ceaf23aeabbb0"
FPS = 30
SEQUENCE = {"width": 1920, "height": 1080, "frameRate": FPS}
PROJECT_DURATION_SECONDS = 298

CLIPS = {
    "film": {
        "clipId": "dca79613-d942-49b4-af71-80c8df2f0e93",
        "trackId": "graphics-3722e18e-5722-438a-81ce-78a46ad7d5ea",
        "trackLabel": "G882",
        "trackName": "Framing / landscape film",
        "clipLabel": "framing-landscape-film",
        "mediaType": "video",
    },
    "title": {
        "clipId": "43d13115-bfb7-4953-ab66-8a69d1f4a379",
        "trackId": "graphics-1599b83b-7a30-4bd7-b984-ced17d13962b",
        "trackLabel": "G883",
        "trackName": "Framing / landscape film title",
        "clipLabel": "A little further.",
        "mediaType": "title",
    },
    "social": {
        "clipId": "9ffe3b2c-992e-4eae-a8cd-5c87bfb54dab",
        "trackId": "graphics-7f0b8e05-7664-449e-9d40-6e69bbe6e264",
        "trackLabel": "G884",
        "trackName": "Framing / vertical social cut",
        "clipLabel": "framing-social-film",
        "mediaType": "video",
    },
    "phoneRim": {
        "clipId": "e8cd3806-3aaf-4487-9823-615bfd3a7a00",
        "trackId": "graphics-9d192e2d-2e07-4d62-9fc7-85cef98de031",
        "trackLabel": "G885",
        "trackName": "framing-phone-rim",
        "clipLabel": "Rounded Rectangle",
        "mediaType": "title",
    },
    "phoneTitle": {
        "clipId": "b0e97aae-1bc8-4070-a616-07abf795f001",
        "trackId": "graphics-aeea7055-8f13-42e5-bd01-3da29c9248a5",
        "trackLabel": "G886",
        "trackName": "Framing / phone title",
        "clipLabel": "A LITTLE FURTHER.",
        "mediaType": "title",
    },
}

SOURCE_FILES = {
    "marketing/animated-trailer/src/creative-launch/FilmPolish.tsx":
        "c879dd5c41e1ceae99f14b7c485eeff04d2e3a198250c4747f856d5294d52d6f",
    "marketing/animated-trailer/src/creative-launch/CreativeFilm.tsx":
        "e9ab5e5f37372c4e7073db7aa88b452fb2bae20b565894b47da74964e179e757",
    "marketing/animated-trailer/src/creative-launch/State.ts":
        "8b7ae64755e9de3a300550a991a40a40f32d35b307e0e5c10a7820783d2f5893",
    "marketing/animated-trailer/src/creative-launch/Kit.tsx":
        "591e74ad93f6ea2b909cbf4bc0243c9cd5ce95f61b07ce22f198675215ab25d9",
    "marketing/animated-trailer/src/creative-launch/BuyerScenes.tsx":
        "c614852258734d6be5de7efe371c9ccc2f7a02d62537b22b731b966737a4bb93",
    "marketing/animated-trailer/public/media/creative-polish-v2/arrival-12.mp4":
        "8c40397a24bdba05f8cdc8a87cd5e10bc030d0e7e575359bf4204acc3a09ac2e",
    "marketing/native-recreation-v4/film_closing_perspective_quad_corrections.py":
        "8c9a3ba057240189098ffc17a7a9fa1178933a879de2f2206a5d221d4073f43e",
    "apps/desktop/src/agent_timeline_commands.h":
        "0607fe9d323f34b6f6570f8693ab44218096ae6f272f883d3791320951440a71",
    "apps/desktop/src/timeline_frame_quad_warp.h":
        "7289860479ff5bc609690956532df2590e824e9f97c275fe0353b5bb709045a3",
    "apps/desktop/src/timeline_frame_quad_warp.cpp":
        "70f82d872fe94c2ab78cd6af3d56f66f7377dc9a17d53885da9d45a0024882a0",
    "apps/desktop/src/timeline_frame_shadow.h":
        "3215dd1380b8609b29656e046c3e19f268ed40c39ad6d96b095127720bd1afa2",
    "apps/desktop/src/timeline_frame_shadow.cpp":
        "7234f885aba37739bdd38c5b6292c50e700472437055b4910c6d9a2b59db8192",
    "apps/desktop/src/timeline_document_controller.cpp":
        "8095cdf386cfc251c0783080246dab29b0ca48b041d23337ffae93134439da95",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_path(relative: str) -> Path:
    return REPO_ROOT / relative


def verify_sources() -> dict[str, dict[str, Any]]:
    verified: dict[str, dict[str, Any]] = {}
    for relative, expected in SOURCE_FILES.items():
        path = source_path(relative)
        actual = sha256(path)
        if actual != expected:
            raise ValueError(f"Source bytes changed since the v22 audit: {relative}")
        verified[relative] = {"sha256": actual, "bytes": path.stat().st_size}
    return verified


def receipt_content() -> dict[str, Any]:
    if sha256(SAVE_RECEIPT) != SAVE_RECEIPT_SHA256:
        raise ValueError("The v22 native SaveCopy receipt changed; refusing to bind actions")
    envelope = json.loads(SAVE_RECEIPT.read_text(encoding="utf-8"))
    receipt = envelope.get("result", {}).get("structuredContent")
    if not isinstance(receipt, dict):
        raise ValueError("The v22 SaveCopy receipt has no structured readback")
    expected = {
        "commandId": "timeline.action.saveTimeline",
        "documentHash": DOCUMENT_HASH,
        "fileSha256": PROJECT_SHA256,
        "outputPath": str(PROJECT),
        "readbackVerified": True,
        "resourceId": RESOURCE_ID,
        "taskStateHash": DOCUMENT_HASH,
        "taskStepId": 153,
        "timelineId": TIMELINE_ID,
    }
    if any(receipt.get(key) != value for key, value in expected.items()):
        raise ValueError("The v22 SaveCopy receipt no longer matches this plan checkpoint")
    if receipt.get("clipCount") != 1212:
        raise ValueError("The v22 SaveCopy receipt no longer reports the audited clip count")
    return receipt


def verify_timing_source_truth() -> None:
    film_text = source_path("marketing/animated-trailer/src/creative-launch/FilmPolish.tsx").read_text()
    if "phaseAt(t, [0, 12, 24, 34, 42, 48, 56, 60, 68])" not in film_text:
        raise ValueError("The FilmPolish phase edges changed; re-audit the delivery timing")
    if '<At from={start} seconds={8} name="Landscape film delivery">' not in film_text:
        raise ValueError("The landscape delivery At window changed")
    if ('from={start}\n                seconds={8}\n                name="Separately framed social delivery"'
            not in film_text):
        raise ValueError("The social delivery At window changed")

    creative_text = source_path("marketing/animated-trailer/src/creative-launch/CreativeFilm.tsx").read_text()
    if "from={184 * fps}" not in creative_text or "durationInFrames={68 * fps}" not in creative_text:
        raise ValueError("The FilmPolish chapter interval changed")
    state_text = source_path("marketing/animated-trailer/src/creative-launch/State.ts").read_text()
    if "t >= edges[phase + 1]" not in state_text or "start: edges[phase]" not in state_text:
        raise ValueError("phaseAt no longer returns the source phase start used by At")
    kit_text = source_path("marketing/animated-trailer/src/creative-launch/Kit.tsx").read_text()
    if "from={Math.round(from * fps)}" not in kit_text or "Math.round(seconds * fps)" not in kit_text:
        raise ValueError("The At frame rounding semantics changed")

    native_text = source_path("apps/desktop/src/timeline_document_controller.cpp").read_text()
    duration_start = native_text.find("double TimelineDocumentController::clipTrimmedDuration(const Clip& clip) const {")
    if duration_start < 0:
        raise ValueError("Native clipTrimmedDuration implementation was not found")
    duration_body = native_text[duration_start:duration_start + 650]
    expected_duration_rules = (
        "clipRetimeKeyframes(clip)",
        "if (retime_keyframes.size() >= 2)",
        "clip.trim_end_seconds - clip.trim_start_seconds",
        "clip.playback_rate",
    )
    if any(token not in duration_body for token in expected_duration_rules):
        raise ValueError("Native trimmed-duration semantics changed; re-audit clip timing")


def native_frame_values(clip: dict[str, Any]) -> dict[str, Any]:
    return {
        "clipId": clip["clipId"],
        "offsetX": clip["frameOffsetX"],
        "offsetY": clip["frameOffsetY"],
        "scale": clip["frameScale"],
        "rotationDegrees": clip["frameRotationDegrees"],
        "perspectiveX": clip["framePerspectiveX"],
        "perspectiveY": clip["framePerspectiveY"],
        "opacity": clip["opacity"],
        "objectFit": clip["frameMode"],
        "width": clip["frameWidth"],
        "height": clip["frameHeight"],
        "cornerRadius": clip["frameCornerRadius"],
        "cropLeft": clip["sourceCropLeft"],
        "cropTop": clip["sourceCropTop"],
        "cropRight": clip["sourceCropRight"],
        "cropBottom": clip["sourceCropBottom"],
    }


def verify_project_and_targets() -> tuple[dict[str, Any], dict[str, dict[str, Any]]]:
    if sha256(PROJECT) != PROJECT_SHA256:
        raise ValueError("The current v22 SaveCopy bytes changed; refusing to use stale IDs")
    project = json.loads(PROJECT.read_text(encoding="utf-8"))
    receipt = receipt_content()
    if project.get("timelineId") != TIMELINE_ID:
        raise ValueError("The v22 project timeline ID changed")
    sequence = project.get("sequenceFormat", {})
    if any(sequence.get(key) != value for key, value in SEQUENCE.items()):
        raise ValueError(f"Unexpected v22 sequence format: {sequence}")
    if len(project.get("tracks", [])) != 936:
        raise ValueError("The v22 target project no longer has the audited 936 tracks")

    matches = {
        clip_id: (track, clip)
        for track in project["tracks"]
        for clip in track.get("clips", [])
        for clip_id in (clip.get("clipId"),)
        if clip_id in {entry["clipId"] for entry in CLIPS.values()}
    }
    if len(matches) != len(CLIPS):
        raise ValueError("A v22 FilmPolish clip ID is missing or duplicated")

    clips: dict[str, dict[str, Any]] = {}
    for alias, expected in CLIPS.items():
        track, clip = matches[expected["clipId"]]
        if track.get("trackId") != expected["trackId"]:
            raise ValueError(f"The v22 track binding changed for {alias}")
        if track.get("displayLabel") != expected["trackLabel"]:
            raise ValueError(f"The v22 track label changed for {alias}")
        if track.get("customName") != expected["trackName"]:
            raise ValueError(f"The v22 track name changed for {alias}")
        if clip.get("displayLabel") != expected["clipLabel"]:
            raise ValueError(f"The v22 clip label changed for {alias}")
        if clip.get("mediaType") != expected["mediaType"]:
            raise ValueError(f"The v22 clip type changed for {alias}")
        clips[alias] = {"track": track, "clip": clip}

    film_path = str(source_path("marketing/animated-trailer/public/media/creative-polish-v2/arrival-12.mp4"))
    for alias in ("film", "social"):
        clip = clips[alias]["clip"]
        if clip.get("filePath") != film_path:
            raise ValueError(f"The v22 source media changed for {alias}")
        if (clip.get("startSeconds"), clip.get("durationSeconds"),
                clip.get("trimStartSeconds"), clip.get("trimEndSeconds"),
                clip.get("playbackRate")) != (244, 12, 0, 8, 1):
            raise ValueError(f"The v22 native timing/source trim changed for {alias}")
        if any(item.get("parameter") == "sourceTime" for item in clip.get("keyframes", [])):
            raise ValueError(f"The v22 {alias} clip has retime keyframes; audit duration separately")
        if clip.get("frameShadow"):
            raise ValueError(f"The v22 {alias} shadow is no longer missing")

    if clips["film"]["clip"].get("framePerspectiveQuad"):
        raise ValueError("The v22 film card already has a static quad; do not replay this repair")
    if clips["title"]["clip"].get("framePerspectiveQuad"):
        raise ValueError("The v22 landscape title already has a static quad; do not replay this repair")

    # The Remotion At sequence is 8 seconds. Native's raw duration field is
    # source-duration metadata; with no retime curve, the active clip interval
    # uses (trimEnd - trimStart) / playbackRate.
    for alias in ("film", "social"):
        clip = clips[alias]["clip"]
        if any(item.get("parameter") == "sourceTime" for item in clip.get("keyframes", [])):
            raise ValueError(f"The v22 {alias} clip has retime keyframes; cannot use trim-only timing proof")
        active_duration = (clip["trimEndSeconds"] - clip["trimStartSeconds"]) / clip["playbackRate"]
        if abs(active_duration - 8.0) > 1.0e-9 or abs(clip["startSeconds"] + active_duration - 252.0) > 1.0e-9:
            raise ValueError(f"The v22 {alias} active interval no longer matches source At [244,252)")

    return project, clips


def build_fragment() -> dict[str, Any]:
    source_hashes = verify_sources()
    verify_timing_source_truth()
    save_receipt = receipt_content()
    project, clips = verify_project_and_targets()
    quad_source.check_source_truth()

    film_points, film_matrix_error, film_min_w = quad_source.local_card_quad()
    title_points, title_matrix_error, title_min_w = quad_source.full_canvas_title_quad("landscape")
    if max(film_matrix_error, title_matrix_error) > 1.0e-10:
        raise ValueError("Independent CSS perspective calculations disagree")

    film_clip = clips["film"]["clip"]
    title_clip = clips["title"]["clip"]
    social_clip = clips["social"]["clip"]

    quad_steps = [
        {
            "id": "film-closing-v22-perspective-film",
            "commandId": "timeline.action.setTimelineClipPerspectiveQuad",
            "values": {
                "clipId": CLIPS["film"]["clipId"],
                "enabled": True,
                "points": quad_source.as_native_points(film_points),
            },
        },
        {
            "id": "film-closing-v22-perspective-title",
            "commandId": "timeline.action.setTimelineClipPerspectiveQuad",
            "values": {
                "clipId": CLIPS["title"]["clipId"],
                "enabled": True,
                "points": quad_source.as_native_points(title_points),
            },
        },
    ]

    shadow_steps = []
    for alias, color, offset_y, blur_radius in (
            ("film", "#88000000", 33.0, 77.0),
            ("social", "#99000000", 27.5, 77.0)):
        clip = clips[alias]["clip"]
        values = native_frame_values(clip)
        values["frameShadow"] = {
            "enabled": True,
            "color": color,
            "offsetX": 0.0,
            "offsetY": offset_y,
            "blurRadius": blur_radius,
            "spread": 0.0,
        }
        shadow_steps.append({
            "id": f"film-closing-v22-shadow-{alias}",
            "commandId": "timeline.action.setTimelineClipFrame",
            "values": values,
        })

    steps = quad_steps + shadow_steps
    import sys
    sys.path.insert(0, str(ROOT))
    from native_mcp import validate_tool_request_size
    request_sizes = [
        validate_tool_request_size(
            "app.invoke_command",
            {"commandId": step["commandId"], "values": step["values"]},
        )
        for step in steps
    ]

    return {
        "schema": "looksy.native-explicit-parity-repairs/v2",
        "correctionKind": "film-polish-static-quad-and-card-shadows",
        "revision": 2,
        "supersedes": "film-closing-v22-parity-corrections-landscape.commands-fragment.json",
        "orientation": "landscape",
        "execution": "prepared, unexecuted; checkpoint changed after v22, so fresh native readback and hash rebinding are mandatory",
        "preconditions": {
            "width": SEQUENCE["width"],
            "height": SEQUENCE["height"],
            "frameRate": FPS,
            "resourceId": RESOURCE_ID,
            "timelineId": TIMELINE_ID,
            "taskId": TASK_ID,
            "v22DocumentHash": DOCUMENT_HASH,
            "v22ProjectPath": str(PROJECT),
            "v22ProjectSha256": PROJECT_SHA256,
            "v22SaveReceiptPath": str(SAVE_RECEIPT.relative_to(ROOT)),
            "v22SaveReceiptSha256": SAVE_RECEIPT_SHA256,
            "v22SaveReceiptReadbackVerified": bool(save_receipt["readbackVerified"]),
            "clipCount": save_receipt["clipCount"],
            "trackCount": len(project["tracks"]),
            "currentTaskMustBeReinspected": True,
            "doNotReplayIfDocumentHashTrackBindingsOrTargetGeometryDiffer": True,
        },
        "sourceIntegrity": {
            "files": [
                {"path": rel, "sha256": info["sha256"], "bytes": info["bytes"]}
                for rel, info in source_hashes.items()
            ],
            "nativeSaveReceipt": {
                "path": str(SAVE_RECEIPT.relative_to(ROOT)),
                "sha256": SAVE_RECEIPT_SHA256,
                "documentHash": DOCUMENT_HASH,
                "fileSha256": PROJECT_SHA256,
                "taskStepId": save_receipt["taskStepId"],
            },
        },
        "sourceTruth": {
            "projectDurationSeconds": PROJECT_DURATION_SECONDS,
            "creativeFilmChapter": {"fromSeconds": 184, "durationSeconds": 68},
            "filmPhase": {"localStartSeconds": 60, "localEndSeconds": 68},
            "atSequence": {"localStartSeconds": 60, "durationSeconds": 8, "globalIntervalSeconds": [244, 252]},
            "filmCard": {
                "width": 830,
                "height": 467,
                "stageOffset": [650, 120],
                "stageScale": 1.1,
                "transform": "perspective(1500px) rotateY(-7deg)",
                "transformOrigin": "center center",
                "boxShadow": "0 30px 70px #0008",
            },
            "socialCard": {
                "width": 260,
                "height": 462,
                "stageOffset": [690, 230],
                "stageScale": 1.1,
                "transform": "translateY(90 * (1 - out(s, 0.3, 1.3))) rotate(5deg)",
                "boxShadow": "0 25px 70px #0009",
                "atSequenceGlobalIntervalSeconds": [244, 252],
            },
            "nativeTimingAudit": {
                "rawDurationSeconds": 12,
                "trimStartSeconds": 0,
                "trimEndSeconds": 8,
                "playbackRate": 1,
                "effectiveNativeIntervalSeconds": [244, 252],
                "result": "No timing mutation is needed: native clipTrimmedDuration uses (trimEnd-trimStart)/playbackRate for these clips with no retime curve, yielding 8 seconds. This exactly matches Remotion At's 240 frames at 30 fps. The raw 12-second duration is not their active timeline duration.",
            },
            "sourceFiles": [
                {"path": rel, "sha256": info["sha256"]}
                for rel, info in source_hashes.items()
                if rel.startswith("marketing/animated-trailer/")
            ],
        },
        "stableTargets": {
            alias: {
                "clipId": details["clip"]["clipId"],
                "trackId": details["track"]["trackId"],
                "trackLabel": details["track"].get("displayLabel"),
                "clipLabel": details["clip"].get("displayLabel"),
                "mediaType": details["clip"].get("mediaType"),
                "startSeconds": details["clip"].get("startSeconds"),
                "rawDurationSeconds": details["clip"].get("durationSeconds"),
                "trimStartSeconds": details["clip"].get("trimStartSeconds"),
                "trimEndSeconds": details["clip"].get("trimEndSeconds"),
            }
            for alias, details in clips.items()
        },
        "geometry": {
            "coordinateOrder": ["top-left", "top-right", "bottom-right", "bottom-left"],
            "videoQuadDomain": "frame-local normalized card coordinates",
            "titleQuadDomain": "full-canvas normalized coordinates extending the same card plane",
            "filmPoints": quad_source.as_native_points(film_points),
            "titlePoints": quad_source.as_native_points(title_points),
            "filmMatrixVsScalarMaxAbsoluteError": film_matrix_error,
            "titleMatrixVsScalarMaxAbsoluteError": title_matrix_error,
            "filmMinimumPerspectiveW": film_min_w,
            "titleMinimumPerspectiveW": title_min_w,
        },
        "shadowMapping": {
            "stageScale": 1.1,
            "film": {"color": "#88000000", "offsetX": 0.0, "offsetY": 33.0, "blurRadius": 77.0, "spread": 0.0},
            "social": {"color": "#99000000", "offsetX": 0.0, "offsetY": 27.5, "blurRadius": 77.0, "spread": 0.0},
            "note": "CSS offsets and blur radius are scaled by the 1.1 landscape Stage. Native CSS-style blur uses approximately half the radius as Gaussian sigma.",
        },
        "reviewFramesSeconds": [244.03333333333333, 244.3, 245.3, 248.0, 251.96666666666667, 252.0],
        "timingReview": {
            "filmVideoEffectiveDurationSeconds": 8.0,
            "socialVideoEffectiveDurationSeconds": 8.0,
            "titleDurationSeconds": 8.0,
            "requiredTimingCommands": [],
            "warning": "None for the audited SaveCopy: raw video durationSeconds=12 is shortened to an 8-second active interval by trimEndSeconds=8. If fresh native readback reveals retime keys or changed trim values, regenerate this plan instead of applying it.",
        },
        "verificationLimit": "Offline geometry, source, timing, and ID verification only; no native action was applied and no rendered frame was reviewed in this task.",
        "nativeRequestAudit": {
            "transport": "native_mcp.py full JSON-RPC HTTP request body",
            "requestLimitBytes": 1048576,
            "actionCount": len(steps),
            "maxRequestBytes": max(request_sizes, default=0),
        },
        "steps": steps,
    }


def write_fragment() -> Path:
    if OUTPUT.exists():
        raise FileExistsError(f"Refusing to overwrite immutable fragment: {OUTPUT}")
    OUTPUT.write_text(json.dumps(build_fragment(), indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return OUTPUT


if __name__ == "__main__":
    print(write_fragment())
