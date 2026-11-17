"""Build unexecuted native quads for the active FilmPolish perspective card.

The parent card is one CSS plane containing two native layers: the delivery
video and its independently editable title. The video uses a frame-local quad;
the full-canvas title layer uses the same projective transform extended over
the canvas so its glyphs stay on the video card's plane.

This builder only writes new correction fragments. It never edits a historical
plan, a receipt, or a native Timeline.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parent
CREATIVE = ROOT.parent / "animated-trailer/src/creative-launch"
SOURCE_COMPONENT = CREATIVE / "FilmPolish.tsx"
SOURCE_ROOT = CREATIVE / "CreativeFilm.tsx"
SOURCE_KIT = CREATIVE / "Kit.tsx"
SOURCE_BUYER = CREATIVE / "BuyerScenes.tsx"
ACTION_SCHEMA = ROOT.parents[1] / "apps/desktop/src/agent_timeline_commands.h"
QUAD_HELPER_HEADER = ROOT.parents[1] / "apps/desktop/src/timeline_frame_quad_warp.h"
CHAPTER_PLANS = {
    "landscape": ROOT / "film-closing-v3-landscape.commands.json",
    "portrait": ROOT / "film-closing-v3-portrait.commands.json",
}
LINEAGE_PLANS = {
    "landscape": ROOT / "full-landscape-v15-timeout-reconciled-suffix.commands.json",
    "portrait": ROOT / "full-portrait-v9-schema-corrected-parent.commands.json",
}
EVIDENCE = ROOT / "evidence"

CARD_X = 0.0
CARD_Y = 65.0
CARD_WIDTH = 830.0
CARD_HEIGHT = 467.0
PERSPECTIVE_PX = 1500.0
ROTATE_Y_DEGREES = -7.0
FPS = 30

ORIENTATIONS = {
    "landscape": {
        "width": 1920,
        "height": 1080,
        "stage_x": 650.0,
        "stage_y": 120.0,
        "stage_scale": 1.1,
    },
    "portrait": {
        "width": 1080,
        "height": 1920,
        "stage_x": 40.0,
        "stage_y": 710.0,
        "stage_scale": 1.0,
    },
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def multiply4(matrix, vector):
    return [sum(matrix[row][col] * vector[col] for col in range(4))
            for row in range(4)]


def css_matrix_project(x: float, y: float, center_x: float, center_y: float,
                       distance: float, angle_degrees: float):
    """Project one 2D plane point using an explicit P * Ry homogeneous matrix."""
    angle = math.radians(angle_degrees)
    cosine, sine = math.cos(angle), math.sin(angle)
    rotate_y = [
        [cosine, 0.0, sine, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [-sine, 0.0, cosine, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]
    perspective = [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, -1.0 / distance, 1.0],
    ]
    transform = [[sum(perspective[row][k] * rotate_y[k][col]
                      for k in range(4)) for col in range(4)]
                 for row in range(4)]
    local = [x - center_x, y - center_y, 0.0, 1.0]
    homogeneous = multiply4(transform, local)
    if homogeneous[3] <= 1.0e-9:
        raise ValueError("CSS perspective crosses the camera plane")
    return (center_x + homogeneous[0] / homogeneous[3],
            center_y + homogeneous[1] / homogeneous[3],
            homogeneous[3])


def css_closed_form_project(x: float, y: float, center_x: float,
                            center_y: float, distance: float,
                            angle_degrees: float):
    """Independent scalar cross-check of the P * Ry matrix projection."""
    dx, dy = x - center_x, y - center_y
    angle = math.radians(angle_degrees)
    rotated_x = math.cos(angle) * dx
    rotated_z = -math.sin(angle) * dx
    homogeneous_w = 1.0 - rotated_z / distance
    if homogeneous_w <= 1.0e-9:
        raise ValueError("CSS perspective crosses the camera plane")
    return (center_x + rotated_x / homogeneous_w,
            center_y + dy / homogeneous_w,
            homogeneous_w)


def assert_convex(points):
    crosses = []
    for index, point in enumerate(points):
        next_point = points[(index + 1) % len(points)]
        after_next = points[(index + 2) % len(points)]
        ax, ay = next_point[0] - point[0], next_point[1] - point[1]
        bx, by = after_next[0] - next_point[0], after_next[1] - next_point[1]
        crosses.append(ax * by - ay * bx)
    if not (all(value > 1.0e-9 for value in crosses) or
            all(value < -1.0e-9 for value in crosses)):
        raise ValueError("Perspective corners must form a finite convex quad")


def rounded(value: float) -> float:
    return round(value, 15)


def local_card_quad():
    corners = [
        (CARD_X, CARD_Y),
        (CARD_X + CARD_WIDTH, CARD_Y),
        (CARD_X + CARD_WIDTH, CARD_Y + CARD_HEIGHT),
        (CARD_X, CARD_Y + CARD_HEIGHT),
    ]
    center_x, center_y = CARD_X + CARD_WIDTH / 2.0, CARD_Y + CARD_HEIGHT / 2.0
    matrix_points = []
    cross_check = []
    min_w = 1.0
    for x, y in corners:
        matrix_x, matrix_y, matrix_w = css_matrix_project(
            x, y, center_x, center_y, PERSPECTIVE_PX, ROTATE_Y_DEGREES)
        form_x, form_y, form_w = css_closed_form_project(
            x, y, center_x, center_y, PERSPECTIVE_PX, ROTATE_Y_DEGREES)
        matrix_points.append(((matrix_x - CARD_X) / CARD_WIDTH,
                              (matrix_y - CARD_Y) / CARD_HEIGHT))
        cross_check.append(max(abs(matrix_x - form_x), abs(matrix_y - form_y),
                               abs(matrix_w - form_w)))
        min_w = min(min_w, matrix_w)
    assert_convex(matrix_points)
    return matrix_points, max(cross_check), min_w


def full_canvas_title_quad(orientation: str):
    settings = ORIENTATIONS[orientation]
    width, height = settings["width"], settings["height"]
    scale = settings["stage_scale"]
    center_x = settings["stage_x"] + scale * (CARD_X + CARD_WIDTH / 2.0)
    center_y = settings["stage_y"] + scale * (CARD_Y + CARD_HEIGHT / 2.0)
    distance = PERSPECTIVE_PX * scale
    corners = [(0.0, 0.0), (float(width), 0.0),
               (float(width), float(height)), (0.0, float(height))]
    matrix_points = []
    cross_check = []
    min_w = 1.0
    for x, y in corners:
        matrix_x, matrix_y, matrix_w = css_matrix_project(
            x, y, center_x, center_y, distance, ROTATE_Y_DEGREES)
        form_x, form_y, form_w = css_closed_form_project(
            x, y, center_x, center_y, distance, ROTATE_Y_DEGREES)
        matrix_points.append((matrix_x / width, matrix_y / height))
        cross_check.append(max(abs(matrix_x - form_x), abs(matrix_y - form_y),
                               abs(matrix_w - form_w)))
        min_w = min(min_w, matrix_w)
    assert_convex(matrix_points)
    return matrix_points, max(cross_check), min_w


def as_native_points(points):
    result = [{"x": rounded(x), "y": rounded(y)} for x, y in points]
    if any(not (-4.0 <= point[axis] <= 4.0)
           for point in result for axis in ("x", "y")):
        raise ValueError("A perspective point falls outside the native -4..4 limit")
    return result


def target_steps(plan_path: Path, step_ids):
    plan = json.loads(plan_path.read_text())
    found = {}
    for index, step in enumerate(plan.get("steps", [])):
        if step.get("id") in step_ids:
            found[step["id"]] = (index, step)
    expected = {
        "film-closing/framing-landscape-film": "timeline.action.createTimelineClip",
        "film-closing/framing-landscape-title": "timeline.action.createTimelineTitle",
        "framing-landscape-film": "timeline.action.createTimelineClip",
        "framing-landscape-title": "timeline.action.createTimelineTitle",
    }
    for step_id in step_ids:
        command = expected[step_id]
        if step_id not in found or found[step_id][1].get("commandId") != command:
            raise ValueError(f"Expected future plan target {step_id} ({command})")
    return plan, found


def check_source_truth():
    film_text = SOURCE_COMPONENT.read_text()
    required = (
        'width: 830', 'height: 467',
        'transform: "perspective(1500px) rotateY(-7deg)"',
        'boxShadow: "0 30px 70px #0008"',
        'fontSize: 65', 'left: 30', 'bottom: 35',
    )
    if any(token not in film_text for token in required):
        raise ValueError("The active FilmPolish phase-7 card no longer matches audited CSS")
    creative_text = SOURCE_ROOT.read_text()
    if 'from={184 * fps}' not in creative_text or 'from={280 * fps}' not in creative_text:
        raise ValueError("CreativeFilm chapter offsets no longer match the audited ranges")
    buyer_text = SOURCE_BUYER.read_text()
    if "perspective(" in buyer_text or "rotateY(" in buyer_text:
        raise ValueError("BuyerScenes gained a perspective transform; re-audit closing before using this fragment")


def build_fragment(orientation: str):
    check_source_truth()
    chapter_plan_path = CHAPTER_PLANS[orientation]
    lineage_path = LINEAGE_PLANS[orientation]
    chapter_plan, chapter_targets = target_steps(
        chapter_plan_path,
        {"framing-landscape-film", "framing-landscape-title"},
    )
    lineage_plan, lineage_targets = target_steps(
        lineage_path,
        {"film-closing/framing-landscape-film",
         "film-closing/framing-landscape-title"},
    )
    local_points, local_error, local_min_w = local_card_quad()
    title_points, title_error, title_min_w = full_canvas_title_quad(orientation)
    if local_error > 1.0e-10 or title_error > 1.0e-10:
        raise ValueError("Independent CSS matrix and scalar calculations disagree")

    selected_targets = {}
    for alias, source_id in (
            ("film-closing/framing-landscape-film", "framing-landscape-film"),
            ("film-closing/framing-landscape-title", "framing-landscape-title")):
        chapter_index, chapter_step = chapter_targets[source_id]
        lineage_index, lineage_step = lineage_targets[alias]
        selected_targets[alias] = {
            "chapterPlanIndex": chapter_index,
            "chapterPlanStepId": source_id,
            "chapterPlanCommandId": chapter_step["commandId"],
            "lineagePlanIndex": lineage_index,
            "lineagePlanCommandId": lineage_step["commandId"],
        }
    lineage = {
        "path": lineage_path.name,
        "sha256": sha256(lineage_path),
        "stepCount": len(lineage_plan.get("steps", [])),
        "targetSteps": selected_targets,
        "nativePlanExecuted": False,
    }
    if orientation == "landscape":
        receipt_path = EVIDENCE / "native-full-landscape-v15-receipt.json"
        replay_path = EVIDENCE / "native-v15-timeout-idempotent-replay-result.json"
        replay_manifest_path = EVIDENCE / "native-v15-timeout-idempotent-replay-manifest.json"
        suffix_manifest = EVIDENCE / "native-v15-timeout-reconciled-suffix-manifest.json"
        manifest = json.loads(suffix_manifest.read_text())
        replay = json.loads(replay_path.read_text())["result"]["structuredContent"]
        replay_manifest = json.loads(replay_manifest_path.read_text())
        receipt = json.loads(receipt_path.read_text())
        receipt_failure = receipt.get("failure", {})
        if (manifest.get("completedPrefixCount") != 863 or
                manifest.get("remainingStepCount") != 228 or
                manifest.get("outputPlanSha256") != sha256(lineage_path) or
                receipt.get("status") != "incomplete" or
                len(receipt.get("steps", [])) != 861 or
                receipt_failure.get("stepId") != "film-closing/audio-mixer-waveform-1-chunk-06-frame" or
                receipt_failure.get("rolledBack") is not False or
                receipt_failure.get("recoveryRequired") is not True or
                replay.get("idempotentReplay") is not True or
                replay.get("operationCount") != 2 or
                replay.get("taskStateHash") != manifest.get("startingDocumentHash") or
                replay.get("batchId") != receipt_failure.get("batchId") or
                replay.get("batchId") != replay_manifest.get("expectedNativeBatchId") or
                replay.get("batchRequestSha256") != replay_manifest.get("expectedNativeRequestSha256") or
                replay.get("resourceId") != manifest.get("resourceId") or
                len(receipt.get("steps", [])) + replay.get("operationCount", 0) !=
                manifest.get("completedPrefixCount")):
            raise ValueError("The v15 receipt/cache-replay lineage no longer proves the exact 863-step prefix")
        lineage.update({
            "completedPrefixCount": manifest["completedPrefixCount"],
            "pendingSuffixCount": manifest["remainingStepCount"],
            "taskId": manifest["taskId"],
            "resourceId": manifest["resourceId"],
            "startingDocumentHash": manifest["startingDocumentHash"],
            "receiptPath": receipt_path.relative_to(ROOT).as_posix(),
            "receiptSha256": sha256(receipt_path),
            "receiptPlanSha256": receipt.get("planSha256"),
            "receiptCompletedStepCount": len(receipt.get("steps", [])),
            "idempotentReplayResultPath": replay_path.relative_to(ROOT).as_posix(),
            "idempotentReplayResultSha256": sha256(replay_path),
            "idempotentReplayManifestSha256": sha256(replay_manifest_path),
            "idempotentReplayBatchId": replay["batchId"],
            "idempotentReplayRequestSha256": replay["batchRequestSha256"],
            "idempotentReplayOperations": replay["operationCount"],
            "nativePlanExecuted": False,
        })

    # A full-canvas transform applies to the independently editable title
    # plane; the video receives the local card quad. Their projected card
    # corners must be identical after the orientation's Stage transform.
    source_stage = ORIENTATIONS[orientation]
    stage_scale = source_stage["stage_scale"]
    stage_x, stage_y = source_stage["stage_x"], source_stage["stage_y"]
    local_corners = [
        (CARD_X, CARD_Y), (CARD_X + CARD_WIDTH, CARD_Y),
        (CARD_X + CARD_WIDTH, CARD_Y + CARD_HEIGHT),
        (CARD_X, CARD_Y + CARD_HEIGHT),
    ]
    center_x, center_y = CARD_X + CARD_WIDTH / 2, CARD_Y + CARD_HEIGHT / 2
    global_center_x = stage_x + stage_scale * center_x
    global_center_y = stage_y + stage_scale * center_y
    global_distance = PERSPECTIVE_PX * stage_scale
    stage_cross_check = 0.0
    for (local_x, local_y), (local_qx, local_qy), _ in zip(
            local_corners, local_points, title_points):
        css_x, css_y, _ = css_matrix_project(
            local_x, local_y, center_x, center_y,
            PERSPECTIVE_PX, ROTATE_Y_DEGREES)
        expected_x = stage_x + stage_scale * css_x
        expected_y = stage_y + stage_scale * css_y
        source_x = stage_x + stage_scale * local_x
        source_y = stage_y + stage_scale * local_y
        canvas_x, canvas_y, _ = css_matrix_project(
            source_x, source_y, global_center_x, global_center_y,
            global_distance, ROTATE_Y_DEGREES)
        stage_cross_check = max(stage_cross_check,
                                abs(expected_x - canvas_x),
                                abs(expected_y - canvas_y))
        local_projected_x = CARD_X + local_qx * CARD_WIDTH
        local_projected_y = CARD_Y + local_qy * CARD_HEIGHT
        stage_local_x = stage_x + stage_scale * local_projected_x
        stage_local_y = stage_y + stage_scale * local_projected_y
        stage_cross_check = max(stage_cross_check,
                                abs(expected_x - stage_local_x),
                                abs(expected_y - stage_local_y))
    if stage_cross_check > 1.0e-9:
        raise ValueError("The media-local and title-canvas quads disagree on the card corners")

    media_ref = {"result": "film-closing/framing-landscape-film", "field": "createdClipId"}
    title_ref = {"result": "film-closing/framing-landscape-title", "field": "createdClipId"}
    steps = [
        {
            "id": f"film-closing-perspective/{orientation}/framing-landscape-film",
            "commandId": "timeline.action.setTimelineClipPerspectiveQuad",
            "values": {"clipId": media_ref, "enabled": True,
                       "points": as_native_points(local_points)},
        },
        {
            "id": f"film-closing-perspective/{orientation}/framing-landscape-title",
            "commandId": "timeline.action.setTimelineClipPerspectiveQuad",
            "values": {"clipId": title_ref, "enabled": True,
                       "points": as_native_points(title_points)},
        },
    ]

    source_provenance = [
        {"path": str(path.relative_to(ROOT.parent.parent)), "sha256": sha256(path)}
        for path in (SOURCE_COMPONENT, SOURCE_ROOT, SOURCE_KIT, SOURCE_BUYER)
    ]
    chapter_hash = sha256(chapter_plan_path)
    return {
        "schema": "looksy.native-explicit-parity-repairs/v1",
        "correctionKind": "css-perspective-rotateY-static-quad",
        "orientation": orientation,
        "composition": {
            "width": source_stage["width"],
            "height": source_stage["height"],
            "fps": FPS,
        },
        "sourceTruth": {
            "activeComposition": "CreativeFilm -> FilmChapter (184-252s) -> FilmPolish phase 7",
            "transform": "perspective(1500px) rotateY(-7deg)",
            "transformOrigin": "CSS default center center",
            "cardLocalRect": {"left": CARD_X, "top": CARD_Y,
                               "width": CARD_WIDTH, "height": CARD_HEIGHT},
            "childLayers": [
                "creative-polish-v2/arrival-12.mp4",
                "editable Instrument Serif overlay: A little further.",
            ],
            "chapterRangeSeconds": [244.0, 252.0],
            "closingAudit": "BuyerClosing (280-298s) contains no perspective() or rotateY() card; no closing action is generated.",
            "sourceFiles": source_provenance,
        },
        "planLineage": {
            "chapterPlanPath": chapter_plan_path.name,
            "chapterPlanSha256": chapter_hash,
            "lineagePlan": lineage,
        },
        "geometry": {
            "coordinateOrder": ["top-left", "top-right", "bottom-right", "bottom-left"],
            "coordinateRange": [-4.0, 4.0],
            "videoClipQuadDomain": "normalized to its native card frame; the source CSS card is 830x467 before Stage scale",
            "titleClipQuadDomain": "normalized to the full output canvas; extends the same CSS plane homography from the card center so the separately editable text follows the parent transform",
            "stage": {key: source_stage[key] for key in
                      ("stage_x", "stage_y", "stage_scale")},
            "calculation": {
                "primary": "CSS homogeneous matrix P(1500px) * Ry(-7deg), applied to TL/TR/BR/BL corners around the card center",
                "independentCrossCheck": "separate scalar projection formula compared to explicit 4x4 matrix multiplication",
                "matrixVsScalarMaxAbsoluteErrorPixelsOrW": rounded(max(local_error, title_error)),
                "minimumPerspectiveW": rounded(min(local_min_w, title_min_w)),
                "stageTransformedCardCornerMaxErrorPixels": rounded(stage_cross_check),
                "videoLocalQuadPoints": as_native_points(local_points),
                "titleFullCanvasQuadPoints": as_native_points(title_points),
            },
        },
        "nativeActionContract": {
            "commandId": "timeline.action.setTimelineClipPerspectiveQuad",
            "registeredValuesKeys": ["clipId", "enabled", "points"],
            "enabled": True,
            "pointsCount": 4,
            "pointOrder": ["top-left", "top-right", "bottom-right", "bottom-left"],
            "pointSchemaRange": [-4.0, 4.0],
            "actionSchemaSource": {
                "path": str(ACTION_SCHEMA.relative_to(ROOT.parents[1])),
                "sha256": sha256(ACTION_SCHEMA),
            },
            "geometryHelperSource": {
                "path": str(QUAD_HELPER_HEADER.relative_to(ROOT.parents[1])),
                "sha256": sha256(QUAD_HELPER_HEADER),
                "helper": "css_perspective_rotate_y_quad",
            },
            "verificationLimit": "Source registration/schema was inspected; no live MCP catalog call or native action was made.",
        },
        "requiresBeforeExecution": [
            "Complete every step in the exact lineage plan and retain its native receipts.",
            "Inspect the resulting active Timeline and bind both symbolic createdClipId references from those receipts; neither clip ID exists in the proven completed prefix yet.",
            "Use a fresh ready documentHash after the parent plan completes; the lineage startingDocumentHash is not the post-suffix hash.",
            "Apply only inside the same authorized Timeline task/resource, after confirming the active task owns the inspected resource.",
        ],
        "steps": steps,
        "nativeAuthoringExecuted": False,
        "execution": "prepared, unexecuted; target clip creation remains in the pending parent plan",
        "parentReceiptSha256": None,
    }


def write_outputs():
    for orientation in ("landscape", "portrait"):
        output = ROOT / f"film-closing-perspective-quad-corrections-v1-{orientation}.commands-fragment.json"
        if output.exists():
            raise SystemExit(f"Refusing to overwrite immutable correction fragment: {output}")
        fragment = build_fragment(orientation)
        output.write_text(json.dumps(fragment, indent=2, ensure_ascii=False) + "\n")
        print(f"{output.relative_to(ROOT)} steps={len(fragment['steps'])} sha256={sha256(output)}")


if __name__ == "__main__":
    write_outputs()
