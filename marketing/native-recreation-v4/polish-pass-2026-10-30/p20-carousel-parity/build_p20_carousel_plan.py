#!/usr/bin/env python3
"""Generate an offline, stable-ID native correction plan for P20 Carousel.

The plan is derived from the exact P17 native snapshot and the Remotion
BuyerCarousel/Panorama source. It performs no app, worker, or MCP calls.
"""
from __future__ import annotations

import hashlib
import json
import math
import sys
from pathlib import Path
from typing import Any

HERE = Path(__file__).resolve().parent
PASS = HERE.parent
REPO = PASS.parents[2]
SNAPSHOT = PASS / "native-projects/looksy-polish-pass17-photo-parity.json"
OUTPUT = HERE / "p20-carousel-native-plan.json"

WIDTH, HEIGHT, FPS = 1920, 1080, 30
START, DURATION = 160.0, 24.0
CONTENT_X, CONTENT_Y = 1394.0, 447.0
CONTENT_W, CONTENT_H = 392.0, 490.0
PHONE_S = CONTENT_W / 1080.0
VIEWPORT = {
    "shape": "rectangle",
    "left": CONTENT_X / WIDTH,
    "top": CONTENT_Y / HEIGHT,
    "right": (CONTENT_X + CONTENT_W) / WIDTH,
    "bottom": (CONTENT_Y + CONTENT_H) / HEIGHT,
    "cornerRadius": 0.0,
}


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def smooth(t: float, a: float, b: float) -> float:
    u = max(0.0, min(1.0, (t - a) / (b - a)))
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def swipe(t: float) -> float:
    return smooth(t, 18.0, 19.3) + smooth(t, 20.7, 22.0)


def shown(t: float) -> float:
    return smooth(t, 15.0, 17.0)


def sampled_channel(value_at, *, duration: float = DURATION) -> list[dict[str, Any]]:
    frame_position = duration * FPS
    nearest_frame = round(frame_position)
    if math.isclose(frame_position, nearest_frame, rel_tol=0.0, abs_tol=1e-9):
        count = int(nearest_frame)
    else:
        count = math.floor(frame_position)
    raw = [
        {"localSeconds": frame / FPS, "value": float(value_at(frame / FPS)),
         "interpolation": "linear"}
        for frame in range(count + 1)
    ]
    compact = []
    for index, item in enumerate(raw):
        if (index == 0 or index == len(raw) - 1 or
                raw[index - 1]["value"] != item["value"] or
                raw[index + 1]["value"] != item["value"]):
            compact.append(item)
    return compact


def clip_groups(clip: dict[str, Any]) -> dict[str, list[dict[str, Any]]]:
    groups: dict[str, list[dict[str, Any]]] = {}
    for key in clip.get("keyframes", []):
        groups.setdefault(key["parameter"], []).append(key)
    return groups


def to_channel(parameter: str, rows: list[dict[str, Any]], *, multiplier: float = 1.0):
    return {
        "parameter": parameter,
        "keyframes": [
            {"localSeconds": float(row["seconds"]),
             "value": float(row["value"]) * multiplier,
             "interpolation": row.get("interpolation", "linear")}
            for row in rows
        ],
    }


def frame_values(clip: dict[str, Any], *, offset_x: float | None = None,
                 offset_y: float | None = None, shadow: dict[str, Any] | None = None):
    values = {
        "clipId": clip["clipId"],
        "offsetX": float(clip.get("frameOffsetX", 0.0) * 4.0 if offset_x is None else offset_x),
        "offsetY": float(clip.get("frameOffsetY", 0.0) * 4.0 if offset_y is None else offset_y),
        "scale": float(clip.get("frameScale", 1.0)),
        "rotationDegrees": float(clip.get("frameRotationDegrees", 0.0)),
        "perspectiveX": float(clip.get("framePerspectiveX", 0.0)),
        "perspectiveY": float(clip.get("framePerspectiveY", 0.0)),
        "opacity": float(clip.get("opacity", 1.0)),
        "objectFit": clip.get("frameMode", "stretch"),
        "width": float(clip.get("frameWidth", 1.0)),
        "height": float(clip.get("frameHeight", 1.0)),
        "cornerRadius": float(clip.get("frameCornerRadius", 0.0)),
        "cropLeft": float(clip.get("sourceCropLeft", 0.0)),
        "cropTop": float(clip.get("sourceCropTop", 0.0)),
        "cropRight": float(clip.get("sourceCropRight", 1.0)),
        "cropBottom": float(clip.get("sourceCropBottom", 1.0)),
    }
    if shadow is not None:
        values["frameShadow"] = shadow
    return values


steps: list[dict[str, Any]] = []


def add(step_id: str, action: str, **values):
    steps.append({"id": step_id, "commandId": "timeline.action." + action,
                  "values": values})


def set_animation(step_id: str, clip_id: Any, channels: list[dict[str, Any]]):
    if not channels:
        return
    add(step_id, "setTimelineClipAnimation", clipId=clip_id, channels=channels)


def set_title(step_id: str, clip_id: str, **style):
    add(step_id, "setTimelineClipTitle", clipId=clip_id, values=style)


def set_mask(step_id: str, clip_id: Any, bounds: dict[str, Any] = VIEWPORT):
    add(step_id, "setTimelineClipMask", clipId=clip_id, **bounds)


