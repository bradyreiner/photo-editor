"""Build typed native MCP plans for the reference Library and Carousel chapters.

The generated JSON is an authoring plan only. It is consumed by author_native.py
after a native Timeline task explicitly admits its exact assets and output path.
It never writes Timeline project internals and never contacts the live app.
"""
from pathlib import Path
import json
import math

ROOT = Path(__file__).resolve().parent
MEDIA = ROOT.parent / "animated-trailer/public/media"
WIDTH, HEIGHT = 1920, 1080
FPS = 30
STEPS = []
LANES = {}


def lane_pool(name, kind):
    return {"__lanePool": name, "__laneKind": kind}


def lane_track(target, start, duration):
    if not isinstance(target, dict) or "__lanePool" not in target:
        return target
    pool_name = target["__lanePool"]
    kind = target["__laneKind"]
    lanes = LANES.setdefault(pool_name, [])
    for item in lanes:
        if item["end"] <= start + 1e-9:
            item["end"] = start + duration
            return item["ref"]
    index = len(lanes) + 1
    step_id = f"lane-{pool_name}-{index:02d}"
    display = pool_name.replace("-", " ").title()[:36]
    add(step_id, "createTimelineTrack", kind=kind,
        name=f"{display} {index:02d}", audioRole="")
    result = ref(step_id)
    lanes.append({"end": start + duration, "ref": result})
    return result


def add(step_id, action, **values):
    STEPS.append({"id": step_id, "commandId": "timeline.action." + action,
                  "values": values})
    return {"result": step_id, "field": "createdClipId"}


def ref(step_id, field="trackId"):
    return {"result": step_id, "field": field}


def track(step_id, name, kind="graphics"):
    add(step_id, "createTimelineTrack", kind=kind, name=name, audioRole="")
    return ref(step_id)


def smooth(t, a, b):
    u = max(0.0, min(1.0, (t - a) / (b - a)))
    return u * u * u * (10 + u * (-15 + 6 * u))


def out(t, a, b):
    u = max(0.0, min(1.0, (t - a) / (b - a)))
    return 1 - (1 - u) ** 4


def compact_keys(duration, value_at, fps=FPS):
    count = round(duration * fps)
    keys = [{"localSeconds": frame / fps, "value": value_at(frame / fps),
             "interpolation": "linear"} for frame in range(count + 1)]
    compact = []
    for index, key in enumerate(keys):
        if (index == 0 or index == len(keys) - 1 or
                keys[index - 1]["value"] != key["value"] or
                keys[index + 1]["value"] != key["value"]):
            compact.append(key)
    return compact


def animate(step_id, clip, duration, channels):
    add(step_id, "setTimelineClipAnimation", clipId=clip,
        channels=[{"parameter": parameter,
                   "keyframes": compact_keys(duration, value_at)}
                  for parameter, value_at in channels.items()])


def frame(step_id, clip, x, y, width, height, *, fit="fill", opacity=1.0,
          rotation=0.0, radius=0.0, crop=None):
    if crop is None:
        crop = (0.0, 0.0, 0.0, 0.0)
    add(step_id, "setTimelineClipFrame", clipId=clip,
        offsetX=(x + width / 2) / WIDTH - 0.5,
        offsetY=(y + height / 2) / HEIGHT - 0.5,
        scale=1, rotationDegrees=rotation, perspectiveX=0, perspectiveY=0,
        opacity=opacity, objectFit=fit, width=width / WIDTH,
        height=height / HEIGHT, cornerRadius=radius / WIDTH,
        cropLeft=crop[0], cropTop=crop[1], cropRight=crop[2], cropBottom=crop[3])


def image(step_id, source, target_track, start, duration, *, label=None):
    target_track = lane_track(target_track, start, duration)
    path = str((MEDIA / source).resolve())
    return add(step_id, "createTimelineClip", trackId=target_track,
        assetId="native-trailer-v4:" + source, filePath=path,
        displayLabel=label or step_id, durationSeconds=duration,
        trimStartSeconds=0, trimEndSeconds=duration, startSeconds=start,
        posterSource="")


def title(step_id, text, target_track, x, y, size, *, family="Anton",
          color="#f2eee1", spacing=-1.8, line_height=1.01,
          alignment="left", italic=False, start=0, duration=30,
          weight=400, background="#00000000"):
    target_track = lane_track(target_track, start, duration)
    return add(step_id, "createTimelineTitle", trackId=target_track,
        templateKey="basic", startSeconds=start, durationSeconds=duration,
        values={"text": text, "fontFamily": family, "fontSize": size,
            "fontWeight": weight, "italic": italic, "letterSpacing": spacing,
            "lineHeight": line_height, "color": color,
            "backgroundColor": background, "alignment": alignment,
            "positionX": x / WIDTH, "positionY": y / HEIGHT})


def graphic(step_id, target_track, x, y, width, height, *, start=0,
            duration=30, shape="rectangle", fill="#00000000", stroke="",
            stroke_width=0.0, radius=0.0, rotation=0.0, opacity=1.0,
            shadow=False):
    target_track = lane_track(target_track, start, duration)
    clip = add(step_id, "createTimelineGraphic", trackId=target_track,
        shapeType=shape, startSeconds=start, durationSeconds=duration,
        values={"positionX": x / WIDTH, "positionY": y / HEIGHT,
            "shapeWidth": width / WIDTH, "shapeHeight": height / HEIGHT,
            "shapeCornerRadius": radius / WIDTH, "backgroundColor": fill,
            "shapeStrokeColor": stroke or "#00000000",
            "shapeStrokeWidth": stroke_width / WIDTH,
            "fontFamily": "Space Mono", "fontSize": 12,
            "fontWeight": 400, "italic": False, "letterSpacing": 0,
            "lineHeight": 1.0, "color": "#ffffff",
            "alignment": "left", "shadowEnabled": shadow,
            "shadowColor": "#00000088", "shadowOffsetX": 0,
            "shadowOffsetY": 12})
    if rotation:
        animate(step_id + "-rotation", clip, duration,
                {"frameRotationDegrees": lambda t: rotation})
    if opacity != 1:
        animate(step_id + "-opacity", clip, duration,
                {"opacity": lambda t: opacity})
    return clip


