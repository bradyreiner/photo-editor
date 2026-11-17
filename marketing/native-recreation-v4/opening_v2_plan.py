"""Create native, editable BuyerOpening plans for both reference formats.

The plans use source media and typed Timeline actions only. The desktop reveal
is authored as per-clip sequence masks because the current native command set
does not expose a shared, nested parent group.
"""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MEDIA = ROOT.parent / "animated-trailer/public/media"
SOURCE_MANIFEST = json.loads((ROOT / "source-manifest.json").read_text())
FPS = 30
DURATION = 12.0
INK = "#0b0d12"
PAPER = "#f1efdf"
LIME = "#d7ff48"


def smooth(t, start, end):
    u = max(0.0, min(1.0, (t - start) / (end - start)))
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def out(t, start, end):
    u = max(0.0, min(1.0, (t - start) / (end - start)))
    return 1.0 - (1.0 - u) ** 4


class OpeningPlan:
    def __init__(self, width, height, format_name):
        self.width = width
        self.height = height
        self.portrait = height > width
        self.format_name = format_name
        self.steps = []

    def add(self, step_id, action, **values):
        self.steps.append({
            "id": step_id,
            "commandId": "timeline.action." + action,
            "values": values,
        })
        return {"result": step_id, "field": "createdClipId"}

    def track(self, step_id, name):
        self.add(step_id, "createTimelineTrack", kind="graphics", name=name, audioRole="")
        return {"result": step_id, "field": "trackId"}

    def animate(self, step_id, clip, parameter, function, start=0.0, end=DURATION):
        first_frame = round(start * FPS)
        last_frame = round(end * FPS)
        samples = [
            {
                "localSeconds": frame / FPS,
                "value": round(float(function(frame / FPS)), 10),
                "interpolation": "linear",
            }
            for frame in range(first_frame, last_frame + 1)
        ]
        keyframes = [
            sample
            for index, sample in enumerate(samples)
            if index in (0, len(samples) - 1)
            or samples[index - 1]["value"] != sample["value"]
            or samples[index + 1]["value"] != sample["value"]
        ]
        self.add(
            step_id,
            "setTimelineClipAnimation",
            clipId=clip,
            channels=[{"parameter": parameter, "keyframes": keyframes}],
        )

    def animate_channels(self, step_id, clip, parameters, function):
        samples = [
            {"localSeconds": frame / FPS, "value": function(frame / FPS)}
            for frame in range(round(DURATION * FPS) + 1)
        ]
        channels = []
        for parameter in parameters:
            keyframes = []
            for index, sample in enumerate(samples):
                value = round(float(sample["value"][parameter]), 10)
                if index not in (0, len(samples) - 1):
                    previous = round(float(samples[index - 1]["value"][parameter]), 10)
                    following = round(float(samples[index + 1]["value"][parameter]), 10)
                    if previous == value == following:
                        continue
                keyframes.append({
                    "localSeconds": sample["localSeconds"],
                    "value": value,
                    "interpolation": "linear",
                })
            channels.append({"parameter": parameter, "keyframes": keyframes})
        self.add(
            step_id,
            "setTimelineClipAnimation",
            clipId=clip,
            channels=channels,
        )

    def frame(self, step_id, clip, x=0.0, y=0.0, width=None, height=None,
              fit="fit", opacity=1.0, scale=1.0, offset_x_px=0.0,
              offset_y_px=0.0, corner_radius=0.0):
        width = self.width if width is None else width
        height = self.height if height is None else height
        self.add(
            step_id,
            "setTimelineClipFrame",
            clipId=clip,
            offsetX=4.0 * ((x + width / 2.0) / self.width - 0.5)
                    + 4.0 * offset_x_px / self.width,
            offsetY=4.0 * ((y + height / 2.0) / self.height - 0.5)
                    + 4.0 * offset_y_px / self.height,
            scale=scale,
            rotationDegrees=0.0,
            perspectiveX=0.0,
            perspectiveY=0.0,
            opacity=opacity,
            objectFit=fit,
            width=width / self.width,
            height=height / self.height,
            cornerRadius=corner_radius,
            cropLeft=0.0,
            cropTop=0.0,
            cropRight=0.0,
            cropBottom=0.0,
        )

    def image(self, step_id, source, target, start=0.0, duration=DURATION):
        path = (MEDIA / source).resolve()
        return self.add(
            step_id,
            "createTimelineClip",
            trackId=target,
            assetId="native-trailer:" + source,
            filePath=str(path),
            displayLabel=step_id,
            durationSeconds=duration,
            trimStartSeconds=0.0,
            trimEndSeconds=duration,
            startSeconds=start,
            posterSource="",
        )

    def title(self, step_id, text, target, x, y, size, family="Anton",
              color=PAPER, spacing=-1.8, line_height=1.01,
              alignment="left", weight=400, italic=False):
        return self.add(
            step_id,
            "createTimelineTitle",
            trackId=target,
            templateKey="headline",
            startSeconds=0.0,
            durationSeconds=DURATION,
            values={
                "text": text,
                "fontFamily": family,
                "fontSize": size,
                "fontWeight": weight,
                "italic": italic,
                "letterSpacing": spacing,
                "lineHeight": line_height,
                "color": color,
                "backgroundColor": "#00000000",
                "alignment": alignment,
                "positionX": x / self.width,
                "positionY": y / self.height,
            },
        )

    def graphic(self, step_id, target, shape, x, y, width, height,
                color, opacity=1.0, radius=0.0, stroke="#00000000",
                stroke_width=0.0, start=0.0, duration=DURATION):
        return self.add(
            step_id,
            "createTimelineGraphic",
            trackId=target,
            shapeType=shape,
            startSeconds=start,
            durationSeconds=duration,
            values={
                "positionX": (x + width / 2.0) / self.width,
                "positionY": (y + height / 2.0) / self.height,
                "shapeWidth": width / self.width,
                "shapeHeight": height / self.height,
                "shapeCornerRadius": radius / max(1.0, min(width, height)),
                "backgroundColor": color,
                "shapeStrokeColor": stroke,
                "shapeStrokeWidth": stroke_width / min(self.width, self.height),
            },
        )

    def handoff_mask(self, step_id, clip):
        center_x = 0.58
        center_y = 0.55
        radius = max(self.width, self.height)

        def edges(t):
            r = radius * smooth(t, 8.0, 9.6)
            return {
                "mask:left": center_x - r / self.width,
                "mask:top": center_y - r / self.height,
                "mask:right": center_x + r / self.width,
                "mask:bottom": center_y + r / self.height,
            }

        initial = edges(0.0)
        self.add(
            step_id + "-mask",
            "setTimelineClipMask",
            clipId=clip,
            shape="ellipse",
            left=initial["mask:left"],
            top=initial["mask:top"],
            right=initial["mask:right"],
            bottom=initial["mask:bottom"],
            cornerRadius=0.0,
        )
        self.animate_channels(
            step_id + "-circle-reveal",
            clip,
            ["mask:left", "mask:top", "mask:right", "mask:bottom"],
            edges,
        )

    def handoff_opacity(self, step_id, clip, multiplier=1.0):
        self.frame(step_id + "-initial-opacity-frame", clip, opacity=0.0)
        self.animate(
            step_id + "-opacity",
            clip,
            "opacity",
            lambda t: multiplier * smooth(t, 8.0, 9.6),
        )
        self.handoff_mask(step_id, clip)

    def build(self):
        width, height = self.width, self.height
        portrait = self.portrait

        # BuyerOpening's full-frame ink and additive light sit behind every
        # reference layer and share the original 0..12 second local clock.
        ink_track = self.track("track-ink", "00 / Ink background")
        self.graphic(
            "ink-background", ink_track, "rectangle",
            0.0, 0.0, width, height, INK,
        )

        light_track = self.track("track-opening-light", "01 / Additive light")
        light = self.add(
            "opening-light",
            "createTimelineGenerator",
            trackId=light_track,
            generatorKind="desktop-light",
            startSeconds=0.0,
            durationSeconds=DURATION,
            parameters={"timeOffsetSeconds": 0.0, "timeScale": 1.0, "gather": 1.0},
        )
        light_clip = light
        self.frame("opening-light-frame", light_clip, fit="stretch", opacity=0.7)
        self.animate(
            "opening-light-gather", light_clip, "generator:gather",
            lambda t: 1.0 - smooth(t, 1.0, 4.0),
        )

        # The photographic panel and its CSS gradient are separate editable
        # layers. Portrait keeps the source full-bleed and uses the vertical
        # four-stop wash from BuyerOpening.
        hero_x = 0.0 if portrait else width * 0.25
        hero_width = width if portrait else width * 0.75
        hero_track = self.track("track-hero-photo", "02 / Camera photograph")
        hero = self.image(
            "hero-photo", "creative-polish-v2/curve/curve-16.png", hero_track,
        )
        hero_clip = hero
        initial_hero_scale = 1.08
        hero_center_x = hero_x + hero_width / 2.0
        hero_base_offset_x = 4.0 * (hero_center_x / width - 0.5)
        self.frame(
            "hero-photo-frame", hero_clip, hero_x, 0.0, hero_width, height,
            fit="fill", opacity=0.0, scale=initial_hero_scale,
            offset_x_px=(1.0 - initial_hero_scale) * (hero_center_x - width / 2.0),
        )
        self.animate(
            "hero-photo-opacity", hero_clip, "opacity",
            lambda t: out(t, 2.0, 3.1) * (1.0 - smooth(t, 8.0, 9.6)),
        )
        self.animate(
            "hero-photo-scale", hero_clip, "frameScale",
            lambda t: 1.08 - 0.08 * smooth(t, 2.0, 8.0),
            2.0, 8.0,
        )
        self.animate(
            "hero-photo-origin-x", hero_clip, "frameOffsetX",
            lambda t: hero_base_offset_x +
            4.0 * ((1.0 - (1.08 - 0.08 * smooth(t, 2.0, 8.0)))
                   * (hero_center_x - width / 2.0)) / width,
            2.0, 8.0,
        )

        gradient_track = self.track("track-hero-gradient", "03 / Photograph fade")
        if portrait:
            gradient_stops = [
                {"position": 0.0, "color": "#080c13", "opacity": 1.0},
                {"position": 0.04, "color": "#080c13", "opacity": 1.0},
                {"position": 0.32, "color": "#080c13", "opacity": 217.0 / 255.0},
                {"position": 0.80, "color": "#080c13", "opacity": 0.0},
                {"position": 1.0, "color": "#080c13", "opacity": 1.0},
            ]
            gradient_angle = 180.0
        else:
            gradient_stops = [
                {"position": 0.0, "color": "#080c13", "opacity": 1.0},
                {"position": 0.08, "color": "#080c13", "opacity": 1.0},
                {"position": 0.30, "color": "#080c13", "opacity": 232.0 / 255.0},
                {"position": 0.80, "color": "#080c13", "opacity": 0.0},
                {"position": 1.0, "color": "#080c13", "opacity": 0.0},
            ]
            gradient_angle = 90.0
        gradient = self.add(
            "hero-photo-gradient",
            "createTimelineGenerator",
            trackId=gradient_track,
            generatorKind="linear-gradient",
            startSeconds=0.0,
            durationSeconds=DURATION,
            parameters={"angleDegrees": gradient_angle, "stops": gradient_stops},
        )
        gradient_clip = gradient
        self.frame("hero-gradient-frame", gradient_clip, fit="stretch", opacity=0.0)
        self.animate(
            "hero-gradient-opacity", gradient_clip, "opacity",
            lambda t: out(t, 2.0, 3.1) * (1.0 - smooth(t, 8.0, 9.6)),
        )

        # Intro brand group: exact source left/top, 21 px mono label, 25 px gap,
        # and the AppWordmark's 480/650 px width with its 454:1795 ratio.
        brand_left = 65.0 if portrait else 90.0
        brand_top = 150.0 if portrait else 80.0
        brand_label_height = 31.0
        wordmark_width = 650.0 if portrait else 480.0
        wordmark_height = wordmark_width * 454.0 / 1795.0
        wordmark_top = brand_top + brand_label_height + 25.0
        brand_track = self.track("track-brand-wordmark", "04 / Wordmark")
        wordmark = self.image("app-wordmark", "looksy-logo-dark.png", brand_track)
        wordmark_clip = wordmark
        initial_lift = 30.0
        self.frame(
            "app-wordmark-frame", wordmark_clip,
            brand_left, wordmark_top, wordmark_width, wordmark_height,
            fit="fit", opacity=1.0, offset_y_px=initial_lift,
        )
        wordmark_center_y = wordmark_top + wordmark_height / 2.0
        self.animate(
            "app-wordmark-intro-lift", wordmark_clip, "frameOffsetY",
            lambda t: 4.0 * (wordmark_center_y / height - 0.5) +
            4.0 * 30.0 * (1.0 - out(t, 0.0, 0.7)) / height,
        )
        self.animate(
            "app-wordmark-handoff-opacity", wordmark_clip, "opacity",
            lambda t: 1.0 - smooth(t, 8.0, 9.6),
        )

        brand_label_track = self.track("track-brand-label", "05 / Opening label")
        brand_label_height_center = brand_top + brand_label_height / 2.0
        brand_label = self.title(
            "opening-label", "PHOTO + VIDEO / DESKTOP", brand_label_track,
            brand_left, brand_label_height_center, 21.0,
            "Space Mono", LIME, 1.8, 1.0,
        )
        brand_label_clip = brand_label
        self.frame("opening-label-frame", brand_label_clip, opacity=1.0,
                   offset_y_px=initial_lift)
        self.animate(
            "opening-label-intro-lift", brand_label_clip, "frameOffsetY",
            lambda t: 4.0 * 30.0 * (1.0 - out(t, 0.0, 0.7)) / height,
        )
        self.animate(
            "opening-label-handoff-opacity", brand_label_clip, "opacity",
            lambda t: 1.0 - smooth(t, 8.0, 9.6),
        )

        # Main headline and explanatory copy share the reference hero opacity
        # and 45 px intro lift. Type size and explicit line breaks match source.
        copy_left = 65.0 if portrait else 90.0
        copy_top = 500.0 if portrait else 310.0
        title_size = 150.0 if portrait else 145.0
        title_line_height = 1.01
        title_lines = 2
        title_block_height = title_size * title_line_height * title_lines
        copy_width = 950.0 if portrait else 1150.0
        copy_track = self.track("track-headline", "06 / Introduction headline")
        headline = self.title(
            "opening-headline", "One shoot.\nEvery deliverable.", copy_track,
            copy_left, copy_top + title_block_height / 2.0, title_size,
            "Anton", PAPER, -1.8, title_line_height,
        )
        headline_clip = headline
        self.frame("opening-headline-frame", headline_clip, opacity=0.0)
        self.animate(
            "opening-headline-opacity", headline_clip, "opacity",
            lambda t: out(t, 2.0, 3.1) * (1.0 - smooth(t, 8.0, 9.6)),
        )
        self.animate(
            "opening-headline-lift", headline_clip, "frameOffsetY",
            lambda t: 4.0 * 45.0 * (1.0 - out(t, 2.0, 3.1)) / height,
        )

        description_size = 39.0 if portrait else 38.0
        description_line_height = 1.4
        description_top = copy_top + title_block_height + 35.0
        description_height = description_size * description_line_height * 2
        description_track = self.track("track-description", "07 / Introduction detail")
        description = self.title(
            "opening-description",
            "From camera RAW to finished photos,\nlayered designs and films. In one app.",
            description_track,
            copy_left,
            description_top + description_height / 2.0,
            description_size,
            "Space Grotesk",
            PAPER,
            0.0,
            description_line_height,
        )
        description_clip = description
        self.frame("opening-description-frame", description_clip, opacity=0.0)
        self.animate(
            "opening-description-opacity", description_clip, "opacity",
            lambda t: out(t, 2.0, 3.1) * (1.0 - smooth(t, 8.0, 9.6)),
        )
        self.animate(
            "opening-description-lift", description_clip, "frameOffsetY",
            lambda t: 4.0 * 45.0 * (1.0 - out(t, 2.0, 3.1)) / height,
        )

        # CSS flex row: 13 px vertical/18 px horizontal padding, 1 px border,
        # 40 px radius, 12 px gap, and the source's exact entry/handoff ramps.
        pill_top = 1390.0 if portrait else 880.0
        pill_left = 65.0 if portrait else 95.0
        pill_font = 19.0
        pill_height = 50.0
        pill_widths = [
            len(label) * pill_font * 0.6 + 38.0
            for label in ("EDIT", "DESIGN", "CUT", "DELIVER")
        ]
        pill_x = pill_left
        for index, (label, pill_width) in enumerate(zip(
            ("EDIT", "DESIGN", "CUT", "DELIVER"), pill_widths
        )):
            pill_track = self.track(
                "track-pill-shape-" + label.lower(),
                "08 / Pill / " + label,
            )
            pill = self.graphic(
                "pill-shape-" + label.lower(), pill_track,
                "rounded-rectangle", pill_x, pill_top, pill_width, pill_height,
                "#bd0b0d12", opacity=0.0, radius=pill_height / 2.0,
                stroke="#55c4c8ce", stroke_width=1.0,
            )
            pill_clip = pill
            self.frame(
                "pill-shape-frame-" + label.lower(), pill_clip,
                opacity=0.0,
            )
            pill_text_track = self.track(
                "track-pill-text-" + label.lower(),
                "09 / Pill text / " + label,
            )
            pill_text = self.title(
                "pill-text-" + label.lower(), label, pill_text_track,
                pill_x + pill_width / 2.0, pill_top + pill_height / 2.0,
                pill_font, "Space Mono", LIME if index == 0 else PAPER,
                0.0, 1.0, "center",
            )
            pill_text_clip = pill_text
            self.frame("pill-text-frame-" + label.lower(), pill_text_clip, opacity=0.0)

            pill_alpha = lambda t: out(t, 5.0, 6.0) * (1.0 - smooth(t, 8.0, 9.6))
            self.animate("pill-shape-opacity-" + label.lower(), pill_clip,
                         "opacity", pill_alpha)
            self.animate("pill-text-opacity-" + label.lower(), pill_text_clip,
                         "opacity", pill_alpha)
            pill_x += pill_width + 12.0

        # The desktop card and text are children of BuyerOpening's full-frame
        # clipPath. Apply the same animated circle mask to each editable child.
        desktop_track = self.track("track-desktop-light", "10 / Desktop handoff light")
        desktop_light = self.add(
            "desktop-handoff-light",
            "createTimelineGenerator",
            trackId=desktop_track,
            generatorKind="desktop-light",
            startSeconds=0.0,
            durationSeconds=DURATION,
            parameters={"timeOffsetSeconds": 0.0, "timeScale": 1.0, "gather": 0.0},
        )
        desktop_light_clip = desktop_light
        self.handoff_opacity("desktop-handoff-light", desktop_light_clip, 0.24)
        self.frame("desktop-handoff-light-base", desktop_light_clip,
                   fit="stretch", opacity=0.0)

        card_x = 40.0 if portrait else 285.0
        card_y = 650.0 if portrait else 245.0
        card_width = 1000.0 if portrait else 1350.0
        card_height = 585.0 if portrait else 790.0
        transform_origin_y = card_y + card_height * 0.40
        start_scale = 0.92
        transform_offset_y = (transform_origin_y - height / 2.0) * (1.0 - start_scale)
        transform_offset_x = (card_x + card_width / 2.0 - width / 2.0) * (1.0 - start_scale)
        card_center_x = card_x + card_width / 2.0
        card_center_y = card_y + card_height / 2.0
        card_base_offset_x = 4.0 * (card_center_x / width - 0.5)
        card_base_offset_y = 4.0 * (card_center_y / height - 0.5)
        card_radius = 13.0

        card_track = self.track("track-desktop-card", "11 / Desktop card")
        card = self.graphic(
            "desktop-card-background", card_track, "rounded-rectangle",
            card_x, card_y, card_width, card_height, "#11151a",
            radius=card_radius,
        )
        card_clip = card
        self.handoff_opacity("desktop-card-background", card_clip)
        self.frame(
            "desktop-card-background-frame", card_clip,
            card_x, card_y, card_width, card_height,
            fit="stretch", opacity=0.0, scale=start_scale,
            offset_x_px=transform_offset_x, offset_y_px=transform_offset_y,
            corner_radius=card_radius / min(card_width, card_height),
        )
        self.animate(
            "desktop-card-scale", card_clip, "frameScale",
            lambda t: 0.92 + 0.08 * smooth(t, 8.0, 9.6),
        )
        self.animate(
            "desktop-card-origin-x", card_clip, "frameOffsetX",
            lambda t: card_base_offset_x +
            4.0 * (card_center_x - width / 2.0)
            * (1.0 - (0.92 + 0.08 * smooth(t, 8.0, 9.6))) / width,
        )
        self.animate(
            "desktop-card-origin-y", card_clip, "frameOffsetY",
            lambda t: card_base_offset_y +
            4.0 * (transform_origin_y - height / 2.0)
            * (1.0 - (0.92 + 0.08 * smooth(t, 8.0, 9.6))) / height,
        )

        ui_track = self.track("track-desktop-image", "12 / Desktop source image")
        inset = 1.0
        desktop_ui = self.image(
            "desktop-source-image", "desktop-launch/raw-editor.png", ui_track,
        )
        desktop_ui_clip = desktop_ui
        self.handoff_opacity("desktop-source-image", desktop_ui_clip)
        self.frame(
            "desktop-source-image-frame", desktop_ui_clip,
            card_x + inset, card_y + inset,
            card_width - inset * 2.0, card_height - inset * 2.0,
            fit="fit", opacity=0.0, scale=start_scale,
            offset_x_px=transform_offset_x, offset_y_px=transform_offset_y,
            corner_radius=card_radius / min(card_width, card_height),
        )
        self.animate(
            "desktop-image-scale", desktop_ui_clip, "frameScale",
            lambda t: 0.92 + 0.08 * smooth(t, 8.0, 9.6),
        )
        self.animate(
            "desktop-image-origin-x", desktop_ui_clip, "frameOffsetX",
            lambda t: card_base_offset_x +
            4.0 * (card_center_x - width / 2.0)
            * (1.0 - (0.92 + 0.08 * smooth(t, 8.0, 9.6))) / width,
        )
        self.animate(
            "desktop-image-origin-y", desktop_ui_clip, "frameOffsetY",
            lambda t: card_base_offset_y +
            4.0 * (transform_origin_y - height / 2.0)
            * (1.0 - (0.92 + 0.08 * smooth(t, 8.0, 9.6))) / height,
        )

        border_track = self.track("track-desktop-border", "13 / Desktop border")
        border = self.graphic(
            "desktop-card-border", border_track, "rounded-rectangle",
            card_x, card_y, card_width, card_height, "#00000000",
            radius=card_radius, stroke="#55788499", stroke_width=1.0,
        )
        border_clip = border
        self.handoff_opacity("desktop-card-border", border_clip)
        self.frame(
            "desktop-card-border-frame", border_clip,
            card_x, card_y, card_width, card_height,
            fit="stretch", opacity=0.0, scale=start_scale,
            offset_x_px=transform_offset_x, offset_y_px=transform_offset_y,
            corner_radius=card_radius / min(card_width, card_height),
        )
        self.animate(
            "desktop-border-scale", border_clip, "frameScale",
            lambda t: 0.92 + 0.08 * smooth(t, 8.0, 9.6),
        )
        self.animate(
            "desktop-border-origin-x", border_clip, "frameOffsetX",
            lambda t: card_base_offset_x +
            4.0 * (card_center_x - width / 2.0)
            * (1.0 - (0.92 + 0.08 * smooth(t, 8.0, 9.6))) / width,
        )
        self.animate(
            "desktop-border-origin-y", border_clip, "frameOffsetY",
            lambda t: card_base_offset_y +
            4.0 * (transform_origin_y - height / 2.0)
            * (1.0 - (0.92 + 0.08 * smooth(t, 8.0, 9.6))) / height,
        )

        desktop_heading_x = 65.0 if portrait else 85.0
        desktop_heading_top = 170.0 if portrait else 75.0
        desktop_label_height = 25.2
        desktop_title_size = 115.0 if portrait else 104.0
        desktop_title_top = desktop_heading_top + desktop_label_height + 22.0
        desktop_heading_track = self.track(
            "track-desktop-heading-label", "14 / Desktop label",
        )
        desktop_label = self.title(
            "desktop-handoff-label", "MEET LOOKSY", desktop_heading_track,
            desktop_heading_x,
            desktop_heading_top + desktop_label_height / 2.0,
            21.0,
            "Space Mono", LIME, 1.8, 1.0,
        )
        desktop_label_clip = desktop_label
        self.handoff_opacity("desktop-handoff-label", desktop_label_clip)

        desktop_title_track = self.track(
            "track-desktop-heading-title", "15 / Desktop headline",
        )
        desktop_title = self.title(
            "desktop-handoff-headline", "Your work. From start to finish.",
            desktop_title_track,
            desktop_heading_x,
            desktop_title_top + desktop_title_size * 1.01 / 2.0,
            desktop_title_size,
            "Anton", PAPER, -1.8, 1.01,
        )
        desktop_title_clip = desktop_title
        self.handoff_opacity("desktop-handoff-headline", desktop_title_clip)

        # Footer is outside the desktop reveal in the source JSX. Its geometry
        # stays fixed and remains visible while the opening hands off.
        footer_track = self.track("track-footer-left", "16 / Footer left")
        footer_left = 62.0 if portrait else 82.0
        footer_right = 62.0 if portrait else 82.0
        footer_bottom = 130.0 if portrait else 22.0
        footer_size = 20.0 if portrait else 17.0
        footer_line_height = footer_size * 1.2
        footer_y = height - footer_bottom - footer_line_height / 2.0
        self.title(
            "footer-left", "FOR PROFESSIONALS. FOR THE WORK YOU MAKE FOR YOURSELF.",
            footer_track, footer_left, footer_y, footer_size,
            "Space Mono", "#aeb6c3", 1.0, 1.2,
        )
        footer_right_track = self.track("track-footer-right", "17 / Footer right")
        self.title(
            "footer-right", "LOOKSY / DESKTOP", footer_right_track,
            width - footer_right, footer_y, footer_size,
            "Space Mono", "#aeb6c3", 1.0, 1.2, "right",
        )

        required_sources = {
            "creative-polish-v2/curve/curve-16.png",
            "desktop-launch/raw-editor.png",
            "looksy-logo-dark.png",
        }
        manifest_sources = SOURCE_MANIFEST.get("sources", [])
        source_assets = [
            {
                "path": row["path"],
                "bytes": row["bytes"],
                "sha256": row["sha256"],
            }
            for row in manifest_sources
            if any(row.get("path", "").endswith("/" + name) for name in required_sources)
        ]
        found_sources = {
            next(name for name in required_sources if row["path"].endswith("/" + name))
            for row in source_assets
        }
        if found_sources != required_sources:
            missing = sorted(required_sources - found_sources)
            raise RuntimeError("Missing canonical source-manifest entries: " + ", ".join(missing))

        return {
            "reference": "CreativeFilm / BuyerOpening",
            "sourceVideo": "landscape.mp4 or portrait.mp4",
            "chapter": [0.0, DURATION],
            "frameRate": FPS,
            "sequence": {"width": width, "height": height},
            "responsiveBranch": "portrait" if portrait else "landscape",
            "sourceAssets": source_assets,
            "steps": self.steps,
            "nestedMaskAudit": {
                "source": "BuyerOpening desktop overlay uses one parent circle clipPath at 58% 55%.",
                "nativeRepresentation": "The same editable animated ellipse mask is copied to each desktop light, card, screenshot, border, and heading clip.",
                "unresolved": "Native command plans do not expose a shared parent group mask. The duplicated masks can diverge if edited independently, and Qt edge antialiasing is not guaranteed pixel-identical to the browser clipPath.",
            },
            "unresolvedParity": [
                "The Native card CSS shadow (0 35px 100px #0009) has no equivalent parent-shadow primitive in the current typed command set; the plan preserves the card, border, and rounded viewport but omits the blur shadow.",
                "CSS and Qt text rasterization may differ slightly even with the source font families, sizes, line heights, and letter spacing.",
            ],
            "noFinishedVideoPlates": True,
        }


def build(width, height, format_name):
    return OpeningPlan(width, height, format_name).build()


if __name__ == "__main__":
    outputs = [
        (1920, 1080, "landscape", "opening-v2-landscape.commands.json"),
        (1080, 1920, "portrait", "opening-v2-portrait.commands.json"),
    ]
    for width, height, name, filename in outputs:
        path = ROOT / filename
        path.write_text(json.dumps(build(width, height, name), indent=2) + "\n")
        print(path, len(json.loads(path.read_text())["steps"]), "native commands")