def add_phone_overlay_animation(step_id: str, clip_id: Any, *, slide=True,
                                phone_y_entry=False, duration=DURATION,
                                scene_time_offset=0.0):
    at = lambda local: local + scene_time_offset
    channels = []
    if slide:
        channels.append({"parameter": "frameOffsetX", "keyframes": sampled_channel(
            lambda t: -4.0 * CONTENT_W / WIDTH * swipe(at(t)), duration=duration)})
    channels.append({"parameter": "opacity", "keyframes": sampled_channel(
        lambda t: shown(at(t)), duration=duration)})
    if phone_y_entry:
        channels.append({"parameter": "frameOffsetY", "keyframes": sampled_channel(
            lambda t: 4.0 * 65.0 / HEIGHT * (1.0 - shown(at(t))), duration=duration)})
    set_animation(step_id, clip_id, channels)


def shape_center_update(clip: dict[str, Any]):
    width = float(clip.get("titleShapeWidth", 0.0))
    height = float(clip.get("titleShapeHeight", 0.0))
    return {
        "positionX": float(clip["titlePositionX"]) + width / 2.0,
        "positionY": float(clip["titlePositionY"]) + height / 2.0,
    }


def add_graphic(step_id: str, track_id: str, shape_type: str, x: float, y: float,
                width: float, height: float, color: str, *, stroke="#00000000",
                stroke_width=0.0, radius=0.0, rotation=0.0, slide=True,
                phone_y_entry=False, mask=True):
    create_id = step_id + "-create"
    add(create_id, "createTimelineGraphic", trackId=track_id,
        shapeType=shape_type, startSeconds=START, durationSeconds=DURATION,
        values={"text": "", "fontFamily": "Space Mono", "fontSize": 12,
                "fontWeight": 400, "italic": False, "letterSpacing": 0,
                "lineHeight": 1, "color": "#ffffffff", "alignment": "left",
                "positionX": x / WIDTH, "positionY": y / HEIGHT,
                "backgroundColor": color, "shapeWidth": width / WIDTH,
                "shapeHeight": height / HEIGHT, "shapeCornerRadius": radius,
                "shapeStrokeColor": stroke, "shapeStrokeWidth": stroke_width})
    clip_ref = {"result": create_id, "field": "createdClipId"}
    if rotation:
        set_animation(step_id + "-rotation", clip_ref, [{
            "parameter": "frameRotationDegrees",
            "keyframes": [
                {"localSeconds": 0.0, "value": rotation, "interpolation": "linear"},
                {"localSeconds": DURATION, "value": rotation, "interpolation": "linear"},
            ],
        }])
    add_phone_overlay_animation(step_id + "-animation", clip_ref, slide=slide,
                                phone_y_entry=phone_y_entry, duration=DURATION)
    if mask:
        set_mask(step_id + "-viewport", clip_ref)
    return create_id


def add_title(step_id: str, track_id: str, text: str, x: float, y: float,
              font_family: str, font_size: float, color: str = "#fff2eee1",
              *, letter_spacing=0.0, line_height=1.01, italic=False,
              alignment="left", slide=True, phone_y_entry=False,
              start=START, duration=DURATION, mask=True):
    add(step_id + "-create", "createTimelineTitle", trackId=track_id,
        templateKey="basic", startSeconds=start, durationSeconds=duration,
        values={"text": text, "fontFamily": font_family, "fontSize": font_size,
                "fontWeight": 400, "italic": italic,
                "letterSpacing": letter_spacing, "lineHeight": line_height,
                "color": color, "alignment": alignment,
                "positionX": x / WIDTH, "positionY": y / HEIGHT,
                "backgroundColor": "#00000000"})
    clip_ref = {"result": step_id + "-create", "field": "createdClipId"}
    if mask:
        set_mask(step_id + "-viewport", clip_ref)
    add_phone_overlay_animation(step_id + "-animation", clip_ref, slide=slide,
                                phone_y_entry=phone_y_entry, duration=duration,
                                scene_time_offset=start - START)
    return step_id + "-create"


def add_vector(step_id: str, track_id: str, *, x_center: float, y_center: float,
               width: float, height: float, commands: list[dict[str, Any]],
               stroke: str, stroke_width: float):
    create_id = step_id + "-create"
    add(create_id, "createTimelineGenerator", trackId=track_id,
        generatorKind="vector-path", startSeconds=START, durationSeconds=DURATION,
        parameters={"vectorPath": {
            "viewBoxWidth": 320, "viewBoxHeight": 300,
            "commands": commands, "frames": [], "stroke": stroke,
            "fill": "#00000000", "strokeWidth": stroke_width,
        }})
    clip_ref = {"result": create_id, "field": "createdClipId"}
    base_offset_x = 4.0 * (x_center / WIDTH - 0.5)
    base_offset_y = 4.0 * (y_center / HEIGHT - 0.5)
    add(step_id + "-frame", "setTimelineClipFrame", clipId=clip_ref,
        offsetX=base_offset_x, offsetY=base_offset_y, scale=1,
        rotationDegrees=0, perspectiveX=0, perspectiveY=0, opacity=1,
        objectFit="stretch", width=width / WIDTH, height=height / HEIGHT,
        cornerRadius=0, cropLeft=0, cropTop=0, cropRight=0, cropBottom=0)
    set_animation(step_id + "-animation", clip_ref, [
        {"parameter": "frameOffsetX", "keyframes": sampled_channel(
            lambda t: base_offset_x - 4.0 * CONTENT_W / WIDTH * swipe(t))},
        {"parameter": "opacity", "keyframes": sampled_channel(shown)},
        {"parameter": "frameOffsetY", "keyframes": sampled_channel(
            lambda t: base_offset_y + 4.0 * 65.0 / HEIGHT * (1.0 - shown(t)))},
    ])
    set_mask(step_id + "-viewport", clip_ref)
    return create_id


