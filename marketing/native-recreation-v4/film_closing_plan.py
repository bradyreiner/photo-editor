"""Build typed native command plans for FilmChapter and BuyerClosing.

This script only writes reviewable command plans. It never calls the native
app, imports Remotion output, or writes Timeline project JSON.
"""
from pathlib import Path
import json
import opening_plan as opening


ROOT = Path(__file__).resolve().parent
MEDIA = ROOT.parent / "animated-trailer/public/media"
PUBLIC = ROOT.parent / "animated-trailer/public"
WIDTH, HEIGHT = 1920, 1080
FPS = 30
PAPER = "#f1efdf"
INK = "#0b0d12"
LIME = "#d7ff48"
MUTED = "#aeb6c3"
STAGE_X, STAGE_Y, STAGE_SCALE = 650, 120, 1.1


def add(step_id, action, **values):
    return opening.add(step_id, action, **values)


def track(name, role=""):
    return opening.track(name, kind="graphics", role=role)


def audio_track(name, role="music"):
    return opening.track(name, kind="audio", role=role)


def frame(name, clip, x=0, y=0, width=WIDTH, height=HEIGHT, fit="fill",
          opacity=1, radius=0, rotation=0, perspective_x=0, perspective_y=0,
          scale=1, crop=(0, 0, 1, 1)):
    """Place the native frame in pixels; transform offsets use quarter-frame units."""
    cx, cy = x + width / 2, y + height / 2
    return add(
        name, "setTimelineClipFrame", clipId=clip,
        offsetX=4 * (cx / WIDTH - 0.5),
        offsetY=4 * (cy / HEIGHT - 0.5),
        scale=scale, rotationDegrees=rotation,
        perspectiveX=perspective_x, perspectiveY=perspective_y,
        opacity=opacity, objectFit=fit,
        width=width / WIDTH, height=height / HEIGHT,
        cornerRadius=radius, cropLeft=crop[0], cropTop=crop[1],
        cropRight=crop[2], cropBottom=crop[3])


def stage_box(x, y, width, height):
    return (STAGE_X + x * STAGE_SCALE,
            STAGE_Y + y * STAGE_SCALE,
            width * STAGE_SCALE,
            height * STAGE_SCALE)


def stage_frame(name, clip, x, y, width, height, fit="fill", opacity=1,
                radius=0, rotation=0, perspective_x=0, perspective_y=0,
                scale=1, crop=(0, 0, 1, 1)):
    sx, sy, sw, sh = stage_box(x, y, width, height)
    return frame(name, clip, sx, sy, sw, sh, fit, opacity,
                 radius / min(width, height) if radius else 0,
                 rotation, perspective_x, perspective_y, scale, crop)


def video(name, source, target, start, duration, trim_start=0,
          trim_end=None, source_duration=None, poster=""):
    source_durations = {
        "creative-polish-v2/arrival-12.mp4": 12.0,
        "MVI_5051-2.3-5.9.mp4": 3.6036,
        "creative-polish-v2/city-10.mp4": 10.0,
        "refine/night.mp4": 5.0,
        "creative-polish-v2/city-grade-10.mp4": 10.0,
        "creative-polish-v2/city-mono-10.mp4": 10.0,
        "studio/gaussian-camera.mp4": 12.0,
    }
    if trim_end is None:
        trim_end = trim_start + duration
    if source_duration is None:
        source_duration = source_durations[source]
    return add(
        name, "createTimelineClip", trackId=target,
        assetId="native-trailer:" + source,
        filePath=str((MEDIA / source).resolve()), displayLabel=name,
        durationSeconds=source_duration, trimStartSeconds=trim_start,
        trimEndSeconds=trim_end, startSeconds=start, posterSource=poster)


def still(name, source, target, start, duration):
    return add(
        name, "createTimelineClip", trackId=target,
        assetId="native-trailer:" + source,
        filePath=str((MEDIA / source).resolve()), displayLabel=name,
        durationSeconds=duration, trimStartSeconds=0,
        trimEndSeconds=duration, startSeconds=start, posterSource="")


def title(name, text, target, x, y, size, family="Anton", color=PAPER,
          spacing=-1.8, start=184, duration=12, alignment="left",
          italic=False, weight=400, line_height=1.01, background="#00000000"):
    values = {
        "text": text, "fontFamily": family, "fontSize": size,
        "fontWeight": weight, "italic": italic,
        "letterSpacing": spacing, "lineHeight": line_height,
        "color": color, "backgroundColor": background,
        "alignment": alignment, "positionX": x / WIDTH,
        "positionY": y / HEIGHT,
    }
    return add(name, "createTimelineTitle", trackId=target,
               templateKey="headline", startSeconds=start,
               durationSeconds=duration, values=values)


def shape(name, target, x, y, width, height, start, duration,
          shape_type="rectangle", fill="#00000000",
          stroke="#00000000", stroke_px=0, radius_px=0, opacity=1):
    values = {
        "text": "", "shapeType": shape_type,
        "shapeWidth": width / WIDTH, "shapeHeight": height / HEIGHT,
        "shapeCornerRadius": radius_px / min(width, height) if radius_px else 0,
        "shapeStrokeColor": stroke,
        "shapeStrokeWidth": stroke_px / min(WIDTH, HEIGHT),
        "backgroundColor": fill,
        "fontFamily": "Space Grotesk", "fontSize": 18,
        "fontWeight": 400, "italic": False,
        "letterSpacing": 0, "lineHeight": 1,
        "color": PAPER, "alignment": "center",
        "positionX": (x + width / 2) / WIDTH,
        "positionY": (y + height / 2) / HEIGHT,
    }
    clip = add(name, "createTimelineGraphic", trackId=target,
               shapeType=shape_type, startSeconds=start,
               durationSeconds=duration, values=values)
    frame(name + "-frame", clip, opacity=opacity)
    return clip


def stage_shape(name, target, x, y, width, height, start, duration,
                shape_type="rectangle", fill="#00000000",
                stroke="#00000000", stroke_px=0, radius=0, opacity=1):
    sx, sy, sw, sh = stage_box(x, y, width, height)
    return shape(name, target, sx, sy, sw, sh, start, duration,
                 shape_type, fill, stroke, stroke_px * STAGE_SCALE,
                 radius * STAGE_SCALE, opacity)


