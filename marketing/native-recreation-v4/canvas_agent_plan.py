"""Native editable command plan for the Canvas and local-Agent chapters.

The plan imports only the original media sources. It recreates titles, cards,
panels, cutouts, controls, and timing as ordinary Timeline layers; it never
uses a reference-video frame as a source. Run with ``--orientation portrait``
to use the portrait Stage/Caption placement branch from the Remotion source.
"""
from __future__ import annotations

import argparse
import json
import math
import textwrap
from pathlib import Path
from typing import Callable

import opening_plan as native

ROOT = Path(__file__).resolve().parent
MEDIA = native.MEDIA
STEPS = native.STEPS
W, H = 1920, 1080
ORIENTATION = "landscape"
_tracks: dict[str, dict[str, str]] = {}
_ids: set[str] = set()
_REUSABLE_TRACK_ROLES = {
    "caption / chapter", "caption / heading", "caption / detail",
    "footer / left", "footer / right",
    "agent / footer left", "agent / footer right",
}


def _stroke_unit() -> float:
    return 1.0 / min(W, H)


def _unique(value: str) -> str:
    result = "".join(c.lower() if c.isalnum() else "-" for c in value).strip("-")
    if result not in _ids:
        _ids.add(result)
        return result
    index = 2
    while f"{result}-{index}" in _ids:
        index += 1
    result = f"{result}-{index}"
    _ids.add(result)
    return result


def add(name: str, action: str, **values):
    return native.add(_unique(name), action, **values)


def track(role: str):
    if role not in _tracks:
        step_id = _unique("Canvas Agent / " + role)
        native.add(step_id, "createTimelineTrack", kind="graphics",
                   name=("CA / " + role)[:48], audioRole="")
        result = {"result": step_id, "field": "trackId"}
        _tracks[role] = result
    return _tracks[role]


def smooth(t: float, a: float, b: float) -> float:
    return native.smooth(t, a, b)


def out(t: float, a: float, b: float) -> float:
    return native.out(t, a, b)


def animate(name: str, clip, parameter: str, duration: float,
            function: Callable[[float], float], fps: int = 30):
    native.animate(_unique(name), clip, parameter, 0.0, duration, function, fps)


def animate_many(name: str, clip, duration: float,
                 curves: dict[str, Callable[[float], float]], fps: int = 30):
    frame_count = round(duration * fps)
    channels = []
    for parameter, function in curves.items():
        keys = [{"localSeconds": frame / fps,
                 "value": function(frame / fps),
                 "interpolation": "linear"}
                for frame in range(frame_count + 1)]
        compact = [key for index, key in enumerate(keys)
                   if index in (0, len(keys) - 1) or
                   keys[index - 1]["value"] != key["value"] or
                   keys[index + 1]["value"] != key["value"]]
        channels.append({"parameter": parameter, "keyframes": compact})
    add(name, "setTimelineClipAnimation", clipId=clip, channels=channels)


def stage_base():
    if ORIENTATION == "portrait":
        return 40.0, 710.0, 1.0
    return 650.0, 120.0, 1.1


def stage_values(x: float, y: float, width: float, height: float,
                 focus: float = 0.0):
    if ORIENTATION == "portrait":
        left, top = 40.0, 710.0 - 60.0 * focus
        scale = 1.0 + 0.02 * focus
    else:
        left, top = 650.0 - 440.0 * focus, 120.0 - 150.0 * focus
        scale = 1.1 + 0.4 * focus
    return left + x * scale, top + y * scale, width * scale, height * scale, scale


def _focus_animation(phase_name: str, duration: float,
                     focus: Callable[[float], float]):
    return focus


def _frame_values(x: float, y: float, width: float, height: float,
                  fit: str, opacity: float, radius: float,
                  rotation: float = 0.0, perspective_x: float = 0.0,
                  perspective_y: float = 0.0,
                  crop=(0.0, 0.0, 0.0, 0.0)):
    return {
        "offsetX": 4.0 * ((x + width / 2.0) / W - 0.5),
        "offsetY": 4.0 * ((y + height / 2.0) / H - 0.5),
        "scale": 1.0,
        "rotationDegrees": rotation,
        "perspectiveX": perspective_x,
        "perspectiveY": perspective_y,
        "opacity": opacity,
        "objectFit": fit,
        "width": width / W,
        "height": height / H,
        "cornerRadius": radius,
        "cropLeft": crop[0],
        "cropTop": crop[1],
        # Canvas geometry uses crop insets; the native timeline accepts the
        # retained source rectangle's edges.
        "cropRight": 1.0 - crop[2],
        "cropBottom": 1.0 - crop[3],
    }


def place_image(name: str, source: str, role: str, start: float,
                duration: float, x: float, y: float, width: float, height: float,
                fit: str = "fill", opacity: float = 1.0, radius: float = 0.0,
                rotation: float = 0.0, focus: Callable[[float], float] | None = None,
                inner_scale: float = 1.0,
                crop=(0.0, 0.0, 0.0, 0.0)):
    left, top, frame_w, frame_h, stage_scale = stage_values(x, y, width, height)
    clip = native.image(name, source, track(role + " / " + name), start, duration)
    add(name + " / frame", "setTimelineClipFrame", clipId=clip,
        **_frame_values(left, top, frame_w * inner_scale, frame_h * inner_scale,
                        fit, opacity, radius, rotation, crop=crop))
    if focus is not None:
        base_left, base_top, base_w, base_h, base_scale = stage_values(x, y, width, height, 0.0)

        def mapped(which: str, local: float):
            px, py, pw, ph, _ = stage_values(x, y, width, height, focus(local))
            if which == "x":
                return 4.0 * ((px + pw * inner_scale / 2.0) / W - 0.5)
            if which == "y":
                return 4.0 * ((py + ph * inner_scale / 2.0) / H - 0.5)
            return stage_values(x, y, width, height, focus(local))[4] / base_scale

        animate_many(name + " / stage transform", clip, duration, {
            "frameOffsetX": lambda t: mapped("x", t),
            "frameOffsetY": lambda t: mapped("y", t),
            "frameScale": lambda t: mapped("scale", t),
        })
    return clip


def add_shape(name: str, role: str, start: float, duration: float,
              x: float, y: float, width: float, height: float,
              color: str, shape: str = "rectangle", radius: float = 0.0,
              stroke: str = "#00000000", stroke_width: float = 0.0,
              opacity: float = 1.0,
              focus: Callable[[float], float] | None = None,
              rotation: float = 0.0, inner_scale: float = 1.0,
              stage_space: bool = True):
    if stage_space:
        left, top, frame_w, frame_h, base_scale = stage_values(x, y, width, height)
    else:
        left, top, frame_w, frame_h, base_scale = x, y, width, height, 1.0
    cx, cy = left + frame_w * inner_scale / 2.0, top + frame_h * inner_scale / 2.0
    values = {
        "positionX": cx / W, "positionY": cy / H,
        "shapeWidth": frame_w * inner_scale / W,
        "shapeHeight": frame_h * inner_scale / H,
        "shapeCornerRadius": radius,
        "backgroundColor": color,
        "shapeStrokeColor": stroke,
        "shapeStrokeWidth": stroke_width,
    }
    track_key = role if role in _REUSABLE_TRACK_ROLES else role + " / " + name
    clip = add(name, "createTimelineGraphic", trackId=track(track_key), shapeType=shape,
               startSeconds=start, durationSeconds=duration, values=values)
    if rotation != 0.0 or opacity != 1.0:
        add(name + " / transform", "setTimelineClipFrame", clipId=clip,
            **_frame_values(0, 0, W, H, "stretch", opacity, 0.0, rotation))
    if focus is not None and stage_space:
        animate_many(name + " / stage transform", clip, duration, {
            "titlePositionX": lambda t: stage_values(
                x, y, width, height, focus(t))[0] / W +
                stage_values(x, y, width, height, focus(t))[2] * inner_scale / (2 * W),
            "titlePositionY": lambda t: stage_values(
                x, y, width, height, focus(t))[1] / H +
                stage_values(x, y, width, height, focus(t))[3] * inner_scale / (2 * H),
            "frameScale": lambda t: stage_values(
                x, y, width, height, focus(t))[4] / base_scale,
        })
    return clip


def _stage_frame_values(focus_value: float):
    """Full-canvas clip transform equivalent to the Stage's 2D pan/scale."""
    left, top, _, _, scale = stage_values(0.0, 0.0, 0.0, 0.0, focus_value)
    delta_x = left + (scale - 1.0) * W / 2.0
    delta_y = top + (scale - 1.0) * H / 2.0
    return {
        "offsetX": 4.0 * delta_x / W,
        "offsetY": 4.0 * delta_y / H,
        "scale": scale,
        "rotationDegrees": 0.0,
        "perspectiveX": 0.0,
        "perspectiveY": 0.0,
        "opacity": 1.0,
        "objectFit": "stretch",
        "width": 1.0,
        "height": 1.0,
        "cornerRadius": 0.0,
        "cropLeft": 0.0,
        "cropTop": 0.0,
        "cropRight": 0.0,
        "cropBottom": 0.0,
    }