doc = json.loads(SNAPSHOT.read_text())
all_tracks = doc["tracks"]
tracks = {track["trackId"]: track for track in all_tracks}
clips = {clip["clipId"]: (track, clip)
         for track in all_tracks for clip in track["clips"]}
p20 = {clip_id: pair for clip_id, pair in clips.items()
       if pair[1]["startSeconds"] < START + DURATION and
       pair[1]["startSeconds"] + pair[1]["durationSeconds"] > START}

# First correct all old full-canvas frame-offset channels. The native API now
# documents these as quarter-canvas units, so old full-canvas values scale x4.
base_frame_targets = {
    "7df86437-abe6-4258-8c5d-ded123c08c1f",
    "16bc3079-ef68-4226-be4f-1691007970dd",
    "c9a4435c-fee6-4d5c-bb2f-6651edb34ffb",
    "d1cd0bf2-a651-42b2-95b2-4946aac414ba",
    "5ec9c9a0-baad-4181-9e72-24bace99a6d3",
    "15bfbdea-5919-47eb-9b7d-3fb5e59811d1",
    "47643166-b19e-4274-9483-415f4f32dd4e",
    "27e510ac-6602-47a7-a09f-014aebefcd95",
    "b8905213-e33b-411a-aec1-a54dd1cc66a0",
    "6550e260-d20c-4b66-a399-514eab2bdf53",
    "a74d6850-b4ed-4c8f-8a2e-3187f5657889",
}
phone_photos = [
    "d1cd0bf2-a651-42b2-95b2-4946aac414ba",
    "5ec9c9a0-baad-4181-9e72-24bace99a6d3",
    "15bfbdea-5919-47eb-9b7d-3fb5e59811d1",
]
phone_page = {clip_id: index for index, clip_id in enumerate(phone_photos)}
phone_gradient_id = "47643166-b19e-4274-9483-415f4f32dd4e"
for clip_id in sorted(base_frame_targets):
    _, clip = p20[clip_id]
    if clip_id in phone_page:
        page = phone_page[clip_id]
        offset_x = 4.0 * (CONTENT_X + page * CONTENT_W + CONTENT_W / 2.0 - WIDTH / 2.0) / WIDTH
        offset_y = 4.0 * (CONTENT_Y + CONTENT_H / 2.0 - HEIGHT / 2.0) / HEIGHT
    else:
        offset_x = float(clip["frameOffsetX"]) * 4.0
        offset_y = float(clip["frameOffsetY"]) * 4.0
    add(f"frame-quarter-units-{clip_id}", "setTimelineClipFrame",
        **frame_values(clip, offset_x=offset_x, offset_y=offset_y))
    groups = clip_groups(clip)
    if clip_id in phone_page:
        channels = []
        for parameter, rows in groups.items():
            if parameter in {"frameOffsetX", "opacity"}:
                continue
            multiplier = 4.0 if parameter in {"frameOffsetX", "frameOffsetY"} else 1.0
            channels.append(to_channel(parameter, rows, multiplier=multiplier))
        page = phone_page[clip_id]
        page_offset = 4.0 * (CONTENT_X + page * CONTENT_W + CONTENT_W / 2.0 - WIDTH / 2.0) / WIDTH
        channels.extend([
            {"parameter": "frameOffsetX", "keyframes": sampled_channel(
                lambda t, base=page_offset: base - 4.0 * CONTENT_W / WIDTH * swipe(t))},
            {"parameter": "opacity", "keyframes": sampled_channel(shown)},
            {"parameter": "frameOffsetY", "keyframes": sampled_channel(
                lambda t, base=offset_y: base + 4.0 * 65.0 / HEIGHT * (1.0 - shown(t)))},
        ])
    else:
        channels = [to_channel(parameter, rows,
                               multiplier=4.0 if parameter in {"frameOffsetX", "frameOffsetY"} else 1.0)
                    for parameter, rows in groups.items()]
        if clip_id == phone_gradient_id:
            channels.append({"parameter": "frameOffsetY", "keyframes": sampled_channel(
                lambda t, base=offset_y: base + 4.0 * 65.0 / HEIGHT * (1.0 - shown(t)))})
    set_animation(f"restore-frame-animation-{clip_id}", clip_id, channels)

# Older P20 artwork also stores its quarter-canvas motion in frameOffsetX/Y
# channels, even when no base frame pose needs changing. Convert those channel
# values independently; partial animation edits preserve scale, opacity,
# rotation, masks, and other authored channels on the same clips.
offset_channel_targets = []
for clip_id, (_, clip) in sorted(p20.items()):
    if clip_id in base_frame_targets or clip_id == "f4b256ed-4fde-445f-b8b1-61199db77aeb":
        # Base frame targets restore all channels above. The shell restores its
        # full entry animation after its shadow frame edit below.
        continue
    groups = clip_groups(clip)
    channels = [to_channel(parameter, groups[parameter], multiplier=4.0)
                for parameter in ("frameOffsetX", "frameOffsetY")
                if parameter in groups]
    if channels:
        offset_channel_targets.append(clip_id)
        set_animation(f"quarter-offset-animation-{clip_id}", clip_id, channels)

