"""Generate responsive native Timeline plans for Library and Carousel.

The generated JSON contains typed MCP commands only.  It does not author a
Timeline file directly or contact Looksy.  Landscape is derived from the
existing plan with an exact sequence-mask wipe for shared-grade reveals;
portrait is laid out from the responsive values in the Remotion source.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OLD_PATH = ROOT / "library_carousel_plan.py"
SPEC = importlib.util.spec_from_file_location("library_carousel_base", OLD_PATH)
base = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(base)

MEDIA = base.MEDIA
PHOTOS = ["IMG_4969", "IMG_5071", "IMG_4779", "IMG_4653", "IMG_4748",
          "IMG_4867", "IMG_4788", "IMG_5053", "IMG_5077", "IMG_5106",
          "IMG_5125", "IMG_5203"]
MATCHES = {"IMG_5071", "IMG_5053", "IMG_5077", "IMG_4788"}
WEDDINGS = ["wedding-walk", "wedding-embrace", "wedding-play"]


def _cmd(step_id, action, **values):
    return {"id": step_id, "commandId": "timeline.action." + action,
            "values": values}


def _ref(step_id, field="createdClipId"):
    return {"result": step_id, "field": field}


def _mask(step_id, clip, left, top, right, bottom, radius=0.0):
    return _cmd(step_id, "setTimelineClipMask", clipId=clip, shape="rectangle",
                left=left, top=top, right=right, bottom=bottom,
                cornerRadius=radius)


def _keys(duration, function):
    return base.compact_keys(duration, function)


def _animate(step_id, clip, duration, channels):
    base.animate(step_id, clip, duration, channels)


def _absolute_frame_offset_curves(plan):
    """Convert authored transform deltas to the controller's absolute channels."""
    frame_offsets = {}
    for step in plan["steps"]:
        if step["commandId"] != "timeline.action.setTimelineClipFrame":
            continue
        values = step["values"]
        clip_ref = values.get("clipId", {})
        clip_id = clip_ref.get("result") if isinstance(clip_ref, dict) else str(clip_ref)
        frame_offsets[clip_id] = (
            float(values.get("offsetX", 0.0)),
            float(values.get("offsetY", 0.0)))

    for step in plan["steps"]:
        if step["commandId"] != "timeline.action.setTimelineClipAnimation":
            continue
        values = step["values"]
        clip_ref = values.get("clipId", {})
        clip_id = clip_ref.get("result") if isinstance(clip_ref, dict) else str(clip_ref)
        base_x, base_y = frame_offsets.get(clip_id, (0.0, 0.0))
        for channel in values.get("channels", []):
            parameter = channel.get("parameter")
            offset = base_x if parameter == "frameOffsetX" else (
                base_y if parameter == "frameOffsetY" else 0.0)
            if parameter not in ("frameOffsetX", "frameOffsetY"):
                continue
            for keyframe in channel.get("keyframes", []):
                keyframe["value"] = float(keyframe["value"]) + offset
    return plan


def _label_box(font_size, text):
    """Approximate the source label's 12px horizontal and 9px vertical padding."""
    return len(text) * font_size * .6 + 24, font_size * 1.2 + 18


def _landscape_library_lighting(plan):
    """Match the source light opacity across LibraryChapter's three branches."""
    steps = plan["steps"]
    main = next(step for step in steps if step["id"] == "lib-light")
    main["values"]["durationSeconds"] = 17
    track_ref = _ref("lib-light-track", "trackId")
    extra = []
    for start, duration in ((79, 7), (86, 6)):
        extra.append(_cmd(f"v2-lib-gallery-light-{start}", "createTimelineGenerator",
            trackId=track_ref, generatorKind="desktop-light",
            startSeconds=start, durationSeconds=duration,
            parameters={"timeOffsetSeconds": start, "timeScale": 1,
                        "gather": 0, "opacity": .22}))
    index = steps.index(main)
    steps[index + 1:index + 1] = extra
    return plan


def _landscape_wipe_upgrade(plan):
    """Use a real left-to-right clip-path equivalent on batch-after photos."""
    steps = plan["steps"]
    drop_suffixes = ("-shared-recipe-crossfade", "-shared-recipe-before-fade")
    steps[:] = [s for s in steps if not (
        any(s["id"].endswith(suffix) for suffix in drop_suffixes) or
        s["id"].startswith("gallery-86-recipe-pill-"))]
    insertions = []
    for index, name in enumerate(WEDDINGS):
        prefix = f"gallery-86-{name}"
        for suffix in ("before-frame", "after-frame"):
            image_frame = next(s for s in steps if s["id"] == prefix + "-" + suffix)
            image_frame["values"]["cornerRadius"] = 5 / base.WIDTH
        frame_step = next(s for s in steps if s["id"] == prefix + "-after-frame")
        frame = frame_step["values"]
        clip_ref = frame["clipId"]
        width, height = frame["width"], frame["height"]
        cx, cy = frame["offsetX"] + 0.5, frame["offsetY"] + 0.5
        left, top = cx - width / 2, cy - height / 2
        left, top, right, bottom = left, top, left + width, top + height
        font_size = 18
        label_width, label_height = _label_box(font_size, "SHARED RECIPE / APPLYING")
        label_bg = next(s for s in steps if s["id"] == prefix + "-label-bg")
        label_values = label_bg["values"]["values"]
        label_values.update({
            "positionX": (left * base.WIDTH + 14) / base.WIDTH,
            "positionY": (top * base.HEIGHT + height * base.HEIGHT - 18 - label_height) / base.HEIGHT,
            "shapeWidth": label_width / base.WIDTH,
            "shapeHeight": label_height / base.HEIGHT})
        for text_suffix in ("label-applying", "label-editable"):
            title_step = next(s for s in steps if s["id"] == prefix + "-" + text_suffix)
            title_values = title_step["values"]["values"]
            title_values.update({
                "positionX": (left * base.WIDTH + 26) / base.WIDTH,
                "positionY": (top * base.HEIGHT + height * base.HEIGHT - 18 - label_height + 9) / base.HEIGHT,
                "fontSize": font_size,
                "letterSpacing": 0})
        insertions.extend([
            (steps.index(frame_step) + 1, _mask(prefix + "-wipe-mask", clip_ref,
                                                 left, top, left, bottom)),
            (steps.index(frame_step) + 2, _cmd(prefix + "-wipe", "setTimelineClipAnimation",
                clipId=clip_ref, channels=[{"parameter": "mask:right",
                    "keyframes": _keys(6, lambda t, i=index, x=left, r=right:
                        x + (r - x) * base.smooth(t, .7 + i * .17, 3.5 + i * .17))}]))
        ])
    for index, step in sorted(insertions, key=lambda pair: pair[0], reverse=True):
        steps.insert(index, step)
    plan["nativeBlockers"] = [
        "The landscape phone switches between three page crops with opacity transitions; the source uses a horizontal clipped swipe. Portrait v2 has the clipped native swipe, but this landscape phone path remains an approximation.",
        "Library grade wipes use native sequence-space masks after clip geometry. Their edges stay axis aligned while the source cards rotate by up to 3 degrees, so the exact rotated clipPath wipe is not expressible with current mask geometry.",
        "Native font rasterization and platform font fallback can shift line breaks or glyph widths; visible native review is still needed.",
        "Native desktop-light output and CSS shadow softness need a live Program comparison; the plan preserves source values but cannot certify pixel identity offline.",
        "Carousel panels and their editable overlays are separate clips with synchronized transforms and masks. Native Timeline has no parent group handle for one-step group edits."
    ]
    plan["requiredActionKinds"] = ["media", "text", "layers", "delivery", "masking", "framing", "effects"]
    plan["planVersion"] = 2
    return plan


