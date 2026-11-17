"""Compile the complete native Photo chapter for both reference orientations.

The historical ``photo-landscape.commands.json`` remains untouched.  This
compiler extends its approved 12--39 chapter with the active pixel, orange-mix,
and delivery beats from creative-launch/Photo.tsx, and projects the same source
layout onto the 1080 x 1920 portrait Stage branch.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
import re
from pathlib import Path

import opening_plan as native

ROOT = Path(__file__).resolve().parent
MEDIA = ROOT.parent / "animated-trailer/public/media"
BASE_PLAN = ROOT / "photo-landscape.commands.json"
FPS = 30
INK = "#0b0d12"
PAPER = "#f1efdf"
LIME = "#d7ff48"
MUTED = "#c9ced6"
COPY = [
    ("Start with the\ncamera original.", "RAW photo editing. Your camera data, with room to refine."),
    ("Find your\nexposure.", "Recover the frame. Shape the light. Keep the original."),
    ("Protect the highlights.\nOpen the shadows.", "Refine the bright and dark regions independently."),
    ("Color, from\nthe ground up.", "A custom color engine, from RAW development to color-managed output."),
    ("Shape the\nwhole image.", "Move a shadow point. Lift the midtones. The photograph follows."),
]
RGB_BLOCK = re.compile(r"rgb\s*:\s*(\[[\s\S]*?\])\s*,\s*source\s*:")


class Plan:
    def __init__(self, orientation: str):
        self.orientation = orientation
        self.width, self.height = (1080, 1920) if orientation == "portrait" else (1920, 1080)
        self.stage_left, self.stage_top, self.stage_scale = (
            (40.0, 710.0, 1.0) if orientation == "portrait" else (650.0, 120.0, 1.1)
        )
        self.steps: list[dict] = []
        self.ids: set[str] = set()

    def unique(self, label: str) -> str:
        value = re.sub(r"[^a-zA-Z0-9_-]+", "-", label).strip("-").lower()
        if value not in self.ids:
            self.ids.add(value)
            return value
        index = 2
        while f"{value}-{index}" in self.ids:
            index += 1
        value = f"{value}-{index}"
        self.ids.add(value)
        return value

    def add(self, label: str, action: str, **values):
        step_id = self.unique(label)
        self.steps.append({"id": step_id, "commandId": "timeline.action." + action,
                           "values": values})
        return {"result": step_id, "field": "createdClipId"}

    def track(self, label: str):
        step_id = self.unique("track-" + label)
        self.steps.append({"id": step_id, "commandId": "timeline.action.createTimelineTrack",
                           "values": {"kind": "graphics", "name": label[:48], "audioRole": ""}})
        return {"result": step_id, "field": "trackId"}

    def world_rect(self, x: float, y: float, width: float, height: float):
        return (self.stage_left + self.stage_scale * x,
                self.stage_top + self.stage_scale * y,
                self.stage_scale * width, self.stage_scale * height)

    def shape(self, label: str, x: float, y: float, width: float, height: float,
              color: str, *, start: float, duration: float, shape: str = "rectangle",
              radius: float = 0, stroke: str = "#00000000", stroke_width: float = 0,
              stage: bool = False):
        if stage:
            x, y, width, height = self.world_rect(x, y, width, height)
        track = self.track("Photo / " + label)
        return self.add(label, "createTimelineGraphic", trackId=track,
                        shapeType=("rounded-rectangle" if radius > 0 else shape),
                        startSeconds=start, durationSeconds=duration,
                        values={"positionX": (x + width / 2) / self.width,
                                "positionY": (y + height / 2) / self.height,
                                "shapeWidth": max(width / self.width, 1.0 / self.width),
                                "shapeHeight": max(height / self.height, 1.0 / self.height),
                                "shapeCornerRadius": min(0.5, radius / max(1.0, min(width, height))),
                                "backgroundColor": color, "shapeStrokeColor": stroke,
                                "shapeStrokeWidth": stroke_width / min(self.width, self.height)})

    def image(self, label: str, source: str, start: float, duration: float,
              *, x: float, y: float, width: float, height: float,
              fit: str = "fill", radius: float = 0, rotation: float = 0,
              stage: bool = False):
        if stage:
            x, y, width, height = self.world_rect(x, y, width, height)
        track = self.track("Photo / " + label)
        clip = self.add(label, "createTimelineClip", trackId=track,
                        assetId="native-trailer:" + source,
                        filePath=str((MEDIA / source).resolve()), displayLabel=label,
                        durationSeconds=duration, trimStartSeconds=0,
                        trimEndSeconds=duration, startSeconds=start, posterSource="")
        self.frame(label + " / frame", clip, x=x, y=y, width=width, height=height,
                   fit=fit, radius=radius, rotation=rotation)
        return clip

    def title(self, label: str, content: str, x: float, top: float, size: float,
              start: float, duration: float, *, family: str = "Anton",
              color: str = PAPER, spacing: float = -1.8, line_height: float = 1.01,
              alignment: str = "left", weight: int = 400, italic: bool = False,
              stage: bool = False):
        line_count = max(1, len(content.split("\n")))
        if stage:
            x, top, _, _ = self.world_rect(x, top, 0, 0)
            size *= self.stage_scale
        track = self.track("Photo / " + label)
        return self.add(label, "createTimelineTitle", trackId=track,
                        templateKey="headline", startSeconds=start, durationSeconds=duration,
                        values={"text": content, "fontFamily": family, "fontSize": size,
                                "fontWeight": weight, "italic": italic,
                                "letterSpacing": spacing * (self.stage_scale if stage else 1.0),
                                "lineHeight": line_height, "color": color,
                                "backgroundColor": "#00000000", "alignment": alignment,
                                "positionX": x / self.width,
                                "positionY": (top + line_count * size * line_height / 2) / self.height})

    def generator(self, label: str, kind: str, parameters: dict, start: float,
                  duration: float, *, x: float, y: float, width: float, height: float,
                  fit: str = "fill", stage: bool = False):
        if stage:
            x, y, width, height = self.world_rect(x, y, width, height)
        track = self.track("Photo / " + label)
        clip = self.add(label, "createTimelineGenerator", trackId=track,
                        generatorKind=kind, startSeconds=start,
                        durationSeconds=duration, parameters=parameters)
        self.frame(label + " / frame", clip, x=x, y=y, width=width, height=height, fit=fit)
        return clip

    def frame(self, label: str, clip, *, x: float, y: float, width: float, height: float,
              fit: str = "fill", radius: float = 0, rotation: float = 0,
              opacity: float = 1.0, full_canvas: bool = False):
        values = {"offsetX": 4 * ((x + width / 2) / self.width - 0.5),
                  "offsetY": 4 * ((y + height / 2) / self.height - 0.5),
                  "scale": 1, "rotationDegrees": rotation, "perspectiveX": 0,
                  "perspectiveY": 0, "opacity": opacity, "objectFit": fit,
                  "width": 1.0 if full_canvas else width / self.width,
                  "height": 1.0 if full_canvas else height / self.height,
                  "cornerRadius": radius / max(1.0, min(width, height)),
                  "cropLeft": 0, "cropTop": 0, "cropRight": 0, "cropBottom": 0}
        self.add(label, "setTimelineClipFrame", clipId=clip, **values)

    def mask(self, label: str, clip, x: float, y: float, width: float, height: float,
             *, radius: float = 0, stage: bool = False):
        if stage:
            x, y, width, height = self.world_rect(x, y, width, height)
        self.add(label, "setTimelineClipMask", clipId=clip, shape="rectangle",
                 left=x / self.width, top=y / self.height,
                 right=(x + width) / self.width, bottom=(y + height) / self.height,
                 cornerRadius=radius / max(1.0, min(width, height)))

    def animation(self, label: str, clip, curves: dict[str, callable], duration: float,
                  *, fps: int = FPS):
        frames = max(1, math.floor(duration * fps + 1e-8))
        channels = []
        for parameter, function in curves.items():
            keys = [{"localSeconds": frame / fps,
                     "value": float(function(frame / fps)),
                     "interpolation": "linear"}
                    for frame in range(frames + 1)]
            if keys[-1]["localSeconds"] < duration - 1e-8:
                keys.append({"localSeconds": duration,
                             "value": float(function(duration)),
                             "interpolation": "linear"})
            compact = [key for i, key in enumerate(keys)
                       if i in (0, len(keys) - 1) or
                       keys[i - 1]["value"] != key["value"] or
                       keys[i + 1]["value"] != key["value"]]
            channels.append({"parameter": parameter, "keyframes": compact})
        self.add(label, "setTimelineClipAnimation", clipId=clip, channels=channels)


def smooth(t: float, a: float, b: float) -> float:
    u = max(0.0, min(1.0, (t - a) / (b - a)))
    return u * u * u * (10 + u * (-15 + 6 * u))


def out(t: float, a: float, b: float) -> float:
    u = max(0.0, min(1.0, (t - a) / (b - a)))
    return 1 - (1 - u) ** 4


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def mix_progress(s: float) -> float:
    return smooth(s, 1.1, 4.9)


def detail_zoom(s: float) -> float:
    return smooth(s, 0.2, 2.2) * (1 - smooth(s, 10, 10.8))


def map_landscape_to_portrait(plan: dict) -> dict:
    """Project 1920x1080 commands through the reference portrait Stage branch."""
    old_w, old_h = 1920.0, 1080.0
    new_w, new_h = 1080.0, 1920.0
    stage_left, stage_top, stage_scale = 650.0, 120.0, 1.1

    def x_point(px):
        return 40.0 + (px - stage_left) / stage_scale

    def y_point(py):
        return 710.0 + (py - stage_top) / stage_scale

    def norm_x(px):
        return x_point(px) / new_w

    def norm_y(py):
        return y_point(py) / new_h

    caption_re = re.compile(r"^photo-(\d+)-(label|title|detail)$")
    for step in plan["steps"]:
        step_id = step["id"]
        action = step["commandId"].rsplit(".", 1)[-1]
        values = step["values"]
        if action == "createTimelineGraphic":
            if step_id == "photo-ink":
                continue
            item = values["values"]
            px, py = item["positionX"] * old_w, item["positionY"] * old_h
            item["positionX"], item["positionY"] = norm_x(px), norm_y(py)
            item["shapeWidth"] *= old_w / (stage_scale * new_w)
            item["shapeHeight"] *= old_h / (stage_scale * new_h)
        elif action == "createTimelineTitle":
            match = caption_re.match(step_id)
            item = values["values"]
            if match:
                index, kind = int(match.group(1)), match.group(2)
                title_text, detail_text = COPY[index]
                if kind == "label":
                    item["positionX"] = 62 / new_w
                    item["positionY"] = (160 + 21 * 1.2 / 2) / new_h
                    item["fontSize"] = 21
                elif kind == "title":
                    item["text"] = title_text
                    item["fontSize"] = 108
                    top = 160 + 21 * 1.2 + 24
                    lines = title_text.count("\n") + 1
                    item["positionX"] = 62 / new_w
                    item["positionY"] = (top + lines * 108 * 1.01 / 2) / new_h
                else:
                    item["text"] = detail_text
                    item["fontSize"] = 36
                    item["lineHeight"] = 1.4
                    item["letterSpacing"] = 0
                    item["positionX"] = 62 / new_w
                    title_lines = title_text.count("\n") + 1
                    top = 160 + 21 * 1.2 + 24 + title_lines * 108 * 1.01 + 28
                    wrapped = {
                        0: "RAW photo editing. Your camera data,\nwith room to refine.",
                        1: "Recover the frame. Shape the light.\nKeep the original.",
                        2: "Refine the bright and dark\nregions independently.",
                        3: "A custom color engine, from RAW\ndevelopment to color-managed output.",
                        4: "Move a shadow point. Lift the\nmidtones. The photograph follows.",
                    }[index]
                    item["text"] = wrapped
                    lines = wrapped.count("\n") + 1
                    item["positionY"] = (top + lines * 36 * 1.4 / 2) / new_h
                continue
            px, py = item["positionX"] * old_w, item["positionY"] * old_h
            item["positionX"], item["positionY"] = norm_x(px), norm_y(py)
            item["fontSize"] /= stage_scale
            item["letterSpacing"] /= stage_scale
        elif action == "setTimelineClipFrame":
            old_cx = (values["offsetX"] / 4 + 0.5) * old_w
            old_cy = (values["offsetY"] / 4 + 0.5) * old_h
            new_cx, new_cy = x_point(old_cx), y_point(old_cy)
            values["offsetX"] = 4 * (new_cx / new_w - 0.5)
            values["offsetY"] = 4 * (new_cy / new_h - 0.5)
            values["width"] *= old_w / (stage_scale * new_w)
            values["height"] *= old_h / (stage_scale * new_h)
        elif action == "setTimelineClipMask":
            for edge in ("left", "right"):
                values[edge] = norm_x(values[edge] * old_w)
            for edge in ("top", "bottom"):
                values[edge] = norm_y(values[edge] * old_h)
        elif action == "setTimelineClipAnimation":
            ref = values.get("clipId", {})
            clip_id = ref.get("result", "") if isinstance(ref, dict) else ""
            is_caption = bool(caption_re.match(clip_id))
            for channel in values["channels"]:
                parameter = channel["parameter"]
                for key in channel["keyframes"]:
                    value = key["value"]
                    if parameter == "frameOffsetX":
                        key["value"] = value * old_w / new_w / (1.0 if is_caption else stage_scale)
                    elif parameter == "frameOffsetY":
                        key["value"] = value * old_h / new_h / (1.0 if is_caption else stage_scale)
                    elif parameter == "titlePositionX":
                        key["value"] = norm_x(value * old_w)
                    elif parameter == "titlePositionY":
                        key["value"] = norm_y(value * old_h)
                    elif parameter in ("mask:left", "mask:right"):
                        key["value"] = norm_x(value * old_w)
                    elif parameter in ("mask:top", "mask:bottom"):
                        key["value"] = norm_y(value * old_h)
    plan["orientation"] = "portrait"
    plan["composition"] = {"width": 1080, "height": 1920, "fps": 30}
    return plan


def add_caption(plan: Plan, label: str, title_text: str, detail_text: str,
                start: float, duration: float, *, reset: bool = False):
    """Caption layers for the source Caption component, with a re-enter option."""
    if plan.orientation == "portrait":
        x, top, title_size, detail_size, detail_width = 62, 160, 108, 36, 900
        label_size = 21
    else:
        x, top, title_size, detail_size, detail_width = 82, 254, 91, 33, 445
        label_size = 21
    title_top = top + label_size * 1.2 + 24
    title_lines = title_text.count("\n") + 1
    detail_top = title_top + title_lines * title_size * 1.01 + 28
    # Preserve the authored phrase breaks and let the native text box wrap at
    # the same source-width boundary where exact line metrics remain font-bound.
    if plan.orientation == "portrait":
        detail_text = {
            "Check the fine detail. Refine selections at the original image resolution.":
                "Check the fine detail. Refine selections\nat the original image resolution.",
            "Add or erase a single selection pixel. Keep neighboring pixels untouched.":
                "Add or erase a single selection pixel.\nKeep neighboring pixels untouched.",
            "Refine orange hues, saturation and luminance while the surrounding greens remain.":
                "Refine orange hues, saturation and\nluminance while the surrounding greens\nremain.",
        }.get(detail_text, detail_text)
    else:
        detail_text = {
            "Check the fine detail. Refine selections at the original image resolution.":
                "Check the fine detail.\nRefine selections at the\noriginal image resolution.",
            "Add or erase a single selection pixel. Keep neighboring pixels untouched.":
                "Add or erase a single\nselection pixel. Keep\nneighboring pixels untouched.",
            "Refine orange hues, saturation and luminance while the surrounding greens remain.":
                "Refine orange hues,\nsaturation and luminance\nwhile the surrounding\ngreens remain.",
        }.get(detail_text, detail_text)
    title_clip = plan.title(label + " / title", title_text, x, title_top, title_size,
                            start, duration, family="Anton", color=PAPER,
                            spacing=-1.8)
    detail_clip = plan.title(label + " / detail", detail_text, x, detail_top,
                             detail_size, start, duration, family="Space Grotesk",
                             color=MUTED, spacing=0, line_height=1.4)
    label_clip = plan.title(label + " / chapter", "01 / PHOTO EDITOR", x, top,
                            label_size, start, duration, family="Space Mono",
                            color=LIME, spacing=1.8, line_height=1.2)
    for suffix, clip, at in (("chapter", label_clip, 0), ("title", title_clip, 0.05),
                             ("detail", detail_clip, 0.2)):
        plan.animation(label + " / " + suffix + " entrance", clip,
                       {"opacity": lambda t, at=at: out(t, at, at + 0.55),
                        "frameOffsetY": lambda t, at=at: 4 * 45 *
                        (1 - out(t, at, at + 0.75)) / plan.height}, duration)
    return title_clip, detail_clip, label_clip


def add_output_footer(plan: Plan):
    if plan.orientation == "portrait":
        x0, x1, y, size, start, duration = 62, 1018, 1920 - 130, 20, 12, 45
    else:
        x0, x1, y, size, start, duration = 82, 1838, 1080 - 22, 17, 12, 45
    left = plan.title("photo-native-footer-left", "YOUR ORIGINAL. YOUR DECISIONS.",
                      x0, y - size * 1.2, size, start, duration,
                      family="Space Mono", color="#aeb6c3", spacing=1,
                      line_height=1.2)
    right = plan.title("photo-native-footer-right",
                       "CUSTOM COLOR ENGINE · NATIVE RESOLUTION", x1,
                       y - size * 1.2, size, start, duration,
                       family="Space Mono", color="#aeb6c3", spacing=1,
                       line_height=1.2, alignment="right")
    return left, right


def set_transform_zoom(plan: Plan, label: str, clip, duration: float,
                       final_center: tuple[float, float],
                       desired_center: callable, scale: callable,
                       *, radius: callable | None = None,
                       opacity: callable | None = None):
    """Animate a standalone layer as if its origin were a nearby card corner."""
    cx, cy = final_center
    def offset_x(t):
        z = scale(t)
        desired_x, _ = desired_center(t)
        zoomed_base = plan.width / 2 + (cx - plan.width / 2) * z
        return 4 * (desired_x - zoomed_base) / plan.width
    def offset_y(t):
        z = scale(t)
        _, desired_y = desired_center(t)
        zoomed_base = plan.height / 2 + (cy - plan.height / 2) * z
        return 4 * (desired_y - zoomed_base) / plan.height
    curves = {"frameScale": scale, "frameOffsetX": offset_x, "frameOffsetY": offset_y}
    if radius is not None:
        curves["frameCornerRadius"] = radius
    if opacity is not None:
        curves["opacity"] = opacity
    plan.animation(label + " / sampled native transform", clip, curves, duration)


def add_photo_detail(plan: Plan):
    start, duration = 39.0, 11.0
    add_caption(plan, "photo-5-caption-before",
                "Right down\nto the pixels.",
                "Check the fine detail. Refine selections at the original image resolution.",
                start, duration)
    add_caption(plan, "photo-5-caption-after",
                "Refine the edge.\nOne pixel at a time.",
                "Add or erase a single selection pixel. Keep neighboring pixels untouched.",
                44.0, 5.0, reset=True)
    # The component wraps its first caption in a cross-fading parent for s<5.
    before_clip_ids = {step["id"] for step in plan.steps
                       if step["id"].startswith("photo-5-caption-before")}
    after_clip_ids = {step["id"] for step in plan.steps
                      if step["id"].startswith("photo-5-caption-after")}
    for step in plan.steps:
        action = step["commandId"].rsplit(".", 1)[-1]
        if action != "setTimelineClipAnimation":
            continue
        ref = step["values"].get("clipId", {})
        clip_id = ref.get("result", "") if isinstance(ref, dict) else ""
        if clip_id in before_clip_ids:
            for channel in step["values"]["channels"]:
                if channel["parameter"] == "opacity":
                    for key in channel["keyframes"]:
                        t = key["localSeconds"]
                        key["value"] *= 1 - smooth(t, 4.6, 4.95)
        elif clip_id in after_clip_ids:
            # The source resets its enter-clock at s=5 and the parent restores
            # full opacity there.  Re-express that timing in the clip local clock.
            for channel in step["values"]["channels"]:
                for key in channel["keyframes"]:
                    t = key["localSeconds"]
                    if channel["parameter"] == "opacity":
                        at = 0.05 if "title" in step["id"] else 0.2 if "detail" in step["id"] else 0
                        key["value"] = out(t, at, at + 0.55) * smooth(t, 0, 0.1)
                    elif channel["parameter"] == "frameOffsetY":
                        at = 0.05 if "title" in step["id"] else 0.2 if "detail" in step["id"] else 0
                        key["value"] = 4 * 45 * (1 - out(t, at, at + 0.75)) / plan.height

    # Shared generator and all three source surfaces stay native independent
    # of orientation.  The full-image comparison plate fades under the detail.
    bx, by, bw, bh = plan.world_rect(0, 40, 1000, 667)
    base = plan.image("photo-detail-base-curve-16",
                      "creative-polish-v2/curve/curve-16.png", start, duration,
                      x=bx, y=by, width=bw, height=bh, fit="fill")
    plan.animation("photo-detail-base fade", base,
                   {"opacity": lambda t: 1 - detail_zoom(t)}, duration)

    def local_rect(t):
        zoom = detail_zoom(t)
        initial_x = ((6024 * 0.57 - 480) / 6024) * 1000
        initial_y = 40 + ((4020 * 0.59 - 480) / 4020) * 667
        initial_w = (960 / 6024) * 1000
        initial_h = (960 / 4020) * 667
        target = 960 if plan.orientation == "portrait" else 960 / 1.1
        return (lerp(initial_x, 0, zoom), lerp(initial_y, -35, zoom),
                lerp(initial_w, target, zoom), lerp(initial_h, target, zoom), zoom)

    target = 960 if plan.orientation == "portrait" else 960 / 1.1
    final_x, final_y, final_w, final_h = plan.world_rect(0, -35, target, target)
    final_center = (final_x + final_w / 2, final_y + final_h / 2)
    initial_size = (960 / 6024) * 1000
    initial_scale = initial_size / target
    def scale(t):
        return lerp(initial_scale, 1.0, detail_zoom(t))
    def center(t):
        x, y, w, h, _ = local_rect(t)
        wx, wy, ww, wh = plan.world_rect(x, y, w, h)
        return wx + ww / 2, wy + wh / 2
    detail_clips = []
    for index in range(9):
        clip = plan.image(f"photo-detail-plate-{index}",
                          f"creative-polish-v2/detail/detail-{index}.png",
                          start, duration, x=final_x, y=final_y,
                          width=final_w, height=final_h, fit="fill", radius=14)
        detail_clips.append(clip)
        def plate_opacity(t, index=index):
            n = 8 * smooth(t, 2.3, 4.8)
            return 1.0 if index == 0 else max(0.0, min(1.0, n - (index - 1)))
        set_transform_zoom(plan, f"photo-detail-{index}", clip, duration,
                           final_center, center, scale,
                           radius=lambda t: lerp(2, 14, detail_zoom(t)) / 960,
                           opacity=plate_opacity)

    border = plan.shape("photo-detail-border", final_x, final_y, final_w, final_h,
                        "#00000000", start=start, duration=duration,
                        stroke="#88d4e2e9", stroke_width=1)
    set_transform_zoom(plan, "photo-detail-border", border, duration,
                       final_center, center, scale,
                       radius=lambda t: lerp(2, 14, detail_zoom(t)) / 960,
                       opacity=lambda t: 1 - smooth(t, 10.7, 10.9))

    # Three pills are independent native graphics and titles.  Counts are
    # represented by frame-accurate native title clips as the source rounds.
    pill_y = 840 if plan.orientation == "portrait" else 740
    pill_values = ["960 × 960 SOURCE PIXELS", "SHARPEN", "NOISE"]
    widths = [300, 160, 140]
    x = 20
    for i, (label, width) in enumerate(zip(pill_values, widths)):
        bx, by, bw, bh = plan.world_rect(x, pill_y, width, 46)
        back = plan.shape(f"photo-detail-pill-{i}-background", bx, by, bw, bh,
                          "#bd0b0d12", start=start, duration=duration,
                          radius=23 * plan.stage_scale,
                          stroke="#55c4c8ce", stroke_width=1)
        plan.animation(f"photo-detail-pill-{i} fade", back,
                       {"opacity": lambda t: out(t, 2, 2.5) * (1 - smooth(t, 5, 6))}, duration)
        color = LIME if i == 0 else PAPER
        font_size = 19
        if i == 0:
            clip = plan.title("photo-detail-pill-source-pixels", label,
                              bx + 18 * plan.stage_scale,
                              by + 10 * plan.stage_scale, font_size * plan.stage_scale,
                              start, duration, family="Space Mono", color=color,
                              spacing=0, line_height=1.2)
            plan.animation("photo-detail-pill-source-pixels fade", clip,
                           {"opacity": lambda t: out(t, 2, 2.5) * (1 - smooth(t, 5, 6))}, duration)
        else:
            maximum = 32 if i == 1 else 24
            x_text = bx + 18 * plan.stage_scale
            top_text = by + 10 * plan.stage_scale
            for first, last, value in value_segments(maximum, 2.3, 4.8, duration):
                name = f"photo-detail-pill-{i}-{value}-{first}"
                clip = plan.title(name, f"{label} {value}", x_text, top_text,
                                  font_size * plan.stage_scale,
                                  start + first, max(1 / FPS, last - first),
                                  family="Space Mono", color=color,
                                  spacing=0, line_height=1.2)
                local_start = first
                plan.animation(name + " fade", clip,
                               {"opacity": lambda t, local_start=local_start:
                                out(local_start + t, 2, 2.5) *
                                (1 - smooth(local_start + t, 5, 6))},
                               max(1 / FPS, last - first))
        x += width + 12

    add_pixel_proof(plan, start, duration, local_rect)


def value_segments(maximum: int, ramp_start: float, ramp_end: float, duration: float):
    values = [round(maximum * smooth(frame / FPS, ramp_start, ramp_end))
              for frame in range(round(duration * FPS) + 1)]
    segments = []
    first = 0
    for frame in range(1, len(values) + 1):
        if frame == len(values) or values[frame] != values[first]:
            segments.append((first / FPS, frame / FPS, values[first]))
            first = frame
    return segments


def rgb_samples():
    source = (ROOT.parent / "animated-trailer/src/creative-launch/pixel-data.ts").read_text()
    match = RGB_BLOCK.search(source)
    if not match:
        raise RuntimeError("The reference source-pixel sample could not be read.")
    return json.loads(re.sub(r",\s*([}\]])", r"\1", match.group(1)))


def add_pixel_proof(plan: Plan, start: float, duration: float, local_rect):
    rgb = rgb_samples()
    if len(rgb) != 64 or any(len(color) != 3 for color in rgb):
        raise RuntimeError("The PixelProof native sample must contain 64 RGB pixels.")
    source_x, source_y = 3633, 2169
    crop_x, crop_y = 680, 278

    def source_point(t):
        x, y, w, h, _ = local_rect(t)
        sx = x + crop_x / 960 * w
        sy = y + crop_y / 960 * h
        wx, wy, _, _ = plan.world_rect(sx, sy, 0, 0)
        return wx, wy

    # The lime source-pixel bounding box and connector are independent layers.
    final_x, final_y = source_point(6.0)
    aw = ah = 8.0
    ax, ay = final_x, final_y
    anchor = plan.shape("photo-pixel-source-anchor", ax, ay, aw, ah,
                        "#00000000", start=start, duration=duration,
                        stroke=LIME, stroke_width=2)
    final_center = (ax + aw / 2, ay + ah / 2)
    def anchor_scale(t):
        target = 960 if plan.orientation == "portrait" else 960 / 1.1
        return lerp(((960 / 6024) * 1000) / target, 1, detail_zoom(t))
    def anchor_center(t):
        sx, sy = source_point(t)
        z = anchor_scale(t)
        width = aw * z
        height = ah * z
        return sx + width / 2, sy + height / 2
    set_transform_zoom(plan, "photo-pixel-source-anchor", anchor, duration,
                       final_center, anchor_center, anchor_scale,
                       opacity=lambda t: smooth(t, 4.95, 5.1) *
                       (1 - smooth(t, 10.7, 10.9)))

    connector_commands = [
        {"verb": "M", "points": [0, 0]},
        {"verb": "L", "points": [0, 0]},
        {"verb": "L", "points": [0, 0]},
    ]
    connector_frames = []
    for frame in range(round(duration * FPS) + 1):
        t = frame / FPS
        x, y, width, height, _ = local_rect(t)
        source_px = x + crop_x / 960 * width
        source_py = y + crop_y / 960 * height
        connector_frames.append({
            "seconds": t,
            "coordinates": [source_px + 8 * width / 960,
                            source_py + 8 * height / 960,
                            430.0, 160.0, 460.0, 160.0],
        })
    connector = plan.generator(
        "photo-pixel-source-connector", "vector-path",
        {"vectorPath": {"viewBoxWidth": 1000, "viewBoxHeight": 800,
                         "commands": connector_commands, "stroke": LIME,
                         "strokeWidth": 2, "fill": "#00000000",
                         "frames": connector_frames}},
        start, duration, x=0, y=0, width=1000, height=800, stage=True)
    plan.animation("photo-pixel-source-connector reveal", connector,
                   {"opacity": lambda t: smooth(t, 5, 6) *
                    (1 - smooth(t, 10.7, 10.9))}, duration)

    # PixelProof's 512x512 sample is reconstructed from actual sampled RGB data,
    # one editable Timeline shape per pixel. It is not a screenshot plate.
    header_h = 55.0
    card_x, card_y = 440.0, 140.0
    card_w, card_h = 512.0, 605.0
    card_xw, card_yw, card_ww, card_hw = plan.world_rect(card_x, card_y, card_w, card_h)
    card = plan.shape("photo-pixel-proof-card", card_xw, card_yw, card_ww, card_hw,
                      "#101b2a", start=start, duration=duration, radius=12 * plan.stage_scale,
                      stroke="#77b8c9da", stroke_width=1)
    card_final_center = (card_xw + card_ww / 2, card_yw + card_hw / 2)
    def open_value(t):
        return lerp(8 / 512, 1, smooth(t, 5, 6))
    def card_center(t):
        open_amount = smooth(t, 5, 6)
        source_px, source_py = source_point(t)
        left = lerp(source_px, plan.stage_left + card_x * plan.stage_scale, open_amount)
        top = lerp(source_py, plan.stage_top + card_y * plan.stage_scale, open_amount)
        z = open_value(t)
        return left + card_ww * z / 2, top + card_hw * z / 2
    set_transform_zoom(plan, "photo-pixel-proof-card", card, duration,
                       card_final_center, card_center, open_value,
                       opacity=lambda t: smooth(t, 4.95, 5.1) *
                       (1 - smooth(t, 10.7, 10.9)))

    def cell_transform(col: int, row: int, t: float):
        open_amount = smooth(t, 5, 6)
        source_px, source_py = source_point(t)
        left = lerp(source_px, plan.stage_left + card_x * plan.stage_scale, open_amount)
        top = lerp(source_py, plan.stage_top + card_y * plan.stage_scale, open_amount)
        z = open_value(t)
        local_x = card_x + (col + 0.5) * 64
        local_y = card_y + header_h + (row + 0.5) * 64
        desired_x = left + plan.stage_scale * (local_x - card_x) * z
        desired_y = top + plan.stage_scale * (local_y - card_y) * z
        return desired_x, desired_y, z

    cell_refs = {}
    for index, color in enumerate(rgb):
        col, row = index % 8, index // 8
        local_x = card_x + col * 64
        local_y = card_y + header_h + row * 64
        wx, wy, ww, wh = plan.world_rect(local_x, local_y, 64, 64)
        fill = "#%02x%02x%02x" % tuple(color)
        clip = plan.shape(f"photo-pixel-sample-{col}-{row}", wx, wy, ww, wh,
                          fill, start=start, duration=duration,
                          stroke="#4ac7d5e8", stroke_width=1)
        def center(t, col=col, row=row):
            x, y, _ = cell_transform(col, row, t)
            return x, y
        set_transform_zoom(plan, f"photo-pixel-sample-{col}-{row}", clip, duration,
                           (wx + ww / 2, wy + wh / 2), center, open_value,
                           opacity=lambda t: smooth(t, 4.95, 5.1) *
                           (1 - smooth(t, 10.7, 10.9)))
        cell_refs[(col, row)] = (wx, wy, ww, wh)

    # The sample selection is drawn as lime overlay shapes, and the one stray
    # neighboring pixel is erased at the source's fourth interaction step.
    selected = [(x, y) for y in range(3) for x in range(8)] + [(3, 4), (2, 3), (3, 3), (4, 3)]
    for col, row in selected:
        wx, wy, ww, wh = cell_refs[(col, row)]
        inset = 2 * plan.stage_scale
        clip = plan.shape(f"photo-pixel-selection-{col}-{row}",
                          wx + inset, wy + inset, ww - 2 * inset, wh - 2 * inset,
                          "#60d7ff48", start=start, duration=duration,
                          stroke=LIME, stroke_width=3)
        def center(t, col=col, row=row):
            x, y, _ = cell_transform(col, row, t)
            return x, y
        set_transform_zoom(plan, f"photo-pixel-selection-{col}-{row}", clip, duration,
                           (wx + ww / 2, wy + wh / 2), center, open_value,
                           opacity=lambda t, col=col, row=row:
                           pixel_selection_opacity(col, row, t) *
                           smooth(t, 4.95, 5.1) * (1 - smooth(t, 10.7, 10.9)))

    # White interaction square follows the cursor, and the arrow is an editable
    # vector-path.  Both scale with the expanding sample card.
    cursor_local_final = (3, 3)
    cursor_base = cell_refs[cursor_local_final]
    wx, wy, ww, wh = cursor_base
    cursor = plan.shape("photo-pixel-cursor-square", wx + plan.stage_scale,
                        wy + plan.stage_scale, ww - 2 * plan.stage_scale,
                        wh - 2 * plan.stage_scale, "#00000000", start=start,
                        duration=duration, stroke="#ffffff", stroke_width=3)
    def cursor_pos(t):
        s = t
        col = (lerp(2, 3, smooth(s, 7.2, 7.7)) if s < 7.8 else
               lerp(3, 4, smooth(s, 8.1, 8.6)) if s < 8.7 else
               lerp(4, 3, smooth(s, 9.0, 9.45)))
        row = 4 if s >= 9.5 else 3
        # Keep subpixel cursor travel through each grid cell.
        left, top = source_point(t)
        open_amount = smooth(t, 5, 6)
        card_left = lerp(left, plan.stage_left + card_x * plan.stage_scale, open_amount)
        card_top = lerp(top, plan.stage_top + card_y * plan.stage_scale, open_amount)
        z = open_value(t)
        relative_x = (col + 0.5) * 64
        relative_y = header_h + (row + 0.5) * 64
        return (card_left + plan.stage_scale * (relative_x + 31) * z,
                card_top + plan.stage_scale * (relative_y + 31) * z)
    set_transform_zoom(plan, "photo-pixel-cursor-square", cursor, duration,
                       (wx + ww / 2, wy + wh / 2), cursor_pos, open_value,
                       opacity=lambda t: smooth(t, 4.95, 5.1) * (1 - smooth(t, 10.7, 10.9)))

    arrow_commands = [
        {"verb": "M", "points": [0, 0]}, {"verb": "L", "points": [0, 26]},
        {"verb": "L", "points": [7, 20]}, {"verb": "L", "points": [14, 32]},
        {"verb": "L", "points": [20, 29]}, {"verb": "L", "points": [13, 17]},
        {"verb": "L", "points": [24, 17]}, {"verb": "Z", "points": []},
    ]
    arrow_x = card_x + 3 * 64 + 35
    arrow_y = card_y + header_h + 3 * 64 + 90
    arrow = plan.generator("photo-pixel-pointer", "vector-path",
                           {"vectorPath": {"viewBoxWidth": 24, "viewBoxHeight": 32,
                            "commands": arrow_commands, "stroke": "#0d1723",
                            "strokeWidth": 2, "fill": "#ffffff", "frames": []}},
                           start, duration, x=arrow_x, y=arrow_y,
                           width=24, height=32, stage=True)
    wx, wy, ww, wh = plan.world_rect(arrow_x, arrow_y, 24, 32)
    def arrow_center(t):
        col, row = cursor_cell(t)
        left, top = source_point(t)
        open_amount = smooth(t, 5, 6)
        card_left = lerp(left, plan.stage_left + card_x * plan.stage_scale, open_amount)
        card_top = lerp(top, plan.stage_top + card_y * plan.stage_scale, open_amount)
        z = open_value(t)
        px = card_left + plan.stage_scale * (col * 64 + 35) * z + ww / 2 * z
        py = card_top + plan.stage_scale * (header_h + 3 * 64 + 90) * z + wh / 2 * z
        return px, py
    set_transform_zoom(plan, "photo-pixel-pointer", arrow, duration,
                       (wx + ww / 2, wy + wh / 2), arrow_center,
                       lambda t: open_value(t) * (1 + 0.12 * pointer_press(t)),
                       opacity=lambda t: smooth(t, 4.95, 5.1) * (1 - smooth(t, 10.7, 10.9)))

    header_y = card_y + 18
    header = plan.title("photo-pixel-card-heading", "SELECTION EDGE / ADD / 1 PX",
                        card_x + 18, header_y, 16, start + 6, 4.7,
                        family="Space Mono", color=PAPER, spacing=0,
                        line_height=1.2, stage=True)
    plan.animation("photo-pixel-card-heading reveal", header,
                   {"opacity": lambda t: 1 - smooth(t, 4.7, 4.9)}, 4.7)
    erase = plan.title("photo-pixel-card-heading-erase", "SELECTION EDGE / ERASE / 1 PX",
                       card_x + 18, header_y, 16, start + 9.5, 1.0,
                       family="Space Mono", color=PAPER, spacing=0,
                       line_height=1.2, stage=True)
    plan.animation("photo-pixel-card-heading-erase fade", erase,
                   {"opacity": lambda t: 1 - smooth(t, 1.15, 1.25)}, 1.0)

    for first, last, count, px, py in ((6.0, 6.9, 25, 2, 3),
                                       (6.9, 7.8, 26, 2, 3),
                                       (7.8, 8.7, 27, 3, 3),
                                       (8.7, 9.5, 28, 4, 3),
                                       (9.5, 10.7, 27, 3, 4)):
        label = plan.title(f"photo-pixel-count-{count}-{first}",
                           f"SOURCE {source_x + px}, {source_y + py} · SELECTED {count} PX",
                           card_x + 18, card_y + 579, 13, start + first,
                           last - first, family="Space Mono", color=PAPER,
                           spacing=0, line_height=1.2, stage=True)
        plan.animation(f"photo-pixel-count-{count}-{first} fade", label,
                       {"opacity": lambda t: 1}, last - first)

    for label, begin, end in (("SELECT THE EDGE. LEAVE THE BACKGROUND.", 5.1, 9.5),
                              ("STRAY PIXEL REMOVED. EDGE RETAINED.", 9.5, 10.8)):
        title = plan.title("photo-pixel-stage-status-" + str(begin), label,
                           20, 750, 16, start + begin, end - begin,
                           family="Space Mono", color=PAPER, spacing=0,
                           line_height=1.2, stage=True)
        plan.animation("photo-pixel-stage-status-" + str(begin) + " fade", title,
                       {"opacity": lambda t: 1}, end - begin)


def cursor_cell(t: float):
    if t < 7.8:
        return lerp(2, 3, smooth(t, 7.2, 7.7)), 3
    if t < 8.7:
        return lerp(3, 4, smooth(t, 8.1, 8.6)), 3
    return lerp(4, 3, smooth(t, 9.0, 9.45)), 4 if t >= 9.5 else 3


def pointer_press(t: float):
    return max(smooth(t, at - 0.08, at) * (1 - smooth(t, at + 0.03, at + 0.18))
               for at in (6.9, 7.8, 8.7, 9.5))


def pixel_selection_opacity(col: int, row: int, t: float):
    if row < 3:
        return 1.0
    if (col, row) == (3, 4):
        return 1 - smooth(t, 9.45, 9.6)
    starts = {(2, 3): 6.82, (3, 3): 7.72, (4, 3): 8.62}
    return smooth(t, starts.get((col, row), 11), starts.get((col, row), 11) + 0.12)


def add_orange_mix(plan: Plan):
    start, duration = 50.0, 7.0
    add_caption(plan, "photo-6-caption", "One color.\nYour call.",
                "Refine orange hues, saturation and luminance while the surrounding greens remain.",
                start, duration)
    x, y, w, h = plan.world_rect(40, 0, 530, 795)
    progress = mix_progress
    for index in range(9):
        clip = plan.image(f"photo-orange-mix-plate-{index}",
                          f"creative-polish-v2/flower/mix-{index}.png",
                          start, duration, x=x, y=y, width=w, height=h,
                          fit="fill", radius=20 * plan.stage_scale, rotation=-4)
        def opacity(t, index=index):
            n = 8 * progress(t)
            return 1.0 if index == 0 else max(0.0, min(1.0, n - (index - 1)))
        plan.animation(f"photo-orange-mix-plate-{index} sample", clip,
                       {"opacity": opacity,
                        "frameRotationDegrees": lambda t: -4 * (1 - out(t, 0, 1.2))}, duration)

    # Native before/after window clipped to the image plate; subsequent color
    # samples remain independently editable overlays.
    before = plan.image("photo-orange-before", "creative-polish-v2/flower/mix-0.png",
                        start, duration, x=x, y=y, width=w, height=h,
                        fit="fill", radius=20 * plan.stage_scale, rotation=-4)
    plan.mask("photo-orange-before initial mask", before, x, y, w * 0.38, h)
    def show(t):
        return smooth(t, 0.15, 0.65) * (1 - smooth(t, 5.7, 6.3))
    plan.animation("photo-orange-before reveal", before,
                   {"mask:right": lambda t: (x + w * 0.38 * show(t)) / plan.width,
                    "frameRotationDegrees": lambda t: -4 * (1 - out(t, 0, 1.2))}, duration)
    divider_x = x + w * 0.38
    divider = plan.shape("photo-orange-before-after-divider", divider_x - plan.stage_scale,
                        y, 2 * plan.stage_scale, h, PAPER,
                        start=start, duration=duration)
    divider_cx, divider_cy = divider_x, y + h / 2
    plan.animation("photo-orange-before-after-divider movement", divider,
                   {"frameOffsetX": lambda t: 4 * (w * 0.38 * (show(t) - 1)) / plan.width,
                    "opacity": show,
                    "frameRotationDegrees": lambda t: -4 * (1 - out(t, 0, 1.2))}, duration)
    for side, left, text, opacity_fn in (
        ("before", x + 22 * plan.stage_scale, "BEFORE", show),
        ("after", x + w - 100 * plan.stage_scale, "AFTER / THIS EDIT",
         lambda t: out(t, 0.4, 0.8)),
    ):
        tag_x, tag_y = left, y + 22 * plan.stage_scale
        tag_w = (96 if side == "before" else 190) * plan.stage_scale
        back = plan.shape("photo-orange-" + side + "-tag-background",
                          tag_x, tag_y, tag_w, 31 * plan.stage_scale,
                          "#dc0b1324", start=start, duration=duration,
                          radius=5 * plan.stage_scale)
        label = plan.title("photo-orange-" + side + "-tag", text,
                           tag_x + 12 * plan.stage_scale,
                           tag_y + 4 * plan.stage_scale,
                           15 * plan.stage_scale, start, duration,
                           family="Space Mono", color=PAPER if side == "before" else LIME,
                           spacing=1.8 * plan.stage_scale, line_height=1.2)
        plan.animation("photo-orange-" + side + "-tag entrance", back,
                       {"opacity": opacity_fn}, duration)
        plan.animation("photo-orange-" + side + "-tag copy entrance", label,
                       {"opacity": opacity_fn}, duration)

    # The color mixer panel and its actual editable gradient rails.
    panel_x, panel_y, panel_w, panel_h = plan.world_rect(590, 135, 405, 360)
    panel = plan.shape("photo-orange-color-mix-panel", panel_x, panel_y,
                       panel_w, panel_h, "#f5121b25", start=start,
                       duration=duration, radius=15 * plan.stage_scale,
                       stroke="#44a7b5bf", stroke_width=1)
    header = plan.title("photo-orange-color-mix-heading", "COLOR MIX / ORANGES",
                        panel_x + 22 * plan.stage_scale,
                        panel_y + 14 * plan.stage_scale,
                        17 * plan.stage_scale, start, duration,
                        family="Space Mono", color="#ff9b38", spacing=1.8 * plan.stage_scale,
                        line_height=1.2)
    sliders = [
        ("Hue", -8, ["#d95425", "#ffc25d"]),
        ("Saturation", 16, ["#6f6f6f", "#ff941c"]),
        ("Luminance", 8, ["#151515", "#eee8d8"]),
    ]
    for row, (name, maximum, colors) in enumerate(sliders):
        row_top_local = 67 + row * 71
        row_top = plan.stage_top + plan.stage_scale * (135 + row_top_local)
        row_left = plan.stage_left + plan.stage_scale * (590 + 22)
        label = plan.title(f"photo-orange-{name.lower()}-label", name,
                           row_left, row_top, 21 * plan.stage_scale,
                           start, duration, family="Space Grotesk",
                           color=PAPER, spacing=0, line_height=1.2)
        rail_x = row_left
        rail_y = row_top + 40 * plan.stage_scale
        rail_w = 360 * plan.stage_scale
        rail_h = 4 * plan.stage_scale
        gradient = plan.generator(f"photo-orange-{name.lower()}-gradient",
                                  "linear-gradient",
                                  {"angleDegrees": 90, "stops": [
                                      {"position": 0.0, "color": colors[0], "opacity": 1.0},
                                      {"position": 1.0, "color": colors[1], "opacity": 1.0}]},
                                  start, duration, x=rail_x, y=rail_y,
                                  width=rail_w, height=rail_h)
        value_st = value_segments(abs(maximum), 1.1, 4.9, duration)
        for first, last, magnitude in value_st:
            signed = -magnitude if maximum < 0 else magnitude
            val = str(signed) if maximum < 0 else f"+{signed}"
            value = plan.title(f"photo-orange-{name.lower()}-value-{first}", val,
                               row_left + rail_w, row_top, 21 * plan.stage_scale,
                               start + first, last - first, family="Space Mono",
                               color=LIME, spacing=0, line_height=1.2,
                               alignment="right")
            plan.animation(f"photo-orange-{name.lower()}-value-{first} visibility",
                           value, {"opacity": lambda t: 1}, last - first)
        initial_percent = (50 + maximum * smooth(0, 1.1, 4.9) * 0.5) / 100
        final_percent = (50 + maximum * smooth(duration, 1.1, 4.9) * 0.5) / 100
        thumb_x = rail_x + rail_w * final_percent
        thumb_y = rail_y + rail_h / 2
        thumb_w = 16 * plan.stage_scale
        thumb = plan.shape(f"photo-orange-{name.lower()}-thumb",
                           thumb_x - thumb_w / 2, thumb_y - thumb_w / 2,
                           thumb_w, thumb_w, PAPER, start=start, duration=duration,
                           shape="ellipse", radius=0,
                           stroke="#550b0d12", stroke_width=3)
        plan.animation(f"photo-orange-{name.lower()}-thumb move", thumb,
                       {"frameOffsetX": lambda t, maximum=maximum:
                        4 * (rail_w * (((50 + maximum * smooth(t, 1.1, 4.9) * 0.5) / 100) - final_percent)) / plan.width},
                       duration)
    source = plan.title("photo-orange-source-label", "IMG_4869.CR2 / CAMERA RAW",
                        panel_x + 22 * plan.stage_scale,
                        panel_y + 315 * plan.stage_scale,
                        12 * plan.stage_scale, start, duration,
                        family="Space Mono", color="#a8b4be", spacing=1.3 * plan.stage_scale,
                        line_height=1.2)


def add_delivery(plan: Plan):
    start, duration = 57.0, 5.0
    label_x = 65 if plan.orientation == "portrait" else 85
    label_top = 160 if plan.orientation == "portrait" else 75
    title_size = 115 if plan.orientation == "portrait" else 108
    preview_x, preview_y, preview_w, preview_h = (
        (45, 740, 990, 660) if plan.orientation == "portrait"
        else (85, 430, 930, 620)
    )
    label = plan.title("photo-delivery-chapter-label", "01 / PHOTO EDITOR",
                       label_x, label_top, 21, start, duration,
                       family="Space Mono", color=LIME, spacing=1.8,
                       line_height=1.2)
    type_top = label_top + 21 * 1.2 + 25
    title = plan.title("photo-delivery-title",
                       "Deliver the photograph.\nKeep every pixel.",
                       label_x, type_top, title_size, start, duration,
                       family="Anton", color=PAPER, spacing=-1.8,
                       line_height=1.01)
    detail_top = type_top + title_size * 2 * 1.01 + 22
    detail = plan.title("photo-delivery-detail",
                        "Full-resolution output from the same camera RAW.",
                        label_x, detail_top,
                        36 if plan.orientation == "portrait" else 32,
                        start, duration, family="Space Grotesk",
                        color=PAPER, spacing=0, line_height=1.3)
    image = plan.image("photo-delivery-preview",
                       "creative-polish-v4/raw-export/preview.jpg",
                       start, duration, x=preview_x, y=preview_y,
                       width=preview_w, height=preview_h,
                       fit="fill", radius=8)
    if plan.orientation == "portrait":
        cards_x, cards_y, cards_w, cards_gap = 65, 1470, 950, 18
        card_w = (cards_w - cards_gap) / 2
        card_h = 220
        entries = [("JPEG", "6024 × 4020", "CLIENT GALLERY"),
                   ("16-BIT TIFF", "6024 × 4020", "FULL-RESOLUTION MASTER")]
    else:
        cards_x, cards_y, cards_w, cards_gap = 1120, 485, 690, 32
        card_w, card_h = 690, 150
        entries = [("JPEG", "6024 × 4020", "CLIENT GALLERY"),
                   ("16-BIT TIFF", "6024 × 4020", "FULL-RESOLUTION MASTER")]
    for index, (format_name, size, use) in enumerate(entries):
        x = cards_x + index * (card_w + cards_gap) if plan.orientation == "portrait" else cards_x
        y = cards_y if plan.orientation == "portrait" else cards_y + index * (card_h + cards_gap)
        line = plan.shape(f"photo-delivery-card-{index}-rule", x, y, card_w, 1,
                          "#6699aeb9", start=start + 0.3 + index * 0.25,
                          duration=duration - (0.3 + index * 0.25))
        heading_size = 25 if plan.orientation == "portrait" else 26
        size_size = 38 if plan.orientation == "portrait" else 42
        title = plan.title(f"photo-delivery-card-{index}-format", format_name,
                           x, y + 20, heading_size,
                           start + 0.3 + index * 0.25,
                           duration - (0.3 + index * 0.25),
                           family="Space Mono", color=PAPER,
                           spacing=1.8, line_height=1.2)
        dims = plan.title(f"photo-delivery-card-{index}-dimensions", size,
                          x, y + 58, size_size,
                          start + 0.3 + index * 0.25,
                          duration - (0.3 + index * 0.25),
                          family="Space Grotesk", color=PAPER,
                          spacing=0, line_height=1.2)
        sub = plan.title(f"photo-delivery-card-{index}-use", use,
                         x, y + 116, 17,
                         start + 0.3 + index * 0.25,
                         duration - (0.3 + index * 0.25),
                         family="Space Mono", color="#aebdcb",
                         spacing=1.0, line_height=1.2)
        for label_name, clip in (("rule", line), ("format", title),
                                 ("dimensions", dims), ("use", sub)):
            plan.animation(f"photo-delivery-card-{index}-{label_name} entrance", clip,
                           {"opacity": lambda t: out(t, 0, 0.6),
                            "frameOffsetY": lambda t: 4 * 25 *
                            (1 - out(t, 0, 0.6)) / plan.height},
                           duration - (0.3 + index * 0.25))


def load_and_transform_base(plan: Plan):
    base = json.loads(BASE_PLAN.read_text(encoding="utf-8"))
    if plan.orientation == "portrait":
        base = map_landscape_to_portrait(base)
    for step in base["steps"]:
        item = copy.deepcopy(step)
        if item["id"] in plan.ids:
            raise RuntimeError("Historical Photo plan unexpectedly contains duplicate IDs.")
        plan.ids.add(item["id"])
        plan.steps.append(item)
    return base


def build(orientation: str):
    plan = Plan(orientation)
    old = load_and_transform_base(plan)
    # Photo.tsx includes a persistent chapter footer in phases 0--6.
    add_output_footer(plan)
    # The generator time source is global sequence time; the active delivery
    # component drops its opacity from .30 to .25 exactly at t=57.
    light = {"result": "photo-light", "field": "createdClipId"}
    plan.animation("photo-chapter-light-delivery-level", light,
                   {"opacity": lambda t: 0.3 if t < 45 else 0.25}, 50.0)
    add_photo_detail(plan)
    add_orange_mix(plan)
    add_delivery(plan)
    return {
        "reference": {"landscape": "landscape.mp4", "portrait": "portrait.mp4"},
        "orientation": orientation,
        "composition": {"width": plan.width, "height": plan.height, "fps": FPS},
        "chapters": [[12, 62]],
        "steps": plan.steps,
        "supplementalAssets": [
            str((MEDIA / "creative-polish-v2/curve/curve-16.png").resolve()),
            *[str((MEDIA / f"creative-polish-v2/detail/detail-{i}.png").resolve()) for i in range(9)],
            *[str((MEDIA / f"creative-polish-v2/flower/mix-{i}.png").resolve()) for i in range(9)],
            str((MEDIA / "creative-polish-v4/raw-export/preview.jpg").resolve()),
        ],
        "unresolvedParity": [
            "The source-detail, PixelProof cells, color-mix panel, gradients, masks, and delivery cards are native editable layers. Native title shaping may differ slightly from Remotion line metrics.",
            "The detail crop, full-frame image, and delivery preview do not reproduce CSS box shadows; the pixel inspector card and flower surface have native borders and rounded masks but no blur shadow.",
            "The Surface -4° to 0° rotation is applied to its image plates; its before/after divider and labels rotate as separate layers, so their grouped pivot is approximate.",
            "PixelProof’s source connector and click pointer are editable vector/shape layers; native cursor hand pressure and dashed SVG stroke timing are simplified.",
            "The orange color-mix values and rails are editable native titles and linear-gradient generators. They demonstrate the reference controls but do not apply a photo color-mix effect to the underlying image.",
            "The delivery cards use the supplied 1600×1068 preview source and original dimensions/copy; they are visual presentation layers rather than live JPEG/TIFF export jobs.",
            "The portrait 12–39 prefix is transformed from the preserved landscape command plan through the source Stage and Caption geometry; manually authored 39–62 scenes use the source portrait branch directly.",
        ],
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--orientation", choices=("landscape", "portrait"), default="landscape")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or ROOT / f"photo-v2-{args.orientation}.commands.json"
    payload = build(args.orientation)
    output.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{output} {len(payload['steps'])} native commands")