# The phone shell needs a transparent center so the masked native photos can
# show through. A separate native frame shadow preserves the source's card edge.
shell_id = "f4b256ed-4fde-445f-b8b1-61199db77aeb"
_, shell = p20[shell_id]
set_title("phone-shell-center-anchor-and-transparent-fill", shell_id,
          positionX=0.828125, positionY=707.5 / HEIGHT,
          backgroundColor="#00000000")
add("phone-shell-native-shadow-frame", "setTimelineClipFrame",
    **frame_values(shell, offset_x=0.0, offset_y=0.0,
                   shadow={"enabled": True, "color": "#88000000",
                           "offsetX": 0, "offsetY": 35,
                           "blurRadius": 90, "spread": 0}))
shell_channels = [to_channel(parameter, rows,
                             multiplier=4.0 if parameter == "frameOffsetY" else 1.0)
                  for parameter, rows in clip_groups(shell).items()]
set_animation("restore-phone-shell-entry-animation", shell_id, shell_channels)

# Center every P20 shape authored from a source top-left rectangle. The shape
# renderer anchors positionX/Y at its center.
shape_ids = []
for clip_id, (_, clip) in p20.items():
    if clip.get("titleShapeType"):
        shape_ids.append(clip_id)
        set_title("shape-center-" + clip_id, clip_id, **shape_center_update(clip))

# Correct source-measured desktop text block baselines and the separated desktop
# First Light title. These are partial title updates; style/animation stays intact.
for clip_id in (
        "b0d18d77-b42d-4cb8-9403-5a9b4a012017",
        "02b039a9-3492-4ac5-bc32-75990be6437a"):
    _, clip = p20[clip_id]
    set_title("desktop-heading-center-" + clip_id, clip_id,
              positionY=float(clip["titlePositionY"]) + 108.0 / HEIGHT)
for clip_id in (
        "f90a6db1-4a7e-48c4-84e4-7c45a87ed0ab",
        "fe02a64a-fe3a-4917-ac1e-2fc3e4aa2d2f"):
    _, clip = p20[clip_id]
    set_title("desktop-deck-center-" + clip_id, clip_id,
              positionY=float(clip["titlePositionY"]) + 26.0 / HEIGHT)
for clip_id, delta in {
        "942cd1bd-483a-44da-ab3a-e5b823a0bf9b": 40.0,
        "bd2145cf-29b3-4a17-847b-84949bac6835": 75.0,
        "cc3d5e56-9bd5-4428-9846-af66641ca72a": 75.0,
        "947ef506-e7a4-4236-9e95-82aa292efdb7": 75.0,
}.items():
    _, clip = p20[clip_id]
    set_title("desktop-panel-title-center-" + clip_id, clip_id,
              positionY=float(clip["titlePositionY"]) + delta / HEIGHT)
set_title("desktop-first-light-label-align", "c1a44713-1de8-4237-9afb-d040c43c93a8",
          alignment="center", positionX=1083.0 / WIDTH,
          positionY=489.2 / HEIGHT)

# Align the existing phone text overlays to their full panoramic design-space
# coordinates, animate them with the exact source scroll, and clip to viewport.
phone_text = {
    # P17 native ID: page, source design x (full 3240-wide artwork), source y,
    # line count, scaled font size. Text baselines use measured native shifts.
    "f538b0db-ddc9-4ad1-a02a-a340762c6ac2": (0, 76, 69, 1, 9.074),
    "d45163cc-d3c2-41ca-9580-9040066f2611": (1, 1156, 69, 1, 9.074),
    "5fd4321f-fd7e-4634-a52e-12e10e903de3": (2, 2236, 69, 1, 9.074),
    "677a8072-393e-443c-950e-8b1d59c6a479": (0, 1004, 69, 1, 9.074),
    "ccf56e64-eba6-4133-b6b2-d88989b6702f": (1, 2084, 69, 1, 9.074),
    "ffcf72ef-e446-47dc-9897-6a5a95657239": (2, 3164, 69, 1, 9.074),
    "b3217ae1-05cf-4b24-bcc1-95f70fbc1ef0": (0, 72, 529, 1, 68.963),
    "42a6c461-bde3-4a5e-b403-e72f9025ee69": (0, 69, 719, 1, 132.481),
    "a6171c20-1bfb-4542-a55c-9f6df7fdaa6b": (1, 1080, 719, 1, 132.481),
    "38009159-c3de-4378-aebd-1da9e6be4857": (2, 2247, 727, 3, 41.741),
}
for clip_id, (page, design_x, design_y, lines, font_size) in phone_text.items():
    _, clip = p20[clip_id]
    x = CONTENT_X + design_x * PHONE_S
    top = CONTENT_Y + design_y * PHONE_S
    # The native title renderer centers the complete text block vertically.
    text_height = font_size * float(clip.get("titleLineHeight", 1.01)) * lines
    if clip_id == "b3217ae1-05cf-4b24-bcc1-95f70fbc1ef0":
        # Source instrument-serif title has a measured 41 px glyph box at phone scale.
        text_height = 41.0
    y = top + text_height / 2.0
    set_title("phone-title-anchor-" + clip_id, clip_id,
              positionX=x / WIDTH, positionY=y / HEIGHT)
    set_mask("phone-title-viewport-" + clip_id, clip_id)
    add_phone_overlay_animation("phone-title-swipe-" + clip_id, clip_id,
                                slide=True, phone_y_entry=True)

# Correct the outer phone UI anchors and frame-entry animation without changing
# its discrete page-state timing or copy.
set_title("phone-brand-dot-center", "d3dac61f-aefd-4d79-a43c-26332009d382",
          positionX=1415.0 / WIDTH, positionY=420.0 / HEIGHT)