def add_vector_path(name: str, role: str, start: float, duration: float,
                    commands: list[dict], coordinate_function: Callable[[float], list[float]],
                    stroke: str, stroke_width: float,
                    focus: Callable[[float], float] | None = None,
                    opacity_function: Callable[[float], float] | None = None):
    """Create an editable native path with per-frame local geometry samples."""
    fps = 30
    frame_count = round(duration * fps)
    frames = [{
        "seconds": frame / fps,
        "coordinates": coordinate_function(frame / fps),
    } for frame in range(frame_count + 1)]
    path = {
        "viewBoxWidth": W,
        "viewBoxHeight": H,
        "commands": commands,
        "frames": frames,
        "stroke": stroke,
        "fill": "#00000000",
        "strokeWidth": stroke_width,
    }
    clip = add(name, "createTimelineGenerator", trackId=track(role),
               generatorKind="vector-path", startSeconds=start,
               durationSeconds=duration, parameters={"vectorPath": path})
    initial_focus = focus(0.0) if focus is not None else 0.0
    add(name + " / Stage frame", "setTimelineClipFrame", clipId=clip,
        **_stage_frame_values(initial_focus))
    if focus is not None:
        animate_many(name + " / Stage motion", clip, duration, {
            "frameOffsetX": lambda t: _stage_frame_values(focus(t))["offsetX"],
            "frameOffsetY": lambda t: _stage_frame_values(focus(t))["offsetY"],
            "frameScale": lambda t: _stage_frame_values(focus(t))["scale"],
        })
    if opacity_function is not None:
        set_opacity(name + " / path visibility", clip, duration, opacity_function)
    return clip


def add_text(name: str, text: str, role: str, start: float, duration: float,
             x: float, y: float, size: float, family: str = "Space Grotesk",
             color: str = "#f1efdf", spacing: float = 0.0,
             line_height: float = 1.1, alignment: str = "left",
             italic: bool = False, weight: int = 400,
             opacity: float = 1.0, shadow: bool = False,
             shadow_color: str = "#bf000000", shadow_x: float = 0.0,
             shadow_y: float = 0.0,
             focus: Callable[[float], float] | None = None,
             inner_scale: float = 1.0,
             rotation: float = 0.0, stage_space: bool = True):
    _, _, stage_scale = stage_base()
    lines = max(1, text.count("\n") + 1)
    actual_size = size * (stage_scale if stage_space else 1.0) * inner_scale
    estimated_width = min(W * 0.9, max(actual_size, max(map(len, text.splitlines() or [text])) * actual_size * 0.58))
    actual_height = actual_size * line_height * lines
    anchor_x = x
    anchor_y = y + actual_height / 2.0
    if stage_space:
        left, top, _, _, _ = stage_values(anchor_x, anchor_y, 0, 0)
    else:
        left, top = anchor_x, anchor_y
    values = {
        "text": text, "fontFamily": family, "fontSize": actual_size,
        "fontWeight": weight, "italic": italic, "letterSpacing": spacing * inner_scale,
        "lineHeight": line_height, "color": color,
        "backgroundColor": "#00000000", "alignment": alignment,
        "positionX": left / W, "positionY": top / H,
    }
    if shadow:
        values.update({"shadowEnabled": True, "shadowColor": shadow_color,
                       "shadowOffsetX": shadow_x * (stage_scale if stage_space else 1.0) * inner_scale,
                       "shadowOffsetY": shadow_y * (stage_scale if stage_space else 1.0) * inner_scale})
    track_key = role if role in _REUSABLE_TRACK_ROLES else role + " / " + name
    clip = add(name, "createTimelineTitle", trackId=track(track_key), templateKey="headline",
               startSeconds=start, durationSeconds=duration, values=values)
    # A native title is rasterized as a full-frame layer. Rotating that layer
    # would pivot around the canvas center, so text rotation is left explicit
    # in the parity notes instead of being applied at the wrong origin.
    if opacity != 1.0:
        add(name + " / opacity frame", "setTimelineClipFrame", clipId=clip,
            **_frame_values(0, 0, W, H, "stretch", opacity, 0.0))
    if focus is not None and stage_space:
        animate_many(name + " / stage transform", clip, duration, {
            "titlePositionX": lambda t: stage_values(anchor_x, anchor_y, 0, 0, focus(t))[0] / W,
            "titlePositionY": lambda t: stage_values(anchor_x, anchor_y, 0, 0, focus(t))[1] / H,
            "titleFontSize": lambda t: size * stage_values(0, 0, 0, 0, focus(t))[4] * inner_scale,
        })
    return clip


def set_opacity(name: str, clip, duration: float,
                function: Callable[[float], float]):
    animate(name, clip, "opacity", duration, function)


def set_sequence_mask(name: str, clip, x: float, y: float,
                      width: float, height: float, radius: float = 0.0):
    left, top, mask_w, mask_h, _ = stage_values(x, y, width, height)
    add(name, "setTimelineClipMask", clipId=clip, shape="rectangle",
        left=left / W, top=top / H,
        right=(left + mask_w) / W, bottom=(top + mask_h) / H,
        cornerRadius=radius)


def set_source_matte(name: str, clip, matte_path: str, mode: str,
                     invert: bool = False, fill_color: str | None = None):
    values = {
        "clipId": clip,
        "filePath": str((MEDIA / matte_path).resolve()),
        "mode": mode,
        "invert": invert,
    }
    if fill_color is not None:
        values["fillColor"] = fill_color
    add(name, "setTimelineClipMatte", **values)


def set_blend_mode(name: str, clip, blend_mode: str):
    add(name, "setTimelineClipBlendMode", clipId=clip, blendMode=blend_mode)


def set_stage_clip_mask(name: str, clip, x: float, y: float,
                        width: float, height: float, duration: float,
                        focus: Callable[[float], float] | None = None):
    rect = stage_values(x, y, width, height, 0.0)
    add(name, "setTimelineClipMask", clipId=clip, shape="rectangle",
        left=rect[0] / W, top=rect[1] / H,
        right=(rect[0] + rect[2]) / W, bottom=(rect[1] + rect[3]) / H,
        cornerRadius=0.0)
    if focus is not None:
        animate_many(name + " / follow Stage", clip, duration, {
            "mask:left": lambda t: stage_values(x, y, width, height, focus(t))[0] / W,
            "mask:top": lambda t: stage_values(x, y, width, height, focus(t))[1] / H,
            "mask:right": lambda t: (stage_values(x, y, width, height, focus(t))[0] +
                                      stage_values(x, y, width, height, focus(t))[2]) / W,
            "mask:bottom": lambda t: (stage_values(x, y, width, height, focus(t))[1] +
                                       stage_values(x, y, width, height, focus(t))[3]) / H,
        })


def set_caption(index: int, start: float, duration: float, chapter: str,
                headline: str, detail: str, serif: bool = False,
                focus: Callable[[float], float] | None = None):
    if ORIENTATION == "portrait":
        left, top = 62, 160
        headline_size, detail_size = 108, 36
        max_width = 900
    else:
        left, top = 82, 254
        headline_size, detail_size = 91, 33
        max_width = 445
    detail = "\n".join(textwrap.wrap(detail, width=49 if ORIENTATION == "portrait" else 27,
                                     break_long_words=False, break_on_hyphens=False))
    chapter_clip = add_text(f"{chapter} / {index} tag", chapter,
                            "caption / chapter", start, duration,
                            left, top, 21, "Space Mono", "#d7ff48", 1.8, 1.0,
                            stage_space=False)
    headline_top = top + 49
    headline_clip = add_text(f"{chapter} / {index} heading", headline,
                             "caption / heading", start, duration,
                             left, headline_top, headline_size,
                             "Instrument Serif" if serif else "Anton",
                             "#f1efdf", -1.8, 1.01, italic=serif,
                             stage_space=False)
    line_count = headline.count("\n") + 1
    detail_top = headline_top + headline_size * 1.01 * line_count + 28
    detail_clip = add_text(f"{chapter} / {index} detail", detail,
                           "caption / detail", start, duration,
                           left, detail_top, detail_size, "Space Grotesk",
                           "#c9ced6", 0, 1.4, stage_space=False)
    for suffix, clip, at in (("tag", chapter_clip, 0.0),
                             ("heading", headline_clip, 0.05),
                             ("detail", detail_clip, 0.2)):
        def caption_opacity(t: float, a: float = at):
            value = out(t, a, a + 0.55)
            if focus is not None and ORIENTATION == "landscape":
                value *= 1.0 - smooth(focus(t), 0.0, 0.65)
            return value

        set_opacity(f"caption {index} {suffix} opacity", clip, duration,
                    caption_opacity)
        animate(f"caption {index} {suffix} lift", clip, "frameOffsetY", duration,
                lambda t, a=at: 4 * 45 * (1 - out(t, a, a + 0.75)) / H)


def footer(prefix: str, start: float, duration: float, left: str, right: str):
    if ORIENTATION == "portrait":
        x, y = 62, H - 155
        size = 20
    else:
        x, y, size = 82, H - 34, 17
    add_text(prefix + " footer left", left, "footer / left", start, duration,
             x, y, size, "Space Mono", "#aeb6c3", 1.0, 1.05,
             stage_space=False)
    add_text(prefix + " footer right", right, "footer / right", start, duration,
             W - x, y, size, "Space Mono", "#aeb6c3", 1.0, 1.05, "right",
             stage_space=False)


def pills(prefix: str, items: list[str], start: float, duration: float,
          x: float, y: float, focus: Callable[[float], float] | None = None,
          stage_scale: float = 1.0):
    cursor = x
    for index, item in enumerate(items):
        font_size = 19
        width = (len(item) * font_size * 0.60 + 36) * stage_scale
        height = 45 * stage_scale
        shape = add_shape(prefix + " / " + item + " pill", "pills / shapes",
                          start, duration, cursor, y, width, height,
                          "#BD0B0D12", "rounded-rectangle", 0.5,
                          "#55C4C8CE", _stroke_unit(), focus=focus)
        label = add_text(prefix + " / " + item + " text", item,
                         "pills / labels", start, duration,
                         cursor + 18 * stage_scale,
                         y + (height - font_size * stage_scale) / 2,
                         font_size, "Space Mono",
                         "#d7ff48" if index == 0 else "#f1efdf", 0.0,
                         1.0, focus=focus, inner_scale=stage_scale)
        cursor += width + 12 * stage_scale
    return cursor