def _landscape_shared_panorama(plan):
    """Replace endpoint crop dissolves with one cropped/panned source layer."""
    steps = plan["steps"]
    for panel in range(3):
        prefixes = (f"carousel-panorama-panel-{panel}-pan-start",
                    f"carousel-panorama-panel-{panel}-pan-end")
        steps[:] = [s for s in steps if not s["id"].startswith(prefixes)]

    width, height, fps = 1920, 1080, 30
    sc, panel_w, panel_h, gap_max = .43, 1080 * .43, 1350 * .43, 14
    left_final, top_final = 249.4, 395.0
    image_path = str((MEDIA / "creative-polish-v2/IMG_5071.jpg").resolve())
    inserted = []
    for panel in range(3):
        track_id = f"v2-carousel-panorama-track-{panel}"
        clip_id = f"v2-carousel-panorama-layer-{panel}"
        inserted.extend([
            _cmd(track_id, "createTimelineTrack", kind="video",
                 name=f"Carousel panorama {panel + 1}", audioRole=""),
            _cmd(clip_id, "createTimelineClip", trackId=_ref(track_id, "trackId"),
                 assetId="native-trailer-v4:creative-polish-v2/IMG_5071.jpg",
                 filePath=image_path, displayLabel=f"Continuous panorama {panel + 1}",
                 durationSeconds=24, trimStartSeconds=0, trimEndSeconds=24,
                 startSeconds=160, posterSource=""),
        ])
        clip = _ref(clip_id)
        art_x = left_final + panel * gap_max
        inserted.append(_cmd(clip_id + "-frame", "setTimelineClipFrame",
            clipId=clip, offsetX=(art_x + 3*panel_w/2)/width-.5,
            offsetY=(top_final + panel_h/2)/height-.5, scale=1.12,
            rotationDegrees=0,perspectiveX=0,perspectiveY=0,opacity=1,
            objectFit="fill",width=3*panel_w/width,height=panel_h/height,
            cornerRadius=0,cropLeft=0,cropTop=0,cropRight=0,cropBottom=0))
        pane_left = left_final + panel * (panel_w + gap_max)
        inserted.append(_mask(clip_id + "-viewport", clip,
            pane_left/width, top_final/height,
            (pane_left+panel_w)/width,(top_final+panel_h)/height,0))
        def group(t):
            split=base.smooth(t,6,8)
            phone=base.smooth(t,15,17)
            gap=gap_max*split
            scale=1-.34*phone
            centered=(width-(3*panel_w+2*gap))/2
            left=centered+(85-centered)*phone
            top=top_final+(470-top_final)*phone-40*phone
            return split,phone,gap,scale,left,top
        def frame_x(t,p=panel,art_x=art_x):
            split,phone,gap,scale,left,_=group(t)
            center=art_x+1.5*panel_w
            # Each viewport carries a full-width artwork child offset by
            # -panel*panel_w, leaving only the inter-panel gap in world space.
            target=left+(p*gap+1.5*panel_w)*scale
            pan=(-110+220*base.smooth(t,8.5,12.5))*sc*scale
            return (target-center+pan)/width
        def mask_edge(t,edge,p=panel):
            split,phone,gap,scale,left,top=group(t)
            x=left+p*(panel_w+gap)*scale
            rect={"left":x,"right":x+panel_w*scale,"top":top,
                  "bottom":top+panel_h*scale,
                  "cornerRadius":3*split/min(panel_w,panel_h)}
            value=rect[edge]
            return value/(width if edge in ("left","right") else height) if edge!="cornerRadius" else value
        inserted.append(_cmd(clip_id + "-pan-gap-zoom", "setTimelineClipAnimation",
            clipId=clip,channels=[
                {"parameter":"frameScale","keyframes":_keys(24,
                    lambda t:1.12*(1-.34*base.smooth(t,15,17)))},
                {"parameter":"frameOffsetX","keyframes":_keys(24,frame_x)},
                {"parameter":"frameOffsetY","keyframes":_keys(24,
                    lambda t:((top_final+(470-top_final)*base.smooth(t,15,17)-
                    40*base.smooth(t,15,17)-top_final)+
                    (panel_h/2)*(-.34*base.smooth(t,15,17)))/height)},
                *[{"parameter":"mask:"+edge,"keyframes":_keys(24,
                    lambda t,e=edge:mask_edge(t,e))}
                  for edge in ("left","top","right","bottom","cornerRadius")]
            ]))
    # Insert above the original gradient and typography stack, after all
    # carousel media tracks already exist from the base plan.
    carousel_at = next((i for i,s in enumerate(steps)
        if s["id"] == "carousel-panorama-panel-0-reference-gradient"), len(steps))
    steps[carousel_at:carousel_at] = inserted
    # Delete track creates whose only users were the removed endpoint crops.
    referenced=set()
    def collect(value):
        if isinstance(value,dict):
            if set(value)=={"result","field"}: referenced.add(value["result"])
            else:
                for item in value.values():collect(item)
        elif isinstance(value,list):
            for item in value:collect(item)
    for s in steps:collect(s["values"])
    steps[:]=[s for s in steps if not (
        s["commandId"]=="timeline.action.createTimelineTrack" and s["id"] not in referenced)]
    return plan


def _landscape_pointer(plan):
    width,height=1920,1080
    pw,ph=1080*.43,1350*.43
    track_id="v2-carousel-pointer-track"
    path={"viewBoxWidth":40,"viewBoxHeight":48,
        "commands":[{"verb":"M","points":[1,1]},
        {"verb":"L","points":[1,31]},{"verb":"L","points":[9,24]},
        {"verb":"L","points":[18,41]},{"verb":"L","points":[25,37]},
        {"verb":"L","points":[16,21]},{"verb":"L","points":[31,21]},
        {"verb":"Z","points":[]}],"frames":[],
        "stroke":"#FF07111D","fill":"#FFFFFFFF","strokeWidth":2}
    x0,y0=pw*1.4-25,395+ph*.55
    plan["steps"].extend([
        _cmd(track_id,"createTimelineTrack",kind="video",
             name="Carousel / pointer",audioRole=""),
        _cmd("v2-carousel-pointer","createTimelineGenerator",
             trackId=_ref(track_id,"trackId"),generatorKind="vector-path",
             startSeconds=160,durationSeconds=24,parameters={"vectorPath":path}),
        _cmd("v2-carousel-pointer-frame","setTimelineClipFrame",
             clipId=_ref("v2-carousel-pointer"),offsetX=(x0+20)/width-.5,
             offsetY=(y0+24)/height-.5,scale=1,rotationDegrees=0,
             perspectiveX=0,perspectiveY=0,opacity=0,objectFit="stretch",
             width=40/width,height=48/height,cornerRadius=0,cropLeft=0,
             cropTop=0,cropRight=0,cropBottom=0),
        _cmd("v2-carousel-pointer-motion","setTimelineClipAnimation",
             clipId=_ref("v2-carousel-pointer"),channels=[
                 {"parameter":"frameOffsetX","keyframes":_keys(24,
                    lambda t:95*base.smooth(t,8.5,12.5)/width)},
                 {"parameter":"opacity","keyframes":_keys(24,
                    lambda t:base.smooth(t,8,8.4)*(1-base.smooth(t,12.7,13.2)))}])
    ])
    return plan


def build_landscape():
    """Retain the established landscape composition and fix batch wipes."""
    base.WIDTH, base.HEIGHT, base.FPS = 1920, 1080, 30
    plan = base.build()
    plan = _landscape_library_lighting(plan)
    plan = _landscape_shared_panorama(plan)
    plan = _landscape_pointer(plan)
    return _absolute_frame_offset_curves(_landscape_wipe_upgrade(plan))


def _portrait_setup():
    base.WIDTH, base.HEIGHT, base.FPS = 1080, 1920, 30
    base.STEPS.clear()
    base.LANES.clear()


def _portrait_caption(prefix, start, duration, heading, detail, *, serif=False):
    x, chapter_y = 62, 160
    chapter = base.title(prefix + "-chapter", "02 / LIBRARY & WORKFLOW",
        base.lane_pool("portrait-library-copy", "graphics"), x, chapter_y, 21,
        family="Space Mono", color="#d7ff48", spacing=1.8,
        start=start, duration=duration)
    title_y = 209
    title_clip = base.title(prefix + "-heading", heading,
        base.lane_pool("portrait-library-copy", "graphics"), x, title_y, 108,
        family="Instrument Serif" if serif else "Anton", italic=serif,
        color="#f2eee1", spacing=-1.8, line_height=1.01,
        start=start, duration=duration)
    detail_y = title_y + (2 if "\n" in heading else 1) * 108 * 1.01 + 28
    detail_clip = base.title(prefix + "-detail", detail,
        base.lane_pool("portrait-library-copy", "graphics"), x, detail_y, 36,
        family="Space Grotesk", color="#c9ced6", spacing=0,
        line_height=1.4, start=start, duration=duration)
    _animate(prefix + "-chapter-enter", chapter, duration, {
        "opacity": lambda t: base.out(t, 0, .55),
        "frameOffsetY": lambda t: 45 * (1 - base.out(t, 0, .75)) / base.HEIGHT})
    _animate(prefix + "-heading-enter", title_clip, duration, {
        "opacity": lambda t: base.out(t, .05, .60),
        "frameOffsetY": lambda t: 45 * (1 - base.out(t, .05, .80)) / base.HEIGHT})
    _animate(prefix + "-detail-enter", detail_clip, duration, {
        "opacity": lambda t: base.out(t, .2, .75),
        "frameOffsetY": lambda t: 45 * (1 - base.out(t, .2, .95)) / base.HEIGHT})