set_title("phone-brand-title-center", "ca53141d-0379-4469-a143-e792d04a75ec",
          positionY=421.0 / HEIGHT)
for clip_id in ("afa5f4bf-4840-4304-8345-37fd15ca99c1",
                "9e77a6c4-8a7c-4985-82a8-ba07304bfcd2",
                "f0554b3d-0751-46f8-81cd-a43d2e068054"):
    set_title("phone-page-number-center-" + clip_id, clip_id,
              positionY=421.0 / HEIGHT)
for clip_id in ("3df7e24c-e3f6-49ba-9aa2-c8f1a74939e0",
                "4d509538-bf5d-4ce2-a0f3-3b9c60aa33f1",
                "0b441b38-9323-4f95-830d-3752974eb893",
                "f5236127-6e2d-42e8-8d2c-abc03fefe91a",
                "c739b1a0-f4fc-4ff6-9c13-d6bdd25364d3",
                "c26a78bf-a830-4dac-9b73-e4c5def2497d"):
    _, clip = p20[clip_id]
    size_w = float(clip["titleShapeWidth"])
    size_h = float(clip["titleShapeHeight"])
    set_title("phone-indicator-dot-center-" + clip_id, clip_id,
              positionX=float(clip["titlePositionX"]) + size_w / 2.0,
              positionY=float(clip["titlePositionY"]) + size_h / 2.0)
set_title("phone-caption-center", "6395e36f-512b-4b7d-8f54-a6039f5439f5",
          positionY=984.5 / HEIGHT)

# Add a source-matched chapter label to the already-existing, visible, unlocked
# G68 graphics lane. It is text-only; no shapeType is passed to a title action.
chapter_track = "graphics-2ac5a311-3601-481f-bbf1-d1b86a294a52"
add("chapter-label-04-carousel-create", "createTimelineTitle",
    trackId=chapter_track, templateKey="basic", startSeconds=160.0,
    durationSeconds=24.0,
    values={"text": "04 / CAROUSEL", "fontFamily": "Space Mono",
            "fontSize": 21, "fontWeight": 400, "italic": False,
            "letterSpacing": 1.8, "lineHeight": 1.0,
            "color": "#ffd7ff48", "alignment": "left",
            "positionX": 85.0 / WIDTH, "positionY": 75.5 / HEIGHT,
            "backgroundColor": "#00000000"})

# The source phone card uses a solid #111822 inner header/footer. The shell fill
# is transparent so photos show; these bars restore only the non-photo regions.
free_graphics = []
for index, track in enumerate(all_tracks):
    if (index >= 159 and track["trackId"] != chapter_track and
            track.get("role") == "graphics" and
            track.get("visible") is True and track.get("locked") is False and
            not any(c["startSeconds"] < 184 and
                    c["startSeconds"] + c["durationSeconds"] > 160
                    for c in track["clips"])):
        free_graphics.append((index, track["trackId"]))
free_graphics.sort()
assert len(free_graphics) >= 29, f"Expected 29 free high-order G tracks, found {len(free_graphics)}"
phone_tracks = [track_id for _, track_id in free_graphics[:29]]

header_bg_id = add_graphic(
    "phone-header-background", phone_tracks[0], "rectangle",
    1590.0, 422.0, 392.0, 50.0, "#ff111822", slide=False,
    phone_y_entry=True, mask=False)
footer_bg_id = add_graphic(
    "phone-footer-background", phone_tracks[1], "rectangle",
    1590.0, (937.0 + 1018.0) / 2.0, 392.0, 81.0, "#ff111822",
    slide=False, phone_y_entry=True, mask=False)

# Duplicate outer UI above the opaque-region backing bars. Original underlying
# UI is retained; these are editable native title/graphic clips on free tracks.
add_graphic("phone-brand-dot-visible", phone_tracks[2], "ellipse",
            1415.0, 420.0, 22.0, 22.0, "#ffd7ff48", slide=False,
            phone_y_entry=True, mask=False)
add_title("phone-brand-visible", phone_tracks[3], "Field journal",
          1438.0, 421.0, "Space Grotesk", 19.0, "#fff2eee1",
          slide=False, phone_y_entry=True, mask=False)
page_track = phone_tracks[4]
for step_id, text, begin, span in [
        ("phone-page-counter-1", "1 / 3", 160.0, 18.65),
        ("phone-page-counter-2", "2 / 3", 178.65, 2.70),
        ("phone-page-counter-3", "3 / 3", 181.35, 2.65)]:
    add_title(step_id, page_track, text, 1725.0, 421.0, "Space Mono", 14.0,
              "#fff2eee1", alignment="right", slide=False,
              phone_y_entry=begin < 178.65, start=begin, duration=span, mask=False)

dot_centers = [1575.5, 1590.5, 1605.5]
for index, x in enumerate(dot_centers):
    add_graphic(f"phone-page-dot-{index+1}-inactive", phone_tracks[5 + index],
                "ellipse", x, 957.5, 7.0, 7.0, "#ff586574",
                slide=False, phone_y_entry=True, mask=False)