def panel(prefix: str, start: float, duration: float,
          x: float, y: float, width: float, height: float,
          fill: str = "#F5111C29", border: str = "#77A9BBC9",
          radius: float = 12.0, focus=None):
    return add_shape(prefix + " panel", "panels / backplates", start, duration,
                     x, y, width, height, fill, "rounded-rectangle",
                     min(0.5, radius / max(1, min(width, height))),
                     border, _stroke_unit(), focus=focus)


def caption_and_footer_canvas():
    edges = [0, 6, 14, 19, 31, 41, 50, 58, 68]
    copy = [
        ("Go beyond\nthe whole frame.",
         "Separate the subject so you can refine the light or build a new composition."),
        ("Your subject.\nIts own light.",
         "Lift the subject. Bypass the adjustment to compare. Keep the background exactly where it was."),
        ("Keep the subject.\nChange the canvas.",
         "A real transparent cutout. Ready to join a new composition."),
        ("Make it\na poster.",
         "Turn the same photograph into a poster. Keep the type, subject and adjustments editable."),
        ("Leave out\nthe distraction.",
         "Select what goes. Inpaint the selected region. Compare the clean result with the original."),
        ("Build the campaign.\nKeep the pieces.",
         "Build a product campaign with cutout artwork, brush texture and bold typography."),
        ("Every element.\nYour decision.",
         "Change the type. Adjust a layer. Keep the whole composition editable."),
        ("A poster today.\nA thumbnail next.",
         "Recompose the campaign as a thumbnail or a graphic for your website."),
    ]
    for index, (a, b) in enumerate(zip(edges, edges[1:])):
        start, duration = 92 + a, b - a
        focus = None
        if index == 5:
            focus = lambda t: smooth(t, 5.2, 6.4) * (1 - smooth(t, 7.6, 8.8))
        elif index == 7:
            focus = lambda t: smooth(t, 1.4, 2.8) * (1 - smooth(t, 8.5, 9.8))
        set_caption(index, start, duration, "03 / CANVAS & COMPOSITING",
                    *copy[index], serif=index == 3, focus=focus)
        footer("canvas %d" % index, start, duration,
               "POSTERS. THUMBNAILS. YOUR NEXT IDEA." if index > 4 else
               "MASKS. CUTOUTS. TEXT. RETAINED CONTROL.",
               "LOOKSY / CANVAS")