def animate(name, clip, parameter, start, end, fn):
    opening.animate(name, clip, parameter, start, end, fn, fps=FPS)


def motion_value(value):
    """FilmPolish uses State.motionValue's clamped quintic smootherstep."""
    u = max(0.0, min(1.0, value))
    return u * u * u * (10 + u * (-15 + 6 * u))


def create_generator(name, target, kind, start, duration, parameters=None):
    return add(name, "createTimelineGenerator", trackId=target,
               generatorKind=kind, startSeconds=start,
               durationSeconds=duration, parameters=parameters or {})


def create_pill(name, x, y, width, label, start, duration,
                text_color=PAPER, font_size=19, height=47):
    bg_track = track(name + " background")
    bg = shape(name + "-pill", bg_track, x, y, width, height,
               start, duration, "rounded-rectangle", "#0b0d12bd",
               "#c4c8ce55", 1, height / 2)
    text_track = track(name + " text")
    title(name + "-label", label, text_track, x + width / 2,
          y + height / 2 + font_size * 0.35, font_size,
          "Space Mono", text_color, 0.4, start, duration, "center")
    return bg


def caption(name, chapter_start, duration, title_text, detail_text,
            serif=False, enter_delay=0, chapter_label="05 / VIDEO TIMELINE"):
    label_track = track(name + " chapter label")
    label = title(name + "-label", chapter_label, label_track, 82, 275,
                  21, "Space Mono", LIME, 1.8,
                  chapter_start, duration)
    animate(name + "-label-enter", label, "opacity", enter_delay,
            enter_delay + 0.55,
            lambda local: opening.out(local, enter_delay, enter_delay + 0.55))

    title_track = track(name + " headline")
    head_y = 399
    head = title(name + "-headline", title_text, title_track, 82,
                 head_y, 91, "Instrument Serif" if serif else "Anton",
                 PAPER, -1.8, chapter_start, duration,
                 italic=serif)
    animate(name + "-headline-enter", head, "opacity", enter_delay + 0.05,
            enter_delay + 0.60,
            lambda local: opening.out(local, enter_delay + 0.05,
                                      enter_delay + 0.60))
    animate(name + "-headline-lift", head, "frameOffsetY", enter_delay + 0.05,
            enter_delay + 0.80,
            lambda local: 45 / (HEIGHT * 0.25) *
            (1 - opening.out(local, enter_delay + 0.05, enter_delay + 0.80)))

    detail_track = track(name + " supporting copy")
    detail = title(name + "-detail", detail_text, detail_track, 82,
                   586, 33, "Space Grotesk", PAPER, 0,
                   chapter_start, duration, line_height=1.4)
    animate(name + "-detail-enter", detail, "opacity", enter_delay + 0.2,
            enter_delay + 0.75,
            lambda local: opening.out(local, enter_delay + 0.2,
                                      enter_delay + 0.75))
    animate(name + "-detail-lift", detail, "frameOffsetY", enter_delay + 0.2,
            enter_delay + 0.95,
            lambda local: 45 / (HEIGHT * 0.25) *
            (1 - opening.out(local, enter_delay + 0.2, enter_delay + 0.95)))


def footer(name, start, duration, left, right="COLOR · MOTION · LAYERS · AUDIO"):
    left_track = track(name + " footer left")
    title(name + "-footer-left", left, left_track, 82, 1058, 17,
          "Space Mono", MUTED, 1, start, duration)
    right_track = track(name + " footer right")
    title(name + "-footer-right", right, right_track, 1838, 1058, 17,
          "Space Mono", MUTED, 1, start, duration, "right")