for index, x in enumerate(dot_centers):
    create_id = f"phone-page-dot-{index+1}-active-create"
    add(create_id, "createTimelineGraphic", trackId=phone_tracks[8 + index],
        shapeType="ellipse", startSeconds=START, durationSeconds=DURATION,
        values={"text": "", "fontFamily": "Space Mono", "fontSize": 12,
                "fontWeight": 400, "italic": False, "letterSpacing": 0,
                "lineHeight": 1, "color": "#ffffffff", "alignment": "left",
                "positionX": x / WIDTH, "positionY": 957.5 / HEIGHT,
                "backgroundColor": "#ffd7ff48", "shapeWidth": 7.0 / WIDTH,
                "shapeHeight": 7.0 / HEIGHT, "shapeCornerRadius": 0.5,
                "shapeStrokeColor": "#00000000", "shapeStrokeWidth": 0})
    ref = {"result": create_id, "field": "createdClipId"}
    page_start = (0.0, 18.65, 21.35)[index]
    page_end = (18.65, 21.35, 24.0)[index]
    set_animation(f"phone-page-dot-{index+1}-active-state", ref, [
        {"parameter": "opacity", "keyframes": sampled_channel(
            lambda t, i=index: shown(t) * (
                1.0 - smooth(t, 18.65, 18.6666666667) if i == 0 else
                smooth(t, 18.65, 18.6666666667) *
                (1.0 - smooth(t, 21.35, 21.3666666667)) if i == 1 else
                smooth(t, 21.35, 21.3666666667)))},
        {"parameter": "frameOffsetY", "keyframes": sampled_channel(
            lambda t: 4.0 * 65.0 / HEIGHT * (1.0 - shown(t)))},
    ])

add_title("phone-caption-visible", phone_tracks[11],
          "Take the long way. ↗", 1403.0, 984.5, "Space Grotesk", 17.0,
          "#fff2eee1", slide=False, phone_y_entry=True, mask=False)

# Phone artwork layers recreated from Panorama.tsx. All are distinct native
# elements; no shape is combined with text, and every in-viewport item is masked.
add_graphic("phone-first-light-plate", phone_tracks[12], "rectangle",
            CONTENT_X + (1770.0 + 275.0 / 2.0) * PHONE_S,
            CONTENT_Y + (172.0 + 94.0 / 2.0) * PHONE_S,
            275.0 * PHONE_S, 94.0 * PHONE_S, "#fff0834f",
            stroke="#00000000", stroke_width=0, rotation=-5,
            phone_y_entry=True)
add_title("phone-first-light-label", phone_tracks[13], "FIRST LIGHT ↗",
          CONTENT_X + (1770.0 + 275.0 / 2.0) * PHONE_S,
          CONTENT_Y + (203.0 + 27.0 / 2.0) * PHONE_S,
          "Space Mono", 27.0 * PHONE_S, "#ff182331",
          letter_spacing=1.0 * PHONE_S, alignment="center",
          phone_y_entry=True)
add_vector("phone-mountain-vector", phone_tracks[14],
           x_center=CONTENT_X + (1438.0 + 160.0) * PHONE_S,
           y_center=CONTENT_Y + (241.0 + 150.0) * PHONE_S,
           width=320.0 * PHONE_S, height=300.0 * PHONE_S,
           commands=[{"verb": "M", "points": [28, 266]},
                     {"verb": "L", "points": [126, 77]},
                     {"verb": "L", "points": [159, 154]},
                     {"verb": "L", "points": [220, 22]},
                     {"verb": "L", "points": [294, 266]}],
           stroke="#b3f2eee1", stroke_width=2)
add_vector("phone-mountain-baseline", phone_tracks[28],
           x_center=CONTENT_X + (1438.0 + 160.0) * PHONE_S,
           y_center=CONTENT_Y + (241.0 + 150.0) * PHONE_S,
           width=320.0 * PHONE_S, height=300.0 * PHONE_S,
           commands=[{"verb": "M", "points": [28, 286]},
                     {"verb": "L", "points": [294, 286]}],
           stroke="#80f2eee1", stroke_width=1)
add_graphic("phone-mountain-sun", phone_tracks[15], "ellipse",
            CONTENT_X + (1438.0 + 220.0) * PHONE_S,
            CONTENT_Y + (241.0 + 22.0) * PHONE_S,
            18.0 * PHONE_S, 18.0 * PHONE_S, "#fff0834f",
            phone_y_entry=True)

# Top divider and bottom rule have one source copy per panel; all three slide
# together with the photo/title pages and are independently masked.
for page in range(3):
    x0 = CONTENT_X + (page * 1080.0 + 76.0) * PHONE_S
    center_x = x0 + 928.0 * PHONE_S / 2.0
    top_rule_center_y = CONTENT_Y + (124.0 + 1.0) * PHONE_S
    add_graphic(f"phone-top-rule-page-{page+1}", phone_tracks[16 + page],
                "rectangle", center_x, top_rule_center_y,
                928.0 * PHONE_S, 2.0 * PHONE_S, "#7af2eee1",
                phone_y_entry=True)
for page in range(3):
    x0 = CONTENT_X + (page * 1080.0 + 76.0) * PHONE_S
    center_x = x0 + 928.0 * PHONE_S / 2.0
    footer_center_y = CONTENT_Y + (1254.0 + 1.0) * PHONE_S
    add_graphic(f"phone-footer-rule-page-{page+1}", phone_tracks[19 + page],
                "rectangle", center_x, footer_center_y,
                928.0 * PHONE_S, 2.0 * PHONE_S, "#7af2eee1",
                phone_y_entry=True)