def create_canvas_chapter():
    # The field generator follows the same global clock as the Remotion scene.
    add_shape("Canvas / dark base", "canvas / base", 92, 68,
              0, 0, W, H, "#0b0d12", "rectangle", stage_space=False)
    light = add("Canvas / additive field", "createTimelineGenerator",
                trackId=track("canvas / light field"), generatorKind="desktop-light",
                startSeconds=92, durationSeconds=68,
                parameters={"timeOffsetSeconds": 92, "timeScale": 1.0,
                            "gather": 0.0, "opacity": 0.32})
    animate("Canvas / field opacity", light, "opacity", 68,
            lambda t: 0.32 if t < 41 else 0.24)

    # Every phase has its own original-source layers. Separate native tracks
    # keep the source order visible and editable in Timeline.
    stage = lambda: stage_base()
    x0, y0, s0 = stage()

    # 03.0–03.1: source photograph and actual segmentation mask.
    add_shape("Canvas / checkerboard tile", "canvas / checkerboard", 92, 6,
              210, -15, 530, 795, "#16212b", "rounded-rectangle", 0.024)
    p0 = place_image("Canvas / original studio photograph", "studio/color/4-8.png",
                     "canvas / photo", 92, 6, 210, -15, 530, 795,
                     fit="fill", radius=0.016)
    subject_tint = place_image(
        "Canvas / alpha-masked lime subject paint", "studio/subject-lift.png",
        "canvas / subject paint", 92, 6, 210, -15, 530, 795,
        fit="fill", radius=0.016, opacity=0.0)
    set_source_matte("Canvas / lime paint subject alpha matte", subject_tint,
                     "studio/subject.png", "alpha", fill_color="#bcffb4")
    set_blend_mode("Canvas / lime paint screen blend", subject_tint, "screen")
    set_opacity("Canvas / lime subject paint", subject_tint, 6,
                lambda t: 0.42 * smooth(t, 0.7, 2.0) * (1 - smooth(t, 5.5, 6.0)))
    mask = place_image("Canvas / exact subject mask preview", "studio/subject.png",
                       "canvas / mask source", 93.3, 4.7,
                       760, 50, 180, 400, fit="fill", radius=0.02,
                       opacity=0.94)
    add_text("Canvas / actual mask label", "ACTUAL MASK", "canvas / labels",
             93.3, 4.7, 760, 50, 15, "Space Mono", "#d7ff48", 1.8)
    set_opacity("Canvas / mask label reveal", mask, 4.7,
                lambda t: out(t + 1.3, 1.3, 1.8) * (1 - smooth(t + 1.3, 5.5, 6.0)))

    # 03.1: independently adjustable subject image with bypass and live EV.
    place_image("Canvas / exposure original background", "studio/color/4-8.png",
                "canvas / exposure base", 98, 8, 210, -15, 530, 795,
                fit="fill", radius=0.016)
    lift = place_image("Canvas / subject exposure cutout", "studio/subject-lift.png",
                       "canvas / exposure cutout", 98, 8, 210, -15, 530, 795,
                       fit="fill", radius=0.016)
    set_source_matte("Canvas / subject exposure alpha matte", lift,
                     "studio/subject.png", "alpha")
    set_opacity("Canvas / subject exposure", lift, 8,
                lambda t: smooth(t, 0.8, 3.2) *
                (1 - smooth(t, 4.1, 4.35) * (1 - smooth(t, 5.15, 5.4))))
    panel("Canvas / subject adjustment inspector", 98, 8,
          620, 225, 365, 400, "#F5111B27", "#4DC5D0D3", 12)
    add_text("Canvas / adjustment heading", "SUBJECT / MASKED EXPOSURE",
             "canvas / labels", 98, 8, 642, 247, 16, "Space Mono", "#d7ff48", 1.4)
    for i, (value, begin, end) in enumerate((
            ("Exposure     +0.00 EV", 0.0, 0.8),
            ("Exposure     +0.18 EV", 0.8, 1.8),
            ("Exposure     +0.37 EV", 1.8, 2.7),
            ("Exposure     +0.55 EV", 2.7, 8.0))):
        exposure_value = add_text(f"Canvas / exposure value {i}", value,
                                   "canvas / labels", 98, 8, 642, 296,
                                   29, "Space Grotesk", "#f1efdf")
        set_opacity(f"Canvas / exposure value timing {i}", exposure_value, 8,
                    lambda t, a=begin, b=end: 1.0 if a <= t < b else 0.0)
    add_shape("Canvas / exposure slider rail", "canvas / slider", 98, 8,
              642, 355, 315, 4, "#4d6378")
    slider_fill = add_shape("Canvas / exposure slider fill", "canvas / slider", 98, 8,
                            642, 355, 315, 4, "#d7ff48")
    animate("Canvas / exposure slider", slider_fill, "frameScale", 8,
            lambda t: 0.80 + 0.20 * smooth(t, 0.8, 3.2))
    bypass = lambda t: smooth(t, 4.1, 4.35) * (1 - smooth(t, 5.15, 5.4))
    bypass_off = add_shape("Canvas / bypass off control", "canvas / slider", 98, 8,
                           642, 390, 315, 44, "#263c20", "rounded-rectangle", 0.13,
                           "#889cb783", _stroke_unit())
    bypass_on = add_shape("Canvas / bypass on control", "canvas / slider", 98, 8,
                          642, 390, 315, 44, "#3c4b5a", "rounded-rectangle", 0.13,
                          "#889cb783", _stroke_unit())
    set_opacity("Canvas / bypass off visibility", bypass_off, 8,
                lambda t: 1.0 - bypass(t))
    set_opacity("Canvas / bypass on visibility", bypass_on, 8, bypass)
    bypass_off_label = add_text("Canvas / bypass off label", "BYPASS OFF / EDIT ACTIVE",
                                "canvas / labels", 98, 8, 660, 399,
                                15, "Space Mono", "#f1efdf")
    bypass_on_label = add_text("Canvas / bypass on label", "BYPASS ON",
                               "canvas / labels", 98, 8, 660, 399,
                               15, "Space Mono", "#f1efdf")
    set_opacity("Canvas / bypass off label visibility", bypass_off_label, 8,
                lambda t: 1.0 - bypass(t))
    set_opacity("Canvas / bypass on label visibility", bypass_on_label, 8, bypass)
    active_note = add_text("Canvas / subject active note", "ACTIVE / BACKGROUND UNCHANGED",
                           "canvas / labels", 98, 8, 642, 464,
                           12, "Space Mono", "#a5b7c3")
    bypass_note = add_text("Canvas / subject bypass note", "BYPASSED / COMPARE THE SUBJECT",
                           "canvas / labels", 98, 8, 642, 464,
                           12, "Space Mono", "#a5b7c3")
    set_opacity("Canvas / active note visibility", active_note, 8,
                lambda t: 1.0 - bypass(t))
    set_opacity("Canvas / bypass note visibility", bypass_note, 8, bypass)

    # 03.2: isolated transparent cutout on the retained source pixels.
    old_photo = place_image("Canvas / background recede", "studio/subject-lift.png",
                            "canvas / photo", 106, 5, 210, -15, 530, 795,
                            fit="fill", radius=0.016)
    set_opacity("Canvas / background recede opacity", old_photo, 5,
                lambda t: 1 - smooth(t, 0.8, 2.0))
    transparent_cutout = place_image("Canvas / alpha subject cutout", "creative-polish-v2/subject-lift-alpha.png",
                                     "canvas / transparent cutout", 106, 5,
                                     210, -15, 530, 795, fit="fill", radius=0.016)
    set_opacity("Canvas / cutout entrance", transparent_cutout, 5,
                lambda t: smooth(t, 0.8, 2.0))
    pills("Canvas / transparency", ["TRANSPARENT BACKGROUND", "SOURCE PIXELS RETAINED"],
          106, 5, 140, 780)

    # 03.3: editable Hiker poster with photo, foreground cutout, and real type.
    create_hiker_poster("Canvas poster", 111, 12, 210, -15, 0.57,
                        label="TEXT / FURTHER.", typing=True)
    selection = add_shape("Canvas / selected poster title bounds", "canvas / selection", 112.2, 5.6,
                          245, 583, 900, 230, "#00000000", "rectangle", 0,
                          "#99edf0df", 2 * _stroke_unit())
    panel("Canvas / text inspector", 112.0, 7.8, 620, 200, 340, 390,
          "#F5111C29", "#77A9BBC9", 12)
    add_text("Canvas / text inspector title", "TEXT / FURTHER.", "canvas / labels",
             112.0, 7.8, 645, 222, 15, "Space Mono", "#d7ff48", 1.4)
    for i, row in enumerate(("Font        Anton", "Size        220 px", "Fill", "Shadow")):
        add_text(f"Canvas / text inspector row {i}", row, "canvas / labels",
                 112.0, 7.8, 645, 274 + i * 60, 22, "Space Grotesk", "#f1efdf")
    add_shape("Canvas / title fill swatch", "canvas / labels", 112.0, 7.8,
              860, 382, 60, 30, "#f1efdf", "rounded-rectangle", 0.12)
    add_shape("Canvas / shadow slider rail", "canvas / labels", 112.0, 7.8,
              645, 510, 275, 3, "#42576a")
    add_shape("Canvas / shadow slider value", "canvas / labels", 112.0, 7.8,
              645, 510, 205, 3, "#d7ff48")
    add_text("Canvas / text mode label", "WRITE IT. STYLE IT.", "canvas / labels",
             111, 3.5, 670, 780, 17, "Space Mono", "#d7ff48", 1.8)
    add_text("Canvas / style mode label", "FILL. SHADOW. LIVE TYPE.", "canvas / labels",
             114.5, 3.3, 670, 780, 17, "Space Mono", "#d7ff48", 1.8)

    # Editable Layers inspector slides in as the text controls leave.
    panel("Canvas / editable layers inspector", 118.8, 4.2,
          620, 250, 350, 285, "#F2121C28", "#4AA5B6C5", 12)
    add_text("Canvas / editable layers heading", "LAYERS / DRAG TO REORDER",
             "canvas / labels", 118.8, 4.2, 642, 273, 16, "Space Mono", "#d7ff48", 1.5)
    for i, row in enumerate(("Typography", "Subject cutout", "Color adjustment", "Original photograph")):
        add_shape(f"Canvas / layer row {i}", "canvas / layer rows", 118.8, 4.2,
                  630, 310 + i * 58, 330, 58,
                  "#354b5c" if i == 0 else "#121c28",
                  "rounded-rectangle", 0.08)
        add_text(f"Canvas / layer name {i}", row, "canvas / labels",
                 118.8, 4.2, 642, 323 + i * 58, 19, "Space Grotesk", "#f1efdf")
    add_text("Canvas / reorder label", "DRAG TYPE BELOW THE SUBJECT",
             "canvas / labels", 119, 2, 670, 780, 17, "Space Mono", "#d7ff48", 1.8)

    # 03.4: source-resolution inpaint comparison and selected-region mask.
    inpaint_x, inpaint_y, view_w, view_h = 20, 10, 950, 750
    vx, vy, vw, vh, _ = stage_values(inpaint_x, inpaint_y, view_w, view_h)
    # Source crop matches the 0.93-scaled image centered on (978,1532) and
    # clipped by the 950x750 inpaint viewport at the start of the scene.
    inpaint_crop = (467.25 / 1800, 1128.77 / 2700,
                    1 - 1488.75 / 1800, 1 - 1935.23 / 2700)
    base_photo = native.image("Canvas / inpaint original", "refine/surf.jpg",
                              track("canvas / inpaint original"), 123, 10)
    add("Canvas / inpaint original viewport", "setTimelineClipFrame", clipId=base_photo,
        **_frame_values(vx, vy, vw, vh, "fill", 1.0, 0.02, crop=inpaint_crop))
    set_sequence_mask("Canvas / inpaint original viewport mask", base_photo,
                      inpaint_x, inpaint_y, view_w, view_h, 15 / 750)
    final_photo = native.image("Canvas / inpaint result", "refine/surf-final.png",
                               track("canvas / inpaint result"), 123, 10)
    add("Canvas / inpaint result viewport", "setTimelineClipFrame", clipId=final_photo,
        **_frame_values(vx, vy, vw, vh, "fill", 0.0, 0.02, crop=inpaint_crop))
    set_sequence_mask("Canvas / inpaint result viewport mask", final_photo,
                      inpaint_x, inpaint_y, view_w, view_h, 15 / 750)
    set_opacity("Canvas / inpaint result reveal", final_photo, 10,
                lambda t: smooth(t, 4.1, 4.9))
    before = native.image("Canvas / original compare overlay", "refine/surf.jpg",
                          track("canvas / original compare"), 123, 10)
    add("Canvas / original compare viewport", "setTimelineClipFrame", clipId=before,
        **_frame_values(vx, vy, vw, vh, "fill", 0.0, 0.02, crop=inpaint_crop))
    set_sequence_mask("Canvas / compare viewport mask", before,
                      inpaint_x, inpaint_y, view_w, view_h, 15 / 750)
    set_opacity("Canvas / original compare hold", before, 10,
                lambda t: smooth(t, 6.0, 6.7) * (1 - smooth(t, 8.3, 8.9)))
    selection_overlay = native.image(
        "Canvas / source-aligned luminance selection overlay", "refine/surf.jpg",
        track("canvas / inpaint selection overlay"), 123, 10)
    add("Canvas / selection overlay frame", "setTimelineClipFrame",
        clipId=selection_overlay,
        **_frame_values(vx, vy, vw, vh, "fill", 0.0, 0.02, crop=inpaint_crop))
    set_sequence_mask("Canvas / selection overlay viewport mask", selection_overlay,
                      inpaint_x, inpaint_y, view_w, view_h, 15 / 750)
    set_source_matte("Canvas / luminance selection source matte", selection_overlay,
                     "refine/surf-mask.png", "luminance", fill_color="#c7a2ff")
    set_opacity("Canvas / luminance selection draw", selection_overlay, 10,
                lambda t: 0.68 * smooth(t, 0.9, 2.7) * (1 - smooth(t, 4.0, 4.8)))
    add_shape("Canvas / inpaint viewport frame", "canvas / inpaint frame",
              123, 10, 20, 10, 950, 750, "#00000000", "rounded-rectangle",
              0.02, "#55c6d4df", _stroke_unit())
    label_refs = {}
    for name, label, x, y, color in (
        ("before", "BEFORE / ORIGINAL RESTORED", 40, 30, "#F3111B29"),
        ("after", "AFTER / OBJECT REMOVED", 760, 30, "#F3111B29"),
    ):
        panel("Canvas / inpaint " + name + " badge", 123, 10, x, y,
              340 if name == "before" else 260, 42, color, "#00000000", 0)
        label_refs[name] = add_text("Canvas / inpaint " + name + " label", label,
                                    "canvas / labels", 123, 10, x + 14, y + 7,
                                    16, "Space Mono", "#f1efdf", 1.1)
    set_opacity("Canvas / inpaint before label hold", label_refs["before"], 10,
                lambda t: smooth(t, 6.0, 6.7) * (1 - smooth(t, 8.3, 8.9)))
    set_opacity("Canvas / inpaint after label", label_refs["after"], 10,
                lambda t: smooth(t, 4.1, 4.9) *
                (1 - smooth(t, 6.0, 6.7) * (1 - smooth(t, 8.3, 8.9))))
    panel("Canvas / inpaint step badge", 123, 10, 45, 710, 560, 46,
          "#E8132332", "#44c6d4df", 8)
    inpaint_states = [
        ("1 / CHOOSE THE DISTRACTION", 0.0, 0.9),
        ("2 / PAINT THE SELECTION", 0.9, 3.4),
        ("3 / INPAINT SELECTION", 3.4, 4.8),
        ("LOCAL FILL / OUTSIDE PIXELS RETAINED", 4.8, 6.0),
        ("COMPARE ORIGINAL / HOLD", 6.0, 8.9),
        ("LOCAL FILL / OUTSIDE PIXELS RETAINED", 8.9, 10.0),
    ]
    for i, (label, begin, end) in enumerate(inpaint_states):
        state_clip = add_text(f"Canvas / inpaint step state {i}", label,
                              "canvas / labels", 123, 10, 60, 722,
                              15, "Space Mono", "#f1efdf", 1.2)
        set_opacity(f"Canvas / inpaint state timing {i}", state_clip, 10,
                    lambda t, a=begin, b=end: 1.0 if a <= t < b else 0.0)
    add_text("Canvas / inpaint footer", "SELECT → INPAINT → COMPARE. KEEP THE ORIGINAL.",
             "canvas / labels", 123, 10, 20, 790, 15, "Space Mono", "#d7ff48", 1.8)
    cursor = add_shape("Canvas / inpaint brush cursor", "canvas / inpaint cursor",
                       123, 3.2, 469, 233, 52, 52,
                       "#00000000", "ellipse", 0.5, "#ffffffff",
                       2 * _stroke_unit(), opacity=0.0)
    draw_cursor = lambda t: smooth(t, 0.9, 2.7)
    animate_many("Canvas / inpaint cursor movement", cursor, 3.2, {
        "titlePositionX": lambda _t: stage_values(495, 259, 0, 0)[0] / W,
        "titlePositionY": lambda t: stage_values(495, 259 + 253 * draw_cursor(t), 0, 0)[1] / H,
        "opacity": lambda t: smooth(t, 0.8, 1.0) * (1 - smooth(t, 3.0, 3.2)),
    })

    # 03.5: fully editable campaign poster and product cutout.
    create_campaign_poster("Canvas Campaign Poster", 133, 9, 210, -10,
                           0.6, brush=True, dynamic_focus=lambda t:
                               smooth(t, 5.2, 6.4) * (1 - smooth(t, 7.6, 8.8)),
                           type_phase="display")
    pills("Canvas / campaign brush", ["BRUSH / TEXTURED", "SIZE 180", "PAINT LAYER"],
          134.1, 4.4, 25, 660, focus=lambda t:
              smooth(t, 5.2, 6.4) * (1 - smooth(t, 7.6, 8.8)))

    # 03.6: source type switch, opacity edit, and independent layer rows.
    create_campaign_poster("Canvas Campaign Type", 142, 8, 180, -10,
                           0.6, brush=True, dynamic_focus=None,
                           type_phase="switch", brush_opacity=lambda t:
                               1 - 0.38 * smooth(t, 5.1, 6.1) *
                               (1 - smooth(t, 6.4, 7.5)))
    panel("Canvas / campaign type inspector", 142, 5, 605, 255,
          370, 355, "#F5111B28", "#55AEB DCA".replace(" ", ""), 12)
    add_text("Canvas / campaign selected type", "SELECTED TYPE / STAY",
             "canvas / labels", 142, 5, 627, 278, 16, "Space Mono", "#d7ff48", 1.5)
    for i, (name, family) in enumerate((("STAY", "Instrument Serif"),
                                        ("STAY", "Space Grotesk"),
                                        ("STAY", "Anton"))):
        choice = add_text(f"Canvas / campaign type choice {i}", name,
                          "canvas / labels", 142, 8, 625, 316 + i * 68,
                          34, family, "#f1efdf")
        left, right = ((0.0, 2.5), (2.5, 4.6), (4.6, 8.0))[i]
        set_opacity(f"Canvas / campaign type choice opacity {i}", choice, 8,
                    lambda t, a=left, b=right:
                        (smooth(t, a, min(b, a + 0.35)) if a > 0 else 1.0) *
                        (1 - smooth(t, max(a, b - 0.35), b) if b < 8 else 1.0))
    panel("Canvas / campaign layers panel", 142, 8, 605, 255,
          370, 355, "#F5111B28", "#55AEB DCA".replace(" ", ""), 12)
    add_text("Canvas / campaign layers heading", "LAYERS / PAINT",
             "canvas / labels", 147, 3, 627, 278, 16, "Space Mono", "#d7ff48", 1.5)
    for i, row in enumerate(("Typography", "Product cutout", "Paint layer")):
        add_shape(f"Canvas / campaign layer row {i}", "canvas / layer rows",
                  147, 3, 625, 316 + i * 62, 325, 54,
                  "#334856" if i == 2 else "#121c28", "rounded-rectangle", 0.08)
        add_text(f"Canvas / campaign layer name {i}", row, "canvas / labels",
                 147, 3, 640, 327 + i * 62, 22, "Space Grotesk", "#f1efdf")
    add_text("Canvas / paint opacity readout", "PAINT OPACITY / 62%",
             "canvas / labels", 147, 3, 627, 522, 13, "Space Mono", "#b8c8d1")

    # 03.7: distinct 16:9 campaign thumbnail artwork.
    create_campaign_thumbnail("Canvas Campaign Thumbnail", 150, 10,
                              x=0, y=80, focus=lambda t:
                                  smooth(t, 1.4, 2.8) * (1 - smooth(t, 8.5, 9.8)))
    pills("Canvas / thumbnail format", ["16:9 / THUMBNAIL", "WEBSITE GRAPHIC", "EDITABLE TYPE & LAYERS"],
          151.3, 7.0, 20, 720, focus=lambda t:
              smooth(t, 1.4, 2.8) * (1 - smooth(t, 8.5, 9.8)))
    caption_and_footer_canvas()