def build_film_closing():
    opening.STEPS.clear()

    # The scene clock and footage remain native media. The shader and curved
    # transitions are editable generator clips with exact source timing.
    film_light = track("13 / Film chapter light")
    create_generator("film-light", film_light, "desktop-light", 196, 56,
                     {"timeOffsetSeconds": 0, "timeScale": 1,
                      "gather": 0, "opacity": 0.28})
    closing_light = track("14 / Closing light")
    closing_glow = create_generator(
        "closing-light", closing_light, "desktop-light", 280, 18,
        {"timeOffsetSeconds": 0, "timeScale": 1,
         "gather": 0, "opacity": 0.65})
    animate("closing-light-gather", closing_glow, "generator:gather", 6, 15,
            lambda local: opening.smooth(local, 6, 15))

    # FilmChapter 184–196: the four original sources that FilmPolish's
    # filmShots/sourceAt model cuts and captions at their authored boundaries.
    montage_track = track("15 / Film montage sources")
    shot_rows = [
        ("arrival", "creative-polish-v2/arrival-12.mp4", 184.0, 3.7,
         1.0, 4.7, "27° N / A FIELD FILM", "After the ridgeline.",
         "studio/arrival-first.jpg"),
        ("ridge", "MVI_5051-2.3-5.9.mp4", 187.7, 3.2,
         0.0, 3.2, "ABOVE THE EVERYDAY", "A little further.",
         "studio/ridge-thumb.jpg"),
        ("city", "creative-polish-v2/city-10.mp4", 190.9, 2.7,
         0.0, 2.7, "KATHMANDU / AFTERNOON", "Down another street.",
         "studio/city-delivery.jpg"),
        ("night", "refine/night.mp4", 193.6, 2.4,
         2.6, 5.0, "27° N / A FIELD FILM", "After the ridgeline.",
         "studio/night-thumb.jpg"),
    ]
    for shot, src, at, length, trim_start, trim_end, _, _, thumb in shot_rows:
        clip = video("film-shot-" + shot, src, montage_track, at, length,
                     trim_start, trim_end, poster=str((MEDIA / thumb).resolve()))
        frame("film-shot-" + shot + "-frame", clip, fit="fill")

    edit_strips = track("16 / EditPreview edge gradients")
    gradient = create_generator(
        "edit-preview-edge-fade", edit_strips, "linear-gradient", 184, 12,
        {"angleDegrees": 180, "stops": [
            {"position": 0.0, "color": "#080c13", "opacity": 1.0},
            {"position": 0.05, "color": "#080c13", "opacity": 1.0},
            {"position": 0.051, "color": "#080c13", "opacity": 0.0},
            {"position": 0.949, "color": "#080c13", "opacity": 0.0},
            {"position": 0.95, "color": "#080c13", "opacity": 1.0},
            {"position": 1.0, "color": "#080c13", "opacity": 1.0},
        ]})

    intro_tag_track = track("17 / Film chapter introduction label")
    intro_tag = title("film-intro-tag", "05 / MAKE THE WHOLE FILM.",
                      intro_tag_track, 100, 92, 21, "Space Mono", PAPER,
                      1.8, 184, 12)
    intro_detail_track = track("18 / Film chapter introduction copy")
    intro_detail = title(
        "film-intro-detail",
        "Footage. Titles. Color. Sound. One finished edit.",
        intro_detail_track, 100, 139, 32, "Space Grotesk", PAPER, 0,
        184, 12)
    animate("film-intro-detail-out", intro_detail, "opacity", 3.4, 4.5,
            lambda local: 1 - opening.smooth(local, 3.4, 4.5))

    # Scene-specific lower-third copy is editable and follows the same four
    # native source ranges, instead of being baked into a trailer frame.
    scene_label_track = track("19 / Film scene labels")
    scene_title_track = track("20 / Film scene titles")
    for shot, _, at, length, _, _, label, heading, _ in shot_rows:
        small = title("film-shot-" + shot + "-label", label,
                      scene_label_track, 40, 967, 10.4, "Space Mono",
                      PAPER, 1, at, length)
        large = title("film-shot-" + shot + "-heading", heading,
                      scene_title_track, 40, 1022, 65, "Instrument Serif",
                      PAPER, -1, at, length, italic=True)

    # Phase 1: native representation of the SyncedTimeline panel. Exact source
    # screenshot strips are cropped from the original authored UI asset.
    panel_start, panel_duration = 196, 12
    panel_bg_track = track("21 / SyncedTimeline application frame")
    stage_shape("synced-timeline-frame", panel_bg_track, *stage_box(0, 0, 1000, 800),
                panel_start, panel_duration, "rounded-rectangle", "#171d24",
                "#66768488", 1, 13)
    top_strip_track = track("22 / SyncedTimeline top source strip")
    top_strip = still("synced-timeline-top-strip", "creative-polish-v2/timeline-native.png",
                      top_strip_track, panel_start, panel_duration)
    frame("synced-timeline-top-strip-frame", top_strip,
          *stage_box(0, 0, 1000, 42), fit="stretch",
          crop=(0, 25 / 585, 1, 67 / 585))
    bottom_strip_track = track("23 / SyncedTimeline bottom source strip")
    bottom_strip = still("synced-timeline-bottom-strip", "creative-polish-v2/timeline-native.png",
                         bottom_strip_track, panel_start, panel_duration)
    frame("synced-timeline-bottom-strip-frame", bottom_strip,
          *stage_box(0, 520, 1000, 42), fit="stretch",
          crop=(0, 389 / 585, 1, 431 / 585))

    panel_heading_track = track("24 / SyncedTimeline program header")
    title("synced-timeline-name", "AFTER THE RIDGELINE", panel_heading_track,
          *[STAGE_X + 22 * STAGE_SCALE, STAGE_Y + 60 * STAGE_SCALE + 12],
          21, "Space Grotesk", PAPER, 0, panel_start, panel_duration)
    clock_title = title("synced-timeline-timecode", "PROGRAM / 00:00:11:29",
                        panel_heading_track,
                        STAGE_X + 750 * STAGE_SCALE,
                        STAGE_Y + 60 * STAGE_SCALE + 10,
                        13, "Space Mono", "#9aafbe", 0,
                        panel_start, panel_duration, "right")

    preview_frame_track = track("25 / SyncedTimeline program viewer frame")
    stage_shape("synced-program-viewer-backdrop", preview_frame_track,
                *stage_box(25, 105, 700, 394), panel_start, panel_duration,
                "rounded-rectangle", "#090e15", "#00000000", 0, 7)
    preview_media_track = track("26 / SyncedTimeline moving program sources")
    # Same edit sources, independently framed into the native program panel.
    for shot, src, _, length, trim_start, trim_end, _, _, thumb in shot_rows:
        rel = 196 + sum(row[3] for row in shot_rows[:shot_rows.index(
            next(row for row in shot_rows if row[0] == shot))])
        preview_clip = video("synced-program-" + shot, src, preview_media_track,
                             rel, length, trim_start, trim_end,
                             poster=str((MEDIA / thumb).resolve()))
        frame("synced-program-" + shot + "-frame", preview_clip,
              *stage_box(25, 105, 700, 394), fit="fill", radius=7 / 394)

    inspector_bg_track = track("27 / SyncedTimeline inspector")
    stage_shape("synced-inspector-background", inspector_bg_track,
                *stage_box(750, 105, 230, 394), panel_start, panel_duration,
                "rounded-rectangle", "#111820", "#00000000", 0, 8)
    inspector_text_track = track("28 / SyncedTimeline inspector labels")
    for idx, (label, value) in enumerate([
        ("INSPECTOR", ""), ("Transform", ""),
        ("Position", "0 / 0"), ("Scale", "100%"), ("Opacity", "100%"),
        ("LAYERED VIDEO", ""), ("TITLES + AUDIO", ""),
    ]):
        y = [135, 170, 215, 270, 325, 420, 448][idx]
        font = "Space Mono" if idx in (0, 5, 6) else "Space Grotesk"
        size = 15 if idx == 0 else 12 if idx in (5, 6) else 21 if idx == 1 else 16
        color = "#8fa5b8" if idx in (5, 6) else "#d7ff48" if value else PAPER
        label_x = 772 if idx not in (2, 3, 4) else 772
        value_text = label + ("    " + value if value else "")
        title("synced-inspector-row-" + str(idx), value_text,
              inspector_text_track,
              STAGE_X + label_x * STAGE_SCALE,
              STAGE_Y + y * STAGE_SCALE,
              size, font, color, 0, panel_start, panel_duration)

    tick_track = track("29 / SyncedTimeline time ticks")
    for tick_index, seconds in enumerate((0, 3, 6, 9, 12)):
        title("synced-timeline-tick-" + str(tick_index),
              f"{seconds:02d}:00", tick_track,
              STAGE_X + (110 + seconds * 70) * STAGE_SCALE,
              STAGE_Y + 593 * STAGE_SCALE,
              13, "Space Mono", "#a9bac8", 0,
              panel_start, panel_duration, "center")

    track_names_track = track("30 / SyncedTimeline lane names")
    title("synced-track-title", "V2 TITLE", track_names_track,
          STAGE_X + 20 * STAGE_SCALE, STAGE_Y + 627 * STAGE_SCALE,
          13, "Space Mono", "#adbdc8", 0, panel_start, panel_duration)
    title("synced-track-video", "V1 VIDEO", track_names_track,
          STAGE_X + 20 * STAGE_SCALE, STAGE_Y + 681 * STAGE_SCALE,
          13, "Space Mono", "#adbdc8", 0, panel_start, panel_duration)
    title("synced-track-audio", "A1 MUSIC", track_names_track,
          STAGE_X + 20 * STAGE_SCALE, STAGE_Y + 735 * STAGE_SCALE,
          13, "Space Mono", "#adbdc8", 0, panel_start, panel_duration)

    timeline_text_track = track("31 / SyncedTimeline title edit")
    stage_shape("synced-title-edit", timeline_text_track,
                *stage_box(110, 607, 840, 26), panel_start, panel_duration,
                "rounded-rectangle", "#5c526e", "#a9a0bf", 1, 3)
    title("synced-title-edit-label", "AFTER THE RIDGELINE / TEXT",
          timeline_text_track, STAGE_X + 125 * STAGE_SCALE,
          STAGE_Y + 620 * STAGE_SCALE, 12, "Space Mono", PAPER, 0,
          panel_start, panel_duration)

    thumb_track = track("32 / SyncedTimeline film thumbnails")
    cursor_x = 110
    for index, (shot, _, _, length, _, _, _, _, thumb) in enumerate(shot_rows):
        block_width = length * 70 - 3
        shot_start = 196 + sum(row[3] for row in shot_rows[:index])
        thumb_clip = still("synced-thumbnail-" + shot, thumb, thumb_track,
                           panel_start, panel_duration)
        frame("synced-thumbnail-" + shot + "-frame", thumb_clip,
              *stage_box(cursor_x, 642, block_width, 62), fit="fill",
              radius=3 / 62)
        label_track = track("33 / Thumbnail label " + shot)
        stage_shape("synced-thumbnail-label-backdrop-" + shot,
                    label_track, cursor_x, 685, block_width, 19,
                    panel_start, panel_duration, "rectangle", "#0c1722dc")
        label_text = {"arrival": "ARRIVAL", "ridge": "RIDGELINE",
                      "city": "KATHMANDU", "night": "AFTER DARK"}[shot]
        title("synced-thumbnail-label-" + shot, label_text, label_track,
              STAGE_X + (cursor_x + 4) * STAGE_SCALE,
              STAGE_Y + 698 * STAGE_SCALE, 11,
              "Space Mono", PAPER, 0, panel_start, panel_duration)
        cursor_x += length * 70

    music_wave_track = track("34 / SyncedTimeline source music waveform")
    stage_shape("synced-music-waveform-block", music_wave_track,
                *stage_box(110, 714, 840, 48), panel_start, panel_duration,
                "rounded-rectangle", "#234d49", "#6caca1", 1, 3)

    playhead_track = track("35 / SyncedTimeline playback cursor")
    playhead = stage_shape("synced-timeline-playhead", playhead_track,
                           110, 574, 2, 188, panel_start, panel_duration,
                           "rectangle", LIME)
    def timeline_clock(s):
        if s < 1.2:
            return 11.95
        if s < 3.5:
            return 11.95 * (1 - opening.smooth(s, 1.2, 2.5))
        if s < 6.8:
            return 9.56 - 0.7 * opening.smooth(s, 3.5, 6.5)
        return 7.45 + (11.25 - 7.45) * opening.smooth(s, 7, 11.5)
    animate("synced-playhead-time", playhead, "titlePositionX", 0, 12,
            lambda s: (STAGE_X + (110 + timeline_clock(s) * 70) * STAGE_SCALE) / WIDTH)

    trim_label_track = track("36 / SyncedTimeline ripple trim annotation")
    trim_bg = stage_shape("synced-ripple-trim-backdrop", trim_label_track,
                          790, 480, 188, 37, 200, 8,
                          "rounded-rectangle", "#142532e8", radius=4)
    trim_label = title("synced-ripple-trim-label", "RIPPLE TRIM / −0.70 s",
                       trim_label_track,
                       STAGE_X + 890 * STAGE_SCALE,
                       STAGE_Y + 498 * STAGE_SCALE,
                       15, "Space Mono", PAPER, 0, 200, 8, "right")

    caption("film-edit", 197, 11, "Build the edit.\nKeep it moving.",
            "Layer footage, titles and audio. Trim a clip; ripple the following shots into place.",
            enter_delay=0)

    # Phase 2: the same City source and a real editable native title whose
    # position, scale and opacity are keyed on the Timeline.
    motion_caption = "Motion, with\nprecise control."
    caption("film-motion", 208, 10, motion_caption,
            "Position, scale, opacity and easing. The animation follows your keyframes.")
    motion_source_track = track("37 / Motion keyframe source footage")
    city_motion = video("motion-source-city", "creative-polish-v2/city-10.mp4",
                        motion_source_track, 208, 10, 0, 10)
    stage_frame("motion-source-city-frame", city_motion, 0, 10,
                1000, 562, "fill", radius=11)
    motion_overlay_track = track("38 / Motion source tint")
    stage_shape("motion-source-tint", motion_overlay_track,
                0, 10, 1000, 562, 208, 10, "rectangle", "#0b162b25")
    card_track = track("39 / NEPAL field title border")
    card = stage_shape("nepal-title-card", card_track,
                       65, 150, 500, 205, 208, 10,
                       "rectangle", "#00000000", "#e8eddfb3", 1)
    card_start_x = 430 + 250
    card_end_x = 65 + 250
    card_start_y = 340 + 102.5
    card_end_y = 150 + 102.5
    animate("nepal-card-scale", card, "frameScale", 0.5, 4,
            lambda s: 0.75 + 0.25 * motion_value((s - 0.5) / 3.5))
    animate("nepal-card-x", card, "titlePositionX", 0.5, 4,
            lambda s: (STAGE_X + (430 + (65 - 430) *
                                   motion_value((s - 0.5) / 3.5) + 250) *
                       STAGE_SCALE) / WIDTH)
    animate("nepal-card-y", card, "titlePositionY", 0.5, 4,
            lambda s: (STAGE_Y + (340 + (150 - 340) *
                                   motion_value((s - 0.5) / 3.5) + 102.5) *
                       STAGE_SCALE) / HEIGHT)
    animate("nepal-card-opacity", card, "opacity", 0.5, 4,
            lambda s: motion_value((s - 0.5) / 3.5))
    nepal_track = track("40 / NEPAL title")
    nepal = title("nepal-title", "NEPAL", nepal_track,
                  STAGE_X + (430 + 28) * STAGE_SCALE,
                  STAGE_Y + (340 + 12 + 73) * STAGE_SCALE,
                  145, "Anton", PAPER, 0, 208, 10)
    animate("nepal-title-size", nepal, "titleFontSize", 0.5, 4,
            lambda s: 145 * (0.75 + 0.25 * motion_value((s - 0.5) / 3.5)))
    animate("nepal-title-x", nepal, "titlePositionX", 0.5, 4,
            lambda s: (STAGE_X + (430 + (65 - 430) *
                                   motion_value((s - 0.5) / 3.5) + 28) *
                       STAGE_SCALE) / WIDTH)
    animate("nepal-title-y", nepal, "titlePositionY", 0.5, 4,
            lambda s: (STAGE_Y + (340 + (150 - 340) *
                                   motion_value((s - 0.5) / 3.5) + 85) *
                       STAGE_SCALE) / HEIGHT)
    animate("nepal-title-opacity", nepal, "opacity", 0.5, 4,
            lambda s: motion_value((s - 0.5) / 3.5))
    sub_track = track("41 / NEPAL subtitle")
    sub = title("nepal-subtitle", "A FIELD FILM", sub_track,
                STAGE_X + (430 + 28) * STAGE_SCALE,
                STAGE_Y + (340 + 12 + 178) * STAGE_SCALE,
                18, "Space Mono", PAPER, 0, 208, 10)
    animate("nepal-subtitle-x", sub, "titlePositionX", 0.5, 4,
            lambda s: (STAGE_X + (430 + (65 - 430) *
                                   motion_value((s - 0.5) / 3.5) + 28) *
                       STAGE_SCALE) / WIDTH)
    animate("nepal-subtitle-y", sub, "titlePositionY", 0.5, 4,
            lambda s: (STAGE_Y + (340 + (150 - 340) *
                                   motion_value((s - 0.5) / 3.5) + 190) *
                       STAGE_SCALE) / HEIGHT)
    animate("nepal-subtitle-opacity", sub, "opacity", 0.5, 4,
            lambda s: motion_value((s - 0.5) / 3.5))

    graph_label_track = track("42 / Motion keyframe readout")
    readout = title("motion-keyframe-readout",
                    "X 65 / Y 150 · SCALE 100% · OPACITY 100%",
                    graph_label_track, STAGE_X + 15 * STAGE_SCALE,
                    STAGE_Y + 800 * STAGE_SCALE, 15,
                    "Space Mono", PAPER, 0, 208, 10)
    graph_curve_layer = track("43 / Motion curve illustration")
    graph_curve_note = title("motion-curve-native-limit",
                             "KEYFRAME CURVE / NATIVE TITLE CONTROLS",
                             graph_curve_layer, STAGE_X + 500 * STAGE_SCALE,
                             STAGE_Y + 805 * STAGE_SCALE, 15,
                             "Space Mono", LIME, 0, 208, 10, "center")

    # Phase 3 uses the exact source variants already referenced by FilmPolish.
    caption("film-grade", 218, 8, "Build the look.\nLayer the effects.",
            "A native color grade and a monochrome effect. The same footage, visibly transformed.")
    grade_base_track = track("44 / Original city source")
    grade_base = video("grade-original-city", "creative-polish-v2/city-10.mp4",
                       grade_base_track, 218, 8, 0, 8)
    stage_frame("grade-original-city-frame", grade_base, 0, 35,
                1000, 562, "fill", radius=11)
    grade_track = track("45 / Native grade source comparison")
    grade_clip = video("grade-reference-source", "creative-polish-v2/city-grade-10.mp4",
                       grade_track, 218, 8, 0, 8)
    stage_frame("grade-reference-source-frame", grade_clip, 0, 35,
                1000, 562, "fill", radius=11, opacity=0)
    animate("grade-source-fade", grade_clip, "opacity", 1, 2,
            lambda s: opening.smooth(s, 1, 2))
    mono_track = track("46 / Monochrome source comparison")
    mono_clip = video("mono-reference-source", "creative-polish-v2/city-mono-10.mp4",
                      mono_track, 218, 8, 0, 8)
    stage_frame("mono-reference-source-frame", mono_clip, 0, 35,
                1000, 562, "fill", radius=11, opacity=0)
    animate("mono-source-fade", mono_clip, "opacity", 4.1, 5.1,
            lambda s: opening.smooth(s, 4.1, 5.1))
    # Native clip masks currently support rounded rectangles, not an animated
    # rectangular edge wipe. These editable labels preserve the exact before/
    # after identities while a renderer mask primitive is tracked separately.
    grade_ui_track = track("47 / Native effects comparison labels")
    title("effects-before-label", "BEFORE", grade_ui_track,
          STAGE_X + 45 * STAGE_SCALE, STAGE_Y + 55 * STAGE_SCALE,
          16, "Space Mono", PAPER, 0, 218, 8)
    title("effects-after-label", "NATIVE EFFECTS", grade_ui_track,
          STAGE_X + 940 * STAGE_SCALE, STAGE_Y + 55 * STAGE_SCALE,
          16, "Space Mono", PAPER, 0, 218, 8, "right")
    badge_track = track("48 / Native effects badges")
    for index, (label, begin, end) in enumerate([
        ("COLOR GRADE", 1, 2), ("MONOCHROME", 4.1, 5.1),
    ]):
        x = 700 + index * 390
        bg = stage_shape("effect-badge-" + str(index), badge_track,
                         x, 640, 380, 67, 218, 8,
                         "rounded-rectangle", "#14202c", "#a7b6c94a", 1, 12)
        indicator_track = track("49 / Effect badge indicator " + str(index))
        dot = stage_shape("effect-badge-dot-" + str(index), indicator_track,
                          x + 18, 663, 18, 18, 218, 8, "ellipse", "#495563")
        animate("effect-badge-dot-opacity-" + str(index), dot, "opacity", begin, end,
                lambda s, a=begin, b=end: opening.smooth(s, a, b))
        title("effect-badge-text-" + str(index), label, badge_track,
              STAGE_X + (x + 60) * STAGE_SCALE,
              STAGE_Y + 677 * STAGE_SCALE, 19, "Space Mono", PAPER, 0,
              218, 8)
        value_track = track("50 / Effect badge amount " + str(index))
        value = title("effect-badge-value-" + str(index), "100%",
                      value_track, STAGE_X + (x + 350) * STAGE_SCALE,
                      STAGE_Y + 677 * STAGE_SCALE, 19,
                      "Space Mono", LIME, 0, 218, 8, "right")
        animate("effect-badge-value-opacity-" + str(index), value,
                "opacity", begin, end,
                lambda s, a=begin, b=end: opening.smooth(s, a, b))
    grade_summary_track = track("51 / Native grade values")
    title("native-grade-values",
          "GRADE: EXPOSURE +0.25 / CONTRAST +14 / VIBRANCE +12",
          grade_summary_track, STAGE_X + 50 * STAGE_SCALE,
          STAGE_Y + 755 * STAGE_SCALE, 15,
          "Space Mono", PAPER, 0, 218, 8)

    # Phase 4 makes the original city footage and the two mix buses visible.
    caption("film-audio", 226, 6, "Picture meets\nsound.",
            "Pull down the music and hear the location. Shape the mix alongside the edit.")
    audio_view_track = track("52 / Picture meets sound source")
    city_audio_view = video("location-audio-city-source", "creative-polish-v2/city-10.mp4",
                            audio_view_track, 226, 6, 0, 6)
    stage_frame("location-audio-city-frame", city_audio_view, 70, 0,
                860, 484, "fill", radius=10)
    mixer_track = track("53 / Native mixer lane cards")
    for index, (name, label, color) in enumerate([
        ("music", "MUSIC", "#294668"),
        ("location", "LOCATION", "#204e43"),
    ]):
        y = 510 + index * 125
        stage_shape("mixer-lane-" + name, mixer_track,
                    20, y, 960, 104, 226, 6,
                    "rounded-rectangle", color, "#00000000", 0, 9)
        gain_track = track("54 / Mixer gain " + name)
        gain_text = "−12.0 dB" if name == "music" else "ON / 0 dB"
        title("mixer-lane-label-" + name, name.upper(), gain_track,
              STAGE_X + 35 * STAGE_SCALE,
              STAGE_Y + (y + 26) * STAGE_SCALE,
              15, "Space Mono", PAPER, 0, 226, 6)
        title("mixer-lane-value-" + name, gain_text, gain_track,
              STAGE_X + 955 * STAGE_SCALE,
              STAGE_Y + (y + 26) * STAGE_SCALE,
              15, "Space Mono", PAPER, 0, 226, 6, "right")
    mixer_cursor_track = track("55 / Mixer program cursor")
    mixer_cursor = stage_shape("mixer-program-cursor", mixer_cursor_track,
                               35, 510, 2, 230, 226, 6,
                               "rectangle", LIME)
    animate("mixer-program-cursor-move", mixer_cursor, "titlePositionX", 0, 6,
            lambda s: (STAGE_X + (35 + (s / 6) * 930) * STAGE_SCALE) / WIDTH)

    # Phase 5 is the dedicated DeliveryProof layout and its source still.
    delivery_light = track("56 / Delivery proof light")
    create_generator("delivery-proof-light", delivery_light, "desktop-light",
                     232, 8, {"timeOffsetSeconds": 0, "timeScale": 1,
                              "gather": 0, "opacity": 0.26})
    proof_header_track = track("57 / Delivery proof heading")
    title("delivery-proof-label", "05 / VIDEO TIMELINE", proof_header_track,
          85, 91, 21, "Space Mono", LIME, 1.8, 232, 8)
    title("delivery-proof-headline", "From camera color\nto final delivery.",
          proof_header_track, 85, 245, 106, "Anton", PAPER, -1.8,
          232, 8)
    delivery_image_track = track("58 / Rec709 delivery still")
    delivery_img = still("rec709-delivery-still", "studio/city-delivery.jpg",
                         delivery_image_track, 232, 8)
    frame("rec709-delivery-still-frame", delivery_img,
          85, 355, 1080, 608, fit="fill", radius=10 / 608)
    tag_track = track("59 / Rec709 source badge")
    shape("rec709-current-example-badge", tag_track,
          110, 913, 455, 50, 232, 8,
          "rectangle", "#0b1424de")
    title("rec709-current-example-label", "CURRENT EXAMPLE / Rec.709 SOURCE",
          tag_track, 130, 945, 20, "Space Mono", PAPER, 0, 232, 8)
    delivery_rows = [
        ("COLOR", "sRGB · Rec.709", "Color-managed workflows"),
        ("LOG INPUT", "Sony S-Log2 · S-Log3", "Supported camera transforms"),
        ("DELIVERY", "4K · ProRes", "Resolution and codec options"),
    ]
    for idx, (label, value, detail) in enumerate(delivery_rows):
        y = 380 + idx * 176
        row_track = track("60 / Delivery specification " + str(idx))
        shape("delivery-row-rule-" + str(idx), row_track,
              1240, y, 590, 1, 232, 8, "rectangle", "#b3c2cf55")
        row_label = title("delivery-row-label-" + str(idx), label,
                          row_track, 1240, y + 25, 19,
                          "Space Mono", LIME, 1, 232, 8)
        row_value = title("delivery-row-value-" + str(idx), value,
                          row_track, 1240, y + 83, 37,
                          "Space Grotesk", PAPER, 0, 232, 8)
        row_detail = title("delivery-row-detail-" + str(idx), detail,
                           row_track, 1240, y + 131, 24,
                           "Space Grotesk", "#b7c4d2", 0, 232, 8)
        begin, end = idx * 0.35, 0.55 + idx * 0.35
        for nm, clip in (("label", row_label), ("value", row_value), ("detail", row_detail)):
            animate(f"delivery-row-{idx}-{nm}-enter", clip, "opacity", begin, end,
                    lambda s, a=begin, b=end: opening.out(s, a, b))
            animate(f"delivery-row-{idx}-{nm}-lift", clip, "frameOffsetY", begin, end,
                    lambda s, a=begin, b=end: 20 / (HEIGHT * 0.25) *
                    (1 - opening.out(s, a, b)))
    pills_x = 20
    for index, (label, pill_width) in enumerate([
        ("3840 × 2160", 195), ("30 FPS", 110),
        ("PRORES / MOV", 195), ("COLOR-MANAGED", 215),
    ]):
        create_pill("delivery-proof-pill-" + str(index), pills_x, 640,
                    pill_width, label, 232, 8,
                    LIME if index == 0 else PAPER)
        pills_x += pill_width + 12
    footer("film-delivery", 232, 8,
           "COLOR SPACES. CAMERA INPUTS. DELIVERY OPTIONS.",
           "LOOKSY / VIDEO")

    # Phase 6: exact native Gaussian source path and the authored credit.
    caption("film-gaussian", 240, 4, "Change your\nperspective.",
            "Gaussian scenes with a moving camera. Another dimension in the creative toolbox.",
            serif=True)
    gaussian_track = track("61 / Gaussian camera footage")
    gaussian = video("gaussian-camera-source", "studio/gaussian-camera.mp4",
                     gaussian_track, 240, 4, 3, 7, source_duration=12)
    stage_frame("gaussian-camera-frame", gaussian, 0, 25,
                1000, 665, "fill", radius=14)
    gaussian_label_track = track("62 / Gaussian scene label")
    title("gaussian-scene-label", "GAUSSIAN SCENE / CAMERA MOTION",
          gaussian_label_track, STAGE_X + 25 * STAGE_SCALE,
          STAGE_Y + 755 * STAGE_SCALE, 17,
          "Space Mono", PAPER, 0, 240, 4)
    footer("film-gaussian", 240, 4,
           "HOUSEPLANT / MARCEL PADILLA / CC BY 4.0")

    # Phase 7: the same arrival footage is independently framed for landscape
    # and a rotated portrait social crop.
    caption("film-delivery", 244, 8, "A full film.\nA social cut.",
            "Landscape productions and independently framed vertical videos.")
    landscape_track = track("63 / Full landscape delivery")
    landscape = video("full-landscape-arrival", "creative-polish-v2/arrival-12.mp4",
                      landscape_track, 244, 8, 0, 8)
    stage_frame("full-landscape-arrival-frame", landscape, 0, 65,
                830, 467, "fill", radius=12,
                perspective_x=0.06)
    landscape_title_track = track("64 / Landscape film title")
    title("landscape-delivery-title", "A little further.",
          landscape_title_track, STAGE_X + 30 * STAGE_SCALE,
          STAGE_Y + 500 * STAGE_SCALE, 65,
          "Instrument Serif", PAPER, -1, 244, 8, italic=True)

    phone_track = track("65 / Social delivery phone shell")
    phone_x, phone_y, phone_w, phone_h = stage_box(690, 230, 260, 462)
    phone_shell = shape("social-delivery-phone-shell", phone_track,
                        phone_x, phone_y, phone_w, phone_h, 244, 8,
                        "rounded-rectangle", "#111822", "#c4d0d4", 5.5,
                        19.8)
    frame("social-phone-shell-transform", phone_shell,
          phone_x, phone_y + 99, phone_w, phone_h,
          fit="stretch", rotation=5)
    animate("social-phone-shell-rise", phone_shell, "frameOffsetY", 0, 1.3,
            lambda s: (90 * (1 - opening.out(s, 0.3, 1.3))) /
            (HEIGHT * 0.25))
    social_video_track = track("66 / Independent portrait social cut")
    social = video("social-cut-arrival", "creative-polish-v2/arrival-12.mp4",
                   social_video_track, 244, 8, 0, 8)
    frame("social-cut-arrival-frame", social,
          phone_x, phone_y + 99, phone_w, phone_h,
          fit="fill", rotation=5, radius=18 / min(phone_w, phone_h))
    animate("social-cut-arrival-rise", social, "frameOffsetY", 0, 1.3,
            lambda s: (90 * (1 - opening.out(s, 0.3, 1.3))) /
            (HEIGHT * 0.25))
    social_text_track = track("67 / Social cut title")
    social_title = title("social-cut-title", "A LITTLE\nFURTHER.",
                         social_text_track,
                         phone_x + 20 * STAGE_SCALE,
                         phone_y + 55 * STAGE_SCALE + 57,
                         52, "Anton", PAPER, 0, 244, 8,
                         line_height=1)
    animate("social-cut-title-rise", social_title, "frameOffsetY", 0, 1.3,
            lambda s: (90 * (1 - opening.out(s, 0.3, 1.3))) /
            (HEIGHT * 0.25))
    for index, (label, width) in enumerate([
        ("FULL PRODUCTIONS", 235), ("SOCIAL CUTS", 165),
        ("HIGH-RESOLUTION DELIVERY", 350),
    ]):
        create_pill("film-delivery-pill-" + str(index),
                    STAGE_X + (10 + sum(w + 12 for w in (235, 165, 350)[:index])) * STAGE_SCALE,
                    STAGE_Y + 745 * STAGE_SCALE, width * STAGE_SCALE,
                    label, 244, 8, PAPER, 19, 47 * STAGE_SCALE)
    footer("film-delivery-social", 244, 8,
           "FROM FIRST CUT TO FINAL FRAME.")

    # BuyerClosing 280–298: closing hero, actual desktop screen, wordmark,
    # serif promise and a native button treatment.
    close_label_track = track("68 / Closing hero label")
    close_label = title("closing-hero-label", "PHOTO. DESIGN. FILM.",
                        close_label_track, 85, 116, 21,
                        "Space Mono", LIME, 1.8, 280, 18)
    close_heading_track = track("69 / Closing hero headline")
    close_heading = title("closing-hero-headline", "Make the whole thing.",
                          close_heading_track, 85, 90 + 21 + 25 + 63,
                          125, "Anton", PAPER, -1.8, 280, 18)
    close_detail_track = track("70 / Closing hero detail")
    close_detail = title("closing-hero-detail",
                         "One desktop studio. Your work stays yours to refine.",
                         close_detail_track, 85, 90 + 21 + 25 + 126 + 26 + 18,
                         36, "Space Grotesk", PAPER, 0, 280, 18)
    for key, clip in (("label", close_label), ("headline", close_heading),
                      ("detail", close_detail)):
        animate("closing-hero-" + key + "-fade", clip, "opacity", 7, 8.5,
                lambda s: 1 - opening.smooth(s, 7, 8.5))

    desktop_track = track("71 / Closing native desktop screenshot")
    desktop = still("closing-native-desktop", "desktop-launch/raw-editor.png",
                    desktop_track, 280, 18)
    frame("closing-native-desktop-frame", desktop,
          310, 390, 1300, 760, "fit", 1, radius=13 / 760)
    animate("closing-desktop-settle", desktop, "frameScale", 0, 7,
            lambda s: 0.96 + 0.04 * opening.smooth(s, 0, 7))
    animate("closing-desktop-fade", desktop, "opacity", 7, 8.5,
            lambda s: 1 - opening.smooth(s, 7, 8.5))

    cta_logo_track = track("72 / Closing Looksy wordmark")
    logo = still("closing-wordmark-source", "looksy-logo-dark.png",
                 cta_logo_track, 280, 18)
    logo_width, logo_height = 900, 900 * 454 / 1795
    frame("closing-wordmark-frame", logo,
          (WIDTH - logo_width) / 2, HEIGHT * 0.26,
          logo_width, logo_height, "fit", 0)
    animate("closing-wordmark-fade", logo, "opacity", 8.5, 10,
            lambda s: opening.smooth(s, 8.5, 10))

    cta_tagline_track = track("73 / Closing serif tagline")
    tagline = title("closing-tagline",
                    "From your first edit to your final delivery.",
                    cta_tagline_track, WIDTH / 2,
                    HEIGHT * 0.26 + logo_height + 32 + 30,
                    57, "Instrument Serif", PAPER, 0,
                    280, 18, "center", italic=True)
    animate("closing-tagline-fade", tagline, "opacity", 8.5, 10,
            lambda s: opening.smooth(s, 8.5, 10))

    button_track = track("74 / Closing website call to action")
    button_width, button_height = 465, 92
    button_x, button_y = (WIDTH - button_width) / 2, 688
    button = shape("closing-website-button", button_track,
                   button_x, button_y, button_width, button_height,
                   280, 18, "rounded-rectangle", LIME,
                   "#00000000", 0, 8)
    frame("closing-button-frame", button, opacity=0)
    animate("closing-button-fade", button, "opacity", 8.5, 10,
            lambda s: opening.smooth(s, 8.5, 10))
    button_text_track = track("75 / Closing call to action text")
    button_text = title("closing-website-button-text", "Visit the website  ↗",
                        button_text_track, WIDTH / 2, button_y + 60,
                        35, "Space Grotesk", "#101820", 0,
                        280, 18, "center", weight=600)
    animate("closing-button-text-fade", button_text, "opacity", 8.5, 10,
            lambda s: opening.smooth(s, 8.5, 10))

    footer("closing", 280, 18,
           "FOR WORK. FOR YOURSELF. FOR WHAT COMES NEXT.",
           "LOOKSY / DESKTOP")

    # Remotion's global transition overlays occur at 184 and 280. Both are
    # native transition-veil generators, including their original half-second
    # preroll and postroll.
    veil_track = track("76 / Native curved chapter transitions")
    create_generator("veil-into-film", veil_track, "transition-veil",
                     183.5, 1.0, {"opacity": 1.0})
    create_generator("veil-into-closing", veil_track, "transition-veil",
                     279.5, 1.0, {"opacity": 1.0})

    steps = list(opening.STEPS)
    return {
        "reference": "landscape.mp4",
        "chapters": [[184, 252], [280, 298]],
        "canvas": {"width": WIDTH, "height": HEIGHT, "fps": FPS},
        "steps": steps,
        "unresolvedNativePrimitives": [
            "animated rectangular comparison wipe with independent frame crop",
            "arbitrary SVG path rendering for the exact easing graph and cursor glyph",
            "live data-bound title strings for PROGRAM timecode and animated readouts",
            "paper-grain fractal overlay used by BuyerClosing",
            "CSS rotateY perspective and pill layout do not have exact native equivalents",
            "native Program Monitor preview of the reverse-scrub/ripple edit clock",
        ],
    }