def _portrait_footer(prefix, left, right, start, duration):
    track = base.lane_pool("portrait-footer-copy", "graphics")
    base.title(prefix + "-footer-left", left, track, 62, 1769, 17,
        family="Space Mono", color="#aeb6c3", spacing=1,
        start=start, duration=duration)
    base.title(prefix + "-footer-right", right, track, 1018, 1769, 17,
        family="Space Mono", color="#aeb6c3", spacing=1, alignment="right",
        start=start, duration=duration)


def _portrait_grid_card(index, photo_id, start, duration, *, search=False):
    sx, sy = 40, 710
    x, y = sx + (index % 4) * 248, sy + 35 + (index // 4) * 230
    w, h, photo_h = 232, 210, 173
    graphics = base.lane_pool("portrait-library-graphics", "graphics")
    media = base.lane_pool("portrait-library-media", "video")
    copy = base.lane_pool("portrait-library-copy", "graphics")
    bg = base.graphic(f"p-lib-{start}-{photo_id}-card", graphics, x, y, w, h,
        start=start, duration=duration, shape="rounded-rectangle",
        fill="#17202b", radius=9,
        shadow=(not search) or (search and photo_id not in MATCHES))
    image = base.image(f"p-lib-{start}-{photo_id}-image",
        f"creative-polish-v2/{photo_id}.jpg", media, start, duration,
        label=photo_id)
    base.frame(f"p-lib-{start}-{photo_id}-image-frame", image,
        x, y, w, photo_h, radius=9)
    label_color = "#d7ff48" if search and photo_id in MATCHES else "#c1c7d0"
    label = base.title(f"p-lib-{start}-{photo_id}-label", photo_id + ".CR2",
        copy, x + 12, y + h - 25, 12, family="Space Mono", color=label_color,
        spacing=.4, start=start, duration=duration)
    if not search:
        _animate(f"p-lib-{start}-{photo_id}-entrance", image, duration, {
            "opacity": lambda t, i=index: base.out(t, i * .035, .6 + i * .035),
            "frameOffsetY": lambda t, i=index: 80 *
                (1 - base.out(t, i * .035, .9 + i * .035)) / base.HEIGHT,
            "frameRotationDegrees": lambda t, i=index: (i - 6) * 3 *
                (1 - base.out(t, 0, 1.2))})
        for suffix, clip in (("bg", bg), ("label", label)):
            _animate(f"p-lib-{start}-{photo_id}-{suffix}-entrance", clip,
                duration, {
                    "opacity": lambda t, i=index:
                        base.out(t, i * .035, .6 + i * .035),
                    "frameOffsetY": lambda t, i=index: 80 *
                        (1 - base.out(t, i * .035, .9 + i * .035)) / base.HEIGHT,
                    "frameRotationDegrees": lambda t, i=index: (i - 6) * 3 *
                        (1 - base.out(t, 0, 1.2))})
    elif photo_id not in MATCHES:
        for suffix, clip in (("bg", bg), ("image", image), ("label", label)):
            _animate(f"p-lib-search-{photo_id}-{suffix}-dim", clip, duration,
                {"opacity": lambda t: 1 - .86 * base.smooth(t, 1, 2.2)})
    else:
        outline = base.graphic(f"p-lib-search-{photo_id}-outline", graphics,
            x - 2, y - 2, w + 4, h + 4, start=start, duration=duration,
            shape="rounded-rectangle", fill="#00000000", stroke="#d7ff48",
            stroke_width=2, radius=9)
        _animate(f"p-lib-search-{photo_id}-outline-on", outline, duration,
                 {"opacity": lambda t: 1.0})
    return bg, image, label


def _portrait_pill(step, text, x, y, start, duration, *, color="#f2eee1"):
    graphics = base.lane_pool("portrait-library-graphics", "graphics")
    copy = base.lane_pool("portrait-library-copy", "graphics")
    width = len(text) * 19 * .62 + 38
    height = 19 + 26
    bg = base.graphic(step + "-shape", graphics, x, y, width, height,
        start=start, duration=duration, shape="rounded-rectangle",
        fill="#0b0d12bd", stroke="#c4c8ce55", stroke_width=1,
        radius=height / 2)
    text_clip = base.title(step + "-text", text, copy, x + 18, y + 13, 19,
        family="Space Mono", color=color, spacing=.3,
        start=start, duration=duration)
    return bg, text_clip, width


def _portrait_gallery_delivery(start, duration, *, batch):
    copy = base.lane_pool("portrait-gallery-copy", "graphics")
    graphics = base.lane_pool("portrait-gallery-graphics", "graphics")
    media = base.lane_pool("portrait-gallery-media", "video")
    base.title(f"p-gallery-{start}-chapter", "02 / LIBRARY & WORKFLOW",
        copy, 65, 160, 21, family="Space Mono", color="#d7ff48", spacing=1.8,
        start=start, duration=duration)
    heading = "A consistent finish.\nAcross the gallery." if batch else \
              "For the moments\nyou only get once."
    base.title(f"p-gallery-{start}-heading", heading, copy, 65, 210, 112,
        family="Anton", color="#f2eee1", start=start, duration=duration)
    detail = "Apply a shared adjustment. Keep individual control." if batch else \
             "Cull the shoot. Refine the keepers. Deliver a considered set."
    base.title(f"p-gallery-{start}-detail", detail, copy, 65, 465, 36,
        family="Space Grotesk", color="#f2eee1", spacing=0,
        start=start, duration=duration)
    margin, gap, card_h, top = 45, 12, 720, 730
    card_w = (base.WIDTH - margin * 2 - gap * 2) / 3
    if not batch:
        for i, name in enumerate(WEDDINGS):
            x = margin + i * (card_w + gap)
            clip = base.image(f"p-gallery-{start}-{name}-photo",
                f"creative-polish-v2/{name}/batch-before.png", media,
                start, duration, label=name)
            base.frame(f"p-gallery-{start}-{name}-frame", clip,
                x, top, card_w, card_h, radius=5)
            _animate(f"p-gallery-{start}-{name}-arrival", clip, duration, {
                "opacity": lambda t, i=i: base.out(t, i * .12, .55 + i * .12),
                "frameOffsetY": lambda t, i=i: 35 *
                    (1 - base.out(t, i * .12, .9 + i * .12)) / base.HEIGHT})
            label_width, label_height = _label_box(17, "WEDDING / SELECTED FRAME")
            label_x, label_y = x + 14, top + card_h - 18 - label_height
            label_bg = base.graphic(f"p-gallery-{start}-{name}-label-bg",
                graphics, label_x, label_y, label_width, label_height,
                start=start, duration=duration, fill="#09101bd9")
            label = base.title(f"p-gallery-{start}-{name}-label",
                "WEDDING / SELECTED FRAME", copy, x + 26,
                label_y + 9, 17, family="Space Mono", color="#f2eee1",
                spacing=.4, start=start, duration=duration)
            for suffix, target in (("label-bg", label_bg), ("label", label)):
                _animate(f"p-gallery-{start}-{name}-{suffix}-arrival", target,
                    duration, {"opacity": lambda t, i=i:
                        base.out(t, i * .12, .55 + i * .12)})
    else:
        for i, name in enumerate(WEDDINGS):
            x = margin + i * (card_w + gap)
            before = base.image(f"p-gallery-{start}-{name}-before",
                f"creative-polish-v2/{name}/batch-before.png", media,
                start, duration, label=name + " before")
            after = base.image(f"p-gallery-{start}-{name}-after",
                f"creative-polish-v2/{name}/batch-after.png", media,
                start, duration, label=name + " after")
            base.frame(f"p-gallery-{start}-{name}-before-frame", before,
                x, top, card_w, card_h, radius=5, rotation=[-3, 0, 3][i])
            base.frame(f"p-gallery-{start}-{name}-after-frame", after,
                x, top, card_w, card_h, radius=5, rotation=[-3, 0, 3][i])
            left, right = x / base.WIDTH, (x + card_w) / base.WIDTH
            upper, lower = top / base.HEIGHT, (top + card_h) / base.HEIGHT
            _mask(f"p-gallery-{start}-{name}-wipe-mask", after,
                  left, upper, left, lower, 0.0)
            _animate(f"p-gallery-{start}-{name}-wipe", after, duration, {
                "mask:right": lambda t, i=i, a=left, b=right:
                    a + (b - a) * base.smooth(t, .7 + i * .17, 3.7 + i * .17)})
            label_width, label_height = _label_box(17, "SHARED RECIPE / APPLYING")
            label_x, label_y = x + 14, top + card_h - 18 - label_height
            label_bg = base.graphic(f"p-gallery-{start}-{name}-label-bg",
                graphics, label_x, label_y, label_width, label_height,
                start=start, duration=duration, fill="#09101bd9")
            label_start = "SHARED RECIPE / APPLYING"
            label_end = "SHARED RECIPE / EDITABLE"
            threshold = 3.3830804693476724
            label1 = base.title(f"p-gallery-{start}-{name}-label-applying",
                label_start, copy, x + 26, label_y + 9, 17,
                family="Space Mono", color="#f2eee1", spacing=.4,
                start=start, duration=threshold)
            label2 = base.title(f"p-gallery-{start}-{name}-label-editable",
                label_end, copy, x + 26, label_y + 9, 17,
                family="Space Mono", color="#f2eee1", spacing=.4,
                start=start + threshold, duration=duration - threshold)
            for suffix, target in (("bg", label_bg), ("applying", label1),
                                   ("editable", label2)):
                if i != 1:
                    _animate(f"p-gallery-{start}-{name}-{suffix}-rotation",
                        target, duration if suffix != "editable" else duration-threshold,
                        {"frameRotationDegrees": lambda t, a=[-3, 0, 3][i]: a})
    _portrait_footer(f"p-gallery-{start}",
        "ONE RECIPE. ROOM FOR EVERY PHOTOGRAPH.", "LOOKSY / LIBRARY",
        start, duration)


def build_portrait_library():
    base.create_light(base.track("p-library-light-track", "Library / ambient light", "video"),
        "p-library-light", 62, 17, 62, .27)
    light_track = _ref("p-library-light-track", "trackId")
    for start, duration in ((79, 7), (86, 6)):
        base.add(f"p-library-gallery-light-{start}", "createTimelineGenerator",
            trackId=light_track, generatorKind="desktop-light",
            startSeconds=start, durationSeconds=duration,
            parameters={"timeOffsetSeconds": start, "timeScale": 1,
                        "gather": 0, "opacity": .22})
    captions = [
        (62, 5, "Bring the\nwhole shoot.",
         "Batch import your photographs and footage.\nKeep the work together."),
        (67, 6, "Keep the\nkeepers.",
         "Compare nearby frames. Rate, pick and cull\nbefore the detailed edit."),
        (73, 6, "Remember the scene.\nFind the frame.",
         "Visual AI search finds what is in the frame,\nbeyond a filename.")]
    for start, duration, heading, detail in captions:
        _portrait_caption(f"p-lib-caption-{start}", start, duration, heading, detail)
    for i, photo in enumerate(PHOTOS):
        _portrait_grid_card(i, photo, 62, 5)
    # Three-frame cull/compare board in the responsive Stage at (40, 710).
    graphics = base.lane_pool("portrait-library-graphics", "graphics")
    media = base.lane_pool("portrait-library-media", "video")
    copy = base.lane_pool("portrait-library-copy", "graphics")
    for i, photo in enumerate(["IMG_5053", "IMG_5071", "IMG_5077"]):
        x = 40 + i * 325
        y = 710 + 80 + (-20 if i == 1 else 20)
        w, h = 300, 520
        bg = base.graphic(f"p-lib-cull-{photo}-card", graphics, x, y, w, h,
            start=67, duration=6, shape="rounded-rectangle", fill="#15202d",
            radius=12, shadow=True)
        photo_clip = base.image(f"p-lib-cull-{photo}-image",
            f"creative-polish-v2/{photo}.jpg", media, 67, 6, label=photo)
        base.frame(f"p-lib-cull-{photo}-frame", photo_clip, x, y, w, 440,
                   radius=12)
        first_label = base.title(f"p-lib-cull-{photo}-raw-label", photo + ".CR2",
            copy, x + 20, y + 465, 17, family="Space Mono", color="#f2eee1",
            spacing=.4, start=67, duration=2)
        status_text = "★★★★★ / PICK" if i == 1 else "REVIEW NEXT"
        status = base.title(f"p-lib-cull-{photo}-status", status_text, copy,
            x + 20, y + 465, 17, family="Space Mono",
            color="#d7ff48" if i == 1 else "#f2eee1", spacing=.4,
            start=69, duration=4)
        outline = base.graphic(f"p-lib-cull-{photo}-outline", graphics,
            x - 3, y - 3, w + 6, h + 6, start=68.8, duration=4.2,
            shape="rounded-rectangle", fill="#00000000",
            stroke="#d7ff48" if i == 1 else "#00000000",
            stroke_width=3, radius=12)
        if i == 1:
            # Group scale is centered on the selected card, matching CSS scale().
            center_x, center_y = x + w / 2, y + h / 2
            for suffix, target, ex, ey, ew, eh in [
                ("bg", bg, x, y, w, h),
                ("image", photo_clip, x, y, w, 440),
                ("label", status, x + 20, y + 465, 220, 30),
                ("outline", outline, x - 3, y - 3, w + 6, h + 6)]:
                item_cx, item_cy = ex + ew / 2, ey + eh / 2
                _animate(f"p-lib-cull-{photo}-{suffix}-selected-zoom", target, 6, {
                    "frameScale": lambda t: 1 + .08 * base.smooth(t, 1.3, 2.7),
                    "frameOffsetX": lambda t, dx=item_cx-center_x:
                        dx * .08 * base.smooth(t, 1.3, 2.7) / base.WIDTH,
                    "frameOffsetY": lambda t, dy=item_cy-center_y:
                        dy * .08 * base.smooth(t, 1.3, 2.7) / base.HEIGHT})
        else:
            for suffix, target in (("bg", bg), ("image", photo_clip),
                                   ("label", first_label)):
                _animate(f"p-lib-cull-{photo}-{suffix}-deemphasis", target, 6, {
                    "opacity": lambda t: 1 - .7 * base.smooth(t, 1.8, 3.2),
                    "frameOffsetY": lambda t: 80 * base.smooth(t, 2.3, 3.8) /
                        base.HEIGHT})
    for i, value in enumerate(["COMPARE", "RATE", "PICK", "CULL"]):
        _portrait_pill(f"p-lib-cull-tool-{i}", value, 180 + i * 160,
            1445, 67, 6, color="#d7ff48" if i == 0 else "#f2eee1")
    for i, photo in enumerate(PHOTOS):
        _portrait_grid_card(i, photo, 73, 6, search=True)
    search_x, search_y, search_w, search_h = 115, 1450, 830, 74
    graphics = base.lane_pool("portrait-library-graphics", "graphics")
    copy = base.lane_pool("portrait-library-copy", "graphics")
    base.graphic("p-lib-search-field", graphics, search_x, search_y,
        search_w, search_h, start=73, duration=6, shape="rounded-rectangle",
        fill="#141e2e", stroke="#94a6bd88", stroke_width=1,
        radius=40)
    base.title("p-lib-search-symbol", "⌕", copy, search_x + 28,
        search_y + 18, 27, family="Space Grotesk", color="#d7ff48",
        spacing=0, start=73, duration=6)
    query = "mountains in warm light"
    for count in range(1, len(query) + 1):
        at, end = count / 16, min(6, (count + 1) / 16)
        base.title(f"p-lib-search-query-{count:02d}", query[:count], copy,
            search_x + 68, search_y + 19, 27, family="Space Grotesk",
            color="#f2eee1", spacing=0, start=73 + at,
            duration=max(.0001, end - at))
    base.title("p-lib-search-label", "VISUAL AI SEARCH", copy,
        search_x + search_w - 210, search_y + 25, 12,
        family="Space Mono", color="#94a4b7", spacing=.3,
        start=73, duration=6)
    _portrait_footer("p-lib", "FIND THE FRAME YOU REMEMBER.",
                     "LOOKSY / LIBRARY", 73, 6)
    _portrait_gallery_delivery(79, 7, batch=False)
    _portrait_gallery_delivery(86, 6, batch=True)


def _landscape_or_portrait_page_mask(step, clip, x, y, width, height, corners=0):
    left, top = x / base.WIDTH, y / base.HEIGHT
    _mask(step, clip, left, top, (x + width) / base.WIDTH,
          (y + height) / base.HEIGHT, corners)


def _phone_slide(name, clip, duration, content_w, *, show=True):
    def swipe(t):
        return base.smooth(t, 18, 19.3) + base.smooth(t, 20.7, 22)
    channels = {"frameOffsetX": lambda t: -content_w * swipe(t) / base.WIDTH}
    if show:
        channels["opacity"] = lambda t: base.smooth(t, 15, 17)
    _animate(name, clip, duration, channels)


def _phone_title(name, text, track, panel, x_design, y_design, size,
                 content_x, content_y, content_w, content_scale, duration,
                 *, family="Space Mono", color="#f2eee1", spacing=0,
                 line_height=1.01, italic=False, alignment="left"):
    local_x = x_design - panel * 1080
    x = content_x + panel * content_w + local_x * content_scale
    y = content_y + y_design * content_scale
    clip = base.title(name, text, track, x, y, size * content_scale,
        family=family, color=color, spacing=spacing * content_scale,
        line_height=line_height, italic=italic, alignment=alignment,
        start=160, duration=duration)
    _landscape_or_portrait_page_mask(name + "-viewport", clip, content_x,
        content_y, content_w, content_w * 1.25)
    _phone_slide(name + "-swipe", clip, duration, content_w)
    return clip


def _phone_graphic(name, track, panel, x_design, y_design, width_design,
                   height_design, content_x, content_y, content_w,
                   content_scale, duration, *, shape="rectangle",
                   fill="#00000000", stroke="#00000000", stroke_width=0,
                   radius=0, rotation=0):
    local_x = x_design - panel * 1080
    x = content_x + panel * content_w + local_x * content_scale
    y = content_y + y_design * content_scale
    clip = base.graphic(name, track, x, y, width_design * content_scale,
        height_design * content_scale, start=160, duration=duration,
        shape=shape, fill=fill, stroke=stroke,
        stroke_width=stroke_width * content_scale,
        radius=radius * content_scale, rotation=rotation)
    _landscape_or_portrait_page_mask(name + "-viewport", clip, content_x,
        content_y, content_w, content_w * 1.25)
    _phone_slide(name + "-swipe", clip, duration, content_w)
    return clip


def _phone_vector(name, track, panel, view_width, view_height, commands,
                  x_design, y_design, width_design, height_design,
                  content_x, content_y, content_w, content_scale, duration,
                  *, stroke="#B3F2EEE1", fill="#00000000", stroke_width=2):
    local_x = x_design - panel * 1080
    x = content_x + panel * content_w + local_x * content_scale
    y = content_y + y_design * content_scale
    path = {"viewBoxWidth": view_width, "viewBoxHeight": view_height,
        "commands": commands, "frames": [], "stroke": stroke,
        "fill": fill, "strokeWidth": stroke_width}
    clip = base.add(name, "createTimelineGenerator", trackId=track,
        generatorKind="vector-path", startSeconds=160, durationSeconds=duration,
        parameters={"vectorPath": path})
    base.add(name + "-frame", "setTimelineClipFrame", clipId=clip,
        offsetX=(x + width_design * content_scale / 2) / base.WIDTH - .5,
        offsetY=(y + height_design * content_scale / 2) / base.HEIGHT - .5,
        scale=1, rotationDegrees=0, perspectiveX=0, perspectiveY=0,
        opacity=1, objectFit="stretch", width=width_design * content_scale / base.WIDTH,
        height=height_design * content_scale / base.HEIGHT, cornerRadius=0,
        cropLeft=0, cropTop=0, cropRight=0, cropBottom=0)
    _landscape_or_portrait_page_mask(name + "-viewport", clip, content_x,
        content_y, content_w, content_w * 1.25)
    _phone_slide(name + "-swipe", clip, duration, content_w)
    return clip


def _portrait_carousel():
    duration, start = 24, 160
    light_track = base.track("p-carousel-light-track", "Carousel / ambient light", "video")
    image_track = base.lane_pool("p-carousel-media", "video")
    effects_track = base.lane_pool("p-carousel-effects", "video")
    graphics_track = base.lane_pool("p-carousel-graphics", "graphics")
    copy_track = base.lane_pool("p-carousel-copy", "graphics")
    phone_media = base.lane_pool("p-carousel-phone-media", "video")
    phone_fx = base.lane_pool("p-carousel-phone-effects", "video")
    phone_graphics = base.lane_pool("p-carousel-phone-graphics", "graphics")
    phone_copy = base.lane_pool("p-carousel-phone-copy", "graphics")
    path_track = base.track("p-carousel-vector-track", "Carousel / editable line art", "video")
    base.create_light(light_track, "p-carousel-light", start, duration, start, .30)
    # Responsive header follows BuyerCarousel's portrait left/top and sizes.
    base.title("p-carousel-heading-first", "One photograph.\nAcross every canvas.",
        copy_track, 65, 209, 110, start=160, duration=15)
    base.title("p-carousel-detail-first",
        "Compose the whole story. Export individual panels.", copy_track,
        65, 455, 36, family="Space Grotesk", color="#c9ced6", spacing=0,
        start=160, duration=15)
    base.title("p-carousel-heading-phone", "Designed together.\nReady to swipe.",
        copy_track, 65, 209, 110, start=175, duration=9)
    base.title("p-carousel-detail-phone",
        "The same composition. Three separate 4:5 images.", copy_track,
        65, 455, 36, family="Space Grotesk", color="#c9ced6", spacing=0,
        start=175, duration=9)

    # Three page viewports show one continuous 3240 x 1350 native photo layer.
    # Sequence masks reproduce the source wrappers; keyframes open their gap,
    # pan the shared image, and shrink/fade the desktop row as the phone arrives.
    scale, pw, ph = .27, 1080 * .27, 1350 * .27
    final_gap, row_y = 14, 790
    final_left = (base.WIDTH - (3 * pw + 2 * final_gap)) / 2
    for panel in range(3):
        image = base.image(f"p-carousel-panorama-panel-{panel}",
            "creative-polish-v2/IMG_5071.jpg", image_track, start, duration,
            label=f"shared panorama {panel + 1}")
        base.add(f"p-carousel-panorama-panel-{panel}-frame", "setTimelineClipFrame",
            clipId=image,
            offsetX=(final_left + panel * final_gap + 3 * pw / 2) / base.WIDTH - .5,
            offsetY=(row_y + ph / 2) / base.HEIGHT - .5,
            scale=1.12, rotationDegrees=0, perspectiveX=0, perspectiveY=0,
            opacity=1, objectFit="fill", width=3 * pw / base.WIDTH,
            height=ph / base.HEIGHT, cornerRadius=0,
            cropLeft=0, cropTop=0, cropRight=0, cropBottom=0)
        _mask(f"p-carousel-panorama-panel-{panel}-viewport", image,
            (final_left + panel * (pw + final_gap)) / base.WIDTH,
            row_y / base.HEIGHT,
            (final_left + panel * (pw + final_gap) + pw) / base.WIDTH,
            (row_y + ph) / base.HEIGHT, 0)
        def row_channels(t, p=panel, cx=final_left, cw=pw, ch=ph,
                         clip=image):
            del clip
            split = base.smooth(t, 6, 8)
            phone = base.smooth(t, 15, 17)
            gap = final_gap * split
            group_scale = 1 - .08 * phone
            left = (base.WIDTH - (3 * cw + 2 * gap)) / 2
            top = row_y - 40 * phone
            return left, top, gap, group_scale
        def mask_edge(t, edge, p=panel):
            left, top, gap, s = row_channels(t, p)
            pane_x = left + p * (pw + gap) * s
            pane_y = top
            pane_w, pane_h = pw * s, ph * s
            return {"left": pane_x, "top": pane_y,
                    "right": pane_x + pane_w, "bottom": pane_y + pane_h}[edge] / (base.WIDTH if edge in ("left", "right") else base.HEIGHT)
        def pan(t):
            return (-110 + 220 * base.smooth(t, 8.5, 12.5)) * scale / base.WIDTH
        def frame_x(t, p=panel):
            left, top, gap, s = row_channels(t, p)
            _ = top
            base_left = final_left + p * final_gap
            base_center = base_left + 3 * pw / 2
            # BuyerCarousel offsets the 3240px artwork by -panel*1080 inside
            # each viewport; in stage coordinates its origin is left+panel*gap.
            target_center = left + (p * gap + 3 * pw / 2) * s
            pan_pixels = (-110 + 220 * base.smooth(t, 8.5, 12.5)) * scale * s
            return (target_center - base_center + pan_pixels) / base.WIDTH
        _animate(f"p-carousel-panorama-panel-{panel}-motion", image, duration, {
            "frameScale": lambda t: 1.12 * (1 - .08 * base.smooth(t, 15, 17)),
            "frameOffsetX": frame_x,
            "frameOffsetY": lambda t: (-40 * base.smooth(t, 15, 17) +
                (row_y + ph / 2 - row_y) * (-.08 * base.smooth(t, 15, 17))) / base.HEIGHT,
            "opacity": lambda t: 1 - base.smooth(t, 15, 17),
            "mask:left": lambda t: mask_edge(t, "left"),
            "mask:top": lambda t: mask_edge(t, "top"),
            "mask:right": lambda t: mask_edge(t, "right"),
            "mask:bottom": lambda t: mask_edge(t, "bottom"),
            "mask:cornerRadius": lambda t: 3 * base.smooth(t, 6, 8) / min(pw, ph)})

    # Source 3240x1350 gradient and pane separators remain native, editable.
    for panel in range(3):
        x = final_left + panel * (pw + final_gap)
        gradient = base.create_reference_gradient(effects_track,
            f"p-carousel-panel-{panel}-gradient", start, duration,
            x, row_y, pw, ph)
        _mask(f"p-carousel-panel-{panel}-gradient-mask", gradient,
            (final_left + panel * (pw + final_gap)) / base.WIDTH,
            row_y / base.HEIGHT,
            (final_left + panel * (pw + final_gap) + pw) / base.WIDTH,
            (row_y + ph) / base.HEIGHT, 0)
        _row_art_mask_and_motion(f"p-carousel-panel-{panel}-gradient",
            gradient, panel, x, row_y, pw, ph)
        border = base.graphic(f"p-carousel-panel-{panel}-separator",
            graphics_track, x, row_y, pw, ph, start=start, duration=duration,
            shape="rounded-rectangle", fill="#00000000", stroke="#d8e4dc66",
            stroke_width=1, radius=3)
        _mask(f"p-carousel-panel-{panel}-separator-mask", border,
            x/base.WIDTH, row_y/base.HEIGHT, (x+pw)/base.WIDTH,
            (row_y+ph)/base.HEIGHT, 3/min(pw,ph))
        _row_art_mask_and_motion(f"p-carousel-panel-{panel}-separator",
            border, panel, x, row_y, pw, ph)

    # Independent page artwork is clipped to its responsive panel and follows
    # the same 14px gap / 0.92 scale transform as the source Stage.
    def row_title(step, text, panel, x_design, y_design, size, **kw):
        local_x = x_design - panel * 1080
        x = final_left + panel * (pw + final_gap) + local_x * scale
        y = row_y + y_design * scale
        clip = base.title(step, text, copy_track, x, y, size*scale,
            start=start, duration=duration, **kw)
        _row_art_mask_and_motion(step, clip, panel, x, y, size*scale, 100)
        return clip

    def row_shape(step, panel, x_design, y_design, w_design, h_design, **kw):
        local_x = x_design - panel * 1080
        x = final_left + panel*(pw+final_gap) + local_x*scale
        y = row_y + y_design*scale
        clip = base.graphic(step, graphics_track, x, y, w_design*scale,
            h_design*scale, start=start, duration=duration, **kw)
        _row_art_mask_and_motion(step, clip, panel, x, y, w_design*scale, h_design*scale)
        return clip

    for panel in range(3):
        row_title(f"p-carousel-art-header-left-{panel}", "FIELD JOURNAL / NEPAL",
            panel, panel*1080+76, 69, 25, family="Space Mono", color="#f2eee1",
            spacing=1.5)
        row_title(f"p-carousel-art-header-right-{panel}", f"0{panel+1} — 03",
            panel, panel*1080+1004, 69, 25, family="Space Mono", color="#f2eee1",
            spacing=1.5, alignment="right")
        row_shape(f"p-carousel-art-top-rule-{panel}", panel,
            panel*1080 + (76 if panel != 1 else 1080), 124,
            1004 if panel != 1 else 1080, 2, fill="#7af2eee1")
    row_title("p-carousel-art-take-the", "Take the", 0, 72, 529, 190,
        family="Instrument Serif", italic=True, color="#f2eee1", spacing=-4)
    row_title("p-carousel-art-long", "LONG", 0, 69, 719, 365,
        family="Anton", color="#f2eee1", spacing=-3)
    row_title("p-carousel-art-way", "WAY.", 1, 1080, 719, 365,
        family="Anton", color="#f2eee1", spacing=-3)
    row_title("p-carousel-art-room", "Some things\nneed a little\nmore room.",
        2, 2247, 727, 115, family="Instrument Serif", italic=True,
        color="#f2eee1", spacing=-2, line_height=1.01)
    row_shape("p-carousel-art-first-light", 1, 1770, 172, 275, 94,
        fill="#f0834f", shape="rectangle", rotation=-5)
    row_title("p-carousel-art-first-light-label", "FIRST LIGHT ↗", 1,
        1780, 203, 27, family="Space Mono", color="#182331", spacing=1)
    # The landscape plan keeps its segmented editable line art. Portrait and
    # phone use vector-path below, which matches the source SVG path directly.
    for panel, copy in enumerate(["A LITTLE FURTHER.",
                                  "A DIFFERENT PERSPECTIVE.",
                                  "KEEP THE CURIOSITY."]):
        row_shape(f"p-carousel-art-footer-rule-{panel}", panel,
            panel*1080+76, 1254, 928, 2, fill="#7af2eee1")
        row_title(f"p-carousel-art-footer-copy-{panel}", copy, panel,
            panel*1080+76, 1278, 25, family="Space Mono", color="#f2eee1")
        row_title(f"p-carousel-art-footer-right-{panel}",
            "END / BEGIN AGAIN" if panel == 2 else "SWIPE →", panel,
            panel*1080+1004, 1278, 25, family="Space Mono",
            color="#f2eee1", alignment="right")

    # Pointer is a true editable native vector layer, visible only during pan.
    pointer_track = path_track
    pointer_path = {"viewBoxWidth": 40, "viewBoxHeight": 48,
        "commands":[{"verb":"M","points":[1,1]}, {"verb":"L","points":[1,31]},
        {"verb":"L","points":[9,24]}, {"verb":"L","points":[18,41]},
        {"verb":"L","points":[25,37]}, {"verb":"L","points":[16,21]},
        {"verb":"L","points":[31,21]}, {"verb":"Z","points":[]}],
        "frames":[], "stroke":"#FF07111D", "fill":"#FFFFFFFF", "strokeWidth":2}
    pointer = base.add("p-carousel-pointer", "createTimelineGenerator",
        trackId=pointer_track, generatorKind="vector-path", startSeconds=160,
        durationSeconds=24, parameters={"vectorPath":pointer_path})
    pointer_x0 = pw*1.4 - 25
    pointer_y = row_y + ph*.55
    base.add("p-carousel-pointer-frame", "setTimelineClipFrame", clipId=pointer,
        offsetX=(pointer_x0+20)/base.WIDTH-.5,
        offsetY=(pointer_y+24)/base.HEIGHT-.5, scale=1, rotationDegrees=0,
        perspectiveX=0,perspectiveY=0,opacity=0,objectFit="stretch",
        width=40/base.WIDTH,height=48/base.HEIGHT,cornerRadius=0,
        cropLeft=0,cropTop=0,cropRight=0,cropBottom=0)
    _animate("p-carousel-pointer-pan", pointer, duration, {
        "frameOffsetX": lambda t: 95*base.smooth(t,8.5,12.5)/base.WIDTH,
        "opacity": lambda t: base.smooth(t,8,8.4)*(1-base.smooth(t,12.7,13.2))})

    # Phone is an actual horizontally swiped, clipped native layer stack.
    phone_w, phone_left, phone_top = 550, (base.WIDTH-550)/2, 720
    content_w, content_scale = phone_w-28, (phone_w-28)/1080
    content_x, content_y = phone_left+14, phone_top+60
    content_h = content_w*1.25
    for panel in range(3):
        image = base.image(f"p-carousel-phone-image-{panel}",
            "creative-polish-v2/IMG_5071.jpg", phone_media, start, duration,
            label=f"phone page {panel+1}")
        base.frame(f"p-carousel-phone-image-{panel}-frame", image,
            content_x+panel*content_w, content_y, content_w, content_h,
            fit="fill", crop=base.panorama_crop(panel, pan=110))
        _landscape_or_portrait_page_mask(f"p-carousel-phone-image-{panel}-viewport",
            image, content_x, content_y, content_w, content_h)
        _phone_slide(f"p-carousel-phone-image-{panel}-swipe", image,
            duration, content_w)
    gradient = base.add("p-carousel-phone-gradient", "createTimelineGenerator",
        trackId=base.lane_track(phone_fx, start, duration),
        generatorKind="linear-gradient", startSeconds=start,
        durationSeconds=duration, parameters={"angleDegrees":180,"stops":[
            {"position":0,"color":"#0e2336","opacity":0x37/255},
            {"position":.32,"color":"#000000","opacity":0},
            {"position":.47,"color":"#15283b","opacity":0x22/255},
            {"position":1,"color":"#081522","opacity":0xdc/255}]})
    base.frame(f"p-carousel-phone-gradient-frame", gradient, content_x,
        content_y, content_w, content_h, fit="stretch")
    _landscape_or_portrait_page_mask("p-carousel-phone-gradient-mask", gradient,
        content_x, content_y, content_w, content_h)
    _animate("p-carousel-phone-gradient-appear", gradient, duration,
             {"opacity":lambda t:base.smooth(t,15,17)})

    for panel in range(3):
        _phone_title(f"p-carousel-phone-{panel}-header-left",
            "FIELD JOURNAL / NEPAL", phone_copy, panel,
            panel*1080+76, 69, 25, content_x, content_y, content_w,
            content_scale, duration, family="Space Mono", spacing=.5)
        _phone_title(f"p-carousel-phone-{panel}-header-right",
            f"0{panel+1} — 03", phone_copy, panel,
            panel*1080+1004, 69, 25, content_x, content_y, content_w,
            content_scale, duration, family="Space Mono", spacing=.5,
            alignment="right")
        _phone_graphic(f"p-carousel-phone-{panel}-top-rule", phone_graphics,
            panel, panel*1080+76, 124, 928, 2, content_x, content_y,
            content_w, content_scale, duration, fill="#7af2eee1")
        if panel == 0:
            _phone_title("p-carousel-phone-take-the", "Take the", phone_copy,
                panel,72,529,190,content_x,content_y,content_w,content_scale,
                duration,family="Instrument Serif",color="#f2eee1",spacing=-4,italic=True)
            _phone_title("p-carousel-phone-long", "LONG", phone_copy,
                panel,69,719,365,content_x,content_y,content_w,content_scale,
                duration,family="Anton",color="#f2eee1",spacing=-3)
        elif panel == 1:
            _phone_title("p-carousel-phone-way", "WAY.", phone_copy,
                panel,1080,719,365,content_x,content_y,content_w,content_scale,
                duration,family="Anton",color="#f2eee1",spacing=-3)
            _phone_graphic("p-carousel-phone-first-light", phone_graphics,
                panel,1770,172,275,94,content_x,content_y,content_w,
                content_scale,duration,fill="#f0834f",rotation=-5)
            _phone_title("p-carousel-phone-first-light-label", "FIRST LIGHT ↗",
                phone_copy,panel,1780,203,27,content_x,content_y,content_w,
                content_scale,duration,family="Space Mono",color="#182331",spacing=1)
            _phone_vector("p-carousel-phone-mountain",path_track,panel,320,300,
                [{"verb":"M","points":[28,266]},{"verb":"L","points":[126,77]},
                 {"verb":"L","points":[159,154]},{"verb":"L","points":[220,22]},
                 {"verb":"L","points":[294,266]}],1438,241,320,300,content_x,
                content_y,content_w,content_scale,duration,stroke="#B3F2EEE1",
                stroke_width=2)
            _phone_vector("p-carousel-phone-mountain-base",path_track,panel,320,300,
                [{"verb":"M","points":[28,286]},{"verb":"L","points":[294,286]}],
                1438,241,320,300,content_x,content_y,content_w,content_scale,
                duration,stroke="#80F2EEE1",stroke_width=1)
            _phone_graphic("p-carousel-phone-mountain-sun",phone_graphics,
                panel,1631,254,18,18,content_x,content_y,content_w,
                content_scale,duration,shape="ellipse",fill="#f0834f")
        else:
            _phone_title("p-carousel-phone-room", "Some things\nneed a little\nmore room.",
                phone_copy,panel,2247,727,115,content_x,content_y,content_w,
                content_scale,duration,family="Instrument Serif",color="#f2eee1",
                spacing=-2,line_height=1.01,italic=True)
        footer_text = ["A LITTLE FURTHER.","A DIFFERENT PERSPECTIVE.",
                       "KEEP THE CURIOSITY."][panel]
        _phone_graphic(f"p-carousel-phone-{panel}-footer-rule",phone_graphics,
            panel,panel*1080+76,1254,928,2,content_x,content_y,content_w,
            content_scale,duration,fill="#7af2eee1")
        _phone_title(f"p-carousel-phone-{panel}-footer-copy",footer_text,
            phone_copy,panel,panel*1080+76,1278,25,content_x,content_y,
            content_w,content_scale,duration,family="Space Mono",color="#f2eee1")
        _phone_title(f"p-carousel-phone-{panel}-footer-right",
            "END / BEGIN AGAIN" if panel==2 else "SWIPE →",phone_copy,panel,
            panel*1080+1004,1278,25,content_x,content_y,content_w,
            content_scale,duration,family="Space Mono",color="#f2eee1",
            alignment="right")

    # Phone shell, its title bar, page number, swipe indicators and caption.
    shell = base.graphic("p-carousel-phone-shell", graphics_track,
        phone_left, phone_top, phone_w, 807.5, start=start, duration=duration,
        shape="rounded-rectangle", fill="#111822", stroke="#8a98aa80",
        stroke_width=2, radius=34, shadow=True)
    _animate("p-carousel-phone-shell-appear", shell, duration, {
        "opacity":lambda t:base.smooth(t,15,17),
        "frameOffsetY":lambda t:65*(1-base.smooth(t,15,17))/base.HEIGHT})
    base.graphic("p-carousel-phone-brand-dot", graphics_track,
        phone_left+24, phone_top+24, 22, 22, start=start, duration=duration,
        shape="ellipse", fill="#d7ff48")
    base.title("p-carousel-phone-brand", "Field journal", copy_track,
        phone_left+58, phone_top+25, 19, family="Space Grotesk",
        color="#f2eee1", spacing=0, start=start, duration=duration)
    for index, (begin,end) in enumerate([(0,18.65),(18.65,21.35),(21.35,24)]):
        page=base.title(f"p-carousel-phone-page-{index+1}",f"{index+1} / 3",
            copy_track,phone_left+phone_w-75,phone_top+28,14,
            family="Space Mono",color="#f2eee1",spacing=0,
            alignment="right",start=start+begin,duration=end-begin)
    for index in range(3):
        x=phone_left+187+index*15
        base.graphic(f"p-carousel-phone-dot-{index}-inactive",graphics_track,
            x,phone_top+content_h+76,7,7,start=start,duration=duration,
            shape="ellipse",fill="#586574")
        active=base.graphic(f"p-carousel-phone-dot-{index}-active",graphics_track,
            x,phone_top+content_h+76,7,7,start=start,duration=duration,
            shape="ellipse",fill="#d7ff48")
        _animate(f"p-carousel-phone-dot-{index}-state",active,duration,{
            "opacity":lambda t,i=index:base.smooth(t,15,17)*(
                (1-base.smooth(t,18.65,18.66)) if i==0 else
                (base.smooth(t,18.65,18.66)*(1-base.smooth(t,21.35,21.36))) if i==1 else
                base.smooth(t,21.35,21.36))})
    base.title("p-carousel-phone-caption", "Take the long way. ↗", copy_track,
        phone_left+23, phone_top+content_h+98, 17,
        family="Space Grotesk", color="#f2eee1", spacing=0,
        start=start, duration=duration)

    # Responsive output pills sit above the portrait footer and fade with the
    # desktop row, as the source does when the phone is introduced.
    bg,text,width=_portrait_pill("p-carousel-pill-composition",
        "3240 × 1350 / ONE COMPOSITION",65,1680,160,6,color="#d7ff48")
    labels=["01.PNG","02.PNG","03.PNG","1080 × 1350 EACH"]
    x=65
    for i,label in enumerate(labels):
        bg,text,width=_portrait_pill(f"p-carousel-pill-output-{i}",label,
            x,1680,166,18,color="#d7ff48" if i==0 else "#f2eee1")
        x += width+12
        for suffix,target in (("shape",bg),("text",text)):
            _animate(f"p-carousel-pill-output-{i}-{suffix}-fade",target,18,
                {"opacity":lambda t:1-base.smooth(t+6,15,17)})
    _portrait_footer("p-carousel", "COMPOSE ONCE. LET THE STORY CONTINUE.",
                     "LOOKSY / CAROUSEL", 160, 24)


def _row_art_mask_and_motion(name, clip, panel, x, y, width, height):
    """Clip one Carousel artwork layer to its portrait 1080px panel."""
    # This helper is used only by portrait source artwork. Each native clip is
    # independently transformed, but shares the responsive parent keyframes.
    scale, pw, ph = .27, 291.6, 364.5
    gap_max, row_y = 14, 790
    left_final = (base.WIDTH - (3*pw+2*gap_max))/2
    panel_left = left_final + panel*(pw+gap_max)
    _mask(name + "-panel-mask",clip,panel_left/base.WIDTH,row_y/base.HEIGHT,
        (panel_left+pw)/base.WIDTH,(row_y+ph)/base.HEIGHT,0)
    cx,cy=x+width/2,y+height/2
    def split(t):return base.smooth(t,6,8)
    def phone(t):return base.smooth(t,15,17)
    def edge(t,which):
        sp,phn=split(t),phone(t)
        gap=gap_max*sp
        left=(base.WIDTH-(3*pw+2*gap))/2
        s=1-.08*phn
        pane=left+panel*(pw+gap)*s
        top=row_y-40*phn
        rect={"left":pane,"right":pane+pw*s,"top":top,"bottom":top+ph*s,
              "cornerRadius":3*sp/min(pw,ph)}
        return rect[which]/(base.WIDTH if which in ("left","right") else base.HEIGHT)
    def frame_dx(t):
        sp,phn=split(t),phone(t)
        gap=gap_max*sp
        left=(base.WIDTH-(3*pw+2*gap))/2
        # Static item was placed in final-gap coordinates. Return it to the
        # center row as the panes close, then apply the parent scale.
        parent_left=(base.WIDTH-(3*pw+2*gap))/2
        item_local_cx=cx-(left_final+panel*(pw+gap_max))
        target_cx=parent_left+(panel*(pw+gap)+item_local_cx)*(1-.08*phn)
        return (target_cx-cx)/base.WIDTH
    _animate(name+"-motion",clip,24,{
        "frameScale":lambda t:1-.08*phone(t),
        "frameOffsetX":frame_dx,
        "frameOffsetY":lambda t:(-40*phone(t)+(cy-row_y)*(-.08*phone(t)))/base.HEIGHT,
        "opacity":lambda t:1-phone(t),
        "mask:left":lambda t:edge(t,"left"),
        "mask:top":lambda t:edge(t,"top"),
        "mask:right":lambda t:edge(t,"right"),
        "mask:bottom":lambda t:edge(t,"bottom"),
        "mask:cornerRadius":lambda t:edge(t,"cornerRadius")})


def build_portrait():
    _portrait_setup()
    build_portrait_library()
    _portrait_carousel()
    plan = {
        "planVersion":2,
        "reference":"marketing/animated-trailer/src/creative-launch/Photo.tsx::LibraryChapter and BuyerScenes.tsx::BuyerCarousel",
        "chapters":[{"name":"Library & Workflow","startSeconds":62,"endSeconds":92},
                    {"name":"Carousel","startSeconds":160,"endSeconds":184}],
        "canvas":{"width":1080,"height":1920,"fps":30,"orientation":"portrait"},
        "steps":base.STEPS,
        "requiredActionKinds":["media","text","layers","delivery","masking","framing","effects"],
        "nativeBlockers":[
            "The native plan represents CSS shadow blur with native layer shadows; spread and edge softness can differ by platform.",
            "Font substitution and glyph rasterization in Qt can change browser text metrics; inspect the live portrait Program before calling typography exact.",
            "The Carousel is a set of individual clips with synchronized pan, mask and phone-swipe keyframes. Native Timeline has no parent group handle for one-step group edits.",
        "Library batch-reveal masks are sequence-axis aligned; the source clips rotate by up to 3 degrees, so native mask edges do not rotate with their cards.",
            "The DesktopLight generator is native and timed to the source, but shader output needs visible native comparison to establish color parity."
        ],
        "reviewRequired":True,
    }
    return _absolute_frame_offset_curves(plan)


def _audit_references(plan):
    """Fail closed if a generated media source is outside the exact manifest."""
    manifest_path=ROOT/"source-manifest.json"
    manifest=json.loads(manifest_path.read_text())
    records=manifest.get("sources",manifest if isinstance(manifest,list) else [])
    by_path={str(Path(record["path"]).resolve()):record for record in records}
    seen=set()
    for step in plan["steps"]:
        if step["commandId"]!="timeline.action.createTimelineClip":continue
        path=str(Path(step["values"]["filePath"]).resolve())
        if path in seen:continue
        seen.add(path)
        if path not in by_path or not Path(path).is_file():
            raise RuntimeError("Missing exact admitted source manifest entry: "+path)
        record=by_path[path]
        import hashlib
        digest=hashlib.sha256(Path(path).read_bytes()).hexdigest()
        if record.get("bytes")!=Path(path).stat().st_size or record.get("sha256")!=digest:
            raise RuntimeError("Source manifest identity changed: "+path)
    return {"uniqueMediaPaths":len(seen),"manifest":"marketing/native-recreation-v4/source-manifest.json"}


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--orientation",choices=["landscape","portrait","both"],default="both")
    args=parser.parse_args()
    outputs=[]
    if args.orientation in ("landscape","both"):
        plan=build_landscape()
        plan["assetAudit"]=_audit_references(plan)
        target=ROOT/"library-carousel-v2-landscape.commands.json"
        target.write_text(json.dumps(plan,indent=2,ensure_ascii=False)+"\n")
        outputs.append((target,len(plan["steps"]),plan["assetAudit"]["uniqueMediaPaths"]))
    if args.orientation in ("portrait","both"):
        plan=build_portrait()
        plan["assetAudit"]=_audit_references(plan)
        target=ROOT/"library-carousel-v2-portrait.commands.json"
        target.write_text(json.dumps(plan,indent=2,ensure_ascii=False)+"\n")
        outputs.append((target,len(plan["steps"]),plan["assetAudit"]["uniqueMediaPaths"]))
    for path,count,assets in outputs:
        print(path, count, "commands", assets, "exact manifest assets")


if __name__=="__main__":
    main()
