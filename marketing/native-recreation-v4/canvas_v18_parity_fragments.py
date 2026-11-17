"""Build immutable additive Canvas and grain corrections for verified v18.

This is an offline planner only. It never opens Looksy, mutates a project, or
rewrites historical plans/receipts. Every output is bound to the exact saved
landscape project and final save receipt below; a changed source or project
causes generation to stop.
"""

from __future__ import annotations

from copy import deepcopy
from pathlib import Path
import hashlib
import json
import math
from typing import Any


ROOT = Path(__file__).resolve().parent
PROJECT = ROOT / "projects/looksy-native-landscape-in-progress-v15.json"
RECEIPT = ROOT / "evidence/native-full-landscape-v18-receipt.json"
CANVAS_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/Canvas.tsx"
CANVAS_POLISH_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/CanvasPolish.tsx"
BUYER_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/BuyerScenes.tsx"
KIT_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/Kit.tsx"
POINTER_SOURCE = ROOT.parent / "animated-trailer/src/creative-launch/ProofTools.tsx"
FPS = 30
SEQUENCE_WIDTH = 1920
SEQUENCE_HEIGHT = 1080

EXPECTED_PROJECT_SHA256 = "00d3f2502ae245883f62a1b00607efd23035db7011318e60fec9f397828c8994"
EXPECTED_RECEIPT_SHA256 = "f058390d011b5066301f6829caeb5fec6a933e2983b9e43e1fdd366aefa4058b"
EXPECTED_DOCUMENT_HASH = "ef36da369dfa3daf828b88ee74cf2237fc849f72fe1472b57c86241cddfb05a3"
EXPECTED_FILE_SHA256 = EXPECTED_PROJECT_SHA256
EXPECTED_PLAN_SHA256 = "0c95be9d89a5d0d370d8875049cee863f94164d369255e81526a5ad87233c11f"
EXPECTED_TIMELINE_ID = "3db4e4fb-6737-415c-b6ee-dde766e7c237"
EXPECTED_RESOURCE_ID = "timeline:" + EXPECTED_TIMELINE_ID

EXPECTED_SOURCE_HASHES = {
    CANVAS_SOURCE: "1367d5774eaf524d46d036a90368a47de1b510cba0a0a65feea9efcc7634cee0",
    CANVAS_POLISH_SOURCE: "2c08e41f57a87525595040ff30128c54c6ddb345d02633af4d30ac6347937266",
    BUYER_SOURCE: "c614852258734d6be5de7efe371c9ccc2f7a02d62537b22b731b966737a4bb93",
    KIT_SOURCE: "591e74ad93f6ea2b909cbf4bc0243c9cd5ce95f61b07ce22f198675215ab25d9",
    POINTER_SOURCE: "79964fc37b96896c30d7de806be956b0206a6e9ebd54235686ba3719b7cdf198",
}

ANCHORS = {
    "typewriter": "graphics-058e5c9c-cc4e-48dc-baa4-e8cdd7cd94f6",
    "poster_grain": "graphics-28324331-95db-4664-801b-5082be2011cf",
    "thumbnail_grain": "graphics-eca7f21b-1d8a-4dcb-982e-a507d4930139",
    "closing_grain": "graphics-855615b0-9f33-45bf-8434-cfe90fbb07b3",
    "checkerboard": "graphics-ecebad31-1d5b-490f-bd50-46353f1e42c3",
    "inpaint_pointer": "graphics-95522568-fe9a-4dec-bccf-531d3ba8b5a4",
}

