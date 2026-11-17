"""Bind native FilmPolish perspective and shadow repairs to the executed v18 receipt.

Offline planner only. It does not inspect or mutate the live app, execute native
commands, or rewrite earlier plans and receipts.
"""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
from typing import Any

import film_closing_perspective_quad_corrections as quad_source


ROOT = Path(__file__).resolve().parent
PROJECT = ROOT / "projects/looksy-native-landscape-in-progress-v15.json"
RECEIPT = ROOT / "evidence/native-full-landscape-v18-receipt.json"
FILM_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/FilmPolish.tsx"
CREATIVE_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/CreativeFilm.tsx"
KIT_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/Kit.tsx"
BUYER_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/BuyerScenes.tsx"
OUTPUT = ROOT / "film-closing-v18-parity-corrections-landscape.commands-fragment.json"

EXPECTED_PROJECT_SHA256 = "00d3f2502ae245883f62a1b00607efd23035db7011318e60fec9f397828c8994"
EXPECTED_RECEIPT_SHA256 = "f058390d011b5066301f6829caeb5fec6a933e2983b9e43e1fdd366aefa4058b"
EXPECTED_PLAN_SHA256 = "0c95be9d89a5d0d370d8875049cee863f94164d369255e81526a5ad87233c11f"
EXPECTED_DOCUMENT_HASH = "ef36da369dfa3daf828b88ee74cf2237fc849f72fe1472b57c86241cddfb05a3"
EXPECTED_RESOURCE_ID = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
EXPECTED_TIMELINE_ID = "3db4e4fb-6737-415c-b6ee-dde766e7c237"
FPS = 30

EXPECTED_SOURCE_HASHES = {
    FILM_SOURCE: "c879dd5c41e1ceae99f14b7c485eeff04d2e3a198250c4747f856d5294d52d6f",
    CREATIVE_SOURCE: "e9ab5e5f37372c4e7073db7aa88b452fb2bae20b565894b47da74964e179e757",
    KIT_SOURCE: "591e74ad93f6ea2b909cbf4bc0243c9cd5ce95f61b07ce22f198675215ab25d9",
    BUYER_SOURCE: "c614852258734d6be5de7efe371c9ccc2f7a02d62537b22b731b966737a4bb93",
}