def create_hiker_poster(prefix: str, start: float, duration: float,
                        x: float, y: float, scale: float,
                        label: str, typing: bool = False,
                        focus: Callable[[float], float] | None = None,
                        foreground_front: bool = True,
                        type_color: str = "#f1efdf"):
    # Artwork order mirrors HikerPoster: photo, quiet top/bottom treatment,
    # metadata, serif line, behind/front cutout, display headline, footer.
    add_shape(prefix + " / poster base", "poster / base", start, duration,
              x, y, 1000, 1250, "#142537", "rectangle", inner_scale=scale,
              focus=focus)
    photo = place_image(prefix + " / source photograph", "studio/subject-lift.png",
                        "poster / photograph", start, duration,
                        x, y, 1000, 1250, fit="fill", inner_scale=scale,
                        focus=focus, crop=(0.0, 1 / 12, 0.0, 1 / 12))
    # The source uses a top/bottom CSS gradient over the full photograph.
    add_shape(prefix + " / dark photo treatment", "poster / treatment",
              start, duration, x, y, 1000, 1250, "#36061A2A", "rectangle",
              inner_scale=scale, focus=focus)
    add_shape(prefix + " / top rule", "poster / rules", start, duration,
              x + 65 * scale, y + 50 * scale, 870, 2, "#99EDF0DF", "rectangle",
              inner_scale=scale, focus=focus)
    add_text(prefix + " / metadata left", "FIELD NOTES / NEPAL",
             "poster / metadata", start, duration, x + 65 * scale, y + 68 * scale,
             18, "Space Mono", "#f1efdf", 0.8, inner_scale=scale, focus=focus)
    add_text(prefix + " / metadata right", "27° N / 2026",
             "poster / metadata", start, duration,
             x + 935 * scale, y + 68 * scale, 18, "Space Mono", "#f1efdf", 0.8,
             alignment="right", inner_scale=scale, focus=focus)
    serif_line = add_text(prefix + " / serif caption", "A little",
                          "poster / typography", start, duration,
                          x + 55 * scale, y + 220 * scale, 130, "Instrument Serif",
                          "#f1efdf", 0, 1.0, italic=True,
                          inner_scale=scale, focus=focus)
    if typing:
        set_opacity(prefix + " / serif entrance", serif_line, duration,
                    lambda t: smooth(t, 0.1, 1.1))
    cutout_under = place_image(prefix + " / subject cutout behind type",
                               "creative-polish-v2/subject-lift-alpha.png",
                               "poster / subject below typography", start, duration,
                               x, y, 1000, 1250, fit="fill", inner_scale=scale,
                               focus=focus, crop=(0.0, 1 / 12, 0.0, 1 / 12))
    if foreground_front:
        set_opacity(prefix + " / behind-layer switch", cutout_under, duration,
                    lambda t: smooth(t, 9.1, 9.8))
    text = add_text(prefix + " / display headline", "FURTHER.",
                    "poster / typography", start, duration,
                    x + 42 * scale, y + 605 * scale, 220, "Anton", type_color, -4,
                    line_height=1.0, inner_scale=scale, focus=focus,
                    shadow=True, shadow_color="#bf000000", shadow_x=5,
                    shadow_y=12)
    if typing:
        set_opacity(prefix + " / type build", text, duration,
                    lambda t: smooth(t, 1.5, 2.25))
    if typing:
        lime_text = add_text(prefix + " / lime headline", "FURTHER.",
                             "poster / typography foreground", start, duration,
                             x + 42 * scale, y + 605 * scale, 220, "Anton", "#d7ff48", -4,
                             line_height=1.0, inner_scale=scale, focus=focus,
                             shadow=True, shadow_color="#bf000000",
                             shadow_x=5, shadow_y=12)
        set_opacity(prefix + " / lime title fill", text, duration,
                    lambda t: (smooth(t, 1.5, 2.25) *
                               (1 - smooth(t, 4.0, 4.7) *
                                (1 - smooth(t, 6.0, 6.5)))) if typing else 0.0)
        set_opacity(prefix + " / lime title highlight", lime_text, duration,
                    lambda t: smooth(t, 4.0, 4.7) * (1 - smooth(t, 6.0, 6.5)))
    cutout_over = place_image(prefix + " / subject cutout above type",
                              "creative-polish-v2/subject-lift-alpha.png",
                              "poster / subject above typography", start, duration,
                              x, y, 1000, 1250, fit="fill", inner_scale=scale,
                              focus=focus, crop=(0.0, 1 / 12, 0.0, 1 / 12))
    if not foreground_front:
        set_opacity(prefix + " / upper cutout hidden", cutout_over, duration,
                    lambda _t: 0.0)
    else:
        set_opacity(prefix + " / upper cutout reorder", cutout_over, duration,
                    lambda t: 1 - smooth(t, 9.1, 9.8))
    if not typing and foreground_front:
        set_opacity(prefix + " / initial foreground cutout", cutout_over,
                    duration, lambda _t: 1.0)
        set_opacity(prefix + " / initial under cutout", cutout_under,
                    duration, lambda _t: 0.0)
    bottom_rule = add_shape(prefix + " / bottom rule", "poster / rules",
                            start, duration, x + 65 * scale, y + 1183 * scale,
                            870, 2, "#88EDF0DF", "rectangle",
                            inner_scale=scale, focus=focus)
    add_text(prefix + " / footer left", "GO FIND YOUR OWN WAY.",
             "poster / metadata", start, duration,
             x + 65 * scale, y + 1207 * scale, 16, "Space Mono", "#f1efdf", 0.5,
             inner_scale=scale, focus=focus)
    add_text(prefix + " / footer right", "↗ KEEP THE CURIOSITY",
             "poster / metadata", start, duration,
             x + 935 * scale, y + 1207 * scale, 16, "Space Mono", "#f1efdf", 0.5,
             alignment="right", inner_scale=scale, focus=focus)