INPAINT_CLIPS = {
    "background": "628f9937-ef85-4ada-87dc-7658c2abe0bb",
    "result": "951ae275-f92c-4192-999e-14db6e5de5ce",
    "comparison": "78522e2c-9215-4677-a066-de923f2c8404",
    "mask_tint": "21a93307-9986-496e-8650-9d04be516e17",
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def round9(value: float) -> float:
    return round(float(value), 9)


def clamp01(value: float) -> float:
    return max(0.0, min(1.0, value))


def smooth(value: float, start: float, end: float) -> float:
    u = clamp01((value - start) / (end - start))
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def out(value: float, start: float, end: float) -> float:
    u = clamp01((value - start) / (end - start))
    return 1.0 - (1.0 - u) ** 4


def ref(step_id: str, field: str) -> dict[str, str]:
    return {"result": step_id, "field": field}


class Fragment:
    def __init__(self) -> None:
        self.steps: list[dict[str, Any]] = []
        self.ids: set[str] = set()

    def add(self, step_id: str, action: str, **values: Any) -> dict[str, str]:
        if step_id in self.ids:
            raise ValueError(f"duplicate fragment step ID: {step_id}")
        self.ids.add(step_id)
        self.steps.append({
            "id": step_id,
            "commandId": "timeline.action." + action,
            "values": values,
        })
        return ref(step_id, "createdClipId")

    def track(self, step_id: str, name: str) -> dict[str, str]:
        self.add(step_id, "createTimelineTrack", kind="graphics", name=name[:48], audioRole="")
        return ref(step_id, "trackId")

    def generator(self, step_id: str, track: dict[str, str], start: float,
                  duration: float, parameters: dict[str, Any], kind: str) -> dict[str, str]:
        self.add(step_id, "createTimelineGenerator", trackId=track,
                 generatorKind=kind, startSeconds=round9(start),
                 durationSeconds=round9(duration), parameters=parameters)
        return ref(step_id, "createdClipId")

    def frame(self, step_id: str, clip: dict[str, str], *, x: float, y: float,
              width: float, height: float, scale: float = 1.0,
              opacity: float = 1.0, corner_radius: float = 0.0,
              fit: str = "stretch") -> None:
        self.add(
            step_id, "setTimelineClipFrame", clipId=clip,
            offsetX=round9(4.0 * (x / SEQUENCE_WIDTH - 0.5)),
            offsetY=round9(4.0 * (y / SEQUENCE_HEIGHT - 0.5)),
            scale=round9(scale), rotationDegrees=0.0, perspectiveX=0.0,
            perspectiveY=0.0, opacity=round9(opacity), objectFit=fit,
            width=round9(width / SEQUENCE_WIDTH),
            height=round9(height / SEQUENCE_HEIGHT),
            cornerRadius=round9(corner_radius),
            cropLeft=0.0, cropTop=0.0, cropRight=0.0, cropBottom=0.0,
        )

    def move_before(self, step_id: str, track: dict[str, str], anchor: str) -> None:
        self.add(step_id, "moveTimelineGraphicsTrack", trackId=track,
                 beforeTrackId=anchor)

    def animate(self, step_id: str, clip: dict[str, str], channels: list[dict[str, Any]]) -> None:
        self.add(step_id, "setTimelineClipAnimation", clipId=clip, channels=channels)

    def perspective(self, step_id: str, clip: dict[str, str], poses: list[dict[str, Any]]) -> None:
        self.add(step_id, "setTimelineClipPerspectiveQuadAnimation",
                 clipId=clip, keyframes=poses)


def _find_project_clip(project: dict[str, Any], clip_id: str) -> tuple[dict[str, Any], dict[str, Any]]:
    matches = []
    for track in project.get("tracks", []):
        for clip in track.get("clips", []):
            if clip.get("clipId") == clip_id:
                matches.append((track, clip))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one clip {clip_id}, got {len(matches)}")
    return matches[0]


def _verify_baseline() -> tuple[dict[str, Any], dict[str, Any]]:
    if sha256(PROJECT) != EXPECTED_PROJECT_SHA256:
        raise ValueError("v18 landscape project bytes changed; refusing to bind this fragment")
    if sha256(RECEIPT) != EXPECTED_RECEIPT_SHA256:
        raise ValueError("v18 receipt bytes changed; refusing to bind this fragment")
    project = json.loads(PROJECT.read_text())
    receipt = json.loads(RECEIPT.read_text())
    if project.get("timelineId") != EXPECTED_TIMELINE_ID:
        raise ValueError("project timeline ID no longer matches the verified v18 resource")
    if receipt.get("status") != "authored-awaiting-native-review":
        raise ValueError("v18 receipt status is not the expected completed authoring state")
    if receipt.get("planSha256") != EXPECTED_PLAN_SHA256:
        raise ValueError("receipt plan hash changed")
    if not receipt.get("steps"):
        raise ValueError("v18 receipt has no command records")
    save = receipt["steps"][-1].get("result", {})
    expected_save = {
        "commandId": "timeline.action.saveTimeline",
        "documentHash": EXPECTED_DOCUMENT_HASH,
        "fileSha256": EXPECTED_FILE_SHA256,
        "readbackVerified": True,
        "resourceId": EXPECTED_RESOURCE_ID,
        "timelineId": EXPECTED_TIMELINE_ID,
    }
    for key, value in expected_save.items():
        if save.get(key) != value:
            raise ValueError(f"v18 final save receipt mismatch at {key}")
    for path, expected in EXPECTED_SOURCE_HASHES.items():
        if sha256(path) != expected:
            raise ValueError(f"creative source changed: {path}")

    expected_tracks = {
        ANCHORS["typewriter"], ANCHORS["poster_grain"], ANCHORS["thumbnail_grain"],
        ANCHORS["closing_grain"], ANCHORS["checkerboard"], ANCHORS["inpaint_pointer"],
    }
    present_tracks = {track.get("trackId") for track in project.get("tracks", [])}
    missing_tracks = expected_tracks - present_tracks
    if missing_tracks:
        raise ValueError("v18 insertion anchor disappeared: " + ", ".join(sorted(missing_tracks)))
    for clip_id in INPAINT_CLIPS.values():
        _find_project_clip(project, clip_id)
    _find_project_clip(project, "66ed4748-7f89-4ebb-8db7-350ee25a176d")
    _find_project_clip(project, "d0ed8128-1e28-462f-8015-ed36697d0d5e")
    return project, receipt


def _source_record(path: Path) -> dict[str, Any]:
    return {"path": str(path.relative_to(ROOT.parent)), "sha256": sha256(path),
            "bytes": path.stat().st_size}


def _poster_local_corners(local_seconds: float) -> tuple[list[tuple[float, float]], float, float]:
    if local_seconds < 9.0:
        focus = smooth(local_seconds, 5.2, 6.4) * (1.0 - smooth(local_seconds, 7.6, 8.8))
        yaw_degrees = 12.0 * (1.0 - out(local_seconds, 0.0, 1.6))
    else:
        focus = 0.0
        yaw_degrees = 0.0
    stage_x = 650.0 - 440.0 * focus
    stage_y = 120.0 - 150.0 * focus
    stage_scale = 1.1 + 0.4 * focus
    z = math.radians(-3.0)
    yaw = math.radians(yaw_degrees)
    cos_z, sin_z = math.cos(z), math.sin(z)
    cos_yaw, sin_yaw = math.cos(yaw), math.sin(yaw)
    points = []
    for x, y in ((0.0, 0.0), (600.0, 0.0), (600.0, 750.0), (0.0, 750.0)):
        x1 = x * cos_z - y * sin_z
        y1 = x * sin_z + y * cos_z
        # CSS matrix order: perspective(1800) * rotateY * rotateZ * scale(.6).
        perspective_w = 1.0 + x1 * sin_yaw / 1800.0
        x2 = x1 * cos_yaw / perspective_w
        y2 = y1 / perspective_w
        points.append((210.0 + x2, -10.0 + y2))
    return points, stage_scale, (stage_x, stage_y)


def _thumbnail_local_corners(local_seconds: float) -> tuple[list[tuple[float, float]], float, float]:
    focus = smooth(local_seconds, 1.4, 2.8) * (1.0 - smooth(local_seconds, 8.5, 9.8))
    stage_x = 650.0 - 440.0 * focus
    stage_y = 120.0 - 150.0 * focus
    stage_scale = 1.1 + 0.4 * focus
    yaw_degrees = -10.0 * (1.0 - out(local_seconds, 0.0, 1.4))
    yaw = math.radians(yaw_degrees)
    cos_yaw, sin_yaw = math.cos(yaw), math.sin(yaw)
    points = []
    for x, y in ((0.0, 0.0), (1000.0, 0.0), (1000.0, 562.0), (0.0, 562.0)):
        centered_x = x - 500.0
        centered_y = y - 281.0
        perspective_w = 1.0 + centered_x * sin_yaw / 1400.0
        x2 = 500.0 + centered_x * cos_yaw / perspective_w
        y2 = 281.0 + centered_y / perspective_w
        points.append((x2, 80.0 + y2))
    return points, stage_scale, (stage_x, stage_y)


def _quad_pose(points: list[tuple[float, float]], box_width: float,
               box_height: float) -> list[dict[str, float]]:
    left = min(point[0] for point in points)
    right = max(point[0] for point in points)
    top = min(point[1] for point in points)
    bottom = max(point[1] for point in points)
    center_x, center_y = (left + right) * 0.5, (top + bottom) * 0.5
    pose = [{"x": round9(0.5 + (x - center_x) / box_width),
             "y": round9(0.5 + (y - center_y) / box_height)}
            for x, y in points]
    return pose


def _build_moving_grain(fragment: Fragment, *, key: str, name: str, anchor: str,
                        start: float, duration: float, local_size: tuple[int, int],
                        opacity: float, corner_fn) -> None:
    frame_count = int(round(duration * FPS))
    samples = []
    local_bboxes = []
    for frame_index in range(frame_count + 1):
        local_seconds = frame_index / FPS
        corners, stage_scale, stage_origin = corner_fn(local_seconds)
        left = min(point[0] for point in corners)
        right = max(point[0] for point in corners)
        top = min(point[1] for point in corners)
        bottom = max(point[1] for point in corners)
        center = ((left + right) * 0.5, (top + bottom) * 0.5)
        pose = _quad_pose(corners, max(right - left, 1e-9), max(bottom - top, 1e-9))
        # The fixed output canvas is centered on the moving projected bounds;
        # padding on each side preserves the exact source quad at every frame.
        local_bboxes.append((right - left, bottom - top))
        samples.append({
            "localSeconds": round9(local_seconds),
            "points": pose,
            "frameScale": round9(stage_scale / 1.1),
            "frameOffsetX": round9(4.0 * ((stage_origin[0] + stage_scale * center[0]) / SEQUENCE_WIDTH - 0.5)),
            "frameOffsetY": round9(4.0 * ((stage_origin[1] + stage_scale * center[1]) / SEQUENCE_HEIGHT - 0.5)),
        })
    max_width = max(value[0] for value in local_bboxes)
    max_height = max(value[1] for value in local_bboxes)
    # Keep a hair of output padding so float rounding never clips a corner.
    output_width = max_width + 1.0e-4
    output_height = max_height + 1.0e-4
    track = fragment.track(key + "-track", name)
    clip = fragment.generator(key + "-generator", track, start, duration, {
        "baseFrequencyX": 0.78, "baseFrequencyY": 0.78,
        "numOctaves": 3, "seed": 19, "stitchTiles": True,
        "outputWidth": local_size[0], "outputHeight": local_size[1],
        "opacity": opacity,
    }, "fractal-noise")
    first = samples[0]
    initial_x = (first["frameOffsetX"] / 4.0 + 0.5) * SEQUENCE_WIDTH
    initial_y = (first["frameOffsetY"] / 4.0 + 0.5) * SEQUENCE_HEIGHT
    fragment.frame(key + "-frame", clip,
                   x=initial_x, y=initial_y,
                   width=output_width * 1.1, height=output_height * 1.1,
                   scale=first["frameScale"], fit="stretch")
    fragment.add(key + "-soft-light", "setTimelineClipBlendMode",
                 clipId=clip, blendMode="soft-light")
    fragment.animate(key + "-motion", clip, [
        {"parameter": "frameScale", "keyframes":[
            {"localSeconds": sample["localSeconds"], "value": sample["frameScale"], "interpolation":"linear"}
            for sample in samples]},
        {"parameter": "frameOffsetX", "keyframes":[
            {"localSeconds": sample["localSeconds"], "value": sample["frameOffsetX"], "interpolation":"linear"}
            for sample in samples]},
        {"parameter": "frameOffsetY", "keyframes":[
            {"localSeconds": sample["localSeconds"], "value": sample["frameOffsetY"], "interpolation":"linear"}
            for sample in samples]},
    ])
    fragment.perspective(key + "-perspective", clip, [
        {"localSeconds": sample["localSeconds"], "points": sample["points"],
         "interpolation": "linear"}
        for sample in samples])
    # The reference noise is drawn inside the source card before adjacent UI.
    fragment.move_before(key + "-stack-position", track, anchor)


def _checkerboard_path(width: float, height: float, tile: float = 40.0) -> list[dict[str, Any]]:
    """Return the light conic-gradient sectors; the existing dark card is base."""
    origin_x, origin_y = width * 0.5 - tile * 0.5, height * 0.5 - tile * 0.5
    first_col = math.floor(-origin_x / tile)
    last_col = math.ceil((width - origin_x) / tile)
    first_row = math.floor(-origin_y / tile)
    last_row = math.ceil((height - origin_y) / tile)
    commands: list[dict[str, Any]] = []

    def polygon(points: tuple[tuple[float, float], ...]) -> None:
        commands.append({"verb": "M", "points": [points[0][0], points[0][1]]})
        for x, y in points[1:]:
            commands.append({"verb": "L", "points": [x, y]})
        commands.append({"verb": "Z", "points": []})

    for row in range(first_row, last_row):
        top = origin_y + row * tile
        bottom = top + tile
        for col in range(first_col, last_col):
            left = origin_x + col * tile
            right = left + tile
            cx, cy = left + tile * 0.5, top + tile * 0.5
            # SVG conic-gradient wedges: A spans 0–25%, B 25–50%, then repeats.
            # The light #263644 sectors are upper-left and lower-right.
            polygon(((cx, cy), (cx, top), (left, top), (left, cy)))
            polygon(((cx, cy), (right, cy), (right, bottom), (cx, bottom)))
    if len(commands) > 4096:
        raise ValueError(f"checkerboard vector exceeds native command limit: {len(commands)}")
    return commands


def build_fragment() -> dict[str, Any]:
    project, receipt = _verify_baseline()
    fragment = Fragment()
    track_by_id = {track["trackId"]: track for track in project["tracks"]}
    if track_by_id[ANCHORS["typewriter"]]["laneIndex"] != 100525:
        raise ValueError("FURTHER typewriter anchor lane drifted")
    if track_by_id[ANCHORS["poster_grain"]]["laneIndex"] != 100586:
        raise ValueError("poster grain insertion anchor lane drifted")
    if track_by_id[ANCHORS["thumbnail_grain"]]["laneIndex"] != 100628:
        raise ValueError("thumbnail grain insertion anchor lane drifted")
    if track_by_id[ANCHORS["closing_grain"]]["laneIndex"] != 100905:
        raise ValueError("closing grain insertion anchor lane drifted")
    if track_by_id[ANCHORS["checkerboard"]]["laneIndex"] != 100491:
        raise ValueError("checkerboard insertion anchor lane drifted")

    # Paper grain is a real editable SVG-turbulence-equivalent native source.
    _build_moving_grain(
        fragment, key="canvas-v18-poster-grain", name="Canvas / poster paper grain",
        anchor=ANCHORS["poster_grain"], start=133.0, duration=17.0,
        local_size=(1000, 1250), opacity=0.28, corner_fn=_poster_local_corners)
    _build_moving_grain(
        fragment, key="canvas-v18-thumbnail-grain", name="Canvas / thumbnail paper grain",
        anchor=ANCHORS["thumbnail_grain"], start=150.0, duration=10.0,
        local_size=(1000, 562), opacity=0.22, corner_fn=_thumbnail_local_corners)

    closing_track = fragment.track("buyer-v18-closing-grain-track", "Closing / paper grain")
    closing_clip = fragment.generator("buyer-v18-closing-grain-generator", closing_track,
        280.0, 18.0, {
            "baseFrequencyX": 0.78, "baseFrequencyY": 0.78,
            "numOctaves": 3, "seed": 19, "stitchTiles": True,
            "outputWidth": SEQUENCE_WIDTH, "outputHeight": SEQUENCE_HEIGHT,
            "opacity": 0.12,
        }, "fractal-noise")
    fragment.frame("buyer-v18-closing-grain-frame", closing_clip,
                   x=SEQUENCE_WIDTH / 2.0, y=SEQUENCE_HEIGHT / 2.0,
                   width=SEQUENCE_WIDTH, height=SEQUENCE_HEIGHT)
    fragment.add("buyer-v18-closing-grain-soft-light", "setTimelineClipBlendMode",
                 clipId=closing_clip, blendMode="soft-light")
    fragment.move_before("buyer-v18-closing-grain-stack", closing_track,
                         ANCHORS["closing_grain"])

    # The active source's Canvas card is a repeating conic checker. Existing
    # native dark rounded card stays beneath this editable light-sector path;
    # opaque photo layers cover it until the phase-two image fades away.
    checker_track = fragment.track("canvas-v18-checkerboard-track", "Canvas / checkerboard")
    checker_path = {
        "viewBoxWidth": 530, "viewBoxHeight": 795,
        "commands": _checkerboard_path(530.0, 795.0),
        "frames": [], "stroke": "#00000000", "fill": "#ff263644", "strokeWidth": 0.0,
    }
    checker_clip = fragment.generator("canvas-v18-checkerboard-generator", checker_track,
        92.0, 19.0, {"vectorPath": checker_path}, "vector-path")
    card_x, card_y = 650.0 + 1.1 * 210.0, 120.0 + 1.1 * -15.0
    card_width, card_height = 530.0 * 1.1, 795.0 * 1.1
    fragment.frame("canvas-v18-checkerboard-frame", checker_clip,
                   x=card_x + card_width * 0.5,
                   y=card_y + card_height * 0.5,
                   width=card_width, height=card_height,
                   corner_radius=(12.0 * 1.1) / min(card_width, card_height))
    fragment.move_before("canvas-v18-checkerboard-stack", checker_track,
                         ANCHORS["checkerboard"])

    # Restore the active HikerPoster source's per-character typewriter and
    # exact sampled paper/lime switch. The current project has a full static
    # word and a cross-fade, so the prefixes and two title opacity channels are
    # added as explicit edits against the receipt-bound stable IDs.
    white_track, white_title = _find_project_clip(project, "66ed4748-7f89-4ebb-8db7-350ee25a176d")
    lime_track, lime_title = _find_project_clip(project, "d0ed8128-1e28-462f-8015-ed36697d0d5e")
    if white_track["trackId"] != ANCHORS["typewriter"] or lime_track["trackId"] != "graphics-20fe7dca-a363-4caf-9184-54f252e007c2":
        raise ValueError("native FURTHER title IDs no longer match the verified project")
    type_track = fragment.track("canvas-v18-further-typewriter-track", "Canvas / typewriter prefixes")
    runs = []
    previous = None
    run_start = 0
    for frame in range(0, 68):
        local = frame / FPS
        count = max(0, math.floor((local - 1.5) * 11.0))
        count = min(len("FURTHER."), count)
        if count == previous:
            continue
        if previous is not None and 1 <= previous <= 7:
            runs.append((previous, run_start, frame))
        previous = count
        run_start = frame
    if previous is not None and 1 <= previous <= 7:
        runs.append((previous, run_start, 68))
    title_style = {
        "fontFamily": white_title["titleFontFamily"],
        "fontSize": white_title["titleFontSize"],
        "fontWeight": white_title["titleFontWeight"],
        "italic": white_title["titleItalic"],
        "letterSpacing": white_title["titleLetterSpacing"],
        "lineHeight": white_title["titleLineHeight"],
        "color": white_title["titleColor"],
        "backgroundColor": white_title["titleBackgroundColor"],
        "backgroundPadding": 0.0,
        "backgroundRadius": 0.0,
        "alignment": white_title["titleAlignment"],
        "positionX": white_title["titlePositionX"],
        "positionY": white_title["titlePositionY"],
    }
    for count, first_frame, end_frame in runs:
        fragment.add(
            f"canvas-v18-further-prefix-{count}-at-frame-{first_frame}",
            "createTimelineTitle", trackId=type_track, templateKey="headline",
            startSeconds=round9(111.0 + first_frame / FPS),
            durationSeconds=round9((end_frame - first_frame) / FPS),
            values={"text": "FURTHER."[:count], **title_style})
    static_white = []
    static_lime = []
    for frame in range(361):
        local = frame / FPS
        count = min(8, max(0, math.floor((local - 1.5) * 11.0)))
        color_mix = smooth(local, 4.0, 4.7) * (1.0 - smooth(local, 6.0, 6.5))
        is_lime = color_mix > 0.5
        static_white.append({"localSeconds": round9(local),
                             "value": 1.0 if count == 8 and not is_lime else 0.0,
                             "interpolation": "linear"})
        static_lime.append({"localSeconds": round9(local),
                            "value": 1.0 if count == 8 and is_lime else 0.0,
                            "interpolation": "linear"})
    fragment.animate("canvas-v18-further-white-opacity", ref("v18-further-white-target", "createdClipId"), [
        {"parameter": "opacity", "keyframes": static_white}])
    # The native stable-ID is passed directly as an explicit target; unlike a
    # symbolic result it remains bound to the exact verified checkpoint.
    fragment.steps[-1]["values"]["clipId"] = "66ed4748-7f89-4ebb-8db7-350ee25a176d"
    fragment.animate("canvas-v18-further-lime-opacity", ref("v18-further-lime-target", "createdClipId"), [
        {"parameter": "opacity", "keyframes": static_lime}])
    fragment.steps[-1]["values"]["clipId"] = "d0ed8128-1e28-462f-8015-ed36697d0d5e"
    fragment.move_before("canvas-v18-further-typewriter-stack", type_track,
                         ANCHORS["typewriter"])

    # Native source-photo viewport correction: retain the fixed 950x750
    # rounded sequence mask while restoring the 1800x2700 source image size,
    # then apply the exact 0.93 CSS inner scaling around (978,1532) in 30 fps
    # samples. The source crop becomes full because the outer clipMask now
    # performs the viewport crop after image geometry.
    source_center = (475.0 - 978.0 * 0.93 + 1800.0 * 0.93 * 0.5,
                     375.0 - 1532.0 * 0.93 + 2700.0 * 0.93 * 0.5)
    pivot = (978.0 * 0.93, 1532.0 * 0.93)
    zoom_samples = []
    for frame in range(301):
        local = frame / FPS
        k = 1.0 - 0.3 * smooth(local, 8.5, 9.7)
        center = (pivot[0] + k * (source_center[0] - pivot[0]),
                  pivot[1] + k * (source_center[1] - pivot[1]))
        zoom_samples.append({
            "localSeconds": round9(local), "scale": round9(k),
            "offsetX": round9(4.0 * ((650.0 + 1.1 * (20.0 + center[0])) / SEQUENCE_WIDTH - 0.5)),
            "offsetY": round9(4.0 * ((120.0 + 1.1 * (10.0 + center[1])) / SEQUENCE_HEIGHT - 0.5)),
        })
    content_width, content_height = 1800.0 * 0.93 * 1.1, 2700.0 * 0.93 * 1.1
    for layer, clip_id in INPAINT_CLIPS.items():
        _, original = _find_project_clip(project, clip_id)
        if original["startSeconds"] != 123 or original["durationSeconds"] != 10:
            raise ValueError(f"inpaint clip timing changed: {clip_id}")
        frame_step = f"canvas-v18-inpaint-{layer}-full-image-frame"
        fragment.add(frame_step, "setTimelineClipFrame", clipId=clip_id,
            offsetX=zoom_samples[0]["offsetX"], offsetY=zoom_samples[0]["offsetY"],
            scale=1.0, rotationDegrees=0.0, perspectiveX=0.0, perspectiveY=0.0,
            opacity=original["opacity"], objectFit="stretch",
            width=round9(content_width / SEQUENCE_WIDTH),
            height=round9(content_height / SEQUENCE_HEIGHT), cornerRadius=0.0,
            cropLeft=0.0, cropTop=0.0, cropRight=0.0, cropBottom=0.0)
        fragment.animate(f"canvas-v18-inpaint-{layer}-zoom", clip_id, [
            {"parameter": "frameScale", "keyframes":[
                {"localSeconds": sample["localSeconds"], "value": sample["scale"], "interpolation":"linear"}
                for sample in zoom_samples]},
            {"parameter": "frameOffsetX", "keyframes":[
                {"localSeconds": sample["localSeconds"], "value": sample["offsetX"], "interpolation":"linear"}
                for sample in zoom_samples]},
            {"parameter": "frameOffsetY", "keyframes":[
                {"localSeconds": sample["localSeconds"], "value": sample["offsetY"], "interpolation":"linear"}
                for sample in zoom_samples]},
        ])

    # The inpaint pointer is a native editable vector; its per-frame scale and
    # center preserve the SVG's top-left transform origin during the press.
    pointer_path = {
        "viewBoxWidth": 40, "viewBoxHeight": 48,
        "commands": [
            {"verb":"M", "points":[1,1]}, {"verb":"L", "points":[1,31]},
            {"verb":"L", "points":[9,24]}, {"verb":"L", "points":[18,41]},
            {"verb":"L", "points":[25,37]}, {"verb":"L", "points":[16,21]},
            {"verb":"L", "points":[31,21]}, {"verb":"Z", "points":[]},
        ],
        "frames": [], "stroke":"#ff07111d", "fill":"#ffffffff", "strokeWidth":2.0,
    }
    pointer_track = fragment.track("canvas-v18-inpaint-pointer-track", "Canvas / inpaint pointer")
    pointer = fragment.generator("canvas-v18-inpaint-pointer-generator", pointer_track,
        123.0, 10.0, {"vectorPath": pointer_path}, "vector-path")
    pointer_base_center = (650.0 + 1.1 * (315.0 + 20.0),
                           120.0 + 1.1 * (690.0 + 24.0))
    fragment.frame("canvas-v18-inpaint-pointer-frame", pointer,
        x=pointer_base_center[0], y=pointer_base_center[1],
        width=40.0 * 1.1, height=48.0 * 1.1, opacity=0.0)
    opacity_keys = []
    pointer_scale = []
    pointer_x = []
    pointer_y = []
    for frame in range(301):
        local = frame / FPS
        visible = 1.0 if local > 2.8 and local < 4.3 else 0.0
        press = smooth(local, 3.3, 3.4) * (1.0 - smooth(local, 3.5, 3.7))
        scale = 1.0 - 0.1 * press
        center_x = 650.0 + 1.1 * (315.0 + 20.0 * scale)
        center_y = 120.0 + 1.1 * (690.0 + 24.0 * scale)
        opacity_keys.append({"localSeconds": round9(local), "value": visible, "interpolation":"linear"})
        pointer_scale.append({"localSeconds": round9(local), "value": round9(scale), "interpolation":"linear"})
        pointer_x.append({"localSeconds": round9(local),
                          "value": round9(4.0 * (center_x / SEQUENCE_WIDTH - 0.5)),
                          "interpolation":"linear"})
        pointer_y.append({"localSeconds": round9(local),
                          "value": round9(4.0 * (center_y / SEQUENCE_HEIGHT - 0.5)),
                          "interpolation":"linear"})
    fragment.animate("canvas-v18-inpaint-pointer-motion", pointer, [
        {"parameter":"opacity", "keyframes":opacity_keys},
        {"parameter":"frameScale", "keyframes":pointer_scale},
        {"parameter":"frameOffsetX", "keyframes":pointer_x},
        {"parameter":"frameOffsetY", "keyframes":pointer_y},
    ])
    fragment.move_before("canvas-v18-inpaint-pointer-stack", pointer_track,
                         ANCHORS["inpaint_pointer"])

    # Validate sampled source semantics and action transport sizes offline.
    _validate_fragment(fragment, runs, static_white, static_lime, zoom_samples,
                       pointer_scale, pointer_x, pointer_y, pointer_path,
                       checker_path, project, receipt)
    return {
        "schema": "looksy.native-canvas-correction-fragment/v1",
        "name": "v18 Canvas fidelity and native PaperGrain sources",
        "execution": "unexecuted; exact v18 checkpoint required",
        "preconditions": {
            "orientation": "landscape", "width": SEQUENCE_WIDTH,
            "height": SEQUENCE_HEIGHT, "fps": FPS,
            "resourceId": EXPECTED_RESOURCE_ID,
            "timelineId": EXPECTED_TIMELINE_ID,
            "documentHash": EXPECTED_DOCUMENT_HASH,
            "projectPath": str(PROJECT),
            "projectFileSha256": EXPECTED_PROJECT_SHA256,
            "receiptPath": str(RECEIPT),
            "receiptSha256": EXPECTED_RECEIPT_SHA256,
            "receiptPlanSha256": EXPECTED_PLAN_SHA256,
        },
        "sourceFiles": [_source_record(path) for path in EXPECTED_SOURCE_HASHES],
        "stableTargets": {
            "posterGrainBeforeTrack": ANCHORS["poster_grain"],
            "thumbnailGrainBeforeTrack": ANCHORS["thumbnail_grain"],
            "closingGrainBeforeTrack": ANCHORS["closing_grain"],
            "checkerboardBeforeTrack": ANCHORS["checkerboard"],
            "typewriterBeforeTrack": ANCHORS["typewriter"],
            "inpaintPointerBeforeTrack": ANCHORS["inpaint_pointer"],
            "inpaintLayerClipIds": INPAINT_CLIPS,
            "fullTitleClipId": "66ed4748-7f89-4ebb-8db7-350ee25a176d",
            "limeTitleClipId": "d0ed8128-1e28-462f-8015-ed36697d0d5e",
        },
        "implementationNotes": [
            "Three PaperGrain instances use editable native fractal-noise parameters, native still caching, and soft-light compositing; none is a flattened video or imported Remotion frame.",
            "Poster and thumbnail noise geometry is sampled at every 30 fps frame from the source Stage and CSS perspective/rotation transforms. The output quad uses a fixed padded frame so transformed corners remain inside the native canvas.",
            "Canvas checkerboard is an editable vector-path reconstruction of the repeating conic gradient. The existing dark rounded card remains the base; source photo layers cover the checker until the phase-two photo fades.",
            "The FURTHER word reveal now follows the exact sampled source character count and paper/lime threshold, with the original stable title clips reused for full-word states.",
            "Inpaint layers retain the fixed sequence-space rounded viewport mask while the 1800x2700 source photo moves and scales around the source CSS transform origin. Existing alpha/luminance matte and opacity animation channels are preserved.",
            "The inpaint pointer uses the exact ProofTools SVG path and samples its strict visibility and press-scale pulse at 30 fps.",
        ],
        "unresolved": [
            "TextLayerDemo's parent-level scale/rotate transform affects the whole HikerPoster subtree. The current project has independent full-canvas tracks, so this fragment does not fake a separate title rotation; exact group transform requires a native group/compound correction applied to every child.",
            "CampaignPoster's SVG feTurbulence plus feDisplacementMap brush filter is not reproduced here. The existing native brush paths must not be described as browser/Remotion displacement-map parity; the separate displacement prototype is not qualified.",
            "Canvas paper-grain still caches the SVG turbulence to an editable native still source; static texture is source-faithful in parameters but does not claim Remotion renderer byte identity.",
            "Portrait output is intentionally omitted because no matching completed portrait v18 project and receipt are available to bind stable IDs safely.",
        ],
        "reviewFramesSeconds": [
            112.6, 113.23333333333333,
            131.5, 132.7,
            133.0, 134.6,
            150.0, 151.4, 152.8,
            280.0, 289.0, 297.96666666666664,
        ],
        "reviewTargets": {
            "112.6": "FURTHER typewriter first visible prefix",
            "113.23333333333333": "FURTHER complete word and source paper-to-lime state",
            "131.5": "Inpaint photo zoom begins at the source transform around its CSS origin",
            "132.7": "Inpaint photo zoom reaches 0.70x",
            "133.0": "Campaign poster starts at 12-degree Y yaw; poster grain is card-local",
            "134.6": "Campaign poster Y yaw settles to zero",
            "150.0": "Thumbnail starts at minus 10-degree Y yaw; thumbnail grain is card-local",
            "151.4": "Thumbnail Y yaw settles while its Stage focus motion begins",
            "152.8": "Thumbnail Stage focus reaches its full-scale endpoint",
            "280.0": "BuyerClosing begins with the paper-grain layer below the footer",
            "289.0": "BuyerClosing mid-card paper-grain soft-light check",
            "297.96666666666664": "Final frame with grain under the closing footer",
        },
        "steps": fragment.steps,
        "offlineValidation": {
            "stepCount": len(fragment.steps),
            "checkerboardVectorCommandCount": len(checker_path["commands"]),
            "posterGrainPerspectivePoses": 17 * FPS + 1,
            "thumbnailGrainPerspectivePoses": 10 * FPS + 1,
            "inpaintSamplesPerLayer": len(zoom_samples),
            "nativeMcpMaxRequestBytes": _max_request_bytes(fragment.steps),
            "verifiedProjectSha256": sha256(PROJECT),
            "verifiedReceiptSha256": sha256(RECEIPT),
        },
    }


def _max_request_bytes(steps: list[dict[str, Any]]) -> int:
    from native_mcp import validate_tool_request_size
    maximum = 0
    for step in steps:
        request_bytes = validate_tool_request_size(
            "app.invoke_command", {"commandId": step["commandId"], "values": step["values"]})
        maximum = max(maximum, request_bytes)
        if request_bytes > 1024 * 1024:
            raise ValueError(f"oversize native command step: {step['id']} ({request_bytes} bytes)")
    return maximum


def _validate_fragment(fragment: Fragment, runs: list[tuple[int, int, int]],
                       white: list[dict[str, Any]], lime: list[dict[str, Any]],
                       zoom: list[dict[str, Any]], pointer_scale: list[dict[str, Any]],
                       pointer_x: list[dict[str, Any]], pointer_y: list[dict[str, Any]],
                       pointer_path: dict[str, Any], checker_path: dict[str, Any],
                       project: dict[str, Any], receipt: dict[str, Any]) -> None:
    if sha256(PROJECT) != EXPECTED_PROJECT_SHA256 or sha256(RECEIPT) != EXPECTED_RECEIPT_SHA256:
        raise ValueError("baseline changed during fragment generation")
    if len(fragment.steps) >= 16 * 128:
        raise ValueError("unexpectedly large correction fragment")
    step_ids = [step["id"] for step in fragment.steps]
    if len(step_ids) != len(set(step_ids)):
        raise ValueError("fragment command IDs are not unique")
    if len(checker_path["commands"]) > 4096:
        raise ValueError("checkerboard path exceeds vector generator bounds")
    if len(pointer_path["commands"]) != 8:
        raise ValueError("pointer path no longer matches the source SVG")
    if len(white) != 361 or len(lime) != 361 or len(zoom) != 301:
        raise ValueError("frame-sampled native animation does not span source duration")
    if white[67]["value"] != 1.0 or lime[67]["value"] != 0.0:
        raise ValueError("the complete paper title should begin at source frame 67")
    if not any(count == 7 for count, _start, _end in runs):
        raise ValueError("the native typewriter is missing its final partial-word state")
    if zoom[0]["scale"] != 1.0 or zoom[291]["scale"] != 0.7 or zoom[-1]["scale"] != 0.7:
        raise ValueError("inpaint zoom must land on the exact 0.7 endpoint at 9.7 seconds")
    if pointer_scale[99]["value"] != 1.0 or pointer_scale[102]["value"] >= 0.91:
        raise ValueError("pointer press pulse does not match source scale animation")
    if pointer_scale[111]["value"] != 1.0:
        raise ValueError("pointer pulse should be released at the source 3.7-second endpoint")
    if pointer_x[0]["value"] != pointer_x[111]["value"] or pointer_y[0]["value"] != pointer_y[111]["value"]:
        raise ValueError("pointer transform-origin compensation should restore its exact anchor")
    # Verify checker path stays in the admitted vector viewBox coordinate range.
    coordinates = [value for command in checker_path["commands"] for value in command["points"]]
    if any(not math.isfinite(value) or abs(value) > 131072.0 for value in coordinates):
        raise ValueError("checkerboard path contains unsupported coordinates")
    # Any new command must target a receipt-bound clip/track or a prior step result.
    known_targets = set(INPAINT_CLIPS.values()) | {
        "66ed4748-7f89-4ebb-8db7-350ee25a176d",
        "d0ed8128-1e28-462f-8015-ed36697d0d5e",
        *ANCHORS.values(),
    }
    for step in fragment.steps:
        values = step["values"]
        for key in ("clipId", "beforeTrackId"):
            value = values.get(key)
            if isinstance(value, str) and value not in known_targets:
                raise ValueError(f"unbound external target {value} in {step['id']}")
        for value in values.values():
            if isinstance(value, dict) and set(value) == {"result", "field"}:
                if value["result"] not in fragment.ids:
                    raise ValueError(f"dangling symbolic reference in {step['id']}")


def write_fragment() -> Path:
    output = ROOT / "canvas-v18-parity-corrections-landscape.commands-fragment.json"
    if output.exists():
        raise FileExistsError(f"Refusing to overwrite immutable correction fragment: {output}")
    fragment = build_fragment()
    output.write_text(json.dumps(fragment, indent=2, ensure_ascii=False) + "\n")
    return output


if __name__ == "__main__":
    print(write_fragment())