def create_light(target_track, step_id, start, duration, offset, opacity):
    clip = add(step_id, "createTimelineGenerator", trackId=target_track,
        generatorKind="desktop-light", startSeconds=start,
        durationSeconds=duration,
        parameters={"timeOffsetSeconds": offset, "timeScale": 1,
                    "gather": 0, "opacity": opacity})
    return clip


def create_reference_gradient(target_track, step_id, start, duration,
                              x, y, width, height):
    target_track = lane_track(target_track, start, duration)
    clip = add(step_id, "createTimelineGenerator", trackId=target_track,
        generatorKind="linear-gradient", startSeconds=start,
        durationSeconds=duration,
        parameters={"angleDegrees": 180, "stops": [
            {"position": 0, "color": "#0e2336", "opacity": 0x37 / 255},
            {"position": .32, "color": "#000000", "opacity": 0},
            {"position": .47, "color": "#15283b", "opacity": 0x22 / 255},
            {"position": 1, "color": "#081522", "opacity": 0xdc / 255}]})
    frame(step_id + "-frame", clip, x, y, width, height, fit="fill")
    return clip


def add_caption(target_track, prefix, start, duration, heading, detail,
                *, chapter="02 / LIBRARY & WORKFLOW", serif=False):
    # The source Caption is anchored at x=82/y=254 on a 1920x1080 landscape.
    chapter_clip = title(prefix + "-chapter", chapter, target_track, 82, 254, 21,
          family="Space Mono", color="#d7ff48", spacing=1.8,
          start=start, duration=duration)
    heading_clip = title(prefix + "-heading", heading, target_track, 82, 303, 91,
          family="Instrument Serif" if serif else "Anton",
          italic=serif, start=start, duration=duration)
    detail_clip = title(prefix + "-detail", detail, target_track, 82, 515, 33,
          family="Space Grotesk", color="#c9ced6", spacing=0,
          line_height=1.4, start=start, duration=duration)
    animate(prefix + "-chapter-enter", chapter_clip, duration,
        {"opacity": lambda t: out(t, 0, .55),
         "frameOffsetY": lambda t: 45 * (1 - out(t, 0, .75)) / HEIGHT})
    animate(prefix + "-heading-enter", heading_clip, duration,
        {"opacity": lambda t: out(t, .05, .60),
         "frameOffsetY": lambda t: 45 * (1 - out(t, .05, .80)) / HEIGHT})
    animate(prefix + "-detail-enter", detail_clip, duration,
        {"opacity": lambda t: out(t, .2, .75),
         "frameOffsetY": lambda t: 45 * (1 - out(t, .2, .95)) / HEIGHT})