footer_specs = [
    (0, "A LITTLE FURTHER.", "SWIPE →", 76.0, 1004.0),
    (1, "A DIFFERENT PERSPECTIVE.", "SWIPE →", 1156.0, 2084.0),
    (2, "KEEP THE CURIOSITY.", "END / BEGIN AGAIN", 2236.0, 3164.0),
]
for page, left_text, right_text, left_design_x, right_design_x in footer_specs:
    y_center = CONTENT_Y + (1278.0 + 25.0 / 2.0) * PHONE_S
    left_x = CONTENT_X + left_design_x * PHONE_S
    right_x = CONTENT_X + right_design_x * PHONE_S
    add_title(f"phone-footer-left-page-{page+1}", phone_tracks[22 + page * 2],
              left_text, left_x, y_center, "Space Mono", 25.0 * PHONE_S,
              "#fff2eee1", slide=True, phone_y_entry=True)
    add_title(f"phone-footer-right-page-{page+1}", phone_tracks[23 + page * 2],
              right_text, right_x, y_center, "Space Mono", 25.0 * PHONE_S,
              "#fff2eee1", alignment="right", slide=True,
              phone_y_entry=True)

# Add a source-matched phone page label above the existing cropped text's masks.
# It is a static native title per page, masked and swept with the same curve.

# Every chosen destination is a visible/unlocked graphics lane that is vacant
# for the full half-open [160,184) interval in this exact planning snapshot.
creation_tracks = [step["values"]["trackId"] for step in steps
                   if step["commandId"] in {
                       "timeline.action.createTimelineTitle",
                       "timeline.action.createTimelineGraphic",
                       "timeline.action.createTimelineGenerator"}]
for track_id in creation_tracks:
    track = tracks[track_id]
    if track.get("role") != "graphics" or not track.get("visible") or track.get("locked"):
        raise ValueError(f"Unsafe destination graphics lane: {track_id}")
    if any(clip["startSeconds"] < START + DURATION and
           clip["startSeconds"] + clip["durationSeconds"] > START
           for clip in track["clips"]):
        raise ValueError(f"Destination lane is occupied in P20: {track_id}")

target_ids = sorted({step["values"]["clipId"] for step in steps
                     if isinstance(step["values"].get("clipId"), str)})
unknown_targets = sorted(set(target_ids) - set(clips))
if unknown_targets:
    raise ValueError(f"Plan references stable clip IDs absent from P17: {unknown_targets}")
all_keyframes = [key for _, clip in p20.values() for key in clip.get("keyframes", [])]
if any(key.get("bezierInCustom") or key.get("bezierOutCustom") or key.get("roving")
       for key in all_keyframes):
    raise ValueError("P20 contains custom or roving handles; exact restore needs a richer action.")
if any(key.get("interpolation") not in {"linear", "hold", "ease-in", "ease-out", "ease-in-out", "bezier"}
       for key in all_keyframes):
    raise ValueError("P20 contains an interpolation not supported by setTimelineClipAnimation.")
shape_text_violations = []
for step in steps:
    values = step["values"].get("values", {})
    if step["commandId"] == "timeline.action.createTimelineGraphic":
        if values.get("text", ""):
            shape_text_violations.append(step["id"])
    elif step["commandId"] == "timeline.action.createTimelineTitle":
        if values.get("shapeType"):
            shape_text_violations.append(step["id"])
if shape_text_violations:
    raise ValueError(f"Native titles and shapes must be separate clips: {shape_text_violations}")