def create_campaign_poster(prefix: str, start: float, duration: float,
                           x: float, y: float, scale: float,
                           brush: bool, dynamic_focus=None,
                           type_phase: str = "display",
                           brush_opacity: Callable[[float], float] | None = None):
    # CampaignPoster is decomposed into independent native shape, title, and
    # alpha-cutout clips in the same bottom-to-top order as the source JSX.
    px = lambda value: x + value * scale
    py = lambda value: y + value * scale
    add_shape(prefix + " / lime stock", "campaign / stock", start, duration,
              x, y, 1000, 1250, "#dfff43", "rectangle",
              focus=dynamic_focus, inner_scale=scale)
    disc = add_shape(prefix + " / blue disc", "campaign / blue disc", start, duration,
                     px(350), py(180), 950, 950, "#2545d5", "ellipse", 0.5,
                     focus=dynamic_focus, inner_scale=scale)
    if type_phase == "display":
        def disc_center_x(t: float) -> float:
            local_x = px(350 + 500 * (1 - out(t, 0.1, 1.4)))
            focus_value = dynamic_focus(t) if dynamic_focus else 0.0
            rect = stage_values(local_x, py(180), 950 * scale, 950 * scale, focus_value)
            return (rect[0] + rect[2] / 2) / W

        animate(prefix + " / blue disc entrance", disc, "titlePositionX", duration,
                disc_center_x)
    set_stage_clip_mask(prefix + " / blue disc poster mask", disc,
                        x, y, 1000 * scale, 1250 * scale, duration,
                        dynamic_focus)
    if brush:
        def brush_progress(t: float) -> float:
            return smooth(t, 1.4, 4.3) if type_phase == "display" else 1.0

        def brush_under_coordinates(t: float) -> list[float]:
            progress = brush_progress(t)
            return [px(-120), py(955),
                    px(-120 + 1220 * progress), py(955 - 555 * progress)]

        stroke = add_vector_path(
            prefix + " / editable orange brush path", "campaign / paint",
            start, duration,
            [{"verb": "M", "points": [px(-120), py(955)]},
             {"verb": "L", "points": [px(1100), py(400)]}],
            brush_under_coordinates, "#ee6d33", 180 * scale,
            focus=dynamic_focus,
            opacity_function=lambda t: (
                (1.0 if brush_progress(t) > 0.0 else 0.0) *
                (brush_opacity(t) if brush_opacity is not None else 1.0)))
        set_stage_clip_mask(prefix + " / brush poster mask", stroke,
                            x, y, 1000 * scale, 1250 * scale, duration,
                            dynamic_focus)

        def brush_highlight_coordinates(t: float) -> list[float]:
            progress = brush_progress(t)
            return [px(-80), py(1000),
                    px(-80 + 1180 * progress), py(1000 - 555 * progress)]

        highlight = add_vector_path(
            prefix + " / editable orange brush highlight", "campaign / paint highlight",
            start, duration,
            [{"verb": "M", "points": [px(-80), py(1000)]},
             {"verb": "L", "points": [px(1100), py(445)]}],
            brush_highlight_coordinates, "#ff8e42", 4 * scale,
            focus=dynamic_focus,
            opacity_function=lambda t: (
                (1.0 if brush_progress(t) > 0.0 else 0.0) *
                (brush_opacity(t) if brush_opacity is not None else 1.0)))
        set_stage_clip_mask(prefix + " / brush highlight poster mask", highlight,
                            x, y, 1000 * scale, 1250 * scale, duration,
                            dynamic_focus)
    else:
        add_shape(prefix + " / thumbnail blue panel", "campaign / paint",
                  start, duration, px(470), py(-200), 800, 1000,
                  "#263ed2", "rectangle", focus=dynamic_focus,
                  inner_scale=scale, rotation=18)

    add_shape(prefix + " / header rule", "campaign / rules", start, duration,
              px(55), py(62), 890, 2, "#142041", "rectangle",
              inner_scale=scale, focus=dynamic_focus)
    add_text(prefix + " / header left", "OFF THE CLOCK / OUTSIDE",
             "campaign / labels", start, duration, px(55), py(40),
             18, "Space Mono", "#142041", 2.0, inner_scale=scale,
             focus=dynamic_focus)
    add_text(prefix + " / header right", "FIELD EQUIPMENT · 01",
             "campaign / labels", start, duration, px(945), py(40),
             18, "Space Mono", "#142041", 2.0, alignment="right",
             inner_scale=scale, focus=dynamic_focus)

    if type_phase == "switch":
        styles = [("Instrument Serif", 0, 2.5),
                  ("Space Grotesk", 2.5, 4.6),
                  ("Anton", 4.6, duration)]
        for i, (family, left, right) in enumerate(styles):
            clip = add_text(prefix + f" / stay type choice {i}", "STAY",
                            "campaign / typography", start, duration,
                            px(32), py(88), 300, family,
                            "#142041", 0.0 if i < 2 else -7.0,
                            line_height=1.0, inner_scale=scale,
                            focus=dynamic_focus)
            set_opacity(prefix + f" / stay type choice {i} opacity", clip,
                        duration,
                        lambda t, a=left, b=right: (
                            (smooth(t, a, min(b, a + 0.35)) if a > 0 else 1.0) *
                            (1 - smooth(t, max(a, b - 0.35), b) if b < duration else 1.0)))
    else:
        stay = add_text(prefix + " / STAY title", "STAY",
                        "campaign / typography", start, duration,
                        px(32), py(88), 300, "Anton", "#142041", -7,
                        line_height=1.0, inner_scale=scale,
                        focus=dynamic_focus)
        set_opacity(prefix + " / STAY build", stay, duration,
                    lambda t: out(t, 0.1, 1.4))
    add_text(prefix + " / in serif", "in", "campaign / typography", start,
             duration, px(77), py(335), 165, "Instrument Serif",
             "#f1efdf", 0, line_height=1.0, italic=True,
             inner_scale=scale, focus=dynamic_focus)
    product = place_image(prefix + " / isolated trail shoe",
                          "creative-launch/trail-shoe-cutout.png",
                          "campaign / product cutout", start, duration,
                          px(-30), py(365), 1120, 840, fit="fit",
                          rotation=-8, inner_scale=scale,
                          focus=dynamic_focus)
    set_stage_clip_mask(prefix + " / product poster mask", product,
                        x, y, 1000 * scale, 1250 * scale, duration,
                        dynamic_focus)
    if type_phase == "display":
        def shoe_rect(t: float):
            build = out(t, 0.1, 1.4) if brush else 1.0
            local_x = px(-30 + 150 * (1 - build))
            local_y = py(365 - 180 * (1 - build))
            focus_value = dynamic_focus(t) if dynamic_focus else 0.0
            return stage_values(local_x, local_y, 1120 * scale, 840 * scale, focus_value)

        animate_many(prefix + " / shoe transform", product, duration, {
            "frameOffsetX": lambda t: 4 * ((shoe_rect(t)[0] + shoe_rect(t)[2] / 2) / W - 0.5),
            "frameOffsetY": lambda t: 4 * ((shoe_rect(t)[1] + shoe_rect(t)[3] / 2) / H - 0.5),
            "frameRotationDegrees": lambda t: -22 + 14 * out(t, 0.1, 1.4) if brush else -8,
        })
    if type_phase == "switch":
        set_opacity(prefix + " / product paint opacity", product, duration,
                    lambda t: 1 - 0.38 * smooth(t, 5.1, 6.1) *
                    (1 - smooth(t, 6.4, 7.5)))
    motion = add_text(prefix + " / MOTION title", "MOTION.",
                      "campaign / typography", start, duration,
                      px(35), py(982), 218, "Anton", "#f1efdf", -4,
                      line_height=1.0, inner_scale=scale,
                      focus=dynamic_focus)
    if type_phase == "display":
        set_opacity(prefix + " / MOTION build", motion, duration,
                    lambda t: out(t, 0.1, 1.4))
    add_shape(prefix + " / footer rule", "campaign / rules", start,
              duration, px(55), py(1195), 890, 2, "#142041",
              "rectangle", focus=dynamic_focus, inner_scale=scale)
    add_text(prefix + " / footer left", "GO FIND YOUR OWN WAY.",
             "campaign / labels", start, duration,
             px(55), py(1215), 17, "Space Mono", "#142041", 0,
             inner_scale=scale, focus=dynamic_focus)
    add_text(prefix + " / footer right", "↗ KEEP MOVING",
             "campaign / labels", start, duration,
             px(945), py(1215), 17, "Space Mono", "#142041", 0,
             alignment="right", inner_scale=scale, focus=dynamic_focus)


def _quadratic_point(p0: tuple[float, float], p1: tuple[float, float],
                     p2: tuple[float, float], u: float) -> tuple[float, float]:
    v = 1.0 - u
    return (v * v * p0[0] + 2.0 * v * u * p1[0] + u * u * p2[0],
            v * v * p0[1] + 2.0 * v * u * p1[1] + u * u * p2[1])