def build_global_audio():
    opening.STEPS.clear()
    score_track = audio_track("01 / Imported reference mix", "music")
    add("reference-master-score", "createTimelineClip",
        trackId=score_track,
        assetId="native-trailer:audio/creative-polish-v4-master.wav",
        filePath=str((PUBLIC / "audio/creative-polish-v4-master.wav").resolve()),
        displayLabel="Imported reference mix",
        durationSeconds=298.0, trimStartSeconds=0.0,
        trimEndSeconds=298.0, startSeconds=0.0, posterSource="")
    return {
        "reference": "creative-polish-v4-master.wav",
        "durationSeconds": 298,
        "sampleRole": "imported-reference-mix",
        "mixingAndSfxEditability": "not established",
        "steps": list(opening.STEPS),
    }


if __name__ == "__main__":
    film_plan = build_film_closing()
    film_path = ROOT / "film-closing.commands.json"
    film_path.write_text(json.dumps(film_plan, indent=2) + "\n")
    audio_plan = build_global_audio()
    audio_path = ROOT / "global-audio.commands.json"
    audio_path.write_text(json.dumps(audio_plan, indent=2) + "\n")
    print(film_path, len(film_plan["steps"]), "native commands")
    print(audio_path, len(audio_plan["steps"]), "native commands")