def add_grid_card(index, photo_id, start, duration, image_track, graphic_track,
                  text_track, *, search=False, cull=False):
    # LibraryChapter's stage is fixed at (650,120), sized 1000x800,
    # transformed to 1.1x in landscape. The media boxes themselves remain
    # exactly 232x173 inside 232x210 cards.
    stage_x, stage_y, scale = 650, 120, 1.1
    local_x, local_y = (index % 4) * 248, 35 + (index // 4) * 230
    x, y = stage_x + local_x * scale, stage_y + local_y * scale
    card_w, card_h = 232 * scale, 210 * scale
    photo_h = 173 * scale
    file_source = f"creative-polish-v2/{photo_id}.jpg"
    card_bg = graphic(f"lib-{start}-{photo_id}-card", graphic_track,
        x, y, card_w, card_h, start=start, duration=duration,
        shape="rounded-rectangle", fill="#17202b", radius=9 * scale,
        shadow=not search)
    clip = image(f"lib-{start}-{photo_id}-image", file_source,
        image_track, start, duration, label=photo_id)
    frame(f"lib-{start}-{photo_id}-image-frame", clip, x, y, card_w,
          photo_h, radius=9 * scale)
    label_y = y + (210 - 11 - 12) * scale
    label_color = "#d7ff48" if search and photo_id in {
        "IMG_5071", "IMG_5053", "IMG_5077", "IMG_4788"} else "#c1c7d0"
    label = title(f"lib-{start}-{photo_id}-label", photo_id + ".CR2",
        text_track, x + 12 * scale, label_y, 12 * scale,
        family="Space Mono", color=label_color, spacing=0.4,
        start=start, duration=duration)
    if start == 62 and not search:
        index_delay = index * 0.035
        enter_end = 0.6 + index_delay
        for suffix, target in [("bg", card_bg), ("img", clip), ("label", label)]:
            if suffix == "bg":
                channels = {"opacity": lambda t, a=index_delay, b=enter_end: out(t, a, b),
                            "frameOffsetY": lambda t, i=index: 80 * scale *
                                (1 - out(t, i * .035, .9 + i * .035)) / HEIGHT,
                            "frameRotationDegrees": lambda t, i=index:
                                (i - 6) * 3 * (1 - out(t, 0, 1.2))}
            elif suffix == "img":
                channels = {"opacity": lambda t, a=index_delay, b=enter_end: out(t, a, b),
                            "frameOffsetY": lambda t, i=index: 80 * scale *
                                (1 - out(t, i * .035, .9 + i * .035)) / HEIGHT,
                            "frameRotationDegrees": lambda t, i=index:
                                (i - 6) * 3 * (1 - out(t, 0, 1.2))}
            else:
                channels = {"opacity": lambda t, a=index_delay, b=enter_end: out(t, a, b),
                            "frameOffsetY": lambda t, i=index: 80 * scale *
                                (1 - out(t, i * .035, .9 + i * .035)) / HEIGHT,
                            "frameRotationDegrees": lambda t, i=index:
                                (i - 6) * 3 * (1 - out(t, 0, 1.2))}
            animate(f"lib-{start}-{photo_id}-{suffix}-entrance", target,
                    duration, channels)
    if search:
        matched = photo_id in {"IMG_5071", "IMG_5053", "IMG_5077", "IMG_4788"}
        if not matched:
            for suffix, target in [("bg", card_bg), ("img", clip), ("label", label)]:
                animate(f"lib-search-{photo_id}-{suffix}-dim", target, duration,
                        {"opacity": lambda t: 1 - .86 * smooth(t, 1, 2.2)})
        if matched:
            outline = graphic(f"lib-search-{photo_id}-match-outline", graphic_track,
                x - 2, y - 2, card_w + 4, card_h + 4, start=start,
                duration=duration, shape="rounded-rectangle", fill="#00000000",
                stroke="#d7ff48", stroke_width=2, radius=9 * scale)
            animate(f"lib-search-{photo_id}-match-outline-on", outline,
                    duration, {"opacity": lambda t: 1.0})
    return clip


def pill(target_graphics, target_text, step, text_value, x, y, start,
         duration, *, text_size=15, color="#f2eee1", stroke="#c4c8ce55",
         fill="#0b0d12bd", width=None):
    width = width or (len(text_value) * text_size * .62 + 38)
    height = text_size + 26
    graphic(step + "-shape", target_graphics, x, y, width, height,
            start=start, duration=duration, shape="rounded-rectangle",
            fill=fill, stroke=stroke, stroke_width=1, radius=height / 2)
    title(step + "-text", text_value, target_text, x + 18, y + 9,
          text_size, family="Space Mono", color=color, spacing=.3,
          start=start, duration=duration)


def add_gallery_delivery(start, duration, *, batch, image_track,
                         graphic_track, text_track):
    # Shared header from BuyerScenes.GalleryDelivery.
    title(f"gallery-{start}-chapter", "02 / LIBRARY & WORKFLOW", text_track,
          85, 75, 21, family="Space Mono", color="#d7ff48", spacing=1.8,
          start=start, duration=duration)
    title(f"gallery-{start}-heading",
          "A consistent finish.\nAcross the gallery." if batch
          else "For the moments\nyou only get once.",
          text_track, 85, 125, 104, start=start, duration=duration)
    title(f"gallery-{start}-detail",
          "Apply a shared adjustment. Keep individual control." if batch
          else "Cull the shoot. Refine the keepers. Deliver a considered set.",
          text_track, 85, 357, 32, family="Space Grotesk", color="#f2eee1",
          spacing=0, start=start, duration=duration)
    names = ["wedding-walk", "wedding-embrace", "wedding-play"]
    if not batch:
        margin, gap = 85, 22
        card_w = (WIDTH - margin * 2 - gap * 2) / 3
        card_h, top = 540, 430
        for index, name in enumerate(names):
            x = margin + index * (card_w + gap)
            y = top
            clip = image(f"gallery-{start}-{name}-before",
                f"creative-polish-v2/{name}/batch-before.png", image_track,
                start, duration, label=name)
            frame(f"gallery-{start}-{name}-image-frame", clip,
                  x, y, card_w, card_h, radius=5)
            animate(f"gallery-{start}-{name}-arrival", clip, duration,
                {"opacity": lambda t, i=index: out(t, i * .12, .55 + i * .12),
                 "frameOffsetY": lambda t, i=index: 35 * (1 - out(t, i * .12, .9 + i * .12)) / HEIGHT})
            graphic(f"gallery-{start}-{name}-label-bg", graphic_track,
                x + 14, y + card_h - 18 - 39, 300, 39,
                start=start, duration=duration, shape="rectangle",
                fill="#09101bd9")
            title(f"gallery-{start}-{name}-label", "WEDDING / SELECTED FRAME",
                text_track, x + 26, y + card_h - 44, 18,
                family="Space Mono", color="#f2eee1", spacing=.4,
                start=start, duration=duration)
            label_bg = ref(f"gallery-{start}-{name}-label-bg", "createdClipId")
            label_text = ref(f"gallery-{start}-{name}-label", "createdClipId")
            for suffix, target in [("bg", label_bg), ("text", label_text)]:
                animate(f"gallery-{start}-{name}-label-{suffix}-arrival", target,
                    duration,
                    {"opacity": lambda t, i=index: out(t, i * .12, .55 + i * .12),
                     "frameOffsetY": lambda t, i=index: 35 * (1 - out(t, i * .12, .9 + i * .12)) / HEIGHT})
    else:
        card_w, card_h = 305 * 1.1, 600 * 1.1
        top, stage_left, stage_top = 60, 650, 120
        for index, name in enumerate(names):
            x = stage_left + index * 326 * 1.1
            y = stage_top + (top + index * 20) * 1.1
            before = image(f"gallery-{start}-{name}-before",
                f"creative-polish-v2/{name}/batch-before.png", image_track,
                start, duration, label=name + " before")
            after = image(f"gallery-{start}-{name}-after",
                f"creative-polish-v2/{name}/batch-after.png", image_track,
                start, duration, label=name + " after")
            frame(f"gallery-{start}-{name}-before-frame", before,
                  x, y, card_w, card_h, radius=9 * 1.1, rotation=[-3, 0, 3][index])
            frame(f"gallery-{start}-{name}-after-frame", after,
                  x, y, card_w, card_h, radius=9 * 1.1, rotation=[-3, 0, 3][index])
            # The reference uses an animated left-to-right clipPath wipe. Native
            # source masks are not keyframeable yet, so the native plan uses an
            # explicitly disclosed full-frame crossfade over the same interval.
            animate(f"gallery-{start}-{name}-shared-recipe-crossfade", after,
                duration, {"opacity": lambda t, i=index:
                    smooth(t, 1 + i * .17, 3.5 + i * .17)})
            animate(f"gallery-{start}-{name}-shared-recipe-before-fade", before,
                duration, {"opacity": lambda t, i=index:
                    1 - smooth(t, 1 + i * .17, 3.5 + i * .17)})
            label_y = y + card_h - 51 + 18
            label_bg = graphic(f"gallery-{start}-{name}-label-bg", graphic_track,
                x, y + card_h - 51, card_w, 51,
                start=start, duration=duration, shape="rectangle",
                fill="#09101bd9")
            # Label content in the reference flips from APPLYING to EDITABLE
            # when smooth(s,.7,3.7) reaches .99.
            threshold = 3.3830804693476724
            applying = title(f"gallery-{start}-{name}-label-applying",
                "SHARED RECIPE / APPLYING", text_track, x + 18, label_y,
                13 * 1.1, family="Space Mono", color="#f2eee1", spacing=.4,
                start=start, duration=threshold)
            editable = title(f"gallery-{start}-{name}-label-editable",
                "SHARED RECIPE / EDITABLE", text_track, x + 18, label_y,
                13 * 1.1, family="Space Mono", color="#f2eee1", spacing=.4,
                start=start + threshold, duration=max(.05, duration-threshold))
            angle = [-3, 0, 3][index]
            for suffix, target, clip_duration in [
                ("bg", label_bg, duration),
                ("applying", applying, threshold),
                ("editable", editable, max(.05, duration - threshold))]:
                if angle:
                    animate(f"gallery-{start}-{name}-label-{suffix}-rotation",
                        target, clip_duration,
                        {"frameRotationDegrees": lambda t, a=angle: a})
        for index, value in enumerate(["EXPOSURE +0.15", "WARMTH +6", "HIGHLIGHTS −20"]):
            pill(graphic_track, text_track, f"gallery-{start}-recipe-pill-{index}",
                 value, 667 + index * 205, 929, start, duration,
                 text_size=15, color="#d7ff48" if index == 0 else "#f2eee1")
        # The label is already embedded in each card; no status-only clips.

    footer_left = "ONE RECIPE. ROOM FOR EVERY PHOTOGRAPH."
    footer_right = "LOOKSY / LIBRARY"
    title(f"gallery-{start}-footer-left", footer_left, text_track,
        85, 1041, 17, family="Space Mono", color="#aeb6c3", spacing=1,
        start=start, duration=duration)
    title(f"gallery-{start}-footer-right", footer_right, text_track,
        1835, 1041, 17, family="Space Mono", color="#aeb6c3", spacing=1,
        alignment="right", start=start, duration=duration)


def add_photo_library_chapter():
    image_track = lane_pool("library-media", "video")
    graphic_track = lane_pool("library-graphics", "graphics")
    text_track = lane_pool("library-copy", "graphics")
    light_track = track("lib-light-track", "Library / ambient light", "video")
    create_light(light_track, "lib-light", 62, 30, 62, .27)
    # Stage captions and 12-photo import entrance, 62-67s.
    add_caption(text_track, "lib-import-caption", 62, 5,
        "Bring the\nwhole shoot.",
        "Batch import your photographs\nand footage. Keep the work together.")
    photos = ["IMG_4969", "IMG_5071", "IMG_4779", "IMG_4653",
        "IMG_4748", "IMG_4867", "IMG_4788", "IMG_5053",
        "IMG_5077", "IMG_5106", "IMG_5125", "IMG_5203"]
    for index, photo_id in enumerate(photos):
        add_grid_card(index, photo_id, 62, 5, image_track, graphic_track,
                      text_track)

    # Culling/compare sequence, 67-73s.
    add_caption(text_track, "lib-cull-caption", 67, 6,
        "Keep the\nkeepers.",
        "Compare nearby frames. Rate,\npick and cull before the detailed edit.")
    for index, photo_id in enumerate(["IMG_5053", "IMG_5071", "IMG_5077"]):
        x = 650 + index * 325 * 1.1
        y = 120 + (80 + (-20 if index == 1 else 20)) * 1.1
        width, height = 300 * 1.1, 520 * 1.1
        bg = graphic(f"lib-cull-{photo_id}-card", graphic_track,
            x, y, width, height, start=67, duration=6,
            shape="rounded-rectangle", fill="#15202d", radius=12 * 1.1,
            shadow=True)
        clip = image(f"lib-cull-{photo_id}-image",
            f"creative-polish-v2/{photo_id}.jpg", image_track,
            67, 6, label=photo_id)
        frame(f"lib-cull-{photo_id}-frame", clip, x, y, width,
              440 * 1.1, radius=12 * 1.1)
        label_track = text_track
        initial_label = title(f"lib-cull-{photo_id}-raw-label", photo_id + ".CR2",
            label_track, x + 20 * 1.1, y + 465 * 1.1, 17 * 1.1,
            family="Space Mono", color="#f2eee1", spacing=.4,
            start=67, duration=2)
        label_value = "★★★★★ / PICK" if index == 1 else "REVIEW NEXT"
        title(f"lib-cull-{photo_id}-status", label_value, label_track,
            x + 20 * 1.1, y + 465 * 1.1, 17 * 1.1,
            family="Space Mono", color="#d7ff48" if index == 1 else "#f2eee1",
            spacing=.4, start=69, duration=4)
        border = graphic(f"lib-cull-{photo_id}-pick-outline", graphic_track,
            x - 3, y - 3, width + 6, height + 6, start=68.8,
            duration=4.2, shape="rounded-rectangle", fill="#00000000",
            stroke="#d7ff48" if index == 1 else "#00000000",
            stroke_width=3, radius=12 * 1.1)
        if index == 1:
            animate(f"lib-cull-{photo_id}-pick-on", border, 4.2,
                    {"opacity": lambda t: 1})
            for suffix, target in [("bg", bg), ("image", clip),
                                   ("label", ref(f"lib-cull-{photo_id}-status", "createdClipId")),
                                   ("outline", border)]:
                animate(f"lib-cull-{photo_id}-{suffix}-selected-zoom", target, 6,
                    {"frameScale": lambda t: 1 + .08 * smooth(t, 1.3, 2.7)})
        else:
            for suffix, target in [("bg", bg), ("image", clip), ("label", initial_label)]:
                animate(f"lib-cull-{photo_id}-{suffix}-deemphasis", target, 6,
                    {"opacity": lambda t: 1 - .7 * smooth(t, 1.8, 3.2),
                     "frameOffsetY": lambda t: 80 * 1.1 * smooth(t, 2.3, 3.8) / HEIGHT})
    for index, value in enumerate(["COMPARE", "RATE", "PICK", "CULL"]):
        pill(graphic_track, text_track, f"lib-cull-tool-{index}", value,
             650 + (140 + index * 160) * 1.1, 120 + 735 * 1.1,
             67, 6, text_size=19 * 1.1,
             color="#d7ff48" if index == 0 else "#f2eee1")

    # Visual-search result state, 73-79s.
    add_caption(text_track, "lib-search-caption", 73, 6,
        "Remember the scene.\nFind the frame.",
        "Visual AI search finds what is in\nthe frame, beyond a filename.")
    for index, photo_id in enumerate(photos):
        add_grid_card(index, photo_id, 73, 6, image_track, graphic_track,
                      text_track, search=True)
    search_x, search_y = 650 + 75 * 1.1, 120 + 740 * 1.1
    search_w, search_h = 830 * 1.1, 74
    graphic("lib-search-field", graphic_track, search_x, search_y,
        search_w, search_h, start=73, duration=6,
        shape="rounded-rectangle", fill="#141e2e", stroke="#94a6bd88",
        stroke_width=1.1, radius=40 * 1.1)
    title("lib-search-symbol", "⌕", text_track, search_x + 28 * 1.1,
          search_y + 18, 27 * 1.1, family="Space Grotesk",
          color="#d7ff48", spacing=0, start=73, duration=6)
    query = "mountains in warm light"
    for count in range(1, len(query) + 1):
        at = count / 16
        end = min(6, (count + 1) / 16)
        title(f"lib-search-query-{count:02d}", query[:count], text_track,
            search_x + 68 * 1.1, search_y + 19, 27 * 1.1,
            family="Space Grotesk", color="#f2eee1", spacing=0,
            start=73 + at, duration=max(.05, end - at))
    title("lib-search-label", "VISUAL AI SEARCH", text_track,
        search_x + search_w - 210, search_y + 25, 12 * 1.1,
        family="Space Mono", color="#94a4b7", spacing=.3,
        start=73, duration=6)

    # Wedding gallery and batch recipe chapters are exact source components.
    add_gallery_delivery(79, 7, batch=False, image_track=image_track,
                         graphic_track=graphic_track, text_track=text_track)
    add_gallery_delivery(86, 6, batch=True, image_track=image_track,
                         graphic_track=graphic_track, text_track=text_track)


def panorama_crop(panel, pan=0.0):
    # PanoramaArtwork fills a 3240x1350 canvas with the source photograph,
    # cover-cropped vertically, then scales it to 1.12x. Pan is the source
    # component's unscaled imagePan value in 3240 design pixels.
    left_art = panel * 1080
    right_art = (panel + 1) * 1080
    left = ((left_art / 3240 - .5 - pan / 3240) / 1.12) + .5
    right = ((right_art / 3240 - .5 - pan / 3240) / 1.12) + .5
    top = .5 - (.625 / 1.12) / 2
    bottom = .5 + (.625 / 1.12) / 2
    return (max(0, left), top, min(1, right), bottom)


def animate_row_transform(step_id, clip, duration, x, y, width, height,
                          *, owner=None):
    row_left_final, row_top = 249.4, 395.0
    if owner is None:
        gap_start = 14.0
    elif owner == 0:
        gap_start = 14.0
    elif owner == 2:
        gap_start = -14.0
    else:
        gap_start = 0.0
    cx, cy = x + width / 2, y + height / 2
    target_left, target_top, group_scale = 85.0, 430.0, .66
    end_cx = target_left + (cx - row_left_final) * group_scale
    end_cy = target_top + (cy - row_top) * group_scale
    final_offset_x = (end_cx - cx) / WIDTH
    final_offset_y = (end_cy - cy) / HEIGHT
    animate(step_id + "-carousel-transform", clip, duration, {
        "frameScale": lambda t: 1 - .34 * smooth(t, 15, 17),
        "frameOffsetX": lambda t, gs=gap_start, fx=final_offset_x:
            (gs * (1 - smooth(t, 6, 8))) / WIDTH + fx * smooth(t, 15, 17),
        "frameOffsetY": lambda t, fy=final_offset_y:
            fy * smooth(t, 15, 17),
    })


def row_layer(step_id, kind, target_track, x_local, y_local,
              width_local, height_local, *, panel=None, text_value=None,
              size_local=0, family="Space Mono", color="#f2eee1",
              spacing=0, line_height=1.01, italic=False,
              align="left", fill="#00000000", stroke="",
              stroke_width_local=0, shape="rectangle", radius_local=0,
              start=160, duration=24, rotation=0, owner=None, crop=None):
    row_left_final, row_top = 249.4, 395.0
    scale = .43
    if owner is None:
        owner = panel
    panel_gap = 14.0 * panel if panel is not None else 0.0
    x = row_left_final + x_local * scale
    if panel is not None:
        x += panel_gap
    y = row_top + y_local * scale
    w, h = width_local * scale, height_local * scale
    if kind == "image":
        clip = image(step_id, text_value, target_track, start, duration,
                     label=step_id)
        crop = crop or panorama_crop(panel or 0)
        frame(step_id + "-frame", clip, x, y, w, h, fit="fill",
              crop=crop)
    elif kind == "title":
        clip = title(step_id, text_value, target_track, x, y, size_local * scale,
            family=family, color=color, spacing=spacing * scale,
            line_height=line_height, italic=italic, alignment=align,
            start=start, duration=duration)
    else:
        clip = graphic(step_id, target_track, x, y, w, h,
            start=start, duration=duration, shape=shape, fill=fill,
            stroke=stroke, stroke_width=stroke_width_local * scale,
            radius=radius_local * scale)
    if rotation and kind != "image":
        animate(step_id + "-rotation", clip, duration,
                {"frameRotationDegrees": lambda t: rotation})
    # Source panel spacing opens 0 -> 14px over local seconds 6..8, then the
    # outer panel scales and moves to the left over local seconds 15..17.
    animate_row_transform(step_id, clip, duration, x, y, w, h, owner=owner)
    if kind == "image":
        animate(step_id + "-corner-radius", clip, duration,
            {"frameCornerRadius": lambda t: .000673 if t >= 8 else
                .000673 * smooth(t, 6, 8)})
    return clip


def add_panorama_layers(image_track, text_track, graphic_track, effects_track, *, start=160,
                        duration=24, phone=False, owner_offsets=None):
    # The three panels are represented with native source crops. Each crop
    # exactly matches one 1080x1350 viewport of the shared panorama photo.
    if not phone:
        for panel in range(3):
            initial = row_layer(f"carousel-panorama-panel-{panel}-pan-start",
                "image", image_track, panel * 1080, 0, 1080, 1350,
                panel=panel, text_value="creative-polish-v2/IMG_5071.jpg",
                start=start, duration=duration, owner=panel,
                crop=panorama_crop(panel, pan=-110))
            animate(f"carousel-panorama-panel-{panel}-pan-start-opacity",
                initial, duration,
                {"opacity": lambda t: 1 - smooth(t, 8.5, 12.5)})
            final = row_layer(f"carousel-panorama-panel-{panel}-pan-end",
                "image", image_track, panel * 1080, 0, 1080, 1350,
                panel=panel, text_value="creative-polish-v2/IMG_5071.jpg",
                start=start, duration=duration, owner=panel,
                crop=panorama_crop(panel, pan=110))
            animate(f"carousel-panorama-panel-{panel}-pan-end-opacity",
                final, duration, {"opacity": lambda t: smooth(t, 8.5, 12.5)})
            x = 249.4 + panel * (1080 * .43 + 14)
            y, width, height = 395.0, 1080 * .43, 1350 * .43
            gradient = create_reference_gradient(effects_track,
                f"carousel-panel-{panel}-reference-gradient",
                start, duration, x, y, width, height)
            animate_row_transform(f"carousel-panel-{panel}-gradient",
                gradient, duration, x, y, width, height, owner=panel)
            animate(f"carousel-panel-{panel}-gradient-radius", gradient,
                duration, {"frameCornerRadius": lambda t:
                    .000673 * smooth(t, 6, 8)})
            border = row_layer(f"carousel-panel-{panel}-separator", "graphic",
                graphic_track, panel * 1080, 0, 1080, 1350,
                panel=panel, start=start, duration=duration,
                fill="#00000000", stroke="#d8e4dc66",
                stroke_width_local=1, radius_local=3)
            animate(f"carousel-panel-{panel}-separator-on", border, duration,
                {"opacity": lambda t: smooth(t, 6, 8),
                 "frameCornerRadius": lambda t: .000673 * smooth(t, 6, 8)})
        scale = .43
        add_text = lambda i, text_value, x, y, size, **kwargs: row_layer(
            f"carousel-art-{i}", "title", text_track, x, y, 800, size * 1.4,
            panel=kwargs.pop("panel", None), text_value=text_value,
            size_local=size, start=start, duration=duration, **kwargs)
        for panel in range(3):
            add_text(f"top-left-{panel}", "FIELD JOURNAL / NEPAL",
                panel * 1080 + 76, 69, 25, family="Space Mono",
                color="#f2eee1", spacing=1.5, panel=panel)
            add_text(f"top-right-{panel}", f"0{panel + 1} — 03",
                panel * 1080 + 1004, 69, 25, family="Space Mono",
                color="#f2eee1", spacing=1.5, align="right", panel=panel)
        for panel, line_x, line_w in [(0, 76, 1004), (1, 1080, 1080),
                                      (2, 2160, 1004)]:
            row_layer(f"carousel-art-top-rule-{panel}", "graphic", graphic_track,
                line_x, 124, line_w, 2, panel=panel,
                start=start, duration=duration, fill="#f2eee17a")
        add_text("take-the", "Take the", 72, 529, 190,
            family="Instrument Serif", italic=True, spacing=-4, owner=0)
        add_text("long-word", "LONG", 69, 719, 365,
            family="Anton", spacing=-3, panel=0)
        add_text("way-word", "WAY.", 1080, 719, 365,
            family="Anton", spacing=-3, panel=1)
        add_text("some-things", "Some things\nneed a little\nmore room.",
            2247, 727, 115, family="Instrument Serif", italic=True,
            spacing=-2, line_height=1.01, owner=2)
        # Orange First Light label and compact mountain line drawing.
        sticker_x, sticker_y = 1770, 172
        sticker = row_layer("carousel-art-first-light-sticker", "graphic",
            graphic_track, sticker_x, sticker_y, 275, 94, panel=1,
            fill="#f0834f", start=start, duration=duration, rotation=-5)
        add_text("first-light", "FIRST LIGHT ↗", sticker_x + 10,
            sticker_y + 31, 27, family="Space Mono", color="#182331",
            spacing=1, panel=1)
        # The source SVG is four straight line segments and one marker.
        points = [(28,266),(126,77),(159,154),(220,22),(294,266)]
        for index, ((x1,y1),(x2,y2)) in enumerate(zip(points, points[1:])):
            dx, dy = x2-x1, y2-y1
            length = math.hypot(dx, dy)
            rotation = math.degrees(math.atan2(dy, dx))
            row_layer(f"carousel-art-mountain-{index}", "graphic", graphic_track,
                1438 + (x1+x2)/2 - length/2, 241 + (y1+y2)/2 - 1,
                length, 2, panel=1, fill="#f2eee1b3", start=start,
                duration=duration, rotation=rotation)
        row_layer("carousel-art-mountain-base", "graphic", graphic_track,
            1466, 527, 266, 1, panel=1, fill="#f2eee180",
            start=start, duration=duration)
        row_layer("carousel-art-mountain-sun", "graphic", graphic_track,
            1649, 254, 18, 18, panel=1, shape="ellipse", fill="#f0834f",
            start=start, duration=duration)
        for panel, left_copy in enumerate([
            "A LITTLE FURTHER.", "A DIFFERENT PERSPECTIVE.",
            "KEEP THE CURIOSITY."]):
            row_layer(f"carousel-art-footer-rule-{panel}", "graphic",
                graphic_track, panel * 1080 + 76, 1254, 928, 2,
                panel=panel, fill="#f2eee17a", start=start, duration=duration)
            add_text(f"footer-copy-{panel}", left_copy,
                panel * 1080 + 76, 1278, 25, family="Space Mono",
                color="#f2eee1", spacing=0, panel=panel)
            right = ["SWIPE →", "SWIPE →", "END / BEGIN AGAIN"][panel]
            add_text(f"footer-right-{panel}", right,
                panel * 1080 + 1004, 1278, 25, family="Space Mono",
                color="#f2eee1", spacing=0, align="right", panel=panel)
        return

    # Phone art is generated separately because it uses a 4:5 panel crop.
    content_x, content_y = 1394, 447
    content_w, content_h = 392, 490
    for panel in range(3):
        clip = image(f"carousel-phone-panel-{panel}",
            "creative-polish-v2/IMG_5071.jpg", image_track,
            start, duration, label=f"phone panel {panel + 1}")
        frame(f"carousel-phone-panel-{panel}-frame", clip,
            content_x, content_y, content_w, content_h, fit="fill",
            crop=panorama_crop(panel, pan=110))
        opacity = (
            (lambda t: smooth(t, 15, 17) * (1 - smooth(t, 18, 19.3))) if panel == 0 else
            (lambda t: smooth(t, 15, 17) * smooth(t, 18, 19.3) *
                (1 - smooth(t, 20.7, 22))) if panel == 1 else
            (lambda t: smooth(t, 15, 17) * smooth(t, 20.7, 22)))
        animate(f"carousel-phone-panel-{panel}-state", clip, duration,
                {"opacity": opacity})
    phone_gradient = create_reference_gradient(effects_track,
        "carousel-phone-reference-gradient", start, duration,
        content_x, content_y, content_w, content_h)
    animate("carousel-phone-reference-gradient-appear", phone_gradient,
        duration, {"opacity": lambda t: smooth(t, 15, 17)})
    # The source PanoramaArtwork uses a 3240x1350 clipped child with horizontal
    # swipe. Native clips support source crops but no nested clip viewport, so
    # this plan displays the exact individual panel crops and crossfades panes.
    # Recreate each panel's visible text and marks at the exact phone scale.
    phone_scale = 392 / 1080
    for panel in range(3):
        page_opacity = (
            (lambda t: smooth(t, 15, 17) * (1 - smooth(t, 18, 19.3))) if panel == 0 else
            (lambda t: smooth(t, 15, 17) * smooth(t, 18, 19.3) *
                (1 - smooth(t, 20.7, 22))) if panel == 1 else
            (lambda t: smooth(t, 15, 17) * smooth(t, 20.7, 22)))
        for side, text_value, x_design in [
            ("left", "FIELD JOURNAL / NEPAL", panel * 1080 + 76),
            ("right", f"0{panel + 1} — 03", panel * 1080 + 1004),
        ]:
            local_x = content_x + (x_design - panel * 1080) * phone_scale
            clip = title(f"carousel-phone-{panel}-header-{side}", text_value,
                text_track, local_x, content_y + 69 * phone_scale,
                25 * phone_scale, family="Space Mono", color="#f2eee1",
                spacing=.5, alignment="right" if side == "right" else "left",
                start=start, duration=duration)
            animate(f"carousel-phone-{panel}-header-{side}-state", clip,
                    duration, {"opacity": page_opacity})
        if panel == 0:
            copy = [("Take the",72,529,190,"Instrument Serif",True,-4),
                    ("LONG",69,719,365,"Anton",False,-3)]
        elif panel == 1:
            copy = [("WAY.",1080,719,365,"Anton",False,-3)]
        else:
            copy = [("Some things\nneed a little\nmore room.",2247,727,115,
                     "Instrument Serif",True,-2)]
        for ci, (text_value, x_design, y_design, size, family, italic, spacing) in enumerate(copy):
            # Keep the long copy within the phone's 4:5 viewport by positioning
            # to its panel origin. The exact source text/font size is retained.
            local_x = content_x + (x_design - panel * 1080) * phone_scale
            clip = title(f"carousel-phone-{panel}-copy-{ci}", text_value,
                text_track, local_x, content_y + y_design * phone_scale,
                size * phone_scale, family=family, color="#f2eee1",
                italic=italic, spacing=spacing * phone_scale,
                start=start, duration=duration)
            animate(f"carousel-phone-{panel}-copy-{ci}-state", clip,
                    duration, {"opacity": page_opacity})


def add_carousel_chapter():
    light_track = track("carousel-light-track", "Carousel / ambient light", "video")
    image_track = lane_pool("carousel-media", "video")
    phone_shell_track = lane_pool("carousel-graphics", "graphics")
    phone_image_track = lane_pool("carousel-phone-media", "video")
    effects_track = lane_pool("carousel-effects", "video")
    graphic_track = lane_pool("carousel-graphics", "graphics")
    phone_ui_track = lane_pool("carousel-graphics", "graphics")
    text_track = lane_pool("carousel-copy", "graphics")
    create_light(light_track, "carousel-light", 160, 24, 160, .30)
    # Source header changes at t=175 with an immediate copy replacement.
    title("carousel-heading-first", "One photograph.\nAcross every canvas.",
        text_track, 85, 114, 102, start=160, duration=15)
    title("carousel-detail-first", "Compose the whole story. Export individual panels.",
        text_track, 85, 342, 32, family="Space Grotesk", color="#c9ced6",
        spacing=0, start=160, duration=15)
    title("carousel-heading-phone", "Designed together.\nReady to swipe.",
        text_track, 85, 114, 102, start=175, duration=9)
    title("carousel-detail-phone", "The same composition. Three separate 4:5 images.",
        text_track, 85, 342, 32, family="Space Grotesk", color="#c9ced6",
        spacing=0, start=175, duration=9)

    add_panorama_layers(image_track, text_track, graphic_track, effects_track)
    add_panorama_layers(phone_image_track, text_track, graphic_track,
                        effects_track, phone=True)
    # Phone frame: 420x645 at (1380,385), 2px border, 34px radius, 12px inset.
    phone_bg = graphic("carousel-phone-shell", phone_shell_track,
        1380, 385, 420, 645, start=160, duration=24,
        shape="rounded-rectangle", fill="#111822", stroke="#8a98aa80",
        stroke_width=2, radius=34, shadow=True)
    animate("carousel-phone-shell-appear", phone_bg, 24, {
        "opacity": lambda t: smooth(t, 15, 17),
        "frameOffsetY": lambda t: 65 * (1 - smooth(t, 15, 17)) / HEIGHT})
    dot = graphic("carousel-phone-brand-dot", phone_ui_track,
        1404, 409, 22, 22, start=160, duration=24,
        shape="ellipse", fill="#d7ff48")
    title("carousel-phone-brand", "Field journal", phone_ui_track,
        1438, 410, 19, family="Space Grotesk", color="#f2eee1",
        spacing=0, start=160, duration=24)
    for index, page_text in enumerate(["1 / 3", "2 / 3", "3 / 3"]):
        start = [160, 178.65, 181.35][index]
        end = [178.65, 181.35, 184][index]
        title(f"carousel-phone-page-{index+1}", page_text, phone_ui_track,
            1720, 414, 14, family="Space Mono", color="#f2eee1",
            spacing=0, alignment="right", start=start, duration=end-start)
    # Persistent shell/header animate in with the same phone() curve.
    for step_id in ["carousel-phone-brand-dot", "carousel-phone-brand"]:
        animate(step_id + "-appear", ref(step_id, "createdClipId"), 24, {
            "opacity": lambda t: smooth(t, 15, 17),
            "frameOffsetY": lambda t: 65 * (1 - smooth(t, 15, 17)) / HEIGHT})
    page_steps = ["carousel-phone-page-1", "carousel-phone-page-2",
                  "carousel-phone-page-3"]
    animate("carousel-phone-page-1-appear", ref(page_steps[0], "createdClipId"),
        18.65, {"opacity": lambda t: smooth(t, 15, 17),
                "frameOffsetY": lambda t: 65 * (1 - smooth(t, 15, 17)) / HEIGHT})
    for index in range(3):
        x = 1572 + index * 15
        gray = graphic(f"carousel-phone-dot-{index}-inactive", phone_ui_track,
            x, 954, 7, 7, start=160, duration=24,
            shape="ellipse", fill="#586574")
        active = graphic(f"carousel-phone-dot-{index}-active", phone_ui_track,
            x, 954, 7, 7, start=160, duration=24,
            shape="ellipse", fill="#d7ff48")
        animate(f"carousel-phone-dot-{index}-active-state", active, 24, {
            "opacity": lambda t, i=index: smooth(t, 15, 17) * (
                (1-smooth(t, 18.65, 18.66)) if i == 0 else
                (smooth(t, 18.65, 18.66) * (1-smooth(t, 21.35, 21.36))) if i == 1 else
                smooth(t, 21.35, 21.36))})
        animate(f"carousel-phone-dot-{index}-inactive-state", gray, 24, {
            "opacity": lambda t, i=index: smooth(t, 15, 17) * (
                (smooth(t, 18.65, 18.66)) if i == 0 else
                (1-smooth(t, 18.65, 18.66) + smooth(t, 21.35, 21.36)) if i == 1 else
                (1-smooth(t, 21.35, 21.36)))})
    phone_footer = title("carousel-phone-caption", "Take the long way. ↗",
        phone_ui_track, 1403, 976, 17, family="Space Grotesk", color="#f2eee1",
        spacing=0, start=160, duration=24)
    animate("carousel-phone-caption-appear", phone_footer, 24, {
        "opacity": lambda t: smooth(t, 15, 17),
        "frameOffsetY": lambda t: 65 * (1 - smooth(t, 15, 17)) / HEIGHT})
    # At this point title/header pan art is created by add_panorama_layers.
    # The phone-specific copies fade with their corresponding page state.
    # Phone shell is visual, not an interactive export device.

    # The source shows pill labels before the layout split and output files after.
    pill(graphic_track, text_track, "carousel-pill-composition",
        "3240 × 1350 / ONE COMPOSITION", 100, 975, 160, 6,
        text_size=19, color="#d7ff48", width=380)
    labels = ["01.PNG", "02.PNG", "03.PNG", "1080 × 1350 EACH"]
    for index, label in enumerate(labels):
        pill(graphic_track, text_track, f"carousel-pill-output-{index}",
            label, 100 + index * 125, 975, 166, 18,
            text_size=19, color="#d7ff48" if index == 0 else "#f2eee1")
        for suffix in ["shape", "text"]:
            step_id = f"carousel-pill-output-{index}-{suffix}"
            animate(step_id + "-phone-fade", ref(step_id, "createdClipId"), 18,
                {"opacity": lambda t: 1 - smooth(t + 6, 15, 17)})
    title("carousel-footer-left", "COMPOSE ONCE. LET THE STORY CONTINUE.",
        text_track, 100, 1041, 17, family="Space Mono", color="#aeb6c3",
        spacing=1, start=160, duration=24)
    title("carousel-footer-right", "LOOKSY / CAROUSEL", text_track,
        1820, 1041, 17, family="Space Mono", color="#aeb6c3",
        spacing=1, alignment="right", start=160, duration=24)


def build():
    STEPS.clear()
    LANES.clear()
    add_photo_library_chapter()
    add_carousel_chapter()
    return {
        "reference": "marketing/animated-trailer/src/creative-launch/Photo.tsx::LibraryChapter and BuyerScenes.tsx::BuyerCarousel",
        "chapters": [{"name": "Library & Workflow", "startSeconds": 62,
                       "endSeconds": 92},
                      {"name": "Carousel", "startSeconds": 160,
                       "endSeconds": 184}],
        "canvas": {"width": WIDTH, "height": HEIGHT, "fps": FPS,
                   "orientation": "landscape"},
        "steps": STEPS,
        "requiredActionKinds": ["media", "text", "layers", "delivery"],
        "nativeBlockers": [
            "Library batch-after clipPath wipes are represented as documented full-frame opacity transitions because no source mask or clipPath keyframes are exposed by the Timeline MCP actions.",
            "The carousel's 110px in-place panorama image pan cannot be represented exactly: Timeline keyframes expose frame transforms and opacity but not source-crop keyframes. Panel positions and 0-to-14px gap opening are represented natively.",
            "The phone's overflowing 3240x1350 child is represented as three exact source-cropped panels with opacity transitions; the Timeline has no nested clip viewport for the original horizontal swipe or artwork clipping at the phone bezel.",
            "Desktop-light is a native generator, but the reference ambient-light shader and exact motion are only matched if the native generator rendering path agrees at runtime."
        ],
        "reviewRequired": True,
    }


if __name__ == "__main__":
    plan = ROOT / "library-carousel.commands.json"
    plan.write_text(json.dumps(build(), indent=2) + "\n")
    print(plan, len(STEPS), "native commands")