def _quadratic_parameter_for_length_fraction(p0: tuple[float, float],
                                              p1: tuple[float, float],
                                              p2: tuple[float, float],
                                              fraction: float) -> float:
    """Invert the quadratic's arc length so pathLength=1 dash reveal matches SVG."""
    if fraction <= 0.0:
        return 0.0
    if fraction >= 1.0:
        return 1.0
    table_size = 512
    points = [_quadratic_point(p0, p1, p2, i / table_size)
              for i in range(table_size + 1)]
    cumulative = [0.0]
    for a, b in zip(points, points[1:]):
        cumulative.append(cumulative[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    target = cumulative[-1] * fraction
    for index in range(1, len(cumulative)):
        if cumulative[index] >= target:
            prior, current = cumulative[index - 1], cumulative[index]
            within = 0.0 if current <= prior else (target - prior) / (current - prior)
            return ((index - 1) + within) / table_size
    return 1.0


def create_campaign_thumbnail(prefix: str, start: float, duration: float,
                              x: float, y: float, focus=None):
    add_shape(prefix + " / paper background", "thumbnail / background",
              start, duration, x, y, 1000, 562, "#f0ead8", "rectangle",
              focus=focus)
    blue_panel = add_shape(prefix + " / blue panel", "thumbnail / blue panel", start,
                           duration, x + 470, y - 200, 800, 1000, "#263ed2",
                           "rectangle", focus=focus, rotation=18)
    set_stage_clip_mask(prefix + " / thumbnail blue panel mask", blue_panel,
                        x, y, 1000, 562, duration, focus)
    p0, p1, p2 = (470.0, 620.0), (550.0, 80.0), (1020.0, 80.0)
    def thumbnail_path_coordinates(t: float) -> list[float]:
        progress = out(t, 0.1, 1.9)
        u = _quadratic_parameter_for_length_fraction(p0, p1, p2, progress)
        control = (p0[0] + (p1[0] - p0[0]) * u,
                   p0[1] + (p1[1] - p0[1]) * u)
        endpoint = _quadratic_point(p0, p1, p2, u)
        return [x + p0[0], y + p0[1],
                x + control[0], y + control[1],
                x + endpoint[0], y + endpoint[1]]

    brush = add_vector_path(
        prefix + " / editable yellow Bezier sweep", "thumbnail / paint",
        start, duration,
        [{"verb": "M", "points": [x + p0[0], y + p0[1]]},
         {"verb": "Q", "points": [x + p1[0], y + p1[1],
                                   x + p2[0], y + p2[1]]}],
        thumbnail_path_coordinates, "#dbff43", 70, focus=focus,
        opacity_function=lambda t: 1.0 if out(t, 0.1, 1.9) > 0.0 else 0.0)
    set_stage_clip_mask(prefix + " / thumbnail brush mask", brush,
                        x, y, 1000, 562, duration, focus)
    circle = add_shape(prefix + " / circular detail", "thumbnail / details", start,
                       duration, x + 610, y + 55, 420, 420, "#00000000", "ellipse",
                       0.5, "#4af0ead8", 2 * _stroke_unit(), focus=focus)
    set_stage_clip_mask(prefix + " / thumbnail circle mask", circle,
                        x, y, 1000, 562, duration, focus)
    add_text(prefix + " / eyebrow", "OUTSIDE / A FIELD TEST",
             "thumbnail / text", start, duration, x + 40, y + 22, 15,
             "Space Mono", "#132044", 1.0, inner_scale=1.0, focus=focus)
    add_text(prefix + " / headline", "OUT\nTHERE.", "thumbnail / text",
             start, duration, x + 30, y + 77, 176, "Anton", "#132044",
             -3, line_height=0.95, focus=focus)
    shoe = place_image(prefix + " / shoe cutout", "creative-launch/trail-shoe-cutout.png",
                       "thumbnail / shoe", start, duration,
                       x + 360, y + 38, 720, 540, fit="fit", rotation=-23,
                       focus=focus)
    set_stage_clip_mask(prefix + " / thumbnail shoe mask", shoe,
                        x, y, 1000, 562, duration, focus)
    set_opacity(prefix + " / shoe build", shoe, duration,
                lambda t: out(t, 0.1, 1.9))
    add_text(prefix + " / supporting serif", "Take the long way.",
             "thumbnail / text", start, duration, x + 38, y + 500,
             39, "Instrument Serif", "#132044", 0, italic=True,
             focus=focus)


def create_agent_chapter():
    light = add("Agent / additive field", "createTimelineGenerator",
                trackId=track("agent / light field"), generatorKind="desktop-light",
                startSeconds=252, durationSeconds=28,
                parameters={"timeOffsetSeconds": 252, "timeScale": 1.0,
                            "gather": 0.6, "opacity": 0.38})
    # Stage phase 06.0: local model invitation screen and source photographs.
    panel("Agent / monitor outer frame", 252, 7, 65, 85, 870, 565,
          "#F5111B2A", "#5CA4B3C3", 25)
    panel("Agent / monitor glass", 252, 7, 100, 125, 800, 480,
          "#FF0A111C", "#00000000", 12)
    add_shape("Agent / monitor stand", "agent / device", 252, 7,
              350, 650, 300, 80, "#26374a", "rounded-rectangle", 0.08)
    progress = add_shape("Agent / local connection progress", "agent / device", 252, 7,
                         290, 350, 420, 3, "#d7ff48")
    animate("Agent / connection progress", progress, "frameScale", 7,
            lambda t: max(0.05, out(t, 0.5, 2.0)))
    panel("Agent / local model card", 252, 7, 390, 285, 220, 140,
          "#FF28485D", "#FFd7ff48", 18)
    add_text("Agent / model label", "LOCAL\nMODEL", "agent / labels",
             252, 7, 500, 318, 23, "Space Mono", "#d7ff48", 0,
             line_height=1.1, alignment="center")
    original_sample = place_image("Agent / original sample", "studio/color/4-8.png",
                                  "agent / samples", 252, 7, 125, 250, 220, 220,
                                  fit="fill", radius=0.055,
                                  crop=(0.0, 1 / 6, 0.0, 1 / 6))
    set_opacity("Agent / original sample reveal", original_sample, 7,
                lambda t: out(t, 0.4, 1.0))
    result_sample = place_image("Agent / extracted subject sample", "studio/subject-lift.png",
                                "agent / result sample", 252, 7, 660, 250, 220, 220,
                                fit="fill", radius=0.055,
                                crop=(0.0, 1 / 6, 0.0, 1 / 6))
    set_opacity("Agent / result sample reveal", result_sample, 7,
                lambda t: out(t, 1.3, 2.0))
    pills("Agent / local model values",
          ["OPTIONAL", "RUNS ON YOUR MACHINE", "YOUR ORIGINALS"],
          252, 7, 110, 750)

    # Stage phase 06.1: a reviewable, source-derived agent proposal card.
    panel("Agent / proposal card", 259, 7, 60, 65, 880, 700,
          "#ED101B29", "#5CA2B4C4", 18)
    add_text("Agent / proposal heading", "EXAMPLE PROPOSAL / REVIEW BEFORE RUNNING",
             "agent / labels", 259, 7, 95, 100, 17, "Space Mono", "#d7ff48", 1.4)
    proposal_rows = [
        ("Lift the subject by +0.55 EV", "studio/subject-lift.png"),
        ("Place the title behind the subject", "creative-polish-v2/subject-lift-alpha.png"),
        ("Keep the photo, mask and type separate", "studio/subject.png"),
    ]
    for i, (row, source) in enumerate(proposal_rows):
        row_y = 145 + i * 158
        reveal = lambda t, j=i: out(t, 0.4 + j * 0.35, 1.1 + j * 0.35)
        if i == 1:
            # Reconstruct the mini poster from its original photograph and
            # type, then crop it to the same 150x100 proposal thumbnail.
            mini = place_image("Agent / proposal mini poster photo", "studio/subject-lift.png",
                               "agent / proposal mini poster", 259, 7,
                               95, row_y - 50, 1000, 1250, fit="fill",
                               crop=(0.0, 1 / 12, 0.0, 1 / 12), inner_scale=0.15)
            set_opacity("Agent / proposal mini poster entrance", mini, 7, reveal)
            mini_cutout = place_image("Agent / proposal mini poster cutout",
                                      "creative-polish-v2/subject-lift-alpha.png",
                                      "agent / proposal mini cutout", 259, 7,
                                      95, row_y - 50, 1000, 1250, fit="fill",
                                      crop=(0.0, 1 / 12, 0.0, 1 / 12), inner_scale=0.15)
            set_opacity("Agent / proposal mini cutout entrance", mini_cutout, 7, reveal)
            mini_title = add_text("Agent / proposal mini poster title", "FURTHER.",
                                  "agent / proposal mini text", 259, 7,
                                  95 + 42 * 0.15, row_y - 50 + 605 * 0.15,
                                  220, "Anton", "#f1efdf", -4,
                                  line_height=1.0, inner_scale=0.15)
            set_opacity("Agent / proposal mini title entrance", mini_title, 7, reveal)
            set_sequence_mask("Agent / mini poster thumbnail mask", mini,
                              95, row_y, 150, 100, 0.08)
            set_sequence_mask("Agent / mini cutout thumbnail mask", mini_cutout,
                              95, row_y, 150, 100, 0.08)
            set_sequence_mask("Agent / mini title thumbnail mask", mini_title,
                              95, row_y, 150, 100, 0.08)
        else:
            thumbnail = place_image(f"Agent / proposal thumbnail {i}", source,
                                     f"agent / proposal image {i}", 259, 7,
                                     95, row_y, 150, 100, fit="fill", radius=0.08,
                                     crop=(0.0, 0.278, 0.0, 0.278))
            set_opacity(f"Agent / proposal thumbnail entrance {i}", thumbnail, 7, reveal)
        proposal_text = add_text(f"Agent / proposal text {i}", row, "agent / labels",
                                 259, 7, 270, row_y + 8, 29, "Space Grotesk", "#f1efdf")
        set_opacity(f"Agent / proposal text entrance {i}", proposal_text, 7, reveal)
        review_note = add_text(f"Agent / proposal review note {i}", "REVIEW THE PROPOSAL",
                               "agent / labels", 259, 7, 270, row_y + 54,
                               13, "Space Mono", "#9bafc1", 1)
        set_opacity(f"Agent / proposal note entrance {i}", review_note, 7, reveal)
    pills("Agent / proposal choices", ["CHOOSE THE SCOPE", "KEEP THE CONTROL"],
          259, 7, 95, 700)

    # Stage phase 06.2: native poster layers and editable layer stack.
    create_hiker_poster("Agent / poster", 266, 7, 190, -10, 0.6,
                        label="LAYERS / EDITABLE", foreground_front=True)
    panel("Agent / layer inspector", 266, 7, 610, 285, 350, 285,
          "#F2121C28", "#4AA5B6C5", 12)
    add_text("Agent / layers heading", "LAYERS / EDITABLE", "agent / labels",
             266, 7, 632, 307, 16, "Space Mono", "#d7ff48", 1.5)
    rows = ("Typography", "Subject cutout", "Color adjustment", "Original photograph")
    for i, row in enumerate(rows):
        add_shape(f"Agent / layer row base {i}", "agent / layer row bases", 266, 7,
                  620, 338 + 58 * i, 330, 52, "#121c28", "rounded-rectangle", 0.08)
        active_row = add_shape(f"Agent / selected row {i}", "agent / layer selection", 266, 7,
                               620, 338 + 58 * i, 330, 52,
                               "#354b5c", "rounded-rectangle", 0.08)
        set_opacity(f"Agent / selected row {i} active", active_row, 7,
                    lambda t, j=i: 1.0 if min(3, math.floor(t / 1.7)) == j else 0.0)
        add_text(f"Agent / layer name {i}", row, "agent / labels", 266, 7,
                 632, 350 + 58 * i, 21, "Space Grotesk", "#f1efdf")

    # Stage phase 06.3: original and refined image layers, with exact history.
    place_image("Agent / original history photo", "studio/color/4-8.png",
                "agent / history photo", 273, 7, 230, -10, 530, 795,
                fit="fill", radius=0.012)
    refined = place_image("Agent / subject-light history edit", "studio/subject-lift.png",
                          "agent / history edit", 273, 7, 230, -10, 530, 795,
                          fit="fill", radius=0.012, opacity=0.0)
    set_source_matte("Agent / history subject alpha matte", refined,
                     "studio/subject.png", "alpha")
    animate("Agent / history exposure", refined, "opacity", 7,
            lambda t: [0.0, 0.35 / 0.55, 1.0][0 if t < 2 else 2 if t < 4.5 else 1])
    panel("Agent / edit history card", 273, 7, 45, 580, 910, 190,
          "#F5111E2B", "#55ACBCC7", 12)
    add_text("Agent / edit history heading", "EDIT HISTORY / SUBJECT EXPOSURE +0.00 EV",
             "agent / labels", 273, 7, 70, 605, 17, "Space Mono", "#d7ff48", 1.3)
    history = [("Original mask", "0.00 EV"),
               ("Your refinement", "+0.35 EV"),
               ("Subject lift", "+0.55 EV")]
    tile_w = 270
    for i, (name, value) in enumerate(history):
        tx = 70 + i * 290
        add_shape(f"Agent / history tile base {i}", "agent / history tile bases",
                  273, 7, tx, 660, tile_w, 85, "#283c50",
                  "rounded-rectangle", 0.08)
        active_tile = add_shape(f"Agent / history selected fill {i}", "agent / history tiles",
                                273, 7, tx, 660, tile_w, 85, "#d7ff48",
                                "rounded-rectangle", 0.08)
        set_opacity(f"Agent / history selected fill opacity {i}", active_tile, 7,
                    lambda t, j=i: 1.0 if ((t < 2 and j == 0) or
                                           (2 <= t < 4.5 and j == 2) or
                                           (t >= 4.5 and j == 1)) else 0.0)
        add_text(f"Agent / history name inactive {i}", name, "agent / labels",
                 273, 7, tx + 14, 673, 20, "Space Grotesk", "#f1efdf")
        active_name = add_text(f"Agent / history name active {i}", name, "agent / labels",
                               273, 7, tx + 14, 673, 20, "Space Grotesk", "#152335")
        set_opacity(f"Agent / history active name opacity {i}", active_name, 7,
                    lambda t, j=i: 1.0 if ((t < 2 and j == 0) or
                                           (2 <= t < 4.5 and j == 2) or
                                           (t >= 4.5 and j == 1)) else 0.0)
        add_text(f"Agent / history value inactive {i}", value, "agent / labels",
                 273, 7, tx + 14, 706, 14, "Space Mono", "#f1efdf")
        active_value = add_text(f"Agent / history value active {i}", value, "agent / labels",
                                273, 7, tx + 14, 706, 14, "Space Mono", "#152335")
        set_opacity(f"Agent / history active value opacity {i}", active_value, 7,
                    lambda t, j=i: 1.0 if ((t < 2 and j == 0) or
                                           (2 <= t < 4.5 and j == 2) or
                                           (t >= 4.5 and j == 1)) else 0.0)
    for i, (label, begin, end) in enumerate((
            ("EDIT HISTORY / SUBJECT EXPOSURE +0.00 EV", 0.0, 2.0),
            ("EDIT HISTORY / SUBJECT EXPOSURE +0.55 EV", 2.0, 4.5),
            ("EDIT HISTORY / SUBJECT EXPOSURE +0.35 EV", 4.5, 7.0))):
        readout = add_text(f"Agent / history exposure readout {i}", label,
                           "agent / labels", 273, 7, 70, 605,
                           17, "Space Mono", "#d7ff48", 1.3)
        set_opacity(f"Agent / history readout timing {i}", readout, 7,
                    lambda t, a=begin, b=end: 1.0 if a <= t < b else 0.0)
    add_text("Agent / optional footer left", "OPTIONAL ASSISTANCE. EDITABLE WORK. RETAINED CONTROL.",
             "agent / footer left", 252, 28, 82, H - 34,
             17, "Space Mono", "#aeb6c3", 1.0, stage_space=False)
    add_text("Agent / optional footer right", "LOOKSY / LOCAL AGENT",
             "agent / footer right", 252, 28, W - 82, H - 34,
             17, "Space Mono", "#aeb6c3", 1.0, alignment="right",
             stage_space=False)

    agent_copy = [
        ("An extra pair\nof hands.\nIf you want it.",
         "Connect a model running on your machine. Ask for help with the work around your ideas."),
        ("Keep creating.\nShare the busywork.",
         "Ask for a subject lift and a layered poster. Review the proposed steps before running them."),
        ("The work stays\neditable.",
         "The same photographs, text, masks and adjustments remain available for your refinement."),
        ("Your call.\nAt every step.",
         "Review an adjustment. Return to an earlier state. Keep control of the result."),
    ]
    agent_edges = [0, 7, 14, 21, 28]
    for i, (a, b) in enumerate(zip(agent_edges, agent_edges[1:])):
        set_caption(20 + i, 252 + a, b - a,
                    "06 / OPTIONAL LOCAL AGENT", *agent_copy[i])


def _write_plan(orientation: str):
    global W, H, ORIENTATION, STEPS, _tracks, _ids
    ORIENTATION = orientation
    W, H = (1080, 1920) if orientation == "portrait" else (1920, 1080)
    native.WIDTH, native.HEIGHT = W, H
    native.STEPS = []
    STEPS = native.STEPS
    _tracks = {}
    _ids = set()
    create_canvas_chapter()
    create_agent_chapter()
    canonical_assets = [
        "studio/color/4-8.png",
        "studio/subject-lift.png",
        "studio/subject.png",
        "creative-polish-v2/subject-lift-alpha.png",
        "refine/surf.jpg",
        "refine/surf-final.png",
        "refine/surf-mask.png",
        "creative-launch/trail-shoe-cutout.png",
    ]
    return {
        "reference": {"landscape": "landscape.mp4", "portrait": "portrait.mp4"},
        "orientation": orientation,
        "composition": {"width": W, "height": H, "fps": 30},
        "chapters": [[92, 160], [252, 280]],
        "steps": native.STEPS,
        "supplementalAssets": [str((MEDIA / path).resolve()) for path in canonical_assets],
        "unresolvedParity": [
            "PaperGrain noise is not a native generator/effect in this authoring set.",
            "The SVG brush turbulence/displacement filter is not a native vector-path parameter; campaign stroke paths and their frame-sampled reveals are native editable paths.",
            "The inpaint viewport uses the exact starting crop, but its image zoom around the noncentral origin is not animated; animated sequence-space masks can clip a layer, not act as a nested poster group mask.",
            "Group transforms with perspective, including poster tilt and browser-style UI device perspectives, are represented by per-layer placement; perspective composition is not exact. Native compound clips create opaque MP4 previews and are not alpha-capable group renderers.",
            "Dynamic text typing, title shadow blur, image drop-shadow, card box-shadow, text-local rotation, checkerboard transparency, and PaperGrain are not native primitives. Title shadow supports color and X/Y offsets only; the native inpaint brush ring is an editable ellipse.",
            "Portrait plans use the source Stage/Caption/Footers layout branch, but the native output composition must be selected as 1080x1920 by the destination timeline/output variant.",
        ],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--orientation", choices=("landscape", "portrait"), default="landscape")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    payload = _write_plan(args.orientation)
    out_path = args.output or ROOT / f"canvas-agent-{args.orientation}.commands.json"
    out_path.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n")
    print(out_path, len(payload["steps"]), "native commands")