TARGET_CREATE_STEPS = {
    "film": "film-closing/framing-landscape-film",
    "title": "film-closing/framing-landscape-title",
    "social": "film-closing/framing-social-film",
}
TARGET_FRAME_STEPS = {
    "film": "film-closing/framing-landscape-film-frame",
    "social": "film-closing/framing-social-film-frame",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _clip_id(step: dict[str, Any]) -> str:
    result = step.get("result", {})
    return str(result.get("createdClipId") or result.get("clip", {}).get("clipId") or "")


def _find_clip(project: dict[str, Any], clip_id: str) -> tuple[dict[str, Any], dict[str, Any]]:
    matches = [
        (track, clip)
        for track in project.get("tracks", [])
        for clip in track.get("clips", [])
        if clip.get("clipId") == clip_id
    ]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one v18 clip {clip_id}, found {len(matches)}")
    return matches[0]


def _verify_v18() -> tuple[dict[str, Any], dict[str, Any], dict[str, dict[str, Any]]]:
    if sha256(PROJECT) != EXPECTED_PROJECT_SHA256:
        raise ValueError("The saved v18 landscape project bytes changed; refusing to bind IDs")
    if sha256(RECEIPT) != EXPECTED_RECEIPT_SHA256:
        raise ValueError("The executed v18 landscape receipt changed; refusing to bind IDs")
    for source, expected in EXPECTED_SOURCE_HASHES.items():
        if sha256(source) != expected:
            raise ValueError(f"Creative source changed since the parity audit: {source}")

    project = json.loads(PROJECT.read_text(encoding="utf-8"))
    receipt = json.loads(RECEIPT.read_text(encoding="utf-8"))
    if project.get("timelineId") != EXPECTED_TIMELINE_ID:
        raise ValueError("The v18 project timeline ID changed")
    if receipt.get("status") != "authored-awaiting-native-review":
        raise ValueError("The v18 receipt is not the expected completed authoring receipt")
    if receipt.get("planSha256") != EXPECTED_PLAN_SHA256 or len(receipt.get("steps", [])) != 228:
        raise ValueError("The executed v18 plan lineage no longer matches the audited 228-step suffix")

    steps_by_id = {step.get("id"): step for step in receipt.get("steps", [])}
    if len(steps_by_id) != len(receipt.get("steps", [])):
        raise ValueError("The v18 receipt contains duplicate action IDs")
    save = receipt["steps"][-1].get("result", {})
    if any(save.get(key) != value for key, value in {
            "commandId": "timeline.action.saveTimeline",
            "documentHash": EXPECTED_DOCUMENT_HASH,
            "fileSha256": EXPECTED_PROJECT_SHA256,
            "readbackVerified": True,
            "resourceId": EXPECTED_RESOURCE_ID,
            "timelineId": EXPECTED_TIMELINE_ID,
    }.items()):
        raise ValueError("The final v18 save receipt does not match the inspected project")

    clips: dict[str, dict[str, Any]] = {}
    for alias, create_step_id in TARGET_CREATE_STEPS.items():
        create = steps_by_id.get(create_step_id)
        if create is None or create.get("commandId") not in {
                "timeline.action.createTimelineClip", "timeline.action.createTimelineTitle"}:
            raise ValueError(f"Missing executed v18 create step: {create_step_id}")
        clip_id = _clip_id(create)
        if not clip_id:
            raise ValueError(f"The v18 receipt has no stable clip ID for {create_step_id}")
        track, clip = _find_clip(project, clip_id)
        if clip.get("displayLabel") not in {
                "framing-landscape-film", "A little further.", "framing-social-film"}:
            raise ValueError(f"Clip label changed for the v18 target {create_step_id}")
        if track.get("trackId") != create.get("result", {}).get("trackId") and create.get("result", {}).get("trackId"):
            raise ValueError(f"Track binding changed for the v18 target {create_step_id}")
        clips[alias] = {"clipId": clip_id, "trackId": track["trackId"], "clip": clip}

    for alias, frame_step_id in TARGET_FRAME_STEPS.items():
        frame_step = steps_by_id.get(frame_step_id)
        target = clips[alias]
        if frame_step is None or frame_step.get("commandId") != "timeline.action.setTimelineClipFrame":
            raise ValueError(f"Missing executed v18 frame step: {frame_step_id}")
        if frame_step.get("values", {}).get("clipId") != target["clipId"]:
            raise ValueError(f"Frame step {frame_step_id} no longer targets its receipt-bound clip")
        timing = target["clip"]
        if (timing.get("startSeconds") != 244 or timing.get("durationSeconds") != 12 or
                timing.get("trimStartSeconds") != 0 or timing.get("trimEndSeconds") != 8):
            raise ValueError(f"The native v18 source timing changed for {target['clipId']}")

    return project, receipt, clips


def build_fragment() -> dict[str, Any]:
    _project, receipt, clips = _verify_v18()
    quad_fragment = quad_source.build_fragment("landscape")
    quad_steps = deepcopy(quad_fragment["steps"])
    if len(quad_steps) != 2:
        raise ValueError("Expected the two media/title quads from the audited source transform")
    bound_quads = []
    aliases = ("film", "title")
    for step, alias in zip(quad_steps, aliases):
        step["values"]["clipId"] = clips[alias]["clipId"]
        step["id"] = f"film-closing-v18-perspective-{alias}"
        bound_quads.append(step)

    receipt_steps = {step.get("id"): step for step in receipt["steps"]}
    shadow_specs = (
        ("film", "#88000000", 33.0, 77.0),
        ("social", "#99000000", 27.5, 77.0),
    )
    shadow_steps = []
    for alias, color, offset_y, blur_radius in shadow_specs:
        frame_id = TARGET_FRAME_STEPS[alias]
        values = deepcopy(receipt_steps[frame_id]["values"])
        values["clipId"] = clips[alias]["clipId"]
        values["frameShadow"] = {
            "enabled": True,
            "color": color,
            "offsetX": 0.0,
            "offsetY": offset_y,
            "blurRadius": blur_radius,
            "spread": 0.0,
        }
        shadow_steps.append({
            "id": f"film-closing-v18-shadow-{alias}",
            "commandId": "timeline.action.setTimelineClipFrame",
            "values": values,
        })

    steps = bound_quads + shadow_steps
    from native_mcp import validate_tool_request_size
    request_sizes = [
        validate_tool_request_size(
            "app.invoke_command",
            {"commandId": step["commandId"], "values": step["values"]})
        for step in steps
    ]
    if max(request_sizes, default=0) > 1024 * 1024:
        raise ValueError("FilmPolish parity action exceeds the native MCP request size ceiling")

    local_geometry = quad_fragment["geometry"]
    return {
        "schema": "looksy.native-explicit-parity-repairs/v2",
        "correctionKind": "film-polish-static-quad-and-card-shadows",
        "orientation": "landscape",
        "execution": "prepared, unexecuted; requires the exact active task/resource and a fresh readback",
        "preconditions": {
            "width": 1920,
            "height": 1080,
            "frameRate": FPS,
            "resourceId": EXPECTED_RESOURCE_ID,
            "timelineId": EXPECTED_TIMELINE_ID,
            "v18DocumentHash": EXPECTED_DOCUMENT_HASH,
            "v18ProjectPath": str(PROJECT),
            "v18ProjectSha256": EXPECTED_PROJECT_SHA256,
            "v18ReceiptPath": str(RECEIPT.relative_to(ROOT)),
            "v18ReceiptSha256": EXPECTED_RECEIPT_SHA256,
            "v18PlanSha256": EXPECTED_PLAN_SHA256,
            "currentTaskMustBeReinspected": True,
            "doNotReplayIfTheActiveDocumentHashOrClipIDsDiffer": True,
        },
        "sourceTruth": {
            "chapterRangeSeconds": [244.0, 252.0],
            "cardTransform": "perspective(1500px) rotateY(-7deg), origin center center",
            "landscapeCardCssShadow": "0 30px 70px #0008",
            "socialCardCssShadow": "0 25px 70px #0009",
            "sourceFiles": [
                {"path": str(path.relative_to(ROOT.parent.parent)), "sha256": sha256(path)}
                for path in EXPECTED_SOURCE_HASHES
            ],
        },
        "stableTargets": {alias: data["clipId"] for alias, data in clips.items()},
        "geometry": {
            "coordinateOrder": ["top-left", "top-right", "bottom-right", "bottom-left"],
            "videoClipQuadDomain": "native frame-local normalized card coordinates",
            "titleClipQuadDomain": "full-canvas normalized coordinates, same source projective plane",
            "videoPoints": local_geometry["calculation"]["videoLocalQuadPoints"],
            "titlePoints": local_geometry["calculation"]["titleFullCanvasQuadPoints"],
            "sourceCrossCheckMaxError": local_geometry["calculation"]["matrixVsScalarMaxAbsoluteErrorPixelsOrW"],
            "stageCornerErrorPixels": local_geometry["calculation"]["stageTransformedCardCornerMaxErrorPixels"],
        },
        "shadowMapping": {
            "landscapeStageScale": 1.1,
            "film": {"color": "#88000000", "offsetY": 33.0, "blurRadius": 77.0},
            "social": {"color": "#99000000", "offsetY": 27.5, "blurRadius": 77.0},
            "note": "Offsets and blur are CSS pixels multiplied by the 1.1 source Stage scale; spread is zero.",
        },
        "reviewFramesSeconds": [244.03333333333333, 244.3, 245.3, 248.0, 251.96666666666667, 252.0],
        "timingReview": {
            "v18FilmClipDurationSeconds": 12.0,
            "v18MediaTrimEndSeconds": 8.0,
            "sourceAtDurationSeconds": 8.0,
            "warning": "The executed v18 receipt places both media clips for 12s while trimming each source to 8s. This fragment preserves timing; inspect frames after 252s and correct duration separately if the native clip remains visible beyond the source At window.",
        },
        "verificationLimit": "Geometry and IDs are bound to the saved v18 project/receipt; the fragment has not been applied or visually reviewed in the live native project.",
        "nativeRequestAudit": {
            "transport": "native_mcp.py full JSON-RPC HTTP body",
            "requestLimitBytes": 1048576,
            "actionCount": len(steps),
            "maxRequestBytes": max(request_sizes, default=0),
        },
        "steps": steps,
    }


def write_fragment() -> Path:
    if OUTPUT.exists():
        raise FileExistsError(f"Refusing to overwrite immutable fragment: {OUTPUT}")
    fragment = build_fragment()
    OUTPUT.write_text(json.dumps(fragment, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return OUTPUT


if __name__ == "__main__":
    print(write_fragment())