plan = {
    "schema": "looksy.native-authoring-plan/v1",
    "name": "P20 Carousel native geometry, swipe, and phone artwork correction",
    "status": "offline-proposed-unbound",
    "requiresFreshBinding": True,
    "resourceId": "timeline:3db4e4fb-6737-415c-b6ee-dd766e7c237",
    "composition": {"width": WIDTH, "height": HEIGHT, "fps": FPS,
                    "durationSeconds": 298},
    "planningSnapshot": {
        "path": str(SNAPSHOT.resolve()),
        "sha256": sha256(SNAPSHOT),
        "documentHash": "d6a5023c817f0fe79109e7a6647b87032e5ed8a48de394697ac4f6a11be6c422",
    },
    "scope": {
        "sequenceSeconds": [START, START + DURATION],
        "sourceFiles": [
            "marketing/animated-trailer/src/creative-launch/BuyerScenes.tsx::BuyerCarousel",
            "marketing/animated-trailer/src/desktop-launch/Panorama.tsx::PanoramaArtwork",
            "marketing/native-recreation-v4/library_carousel_v2_plan.py::_phone_slide",
        ],
        "offlineOnly": True,
        "noWorkerCallsOrMutations": True,
        "mustWaitForP16FullRenderTerminal": True,
        "nativeReviewFrames": [
            {"sequenceFrame": 5040, "sequenceSeconds": 168,
             "path": "marketing/native-recreation-v4/polish-pass-2026-10-30/full-p16-in-progress-frame-review/frame-005040.png"},
            {"sequenceFrame": 5220, "sequenceSeconds": 174,
             "path": "marketing/native-recreation-v4/polish-pass-2026-10-30/full-p16-in-progress-frame-review/frame-005220.png"},
            {"sequenceFrame": 5400, "sequenceSeconds": 180,
             "path": "marketing/native-recreation-v4/polish-pass-2026-10-30/full-p16-in-progress-frame-review/frame-005400.png"},
        ],
        "referenceFrames": [
            "marketing/native-recreation-v4/polish-pass-2026-10-30/p20-carousel-parity/reference-004860.png",
            "marketing/native-recreation-v4/polish-pass-2026-10-30/p20-carousel-parity/reference-004920.png",
            "marketing/native-recreation-v4/polish-pass-2026-10-30/p20-carousel-parity/reference-005100.png",
            "marketing/native-recreation-v4/polish-pass-2026-10-30/p20-carousel-parity/reference-005280.png",
            "marketing/native-recreation-v4/polish-pass-2026-10-30/p20-carousel-parity/reference-005400.png",
        ],
    },
    "runtimePreconditions": {
        "requiresFrameBasePoseAnimationRegressionFix": True,
        "requiredSetterContract": "setTimelineClipFrame must update the base frame pose without leaving its previous first-keyframe pose as the base; the complete intended frame animation channels are then restored immediately by the following setTimelineClipAnimation step.",
        "proofReceipt": "marketing/native-recreation-v4/polish-pass-2026-10-30/automation-frame-base-regression/verification.json",
        "legacyWorkerMustNotBeBound": "763066 worker/build until replaced by the fixed reviewed bundle after the P16 export is terminal.",
        "enforceBeforeBinding": "Root must verify the running worker binary includes the source fix and the regression receipt before binding this plan.",
    },
    "reviewNotes": [
        "Bind only after the P16 immutable full-render job is terminal and a fresh native snapshot is saved.",
        "The P17 snapshot is the planning source, not an execution credential; binder must verify all target IDs and fields against the fresh task-owned document.",
        "Old P20 frameOffsetX/Y samples are in full-canvas fractions. This plan multiplies only those authored channels by four to use the current native quarter-canvas contract.",
        "Every full-frame setter is immediately followed by setTimelineClipAnimation restoring every existing channel; frameOffsetX/Y are converted by four, every other channel value/time/interpolation is retained, except phone photo opacity/slide and phone-child entrance channels intentionally rebuilt from BuyerCarousel/Panorama source timing.",
        "P20's native shape clips were authored at CSS top-left coordinates; all shape positions below are converted to center anchors while shape dimensions and rotation stay fixed.",
        "Phone image crops remain unchanged. The continuous source swipe uses two 30 fps sampled smoothstep curves at local [18,19.3] and [20.7,22]. The full page art remains editable native photos, titles, shapes, and a vector path.",
        "The phone shell and every phone-content layer use the source 65 px translateY entrance during local 15–17; the phone image/photo layer additionally keeps its base quarter-canvas center offset while the entry curve is active.",
        "The phone shell fill is made transparent so the photo layers can show through. Separate dark header/footer plates preserve the source card's #111822 bars; existing UI is recreated above those plates.",
        "New title actions are text-only; First Light's plate and label are separate clips so native shape rendering does not suppress text.",
        "All added clips reuse existing empty graphics tracks. Fresh bind must revalidate global track order, visibility/locks, interval vacancy, resource hash, and exact current clip fields.",
    ],
    "placementGuards": {
        "halfOpenInterval": [160, 184],
        "destinationTrackIds": sorted(set(creation_tracks)),
        "targetClipIds": target_ids,
        "existingTargetClipIds": target_ids,
        "expectedTrackOrderUnchanged": True,
        "requireAllDestinationTracksVisibleUnlockedGraphics": True,
        "requireDestinationTracksEmptyInInterval": True,
    },
    "keyframePreservationAudit": {
        "targetClipCount": len(target_ids),
        "originalKeyframeCountInP20": len(all_keyframes),
        "customBezierHandleCount": 0,
        "rovingKeyframeCount": 0,
        "originalInterpolationCounts": {
            name: sum(1 for key in all_keyframes if key.get("interpolation") == name)
            for name in sorted({key.get("interpolation", "linear") for key in all_keyframes})
        },
        "oldOffsetChannelsMultiplyBy": 4,
        "additionalOffsetChannelClipIds": offset_channel_targets,
        "phonePhotoOpacityReplacement": "source phone reveal smoothstep local 15–17 plus continuous swipe; old page crossfades intentionally removed",
    },
    "shapeTextExclusivityAudit": {
        "result": "passed",
        "creationStepsChecked": sum(step["commandId"] in {
            "timeline.action.createTimelineGraphic",
            "timeline.action.createTimelineTitle"} for step in steps),
        "violations": shape_text_violations,
        "rule": "All native graphics carry an empty text field; all visible copy uses separate text-only title clips.",
    },
    "steps": steps,
}

# Use the same pure placement/reference guards as the binding helper before
# writing this proposed plan. This is an offline structural check only.
sys.path.insert(0, str(PASS))
from native_fragment_placement import validate_clip_references, verify_placement

validated_target_ids, created_references = validate_clip_references(plan)
if validated_target_ids != set(target_ids):
    raise ValueError("The existing-target list differs from the clip IDs in the plan.")
placement_audit = verify_placement(plan, doc, doc)
plan["placementGuards"]["createdClipReferences"] = [
    {"stepId": step_id, "creatorStepId": creator_id,
     "field": "createdClipId"}
    for step_id, creator_id in created_references
]
plan["offlineValidation"] = {
    "planReferences": "passed",
    "createdClipReferenceCount": len(created_references),
    "placement": placement_audit,
    "noWorkerCallsOrMutations": True,
}

OUTPUT.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n")
print(json.dumps({
    "plan": str(OUTPUT), "stepCount": len(steps),
    "targetClipCount": len(target_ids), "destinationTrackCount": len(set(creation_tracks)),
    "p20ClipCount": len(p20), "originalKeyframeCount": len(all_keyframes),
    "planBytes": OUTPUT.stat().st_size, "planSha256": sha256(OUTPUT),
}, indent=2))

