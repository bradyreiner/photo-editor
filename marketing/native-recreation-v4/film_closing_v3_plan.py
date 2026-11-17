"""Build compact editable native command plans for CreativeFilm Film/Closing.

The plans cover the source composition's global ranges 184–252 and 280–298.
They use the active CreativeFilm assets, native media clips, text, shapes and
procedural paths. Animated waveform bars are partitioned into exact groups so
each native MCP request stays below the HTTP body limit. Global TransitionVeil
clips are authored by the companion transition_veils_plan.py.
"""
from pathlib import Path
import json
import math
import re

from native_mcp import encode_request, validate_tool_request_size


ROOT = Path(__file__).resolve().parent
PUBLIC = ROOT.parent / "animated-trailer/public"
MEDIA = PUBLIC / "media"
SOURCE_MANIFEST = json.loads((ROOT / "source-manifest.json").read_text())
FPS = 30
INK = "#0b0d12"
PAPER = "#f1efdf"
LIME = "#d7ff48"
MUTED = "#aeb6c3"
LIGHT_MUTED = "#c9ced6"
WAVEFORM_BARS_PER_GENERATOR = 16
MCP_HTTP_BODY_LIMIT = 1024 * 1024


def clamp01(value):
    return max(0.0, min(1.0, value))


def smooth(t, start, end):
    u = clamp01((t - start) / (end - start))
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def out(t, start, end):
    u = clamp01((t - start) / (end - start))
    return 1.0 - (1.0 - u) ** 4


def motion_value(value):
    u = clamp01(value)
    return u * u * u * (10.0 + u * (-15.0 + 6.0 * u))


def stage_mapping(width, height):
    portrait = height > width
    return {
        "portrait": portrait,
        "x": 40.0 if portrait else 650.0,
        "y": 710.0 if portrait else 120.0,
        "scale": 1.0 if portrait else 1.1,
    }


def read_audio_peak_samples(source_key):
    data_text = (PUBLIC.parent / "src/creative-launch/audio-data.ts").read_text()
    marker = f'"{source_key}":['
    begin = data_text.index(marker) + len(marker)
    end = data_text.index("]", begin)
    return [float(value) for value in data_text[begin:end].split(",")]


def waveform_coordinates(samples, view_height, gain):
    coordinates = []
    for index, value in enumerate(samples):
        x = (index * 1000.0) / len(samples)
        y = view_height / 2.0 - min(view_height / 2.0 - 3.0,
                                   value * gain * view_height * 0.85)
        height = max(2.0, min(view_height - 6.0,
                              value * gain * view_height * 1.7))
        x2 = x + 3.0
        coordinates.extend([x, y, x2, y, x2, y + height, x, y + height])
    return coordinates


def waveform_commands(samples, view_height):
    commands = []
    for index, value in enumerate(samples):
        x = (index * 1000.0) / len(samples)
        y = view_height / 2.0 - min(view_height / 2.0 - 3.0,
                                    value * view_height * 0.85)
        height = max(2.0, min(view_height - 6.0,
                              value * view_height * 1.7))
        x2 = x + 3.0
        commands.extend([
            {"verb": "M", "points": [x, y]},
            {"verb": "L", "points": [x2, y]},
            {"verb": "L", "points": [x2, y + height]},
            {"verb": "L", "points": [x, y + height]},
            {"verb": "Z", "points": []},
        ])
    return commands


class FilmClosingPlan:
    def __init__(self, width, height, format_name):
        self.width = width
        self.height = height
        self.format_name = format_name
        self.portrait = height > width
        self.stage = stage_mapping(width, height)
        self.steps = []
        self.clip_starts = {}
        self.asset_paths = set()
        self.generated_track_index = 0

    def add(self, step_id, action, **values):
        if any(step["id"] == step_id for step in self.steps):
            raise ValueError("duplicate command id: " + step_id)
        self.steps.append({
            "id": step_id,
            "commandId": "timeline.action." + action,
            "values": values,
        })
        return {"result": step_id, "field": "createdClipId"}

    def track(self, step_id, name):
        self.add(step_id, "createTimelineTrack", kind="graphics", name=name,
                 audioRole="")
        return {"result": step_id, "field": "trackId"}

    def record_asset(self, source):
        path = (MEDIA / source).resolve()
        self.asset_paths.add(str(path))
        return path

    def frame(self, step_id, clip, x=0.0, y=0.0, width=None, height=None,
              fit="fill", opacity=1.0, scale=1.0, offset_x_px=0.0,
              offset_y_px=0.0, radius=0.0, rotation=0.0,
              perspective_x=0.0, perspective_y=0.0, crop=(0.0, 0.0, 0.0, 0.0)):
        width = self.width if width is None else width
        height = self.height if height is None else height
        center_x, center_y = x + width / 2.0, y + height / 2.0
        return self.add(
            step_id, "setTimelineClipFrame", clipId=clip,
            offsetX=4.0 * ((center_x / self.width) - 0.5)
                    + 4.0 * offset_x_px / self.width,
            offsetY=4.0 * ((center_y / self.height) - 0.5)
                    + 4.0 * offset_y_px / self.height,
            scale=scale, rotationDegrees=rotation,
            perspectiveX=perspective_x, perspectiveY=perspective_y,
            opacity=opacity, objectFit=fit, width=width / self.width,
            height=height / self.height,
            cornerRadius=min(0.5, radius / min(width, height)) if radius else 0.0,
            cropLeft=crop[0], cropTop=crop[1],
            cropRight=crop[2], cropBottom=crop[3],
        )

    def shape(self, step_id, source_track, shape_type, x, y, width, height,
              start, duration, color, stroke="#00000000", stroke_width=0.0,
              radius=0.0, opacity=1.0, rotation=0.0):
        clip = self.add(
            step_id, "createTimelineGraphic", trackId=source_track,
            shapeType=shape_type, startSeconds=start, durationSeconds=duration,
            values={
                "positionX": (x + width / 2.0) / self.width,
                "positionY": (y + height / 2.0) / self.height,
                "shapeWidth": width / self.width,
                "shapeHeight": height / self.height,
                "shapeCornerRadius": min(0.5, radius / max(1.0, min(width, height))),
                "backgroundColor": color,
                "shapeStrokeColor": stroke,
                "shapeStrokeWidth": stroke_width / min(self.width, self.height),
            },
        )
        self.clip_starts[step_id] = start
        self.frame(step_id + "-frame", clip, x, y, width, height,
                   fit="stretch", opacity=opacity, radius=radius,
                   rotation=rotation)
        return clip

    def title(self, step_id, text, source_track, x, y, size, start, duration,
              family="Anton", color=PAPER, spacing=-1.8, line_height=1.01,
              alignment="left", italic=False, weight=400, opacity=1.0):
        lines = text.count("\n") + 1
        height = size * line_height * lines
        clip = self.add(
            step_id, "createTimelineTitle", trackId=source_track,
            templateKey="headline", startSeconds=start,
            durationSeconds=duration,
            values={
                "text": text, "fontFamily": family, "fontSize": size,
                "fontWeight": weight, "italic": italic,
                "letterSpacing": spacing, "lineHeight": line_height,
                "color": color, "backgroundColor": "#00000000",
                "alignment": alignment,
                "positionX": x / self.width,
                "positionY": (y + height / 2.0) / self.height,
            },
        )
        self.clip_starts[step_id] = start
        self.frame(step_id + "-frame", clip, opacity=opacity)
        return clip

    def image(self, step_id, source, source_track, start, duration):
        path = self.record_asset(source)
        clip = self.add(
            step_id, "createTimelineClip", trackId=source_track,
            assetId="native-trailer:" + source, filePath=str(path),
            displayLabel=step_id, durationSeconds=duration,
            trimStartSeconds=0.0, trimEndSeconds=duration,
            startSeconds=start, posterSource="",
        )
        self.clip_starts[step_id] = start
        return clip

    def video(self, step_id, source, source_track, start, duration,
              source_duration, trim_start=0.0, poster=""):
        path = self.record_asset(source)
        poster_path = str(self.record_asset(poster)) if poster else ""
        clip = self.add(
            step_id, "createTimelineClip", trackId=source_track,
            assetId="native-trailer:" + source, filePath=str(path),
            displayLabel=step_id, durationSeconds=source_duration,
            trimStartSeconds=trim_start,
            trimEndSeconds=trim_start + duration,
            startSeconds=start, posterSource=poster_path,
        )
        self.clip_starts[step_id] = start
        return clip

    def generator(self, step_id, source_track, kind, start, duration,
                  parameters, opacity=1.0):
        clip = self.add(
            step_id, "createTimelineGenerator", trackId=source_track,
            generatorKind=kind, startSeconds=start, durationSeconds=duration,
            parameters={**parameters, "opacity": opacity},
        )
        self.clip_starts[step_id] = start
        return clip

    def animate(self, step_id, clip, channels, start, end):
        clip_id = clip["result"]
        clip_start = self.clip_starts[clip_id]
        first = max(0, round((start - clip_start) * FPS))
        last = max(first, round((end - clip_start) * FPS))
        seconds = [frame / FPS for frame in range(first, last + 1)]
        normalized = []
        for parameter, fn in channels.items():
            values = [round(float(fn(clip_start + t)), 9) for t in seconds]
            selected = []
            for i, value in enumerate(values):
                if (i == 0 or i == len(values) - 1 or
                    values[i - 1] != value or values[i + 1] != value):
                    selected.append({
                        "localSeconds": seconds[i], "value": value,
                        "interpolation": "linear",
                    })
            normalized.append({"parameter": parameter, "keyframes": selected})
        self.add(step_id, "setTimelineClipAnimation", clipId=clip,
                 channels=normalized)

    def stage_box(self, x, y, width, height):
        scale = self.stage["scale"]
        return (self.stage["x"] + x * scale,
                self.stage["y"] + y * scale,
                width * scale, height * scale)

    def stage_title(self, step_id, text, source_track, x, y, size, start,
                    duration, **style):
        sx, sy, _, _ = self.stage_box(x, y, 0.0, 0.0)
        scale = self.stage["scale"]
        return self.title(step_id, text, source_track, sx, sy, size * scale,
                          start, duration, **style)

    def stage_shape(self, step_id, source_track, shape_type, x, y, width,
                    height, start, duration, color, **style):
        sx, sy, sw, sh = self.stage_box(x, y, width, height)
        scale = self.stage["scale"]
        return self.shape(step_id, source_track, shape_type, sx, sy, sw, sh,
                          start, duration, color,
                          stroke_width=style.get("stroke_width", 0.0) * scale,
                          stroke=style.get("stroke", "#00000000"),
                          radius=style.get("radius", 0.0) * scale,
                          opacity=style.get("opacity", 1.0),
                          rotation=style.get("rotation", 0.0))

    def stage_video(self, step_id, source, source_track, start, duration,
                    source_duration, x, y, width, height, trim_start=0.0,
                    radius=0.0, poster="", fit="fill", opacity=1.0,
                    rotation=0.0):
        clip = self.video(step_id, source, source_track, start, duration,
                          source_duration, trim_start, poster)
        sx, sy, sw, sh = self.stage_box(x, y, width, height)
        self.frame(step_id + "-frame", clip, sx, sy, sw, sh, fit=fit,
                   opacity=opacity, radius=radius * self.stage["scale"],
                   rotation=rotation)
        return clip

    def stage_image(self, step_id, source, source_track, start, duration,
                    x, y, width, height, radius=0.0, fit="fill", opacity=1.0):
        clip = self.image(step_id, source, source_track, start, duration)
        sx, sy, sw, sh = self.stage_box(x, y, width, height)
        self.frame(step_id + "-frame", clip, sx, sy, sw, sh, fit=fit,
                   opacity=opacity, radius=radius * self.stage["scale"])
        return clip

    def stage_generator(self, step_id, source_track, kind, start, duration,
                        params, x, y, width, height, opacity=1.0):
        clip = self.generator(step_id, source_track, kind, start, duration,
                              params, opacity=opacity)
        sx, sy, sw, sh = self.stage_box(x, y, width, height)
        self.frame(step_id + "-frame", clip, sx, sy, sw, sh,
                   fit="stretch", opacity=opacity)
        return clip

    def vector_path(self, step_id, source_track, start, duration, x, y,
                    width, height, view_width, view_height, commands,
                    stroke, stroke_width, fill="#00000000", frames=None,
                    opacity=1.0):
        params = {
            "vectorPath": {
                "viewBoxWidth": view_width,
                "viewBoxHeight": view_height,
                "commands": commands,
                "stroke": stroke,
                "fill": fill,
                "strokeWidth": stroke_width,
            },
        }
        if frames:
            params["vectorPath"]["frames"] = frames
        clip = self.generator(step_id, source_track, "vector-path", start,
                              duration, params, opacity=opacity)
        self.frame(step_id + "-frame", clip, x, y, width, height,
                   fit="stretch", opacity=opacity)
        return clip

    def gradient(self, step_id, source_track, start, duration, angle, stops):
        clip = self.generator(
            step_id, source_track, "linear-gradient", start, duration,
            {"angleDegrees": angle, "stops": stops},
        )
        self.frame(step_id + "-frame", clip, fit="stretch")
        return clip

    def add_ink(self, name, start, duration):
        track = self.track(name + "-ink-track", name + " / ink")
        self.shape(name + "-ink", track, "rectangle", 0, 0,
                   self.width, self.height, start, duration, INK)

    def source_assets(self):
        manifest_by_path = {row["path"]: row for row in SOURCE_MANIFEST["sources"]}
        required = sorted(self.asset_paths)
        missing = [path for path in required if path not in manifest_by_path]
        if missing:
            raise RuntimeError("Missing exact source-manifest entries: " + ", ".join(missing))
        return [
            {"path": path, "bytes": manifest_by_path[path]["bytes"],
             "sha256": manifest_by_path[path]["sha256"]}
            for path in required
        ]

    def build(self):
        self.film_chapter()
        self.closing_chapter()
        self.validate_waveform_partitions()
        request_audit = self.audit_native_requests()
        return {
            "reference": "CreativeFilm / FilmChapter + BuyerClosing",
            "sourceVideo": "landscape.mp4 or portrait.mp4",
            "chapterRanges": [[184.0, 252.0], [280.0, 298.0]],
            "frameRate": FPS,
            "planVersion": 3,
            "sequence": {"width": self.width, "height": self.height},
            "responsiveBranch": "portrait" if self.portrait else "landscape",
            "nativeRequestAudit": request_audit,
            "waveformPartition": {
                "barsPerGenerator": WAVEFORM_BARS_PER_GENERATOR,
                "waveformClipCount": sum(
                    step["commandId"] == "timeline.action.createTimelineGenerator" and
                    (step["id"] == "synced-timeline-music-wave" or
                     bool(re.fullmatch(r"audio-mixer-waveform-[01]-chunk-\d{2}", step["id"])))
                    for step in self.steps
                ),
                "staticFrameSamplesOmitted": True,
                "geometryPreservation": "Every M/L/L/L/Z bar subpath and every per-frame coordinate is retained exactly once.",
            },
            "sourceAssets": self.source_assets(),
            "requiredCompanionPlans": [
                "global-transition-veils-v2-" + self.format_name + ".commands.json",
                "global-audio.commands.json",
            ],
            "steps": self.steps,
            "unsupportedNativePrimitives": [
                "Program Monitor reverse scrubbing and the same-frame dynamic clock, trim readout, and ripple-duration readout are data-bound to FilmPolish state; the plan keeps the footage and editable timeline layers but cannot reproduce that live state as read-only text.",
                "The original 3D perspective transform and CSS box shadows do not have equivalent native group effects; rounded viewports and 2D rotations are represented where supported.",
                "SyncedTimeline uses nested overflow containers and a shared UI image group. Native commands have per-clip masks, so screenshot crops and their borders must be adjusted separately.",
                "BuyerClosing PaperGrain is a Remotion procedural overlay with no source-aligned native noise generator; it is omitted rather than replaced with a different texture.",
                "CSS flex sizing, browser font rasterization, and live HTML text wrapping can differ slightly from the native text and shape layout.",
            ],
            "noFinishedVideoPlates": True,
        }

    def audit_native_requests(self, steps=None):
        steps = self.steps if steps is None else steps
        rows = []
        for step in steps:
            arguments = {"commandId": step["commandId"], "values": step["values"]}
            measured = validate_tool_request_size("app.invoke_command", arguments)
            encoded = encode_request(
                "tools/call",
                {"name": "app.invoke_command", "arguments": arguments},
            )
            if measured != len(encoded):
                raise AssertionError(
                    f"MCP request-size helper disagrees for {step['id']}: "
                    f"{measured} vs {len(encoded)} bytes"
                )
            if measured > MCP_HTTP_BODY_LIMIT:
                raise AssertionError(
                    f"MCP request exceeds HTTP limit for {step['id']}: {measured} bytes"
                )
            rows.append((measured, step["id"]))
        largest_bytes, largest_step = max(rows, default=(0, ""))
        return {
            "transport": "native_mcp.py full JSON-RPC HTTP body",
            "limitBytes": MCP_HTTP_BODY_LIMIT,
            "checkedIndividualCommandCount": len(rows),
            "maxRequestBytes": largest_bytes,
            "maxRequestStepId": largest_step,
            "allRequestsWithinLimit": largest_bytes <= MCP_HTTP_BODY_LIMIT,
        }

    def validate_waveform_partitions(self, steps=None):
        """Recombine emitted chunks against the preserved v2 source geometry."""
        steps = self.steps if steps is None else steps
        legacy_path = ROOT / f"film-closing-v2-{self.format_name}.commands.json"
        legacy_plan = json.loads(legacy_path.read_text())
        legacy_waveforms = {
            step["id"]: step["values"]["parameters"]["vectorPath"]
            for step in legacy_plan["steps"]
            if step["commandId"] == "timeline.action.createTimelineGenerator" and
            step["id"] in ("synced-timeline-music-wave",
                           "audio-mixer-waveform-0",
                           "audio-mixer-waveform-1")
        }
        static_steps = [step for step in steps
                        if step["id"] == "synced-timeline-music-wave"]
        if len(static_steps) != 1:
            raise AssertionError("Expected one static synced-timeline waveform")
        static_path = static_steps[0]["values"]["parameters"]["vectorPath"]
        if "frames" in static_path:
            raise AssertionError("Static synced-timeline waveform still stores frames")
        legacy_static = legacy_waveforms["synced-timeline-music-wave"]
        if static_path["commands"] != legacy_static["commands"]:
            raise AssertionError("Static waveform commands changed from the v2 plan")
        legacy_static_frames = legacy_static.get("frames", [])
        if len(legacy_static_frames) != round(12.0 * FPS) + 1:
            raise AssertionError("Preserved static v2 waveform sample count changed")
        if any(frame["coordinates"] != legacy_static_frames[0]["coordinates"]
               for frame in legacy_static_frames[1:]):
            raise AssertionError("v2 synced timeline waveform frames are not identical")
        if waveform_commands(read_audio_peak_samples("music"), 48.0) != static_path["commands"]:
            raise AssertionError("Static waveform commands no longer match source peaks")
        if waveform_coordinates(read_audio_peak_samples("music"), 48.0, 1.0) != \
                legacy_static_frames[0]["coordinates"]:
            raise AssertionError("Static source waveform differs from preserved v2 frame")

        groups = {}
        for step in steps:
            step_id = step["id"]
            match = re.fullmatch(r"(audio-mixer-waveform-[01])-chunk-(\d{2})", step_id)
            if not match or step["commandId"] != "timeline.action.createTimelineGenerator":
                continue
            groups.setdefault(match.group(1), []).append(
                (int(match.group(2)), step)
            )

        if set(groups) != {"audio-mixer-waveform-0", "audio-mixer-waveform-1"}:
            raise AssertionError("Both animated audio-mixer waveforms must be partitioned")

        source_keys = {"audio-mixer-waveform-0": "music",
                       "audio-mixer-waveform-1": "location"}
        duration = 6.0
        frame_count = round(duration * FPS)
        steps_by_id = {step["id"]: step for step in steps}
        for group_name, source_key in source_keys.items():
            samples = read_audio_peak_samples(source_key)
            gain_fn = (lambda local: 10.0 ** ((-12.0 * self.mixer_mix(local)) / 20.0)
                       if source_key == "music" else self.mixer_mix(local))
            chunks = sorted(groups[group_name])
            expected_chunk_count = math.ceil(len(samples) / WAVEFORM_BARS_PER_GENERATOR)
            if len(chunks) != expected_chunk_count:
                raise AssertionError(
                    f"{group_name} has {len(chunks)} chunks, expected {expected_chunk_count}"
                )

            recombined_commands = []
            chunk_frames_by_index = []
            track_refs = set()
            common_generator_values = None
            common_frame_values = None
            for chunk_index, generator_step in chunks:
                if chunk_index != len(chunk_frames_by_index):
                    raise AssertionError(f"{group_name} chunk indices are not contiguous")
                chunk_id = f"{group_name}-chunk-{chunk_index:02d}"
                generator_values = generator_step["values"]
                params = generator_values["parameters"]["vectorPath"]
                frames = params["frames"]
                track_ref = generator_values["trackId"]
                if track_ref.get("result") != chunk_id + "-track":
                    raise AssertionError(f"{chunk_id} does not use its own track")
                if track_ref["result"] in track_refs:
                    raise AssertionError(f"{group_name} chunks share a track")
                track_refs.add(track_ref["result"])
                if generator_values["generatorKind"] != "vector-path":
                    raise AssertionError(f"{chunk_id} is not a vector-path generator")
                if generator_values["durationSeconds"] != duration:
                    raise AssertionError(f"{chunk_id} duration changed")
                if generator_values["startSeconds"] != (226.0):
                    raise AssertionError(f"{chunk_id} start time changed")
                generator_common = {
                    key: params[key]
                    for key in ("viewBoxWidth", "viewBoxHeight", "stroke", "fill", "strokeWidth")
                }
                if common_generator_values is None:
                    common_generator_values = generator_common
                elif generator_common != common_generator_values:
                    raise AssertionError(f"{chunk_id} generator style/viewbox changed")

                frame_step = steps_by_id.get(chunk_id + "-frame")
                if frame_step is None or frame_step["commandId"] != "timeline.action.setTimelineClipFrame":
                    raise AssertionError(f"{chunk_id} has no native geometry frame")
                frame_values = frame_step["values"]
                if frame_values["clipId"].get("result") != chunk_id:
                    raise AssertionError(f"{chunk_id} frame action references the wrong clip")
                geometry_values = {key: value for key, value in frame_values.items()
                                   if key != "clipId"}
                if common_frame_values is None:
                    common_frame_values = geometry_values
                elif geometry_values != common_frame_values:
                    raise AssertionError(f"{chunk_id} viewport geometry differs across chunks")
                if len(frames) != frame_count + 1:
                    raise AssertionError(
                        f"{group_name} chunk {chunk_index} has {len(frames)} frames"
                    )
                if any(len(frame["coordinates"]) > 24576 for frame in frames):
                    raise AssertionError("A vector frame exceeds native coordinate limits")
                commands = params["commands"]
                if len(commands) % 5 != 0:
                    raise AssertionError(f"{chunk_id} splits a closed rectangle subpath")
                if any(len(frame["coordinates"]) != (len(commands) // 5) * 8
                       for frame in frames):
                    raise AssertionError(f"{chunk_id} frame coordinates do not align to bars")
                recombined_commands.extend(commands)
                chunk_frames_by_index.append(frames)

            expected_commands = waveform_commands(samples, 64.0)
            legacy = legacy_waveforms[group_name]
            if recombined_commands != legacy["commands"] or recombined_commands != expected_commands:
                raise AssertionError(f"{group_name} commands changed from v2/source geometry")
            legacy_frames = legacy.get("frames", [])
            if len(legacy_frames) != frame_count + 1:
                raise AssertionError(f"{group_name} v2 reference frame count changed")

            for frame_index in range(frame_count + 1):
                local = frame_index / FPS
                source_coordinates = waveform_coordinates(
                    samples, 64.0, float(gain_fn(local))
                )
                reference_frame = legacy_frames[frame_index]
                if reference_frame["seconds"] != local:
                    raise AssertionError(f"{group_name} v2 frame time is not source-local")
                if reference_frame["coordinates"] != source_coordinates:
                    raise AssertionError(f"{group_name} v2 waveform no longer matches source formula")
                recombined_coordinates = []
                expected_seconds = frame_index / FPS
                for frames in chunk_frames_by_index:
                    frame = frames[frame_index]
                    if frame["seconds"] != expected_seconds:
                        raise AssertionError(f"{group_name} chunk frame time changed")
                    recombined_coordinates.extend(frame["coordinates"])
                if recombined_coordinates != reference_frame["coordinates"]:
                    raise AssertionError(
                        f"{group_name} coordinates differ at frame {frame_index}"
                    )

    def film_chapter(self):
        self.add_ink("film", 184.0, 68.0)

        # FilmPolish returns its full-frame EditPreview before drawing its Light
        # and footer. The procedural light starts at global 196, pauses for the
        # dedicated DeliveryProof scene, and resumes at global 240.
        film_light_track = self.track("film-light-track", "Film / additive RGB light")
        film_light = self.generator(
            "film-light-first", film_light_track, "desktop-light", 196.0, 36.0,
            {"timeOffsetSeconds": 196.0, "timeScale": 1.0, "gather": 0.0},
            opacity=0.28,
        )
        self.frame("film-light-first-frame", film_light, fit="stretch", opacity=0.28)
        self.generator(
            "film-light-last", film_light_track, "desktop-light", 240.0, 12.0,
            {"timeOffsetSeconds": 240.0, "timeScale": 1.0, "gather": 0.0},
            opacity=0.28,
        )
        last_light = {"result": "film-light-last", "field": "createdClipId"}
        self.frame("film-light-last-frame", last_light, fit="stretch", opacity=0.28)

        # Phase 0 (184–196): native source cuts, top/bottom ink strips, and
        # scene-specific FilmPolish lower thirds.
        shots = [
            ("arrival", "creative-polish-v2/arrival-12.mp4", 184.0, 3.7,
             12.0, 1.0, "27° N / A FIELD FILM", "After the ridgeline.",
             "studio/arrival-first.jpg"),
            ("ridge", "MVI_5051-2.3-5.9.mp4", 187.7, 3.2,
             3.6036, 0.0, "ABOVE THE EVERYDAY", "A little further.",
             "studio/ridge-thumb.jpg"),
            ("city", "creative-polish-v2/city-10.mp4", 190.9, 2.7,
             10.0, 0.0, "KATHMANDU / AFTERNOON", "Down another street.",
             "studio/city-delivery.jpg"),
            ("night", "refine/night.mp4", 193.6, 2.4,
             5.0, 2.6, "27° N / A FIELD FILM", "After the ridgeline.",
             "studio/night-thumb.jpg"),
        ]
        video_track = self.track("film-intro-video-track", "Film / full-frame source cuts")
        for shot, source, start, length, source_duration, trim, label, heading, poster in shots:
            clip = self.video("film-intro-video-" + shot, source,
                              video_track, start, length, source_duration,
                              trim_start=trim, poster=poster)
            self.frame("film-intro-video-" + shot + "-frame", clip, fit="fill")

        strips_track = self.track("film-intro-edge-strips-track", "Film / EditPreview edge strips")
        self.gradient("film-intro-edge-strips", strips_track, 184.0, 12.0, 180.0, [
            {"position": 0.0, "color": "#080c13", "opacity": 1.0},
            {"position": 0.05, "color": "#080c13", "opacity": 1.0},
            {"position": 0.051, "color": "#080c13", "opacity": 0.0},
            {"position": 0.949, "color": "#080c13", "opacity": 0.0},
            {"position": 0.95, "color": "#080c13", "opacity": 1.0},
            {"position": 1.0, "color": "#080c13", "opacity": 1.0},
        ])

        intro_x = 80.0 if self.portrait else 100.0
        intro_y = 160.0 if self.portrait else 75.0
        intro_size = 37.0 if self.portrait else 32.0
        intro_label_track = self.track("film-intro-label-track", "Film / opening caption label")
        self.title("film-intro-label", "05 / MAKE THE WHOLE FILM.", intro_label_track,
                   intro_x, intro_y, 21.0, 184.0, 12.0,
                   family="Space Mono", color=PAPER, spacing=1.8)
        intro_detail_track = self.track("film-intro-detail-track", "Film / opening caption detail")
        intro_detail = self.title(
            "film-intro-detail", "Footage. Titles. Color. Sound. One finished edit.",
            intro_detail_track, intro_x, intro_y + 45.0, intro_size, 184.0, 12.0,
            family="Space Grotesk", color=PAPER, spacing=0.0,
        )
        self.animate("film-intro-detail-fade", intro_detail,
                     {"opacity": lambda at: 1.0 - smooth(at - 184.0, 3.4, 4.5)},
                     184.0, 196.0)

        lower_label_track = self.track("film-intro-lower-label-track", "Film / scene lower labels")
        lower_title_track = self.track("film-intro-lower-title-track", "Film / scene lower titles")
        for shot, _, start, length, _, _, label, heading, _ in shots:
            title_bottom = 230.0 if self.portrait else 35.0
            small_size = 16.0 if self.portrait else 20.8
            large_size = 100.0 if self.portrait else 130.0
            small_y = (self.height - title_bottom - small_size * 1.2 - 8.0 -
                       large_size * 1.01)
            large_y = small_y + small_size * 1.2 + 8.0
            self.title("film-intro-lower-label-" + shot, label, lower_label_track,
                       40.0, small_y, small_size, start, length,
                       family="Space Mono", color=PAPER, spacing=1.0)
            self.title("film-intro-lower-title-" + shot, heading, lower_title_track,
                       40.0, large_y, large_size, start, length,
                       family="Instrument Serif", color=PAPER, spacing=-1.0,
                       italic=True)

        # Phases 1–4, 6–7 share Caption + Stage. Landscape and portrait use
        # independent source coordinates rather than a letterboxed master.
        self.timeline_scene(shots)
        self.keyframe_scene()
        self.grade_scene()
        self.audio_scene()
        self.delivery_scene()
        self.gaussian_scene()
        self.framing_scene()

    def caption(self, scene, phase_start, phase_duration, title_text,
                detail_text, serif=False, enter_offset=0.0):
        x = 62.0 if self.portrait else 82.0
        top = 160.0 if self.portrait else 254.0
        width = 950.0 if self.portrait else 470.0
        label_height = 25.0
        title_size = 108.0 if self.portrait else 91.0
        title_y = top + label_height + 24.0
        title_height = title_size * 1.01 * (title_text.count("\n") + 1)
        detail_y = title_y + title_height + 28.0
        detail_size = 36.0 if self.portrait else 33.0
        detail_line = 1.4
        label_track = self.track(scene + "-caption-label-track", scene + " / caption label")
        label = self.title(scene + "-caption-label", "05 / VIDEO TIMELINE",
                           label_track, x, top, 21.0, phase_start,
                           phase_duration, family="Space Mono", color=LIME,
                           spacing=1.8, opacity=0.0)
        title_track = self.track(scene + "-caption-title-track", scene + " / caption heading")
        heading = self.title(scene + "-caption-title", title_text,
                             title_track, x, title_y, title_size,
                             phase_start, phase_duration,
                             family="Instrument Serif" if serif else "Anton",
                             color=PAPER, spacing=-1.8, line_height=1.01,
                             italic=serif, opacity=0.0)
        detail_track = self.track(scene + "-caption-detail-track", scene + " / caption detail")
        detail_breaks = {
            "Layer footage, titles and audio. Trim a clip; ripple the following shots into place.": (
                "Layer footage, titles and\naudio. Trim a clip; ripple the\nfollowing shots into place.",
                "Layer footage, titles and audio.\nTrim a clip; ripple the following shots into place.",
            ),
            "Position, scale, opacity and easing. The animation follows your keyframes.": (
                "Position, scale, opacity and\neasing. The animation follows\nyour keyframes.",
                "Position, scale, opacity and easing.\nThe animation follows your keyframes.",
            ),
            "A native color grade and a monochrome effect. The same footage, visibly transformed.": (
                "A native color grade and a\nmonochrome effect. The same\nfootage, visibly transformed.",
                "A native color grade and a monochrome effect.\nThe same footage, visibly transformed.",
            ),
            "Pull down the music and hear the location. Shape the mix alongside the edit.": (
                "Pull down the music and hear\nthe location. Shape the mix\nalongside the edit.",
                "Pull down the music and hear the location.\nShape the mix alongside the edit.",
            ),
            "Gaussian scenes with a moving camera. Another dimension in the creative toolbox.": (
                "Gaussian scenes with a moving\ncamera. Another dimension in\nthe creative toolbox.",
                "Gaussian scenes with a moving camera.\nAnother dimension in the creative toolbox.",
            ),
            "Landscape productions and independently framed vertical videos.": (
                "Landscape productions and\nindependently framed vertical videos.",
                "Landscape productions and independently\nframed vertical videos.",
            ),
        }
        if detail_text in detail_breaks:
            detail_text = detail_breaks[detail_text][1 if self.portrait else 0]
        detail = self.title(scene + "-caption-detail", detail_text,
                            detail_track, x, detail_y, detail_size,
                            phase_start, phase_duration,
                            family="Space Grotesk", color=LIGHT_MUTED,
                            spacing=0.0, line_height=detail_line,
                            opacity=0.0)
        enter_start = phase_start + enter_offset
        self.caption_enter(scene + "-caption-label-enter", label,
                           enter_start, 0.0, 0.55, top)
        self.caption_enter(scene + "-caption-title-enter", heading,
                           enter_start, 0.05, 0.60, title_y)
        self.caption_enter(scene + "-caption-detail-enter", detail,
                           enter_start, 0.2, 0.75, detail_y)
        self.caption_lift(scene + "-caption-label-lift", label,
                          enter_start, 0.0, 0.75)
        self.caption_lift(scene + "-caption-title-lift", heading,
                          enter_start, 0.05, 0.80)
        self.caption_lift(scene + "-caption-detail-lift", detail,
                          enter_start, 0.2, 0.95)

    def caption_enter(self, step_id, clip, anchor, delay, duration, _y):
        self.animate(step_id, clip,
                     {"opacity": lambda at: out(at - anchor, delay, delay + duration)},
                     anchor + delay, anchor + delay + duration)

    def caption_lift(self, step_id, clip, anchor, delay, duration):
        self.animate(step_id, clip,
                     {"frameOffsetY": lambda at: 4.0 * 45.0 *
                      (1.0 - out(at - anchor, delay, delay + duration)) / self.height},
                     anchor + delay, anchor + delay + duration)

    def stage_caption(self, scene, phase_start, phase_duration, headline, detail,
                      serif=False, enter_offset=0.0):
        # The authored stage and caption have independent coordinate systems.
        self.caption(scene, phase_start, phase_duration, headline, detail,
                     serif=serif, enter_offset=enter_offset)

    def common_footer(self, scene, start, duration, left="FROM FIRST CUT TO FINAL FRAME.",
                      right="COLOR · MOTION · LAYERS · AUDIO"):
        x = 62.0 if self.portrait else 82.0
        margin_right = x
        bottom = 130.0 if self.portrait else 22.0
        size = 20.0 if self.portrait else 17.0
        y = self.height - bottom - size * 1.2 / 2.0
        left_track = self.track(scene + "-footer-left-track", scene + " / footer left")
        self.title(scene + "-footer-left", left, left_track, x, y, size,
                   start, duration, family="Space Mono", color=MUTED,
                   spacing=1.0, line_height=1.2)
        right_track = self.track(scene + "-footer-right-track", scene + " / footer right")
        self.title(scene + "-footer-right", right, right_track,
                   self.width - margin_right, y, size, start, duration,
                   family="Space Mono", color=MUTED, spacing=1.0,
                   line_height=1.2, alignment="right")

    def stage_graphic(self, name, shape_type, x, y, width, height, start,
                      duration, color, **style):
        track = self.track(name + "-track", name)
        return self.stage_shape(name, track, shape_type, x, y, width, height,
                                start, duration, color, **style)

    def stage_media_track(self, name):
        self.generated_track_index += 1
        slug = re.sub(r"[^a-z0-9-]+", "-", name.lower()).strip("-")
        return self.track(f"generated-track-{self.generated_track_index}-{slug}", name)

    def timeline_scene(self, shots):
        start, duration = 196.0, 12.0
        self.caption("timeline", start, duration,
                     "Build the edit.\nKeep it moving.",
                     "Layer footage, titles and audio. Trim a clip; ripple the following shots into place.",
                     enter_offset=1.0)

        # SyncedTimeline's real 1000 x 800 design stage is repositioned below
        # the caption in portrait; landscape keeps its authored 650,120 origin.
        self.stage_graphic("synced-timeline-shell", "rounded-rectangle",
                           0, 0, 1000, 800, start, duration,
                           "#171d24", stroke="#66768488", stroke_width=1.0,
                           radius=13.0)
        top_strip_track = self.stage_media_track("SyncedTimeline / top screenshot strip")
        top = self.stage_image("synced-timeline-top-strip",
                               "creative-polish-v2/timeline-native.png",
                               top_strip_track, start, duration, 0, 0, 1000, 42,
                               fit="fill")
        sx, sy, sw, sh = self.stage_box(0, 0, 1000, 42)
        self.frame("synced-timeline-top-strip-crop", top, sx, sy, sw, sh,
                   fit="stretch", crop=(0.0, 25.0 / 585.0, 1.0, 67.0 / 585.0))
        bottom_track = self.stage_media_track("SyncedTimeline / bottom screenshot strip")
        bottom = self.stage_image("synced-timeline-bottom-strip",
                                  "creative-polish-v2/timeline-native.png",
                                  bottom_track, start, duration, 0, 520, 1000, 42,
                                  fit="fill")
        sx, sy, sw, sh = self.stage_box(0, 520, 1000, 42)
        self.frame("synced-timeline-bottom-strip-crop", bottom, sx, sy, sw, sh,
                   fit="stretch", crop=(0.0, 389.0 / 585.0, 1.0, 431.0 / 585.0))

        header_title_track = self.stage_media_track("SyncedTimeline / program title")
        self.stage_title("synced-timeline-program-title", "AFTER THE RIDGELINE",
                         header_title_track, 22, 60, 21, start, duration,
                         family="Space Grotesk", color=PAPER, spacing=0.0)
        clock_track = self.stage_media_track("SyncedTimeline / program clock")
        self.stage_title("synced-timeline-program-clock", "PROGRAM / 00:00:11:29",
                         clock_track, 750, 60, 13, start, duration,
                         family="Space Mono", color="#9aafbe", spacing=0.0,
                         alignment="right")

        self.stage_graphic("synced-program-viewer-backdrop", "rounded-rectangle",
                           25, 105, 700, 394, start, duration,
                           "#090e15", radius=7.0)
        preview_track = self.stage_media_track("SyncedTimeline / moving Program sources")
        clip_starts = [196.0, 199.7, 202.9, 205.6]
        for i, (shot, source, _, length, source_duration, trim, label, title, poster) in enumerate(shots):
            clip = self.stage_video("synced-program-" + shot, source,
                                    preview_track, clip_starts[i], length,
                                    source_duration, 25, 105, 700, 394,
                                    trim_start=trim, radius=7.0, poster=poster)
            lower_label_track = self.stage_media_track("SyncedTimeline / preview lower label / " + shot)
            small = self.stage_title("synced-program-label-" + shot, label,
                                     lower_label_track, 40, 105 + 394 - 35 - 60,
                                     9.44, clip_starts[i], length,
                                     family="Space Mono", color=PAPER,
                                     spacing=1.1)
            lower_title_track = self.stage_media_track("SyncedTimeline / preview heading / " + shot)
            self.stage_title("synced-program-heading-" + shot, title,
                             lower_title_track, 40, 105 + 394 - 35 - 45,
                             64.9, clip_starts[i], length,
                             family="Instrument Serif", color=PAPER,
                             spacing=-1.1, italic=True)
            del clip, small

        self.stage_graphic("synced-inspector-background", "rounded-rectangle",
                           750, 105, 230, 394, start, duration,
                           "#111820", radius=8.0)
        inspector_rows = [
            ("INSPECTOR", 135, 15, "Space Mono", PAPER),
            ("Transform", 170, 21, "Space Grotesk", PAPER),
            ("Position    0 / 0", 215, 16, "Space Grotesk", LIME),
            ("Scale    100%", 270, 16, "Space Grotesk", LIME),
            ("Opacity    100%", 325, 16, "Space Grotesk", LIME),
            ("LAYERED VIDEO", 420, 12, "Space Mono", "#8fa5b8"),
            ("TITLES + AUDIO", 448, 12, "Space Mono", "#8fa5b8"),
        ]
        for i, (text, y, size, family, color) in enumerate(inspector_rows):
            inspector_track = self.stage_media_track("SyncedTimeline / Inspector row " + str(i))
            self.stage_title("synced-inspector-row-" + str(i), text,
                             inspector_track, 772, y, size, start, duration,
                             family=family, color=color, spacing=0.0)

        for tick_index, seconds in enumerate((0, 3, 6, 9, 12)):
            tick_track = self.stage_media_track("SyncedTimeline / time tick " + str(tick_index))
            self.stage_title("synced-tick-" + str(tick_index),
                             f"{seconds:02d}:00", tick_track,
                             110 + seconds * 70, 575, 13, start, duration,
                             family="Space Mono", color="#a9bac8",
                             spacing=0.0, alignment="center")

        for i, (label, y) in enumerate((("V2 TITLE", 615), ("V1 VIDEO", 652), ("A1 MUSIC", 730))):
            lane_track = self.stage_media_track("SyncedTimeline / lane label " + str(i))
            self.stage_title("synced-lane-label-" + str(i), label,
                             lane_track, 20, y, 13, start, duration,
                             family="Space Mono", color="#adbdc8", spacing=0.0)

        self.stage_graphic("synced-title-clip", "rounded-rectangle",
                           110, 607, 840, 26, start, duration,
                           "#5c526e", stroke="#a9a0bf", stroke_width=1,
                           radius=3.0)
        v2_track = self.stage_media_track("SyncedTimeline / title clip label")
        self.stage_title("synced-title-clip-label", "AFTER THE RIDGELINE / TEXT",
                         v2_track, 115, 612, 12, start, duration,
                         family="Space Mono", color=PAPER, spacing=0.0)

        thumbnails = [
            ("arrival", "studio/arrival-first.jpg", 0.0, 3.7),
            ("ridge", "studio/ridge-thumb.jpg", 3.7, 3.2),
            ("city", "studio/city-delivery.jpg", 6.9, 2.7),
            ("night", "studio/night-thumb.jpg", 9.6, 2.4),
        ]
        for name, source, at, length in thumbnails:
            track = self.stage_media_track("SyncedTimeline / V1 thumbnail / " + name)
            x, y, w, h = 110 + at * 70, 642, length * 70 - 3, 62
            thumb = self.stage_image("synced-thumbnail-" + name, source,
                                     track, start, duration,
                                     x, y, w, h, fit="fill")
            self.stage_graphic("synced-thumbnail-label-backdrop-" + name,
                               "rectangle", x, y + 43, w, 19, start, duration,
                               "#0c1722dc")
            label_track = self.stage_media_track("SyncedTimeline / V1 label / " + name)
            display = {"arrival": "ARRIVAL", "ridge": "RIDGELINE",
                       "city": "KATHMANDU", "night": "AFTER DARK"}[name]
            self.stage_title("synced-thumbnail-label-" + name, display,
                             label_track, x + 5, y + 45, 11, start, duration,
                             family="Space Mono", color=PAPER, spacing=0.0)
            if name == "city":
                self.stage_graphic("synced-ripple-trim-edge", "rectangle",
                                   x + w - 6, y, 6, h, 199.5, 8.5, LIME)
            del thumb

        # The source waveform is drawn from its exact audioPeaks arrays. Its
        # editable vector path is sampled at every authored 30 fps state.
        self.audio_waveform("synced-timeline-music-wave", "music",
                            (110, 714, 840, 48), start, duration,
                            "#c1e3df", lambda t: 1.0)

        playhead = self.stage_graphic("synced-playhead", "rectangle",
                                      110 + 11.95 * 70 - 1, 574, 2, 168,
                                      start, duration, LIME)

        def playhead_x(abs_t):
            t = abs_t - start
            if t < 1.2:
                clock = 11.95
            elif t < 3.5:
                clock = 11.95 * (1.0 - smooth(t, 1.2, 2.5))
            elif t < 6.8:
                trim = smooth(t, 3.5, 6.5)
                clock = 9.56 - 0.7 * trim
            else:
                clock = 7.45 + (11.25 - 7.45) * smooth(t, 7.0, 11.5)
            x, _, _, _ = self.stage_box(110 + clock * 70 - 1, 574, 2, 168)
            _, _, width, _ = self.stage_box(110 + clock * 70 - 1, 574, 2, 168)
            return 4.0 * ((x + width / 2.0) / self.width - 0.5)

        self.animate("synced-playhead-motion", playhead,
                     {"frameOffsetX": playhead_x}, start, start + duration)

        # The cut pointer is a native editable vector mark; unlike the source's
        # SVG event cursor, it is static because there is no pointer input in a
        # rendered command plan.
        pointer_track = self.stage_media_track("SyncedTimeline / trim cursor")
        pointer_commands = [
            {"verb": "M", "points": [0, 0]},
            {"verb": "L", "points": [0, 25]},
            {"verb": "L", "points": [7, 19]},
            {"verb": "L", "points": [14, 33]},
            {"verb": "L", "points": [20, 29]},
            {"verb": "L", "points": [12, 17]},
            {"verb": "L", "points": [25, 17]},
            {"verb": "Z", "points": []},
        ]
        sx, sy, sw, sh = self.stage_box(110 + 8.9 * 70, 660, 28, 36)
        pointer = self.vector_path("synced-trim-cursor", pointer_track,
                                   start + 3.5, 3.0, sx, sy, sw, sh, 28, 36,
                                   pointer_commands, "#09121b", 1,
                                   fill="#ffffffff")
        self.animate("synced-trim-cursor-motion", pointer,
                     {"frameOffsetX": lambda at: 4.0 * (
                      (self.stage["x"] +
                       (110.0 + (9.6 - 0.7 * smooth(at - (start + 3.5), 0.0, 3.0))
                        * 70.0 + 14.0) * self.stage["scale"]) / self.width - 0.5)},
                     start + 3.5, start + 6.5)

        # Source's ripple readout is dynamic text; preserve its label and
        # visually show the authored 0.70 s edit while the native title itself
        # remains editable.
        ripple_track = self.stage_media_track("SyncedTimeline / ripple trim readout")
        ripple = self.stage_title("synced-ripple-readout", "RIPPLE TRIM / −0.70 s",
                                  ripple_track, 975, 480, 15, start + 4.0,
                                  8.0, family="Space Mono", color=PAPER,
                                  spacing=0.0, alignment="right")
        del ripple

    def keyframe_scene(self):
        start, duration = 208.0, 10.0
        self.caption("keyframe", start, duration,
                     "Motion, with\nprecise control.",
                     "Position, scale, opacity and easing. The animation follows your keyframes.")
        self.stage_video("keyframe-city-source", "creative-polish-v2/city-10.mp4",
                         self.stage_media_track("Keyframes / source footage"),
                         start, duration, 10.0, 0, 10, 1000, 562,
                         radius=11.0)
        self.stage_shape("keyframe-source-tint", self.stage_media_track("Keyframes / cool tint"),
                         "rectangle", 0, 10, 1000, 562, start, duration,
                         "#0b162b25")

        initial_left, initial_top, box_width, box_height = 430.0, 340.0, 500.0, 205.0
        final_left, final_top = 65.0, 150.0
        scale0 = 0.75
        border_track = self.stage_media_track("Keyframes / native title selection border")
        border = self.stage_shape("keyframe-title-selection", border_track,
                                  "rectangle", final_left, final_top,
                                  box_width, box_height, start, duration,
                                  "#00000000", stroke="#e8eddfb3",
                                  stroke_width=1.0)
        title_track = self.stage_media_track("Keyframes / title content")
        nepal = self.stage_title("keyframe-nepal-title", "NEPAL",
                                 title_track, final_left + 12, final_top + 12,
                                 145, start, duration, family="Anton",
                                 color=PAPER, spacing=-1.8)
        film_tag_track = self.stage_media_track("Keyframes / title label")
        film_tag = self.stage_title("keyframe-field-film-tag", "A FIELD FILM",
                                    film_tag_track, final_left + 12,
                                    final_top + 166, 18, start, duration,
                                    family="Space Mono", color=PAPER,
                                    spacing=1.2)
        handles = []
        for index, (x, y) in enumerate(((final_left - 4, final_top - 4),
                                        (final_left + box_width - 4, final_top - 4),
                                        (final_left - 4, final_top + box_height - 4),
                                        (final_left + box_width - 4,
                                         final_top + box_height - 4))):
            clip = self.stage_shape(
                "keyframe-handle-" + str(index),
                self.stage_media_track("Keyframes / selection handles"),
                "rectangle", x, y, 8, 8, start, duration, PAPER)
            handles.append((clip, x, y, 8.0, 8.0))

        def animation_geometry(abs_t, rect_x, rect_y, rect_width, rect_height):
            local = abs_t - start
            p = motion_value((local - 0.5) / 3.5)
            desired_x = initial_left + (final_left - initial_left) * p
            desired_y = initial_top + (final_top - initial_top) * p
            scale = scale0 + 0.25 * p
            stage_scale = self.stage["scale"]
            relative_x = rect_x - final_left
            relative_y = rect_y - final_top
            base_cx = self.stage["x"] + (rect_x + rect_width / 2) * stage_scale
            base_cy = self.stage["y"] + (rect_y + rect_height / 2) * stage_scale
            desired_cx = self.stage["x"] + (
                desired_x + (relative_x + rect_width / 2) * scale) * stage_scale
            desired_cy = self.stage["y"] + (
                desired_y + (relative_y + rect_height / 2) * scale) * stage_scale
            native_cx = base_cx + (scale - 1.0) * (base_cx - self.width / 2)
            native_cy = base_cy + (scale - 1.0) * (base_cy - self.height / 2)
            correction_x = desired_cx - native_cx
            correction_y = desired_cy - native_cy
            return {
                "frameScale": scale,
                "frameOffsetX": 4.0 * (base_cx / self.width - 0.5) +
                4.0 * correction_x / self.width,
                "frameOffsetY": 4.0 * (base_cy / self.height - 0.5) +
                4.0 * correction_y / self.height,
                "opacity": p,
            }

        def headline_geometry(abs_t):
            local = abs_t - start
            p = motion_value((local - 0.5) / 3.5)
            scale = scale0 + 0.25 * p
            x = initial_left + (final_left - initial_left) * p + 12.0 * scale
            top = initial_top + (final_top - initial_top) * p + 12.0 * scale
            size = 145.0 * scale * self.stage["scale"]
            return {
                "titleFontSize": size,
                "titlePositionX": (self.stage["x"] + x * self.stage["scale"]) / self.width,
                "titlePositionY": (self.stage["y"] +
                                   (top + (145.0 * scale * 1.01) / 2.0) *
                                   self.stage["scale"]) / self.height,
                "opacity": p,
            }

        def film_tag_geometry(abs_t):
            local = abs_t - start
            p = motion_value((local - 0.5) / 3.5)
            scale = scale0 + 0.25 * p
            x = initial_left + (final_left - initial_left) * p + 12.0 * scale
            top = initial_top + (final_top - initial_top) * p + 172.0 * scale
            size = 18.0 * scale * self.stage["scale"]
            return {
                "titleFontSize": size,
                "titlePositionX": (self.stage["x"] + x * self.stage["scale"]) / self.width,
                "titlePositionY": (self.stage["y"] +
                                   (top + (18.0 * scale * 1.01) / 2.0) *
                                   self.stage["scale"]) / self.height,
                "opacity": p,
            }

        shape_motion = [("selection", border, final_left, final_top,
                         box_width, box_height)] + [
            ("handle-" + str(i), clip, x, y, w, h)
            for i, (clip, x, y, w, h) in enumerate(handles)
        ]
        for suffix, clip, rect_x, rect_y, rect_w, rect_h in shape_motion:
            self.animate("keyframe-motion-" + suffix, clip,
                         {key: lambda at, key=key, x=rect_x, y=rect_y, w=rect_w, h=rect_h:
                          animation_geometry(at, x, y, w, h)[key]
                          for key in ("frameScale", "frameOffsetX", "frameOffsetY", "opacity")},
                         start, start + duration)
        self.animate("keyframe-motion-headline", nepal,
                     {key: lambda at, key=key: headline_geometry(at)[key]
                      for key in ("titleFontSize", "titlePositionX", "titlePositionY", "opacity")},
                     start, start + duration)
        self.animate("keyframe-motion-tag", film_tag,
                     {key: lambda at, key=key: film_tag_geometry(at)[key]
                      for key in ("titleFontSize", "titlePositionX", "titlePositionY", "opacity")},
                     start, start + duration)
        graph_track = self.stage_media_track("Keyframes / easing graph")
        axis = [
            {"verb": "M", "points": [30, 175]}, {"verb": "L", "points": [30, 15]},
            {"verb": "M", "points": [30, 175]}, {"verb": "L", "points": [970, 175]},
        ]
        self.vector_path("keyframe-graph-axes", graph_track, start, duration,
                         *self.stage_box(0, 595, 1000, 175), 1000, 200,
                         axis, "#657789", 1.0)
        curve = [{"verb": "M", "points": [30, 175]}]
        for i in range(1, 129):
            u = i / 128.0
            curve.append({"verb": "L", "points": [30 + u * 940,
                                                      175 - motion_value(u) * 160]})
        curve_track = self.stage_media_track("Keyframes / easing curve")
        self.vector_path("keyframe-graph-curve", curve_track, start, duration,
                         *self.stage_box(0, 595, 1000, 175), 1000, 200,
                         curve, LIME, 3.0)
        dot_track = self.stage_media_track("Keyframes / graph playhead")
        dot = self.stage_shape("keyframe-graph-dot", dot_track,
                               "ellipse", 22, 740.125, 16, 16, start, duration, PAPER)
        self.animate("keyframe-graph-dot-motion", dot, {
            "frameOffsetX": lambda at: 4.0 * (
                (self.stage["x"] + (30.0 +
                 clamp01((at - start - 0.5) / 3.5) * 940.0) * self.stage["scale"])
                / self.width - 0.5),
            "frameOffsetY": lambda at: 4.0 * (
                (self.stage["y"] + (595.0 +
                 (175.0 - motion_value((at - start - 0.5) / 3.5) * 160.0) * 0.875)
                 * self.stage["scale"]) / self.height - 0.5),
        }, start, start + duration)
        readout_track = self.stage_media_track("Keyframes / live transform readout")
        readout = self.stage_title("keyframe-transform-readout",
                                   "X 65 / Y 150 · SCALE 100% · OPACITY 100%",
                                   readout_track, 15, 780, 15, start, duration,
                                   family="Space Mono", color=PAPER, spacing=0.0)
        del readout

    def grade_scene(self):
        start, duration = 218.0, 8.0
        self.caption("grade", start, duration,
                     "Build the look.\nLayer the effects.",
                     "A native color grade and a monochrome effect. The same footage, visibly transformed.")
        x, y, w, h = self.stage_box(0, 35, 1000, 562)
        source_track = self.stage_media_track("Grade / original source")
        source = self.video("grade-original-city", "creative-polish-v2/city-10.mp4",
                            source_track, start, duration, 10.0)
        self.frame("grade-original-city-frame", source, x, y, w, h,
                   fit="fill", radius=11.0 * self.stage["scale"])
        grade_track = self.stage_media_track("Grade / native grade comparison")
        graded = self.video("grade-city-graded", "creative-polish-v2/city-grade-10.mp4",
                            grade_track, start, duration, 10.0)
        self.frame("grade-city-graded-frame", graded, x, y, w, h,
                   fit="fill", radius=11.0 * self.stage["scale"])
        # The browser reveals both original effect outputs through a moving
        # straight split. A native per-clip sequence mask carries that wipe.
        panel_right = (self.stage["x"] + 1000.0 * self.stage["scale"]) / self.width
        panel_top = (self.stage["y"] + 35.0 * self.stage["scale"]) / self.height
        panel_bottom = (self.stage["y"] + (35.0 + 562.0) * self.stage["scale"]) / self.height
        split_left = (self.stage["x"] + 380.0 * self.stage["scale"]) / self.width
        self.add("grade-city-graded-mask", "setTimelineClipMask",
                 clipId=graded, shape="rectangle", left=split_left,
                 top=panel_top, right=panel_right, bottom=panel_bottom,
                 cornerRadius=0.0)
        mono_track = self.stage_media_track("Grade / monochrome comparison")
        mono = self.video("grade-city-monochrome", "creative-polish-v2/city-mono-10.mp4",
                          mono_track, start, duration, 10.0)
        self.frame("grade-city-monochrome-frame", mono, x, y, w, h,
                   fit="fill", opacity=0.0, radius=11.0 * self.stage["scale"])
        self.add("grade-city-monochrome-mask", "setTimelineClipMask",
                 clipId=mono, shape="rectangle", left=split_left,
                 top=panel_top, right=panel_right, bottom=panel_bottom,
                 cornerRadius=0.0)
        wipe = lambda at: (self.stage["x"] +
                           1000.0 * self.stage["scale"] *
                           0.38 * (1.0 - smooth(at - start, 6.5, 7.3))) / self.width
        self.animate("grade-city-graded-wipe", graded,
                     {"mask:left": wipe}, start, start + duration)
        self.animate("grade-city-monochrome-wipe", mono,
                     {"mask:left": wipe}, start, start + duration)
        self.animate("grade-color-reveal", graded,
                     {"opacity": lambda at: smooth(at - start, 1.0, 2.0)},
                     start, start + duration)
        self.animate("grade-mono-reveal", mono,
                     {"opacity": lambda at: smooth(at - start, 4.1, 5.1)},
                     start, start + duration)
        before_track = self.stage_media_track("Grade / before-after divider")
        divider = self.stage_shape("grade-before-after-divider", before_track,
                                   "rectangle", 0, 35, 2, 562, start,
                                   duration, PAPER)
        self.animate("grade-before-after-divider-opacity", divider,
                     {"frameOffsetX": lambda at: 4.0 * (
                      (self.stage["x"] +
                       (1000.0 * 0.38 * (1.0 - smooth(at - start, 6.5, 7.3)) + 1.0)
                       * self.stage["scale"]) / self.width - 0.5),
                      "opacity": lambda at: 1.0 - smooth(at - start, 6.5, 7.3)},
                     start, start + duration)
        before_badge_bg_track = self.stage_media_track("Grade / before badge background")
        self.stage_shape("grade-before-badge-background", before_badge_bg_track,
                         "rounded-rectangle", 18, 20, 108, 34, start,
                         duration, "#09111fce", radius=5.0)
        before_badge_track = self.stage_media_track("Grade / before badge label")
        self.stage_title("grade-before-badge", "BEFORE", before_badge_track,
                         28, 23, 16, start, duration, family="Space Mono",
                         color=PAPER, spacing=0.0)
        effect_badge_bg_track = self.stage_media_track("Grade / effect badge background")
        self.stage_shape("grade-effect-badge-background", effect_badge_bg_track,
                         "rounded-rectangle", 842, 20, 140, 34, start,
                         duration, "#09111fce", radius=5.0)
        effect_badge_track = self.stage_media_track("Grade / effect badge label")
        self.stage_title("grade-effect-badge", "NATIVE EFFECTS",
                         effect_badge_track, 972, 23, 16, start, duration,
                         family="Space Mono", color=PAPER, spacing=0.0,
                         alignment="right")
        # Two effect setting cards retain the exact source labels and values.
        for i, (label, active) in enumerate((("COLOR GRADE", 1.0),
                                             ("MONOCHROME", 4.1))):
            bx, by = 50 + i * 440, 640
            card_track = self.stage_media_track("Grade / setting card / " + str(i))
            card = self.stage_shape("grade-setting-card-" + str(i), card_track,
                                    "rounded-rectangle", bx, by, 420, 70,
                                    start, duration, "#14202c",
                                    stroke="#a7b6c94a", stroke_width=1.0,
                                    radius=12.0)
            dot_track = self.stage_media_track("Grade / setting state / " + str(i))
            self.stage_shape("grade-setting-state-off-" + str(i), dot_track,
                             "ellipse", bx + 22, by + 26, 17, 17,
                             start, duration, "#495563")
            dot_active_track = self.stage_media_track("Grade / setting active state / " + str(i))
            dot = self.stage_shape("grade-setting-state-on-" + str(i), dot_active_track,
                                   "ellipse", bx + 22, by + 26, 17, 17,
                                   start, duration, LIME, opacity=0.0)
            self.animate("grade-setting-state-fill-" + str(i), dot,
                         {"opacity": lambda at, active=active: smooth(at - start, active, active + 1.0)},
                         start, start + duration)
            label_track = self.stage_media_track("Grade / setting label / " + str(i))
            self.stage_title("grade-setting-label-" + str(i), label,
                             label_track, bx + 50, by + 24, 19, start,
                             duration, family="Space Mono", color=PAPER,
                             spacing=0.0)
            percent_track = self.stage_media_track("Grade / setting percentage / " + str(i))
            percent = self.stage_title("grade-setting-percent-" + str(i), "100%",
                                      percent_track, bx + 385, by + 24, 19,
                                      start, duration, family="Space Mono",
                                      color=LIME, spacing=0.0,
                                      alignment="right")
            self.animate("grade-setting-percent-opacity-" + str(i), percent,
                         {"opacity": lambda at, active=active: smooth(at - start, active, active + 1.0)},
                         start, start + duration)
            del card
        grade_text_track = self.stage_media_track("Grade / settings summary")
        self.stage_title("grade-settings-summary",
                         "GRADE: EXPOSURE +0.25 / CONTRAST +14 / VIBRANCE +12",
                         grade_text_track, 50, 745, 15, start, duration,
                         family="Space Mono", color=PAPER, spacing=0.0)

    def audio_scene(self):
        start, duration = 226.0, 6.0
        self.caption("audio", start, duration,
                     "Picture meets\nsound.",
                     "Pull down the music and hear the location. Shape the mix alongside the edit.")
        self.stage_video("audio-city-source", "creative-polish-v2/city-10.mp4",
                         self.stage_media_track("Audio mixer / city footage"),
                         start, duration, 10.0, 70, 0, 860, 484,
                         radius=10.0)
        panels = [("MUSIC", "#294668"), ("LOCATION", "#204e43")]
        for i, (label, color) in enumerate(panels):
            x, y, w, h = 20.0, 510.0 + i * 125.0, 960.0, 104.0
            self.stage_graphic("audio-mixer-panel-" + str(i), "rounded-rectangle",
                               x, y, w, h, start, duration, color, radius=9.0)
            label_track = self.stage_media_track("Audio mixer / label / " + label)
            self.stage_title("audio-mixer-label-" + str(i), label,
                             label_track, x + 15, y + 14, 15, start, duration,
                             family="Space Mono", color=PAPER, spacing=0.0)
            readout = "0.0 dB" if i == 0 else "MUTED / ON"
            readout_track = self.stage_media_track("Audio mixer / readout / " + label)
            self.stage_title("audio-mixer-readout-" + str(i), readout,
                             readout_track, x + w - 15, y + 14, 15,
                             start, duration, family="Space Mono",
                             color=PAPER, spacing=0.0, alignment="right")
            mode = (lambda t: 10.0 ** ((-12.0 * self.mixer_mix(t)) / 20.0)) if i == 0 else self.mixer_mix
            self.audio_waveform("audio-mixer-waveform-" + str(i),
                                "music" if i == 0 else "location",
                                (0, 0, 1000, 64), start, duration,
                                "#c1e3df", mode, stage=True,
                                stage_origin=(20.0, y + 35.0),
                                stage_width=960.0, stage_height=64.0)
        playhead = self.stage_graphic("audio-mixer-playhead", "rectangle",
                                      35, 510, 2, 230, start, duration, LIME)
        self.animate("audio-mixer-playhead-motion", playhead,
                     {"frameOffsetX": lambda at: 4.0 * (
                      (self.stage["x"] +
                       (36.0 + ((at - start) / 6.0) * 930.0) * self.stage["scale"])
                      / self.width - 0.5)},
                     start, start + duration)

    @staticmethod
    def mixer_mix(local_time):
        u = smooth(local_time, 0.8, 2.0) * (1.0 - smooth(local_time, 4.5, 5.7))
        return u

    def audio_waveform(self, step_id, source_key, box, start, duration, color,
                       gain_fn, stage=False, stage_origin=(0.0, 0.0),
                       stage_width=1000.0, stage_height=64.0):
        samples = read_audio_peak_samples(source_key)
        view_w, view_h = 1000.0, float(box[3])
        commands = waveform_commands(samples, view_h)
        frame_count = round(duration * FPS)
        frame_rows = [
            {"seconds": frame / FPS,
             "coordinates": waveform_coordinates(
                 samples, view_h, max(0.0, float(gain_fn(frame / FPS))))}
            for frame in range(frame_count + 1)
        ]
        if stage:
            origin_x, origin_y = stage_origin
            x, y, width, height = self.stage_box(origin_x, origin_y,
                                                stage_width, stage_height)
        else:
            x, y, width, height = box

        if step_id == "synced-timeline-music-wave":
            # This source waveform has constant gain 1 for all 361 local
            # samples. Prove those frames are redundant, then store its exact
            # static M/L/L/L/Z geometry once.
            static_coordinates = waveform_coordinates(samples, view_h, 1.0)
            if any(frame["coordinates"] != static_coordinates for frame in frame_rows):
                raise AssertionError("The synced timeline waveform is not actually static")
            track = self.track(step_id + "-track", step_id.replace("-", " "))
            return self.vector_path(step_id, track, start, duration, x, y,
                                    width, height, view_w, view_h, commands,
                                    "#00000000", 0.0, fill=color)

        if not step_id.startswith("audio-mixer-waveform-"):
            raise AssertionError("Unexpected waveform source in Film/Closing plan")

        # Each source sample is an independent closed rectangle. Partition
        # whole rectangles so each generator request stays small while all
        # frame coordinates, timing and min-height clamps remain exact.
        chunk_count = math.ceil(len(samples) / WAVEFORM_BARS_PER_GENERATOR)
        clips = []
        for chunk_index in range(chunk_count):
            bar_start = chunk_index * WAVEFORM_BARS_PER_GENERATOR
            bar_end = min(len(samples), bar_start + WAVEFORM_BARS_PER_GENERATOR)
            path_commands = commands[bar_start * 5:bar_end * 5]
            coordinate_start = bar_start * 8
            coordinate_end = bar_end * 8
            path_frames = [
                {"seconds": frame["seconds"],
                 "coordinates": frame["coordinates"][coordinate_start:coordinate_end]}
                for frame in frame_rows
            ]
            chunk_id = f"{step_id}-chunk-{chunk_index:02d}"
            track = self.track(chunk_id + "-track",
                               f"{step_id.replace('-', ' ')} / bars {bar_start + 1}-{bar_end}")
            clips.append(self.vector_path(
                chunk_id, track, start, duration, x, y, width, height,
                view_w, view_h, path_commands,
                "#00000000", 0.0, fill=color, frames=path_frames,
            ))
        return clips

    def delivery_scene(self):
        start, duration = 232.0, 8.0
        light_track = self.track("delivery-light-track", "Delivery / RGB light")
        light = self.generator("delivery-light", light_track, "desktop-light",
                               start, duration,
                               {"timeOffsetSeconds": start, "timeScale": 1.0,
                                "gather": 0.0}, opacity=0.26)
        self.frame("delivery-light-frame", light, fit="stretch", opacity=0.26)
        top_x = 65.0 if self.portrait else 85.0
        top_y = 160.0 if self.portrait else 70.0
        header_label_track = self.track("delivery-header-label-track", "Delivery / chapter label")
        self.title("delivery-label", "05 / VIDEO TIMELINE", header_label_track,
                   top_x, top_y, 21.0, start, duration,
                   family="Space Mono", color=LIME, spacing=1.8)
        header_size = 114.0 if self.portrait else 106.0
        header_title_track = self.track("delivery-header-title-track", "Delivery / headline")
        self.title("delivery-headline", "From camera color\nto final delivery.",
                   header_title_track, top_x, top_y + 45.0, header_size, start,
                   duration, family="Anton", color=PAPER,
                   spacing=-1.8, line_height=1.01)

        photo_x, photo_y, photo_w, photo_h = (
            (45.0, 690.0, 990.0, 557.0) if self.portrait
            else (85.0, 355.0, 1080.0, 608.0)
        )
        photo_track = self.track("delivery-image-track", "Delivery / Rec.709 image")
        photo = self.image("delivery-city-example", "studio/city-delivery.jpg",
                           photo_track, start, duration)
        self.frame("delivery-city-example-frame", photo, photo_x, photo_y,
                   photo_w, photo_h, fit="fill", radius=10.0)
        badge_bg_track = self.track("delivery-image-badge-bg-track", "Delivery / source label background")
        badge_x, badge_y = photo_x + 25.0, photo_y + photo_h - 25.0 - 48.0
        self.shape("delivery-source-label-backdrop", badge_bg_track, "rectangle",
                   badge_x, badge_y, 420.0, 48.0, start, duration, "#0b1424de")
        badge_text_track = self.track("delivery-image-badge-text-track", "Delivery / source label")
        self.title("delivery-source-label", "CURRENT EXAMPLE / Rec.709 SOURCE",
                   badge_text_track, badge_x + 17.0, badge_y + 12.0, 20.0,
                   start, duration, family="Space Mono", color=PAPER,
                   spacing=1.8)

        columns_x, columns_y, columns_width = (
            (65.0, 1260.0, 950.0) if self.portrait
            else (1240.0, 380.0, 590.0)
        )
        rows = [
            ("COLOR", "sRGB · Rec.709", "Color-managed workflows"),
            ("LOG INPUT", "Sony S-Log2 · S-Log3", "Supported camera transforms"),
            ("DELIVERY", "4K · ProRes", "Resolution and codec options"),
        ]
        row_height = 148.0 if self.portrait else 160.0
        label_size = 20.0 if self.portrait else 19.0
        value_size = 35.0 if self.portrait else 37.0
        for i, (label, value, detail) in enumerate(rows):
            y = columns_y + i * row_height
            rule_track = self.track("delivery-column-rule-track-" + str(i), "Delivery / column rule")
            line = self.shape("delivery-column-rule-" + str(i), rule_track,
                              "rectangle", columns_x, y, columns_width, 1.0,
                              start, duration, "#b3c2cf55")
            rule_base_offset_y = 4.0 * ((y + 0.5) / self.height - 0.5)
            label_track = self.track("delivery-column-label-track-" + str(i), "Delivery / column label")
            label_clip = self.title("delivery-column-label-" + str(i), label,
                                    label_track, columns_x, y + 14.0,
                                    label_size, start, duration,
                                    family="Space Mono", color=LIME,
                                    spacing=1.8, opacity=0.0)
            value_track = self.track("delivery-column-value-track-" + str(i), "Delivery / column value")
            value_clip = self.title("delivery-column-value-" + str(i), value,
                                    value_track, columns_x, y + 44.0,
                                    value_size, start, duration,
                                    family="Space Grotesk", color=PAPER,
                                    spacing=0.0, opacity=0.0)
            detail_track = self.track("delivery-column-detail-track-" + str(i), "Delivery / column detail")
            detail_clip = self.title("delivery-column-detail-" + str(i), detail,
                                     detail_track, columns_x, y + 96.0,
                                     24.0, start, duration,
                                     family="Space Grotesk", color="#b7c4d2",
                                     spacing=0.0, opacity=0.0)
            for suffix, clip in (("rule", line), ("label", label_clip),
                                 ("value", value_clip), ("detail", detail_clip)):
                self.animate("delivery-column-enter-" + str(i) + "-" + suffix,
                             clip, {"opacity": lambda at, i=i: out(at - start, i * 0.35, 0.55 + i * 0.35),
                                    "frameOffsetY": lambda at, i=i, base=(
                                        rule_base_offset_y if suffix == "rule" else 0.0
                                    ): base + 4.0 * 20.0 *
                                    (1.0 - out(at - start, i * 0.35, 0.55 + i * 0.35)) / self.height},
                             start, start + duration)

        self.common_footer("delivery", start, duration,
                           "COLOR SPACES. CAMERA INPUTS. DELIVERY OPTIONS.",
                           "LOOKSY / VIDEO")

    def gaussian_scene(self):
        start, duration = 240.0, 4.0
        self.caption("gaussian", start, duration,
                     "Change your\nperspective.",
                     "Gaussian scenes with a moving camera. Another dimension in the creative toolbox.",
                     serif=True)
        self.stage_video("gaussian-camera-source", "studio/gaussian-camera.mp4",
                         self.stage_media_track("Gaussian / camera render"),
                         start, duration, 12.0, 0, 25, 1000, 665,
                         trim_start=3.0, radius=14.0)
        label_track = self.stage_media_track("Gaussian / scene label")
        self.stage_title("gaussian-scene-label", "GAUSSIAN SCENE / CAMERA MOTION",
                         label_track, 25, 610, 17, start, duration,
                         family="Space Mono", color=PAPER, spacing=1.0)
        self.common_footer("gaussian", start, duration,
                           "HOUSEPLANT / MARCEL PADILLA / CC BY 4.0")

    def framing_scene(self):
        start, duration = 244.0, 8.0
        self.caption("framing", start, duration,
                     "A full film.\nA social cut.",
                     "Landscape productions and independently framed vertical videos.")
        main_track = self.stage_media_track("Framing / landscape film")
        main = self.stage_video("framing-landscape-film",
                                "creative-polish-v2/arrival-12.mp4",
                                main_track, start, duration, 12.0,
                                0, 65, 830, 467, radius=12.0)
        # The source applies perspective(1500px) rotateY(-7deg); the native
        # clip preserves its rounded box and source framing but not 3D skew.
        title_track = self.stage_media_track("Framing / landscape film title")
        self.stage_title("framing-landscape-title", "A little further.",
                         title_track, 30, 65 + 467 - 35 - 70, 65,
                         start, duration, family="Instrument Serif",
                         color=PAPER, spacing=-1.0, italic=True)

        phone_track = self.stage_media_track("Framing / vertical social cut")
        phone = self.stage_video("framing-social-film",
                                 "creative-polish-v2/arrival-12.mp4",
                                 phone_track, start, duration, 12.0,
                                 690, 230, 260, 462, radius=18.0,
                                 rotation=5.0)
        # Preserve source's 5 px phone rim as an editable rounded outline.
        phone_rim = self.stage_graphic("framing-phone-rim", "rounded-rectangle",
                                      690, 230, 260, 462, start, duration,
                                      "#00000000", stroke="#c4d0d4", stroke_width=5.0,
                                      radius=18.0, rotation=5.0)
        phone_title_track = self.stage_media_track("Framing / phone title")
        phone_title = self.stage_title("framing-phone-title", "A LITTLE\nFURTHER.",
                                       phone_title_track, 710, 285, 52,
                                       start, duration, family="Anton",
                                       color=PAPER, spacing=-1.8,
                                       line_height=1.0)
        self.animate("framing-phone-title-enter", phone_title,
                     {"frameOffsetY": lambda at: 4.0 * 90.0 *
                      (1.0 - out(at - start, 0.3, 1.3)) / self.height},
                     start, start + duration)
        phone_shape = phone
        phone_center_y = self.stage["y"] + (230.0 + 462.0 / 2.0) * self.stage["scale"]
        phone_base_offset_y = 4.0 * (phone_center_y / self.height - 0.5)
        self.animate("framing-phone-enter", phone_shape,
                     {"frameOffsetY": lambda at: phone_base_offset_y +
                      4.0 * 90.0 * (1.0 - out(at - start, 0.3, 1.3)) / self.height},
                     start, start + duration)
        self.animate("framing-phone-rim-enter", phone_rim,
                     {"frameOffsetY": lambda at: phone_base_offset_y +
                      4.0 * 90.0 * (1.0 - out(at - start, 0.3, 1.3)) / self.height},
                     start, start + duration)

        pills = ["FULL PRODUCTIONS", "SOCIAL CUTS", "HIGH-RESOLUTION DELIVERY"]
        pill_x = 10.0
        for i, label in enumerate(pills):
            font_size, pill_height = 19.0, 48.0
            pill_width = len(label) * font_size * 0.6 + 36.0
            pill_track = self.stage_media_track("Framing / delivery background / " + str(i))
            self.stage_shape("framing-pill-background-" + str(i), pill_track,
                             "rounded-rectangle", pill_x, 745, pill_width,
                             pill_height, start, duration, "#0b0d12bd",
                             stroke="#c4c8ce55", stroke_width=1.0,
                             radius=40.0)
            label_track = self.stage_media_track("Framing / delivery label / " + str(i))
            self.stage_title("framing-pill-label-" + str(i), label,
                             label_track, pill_x + pill_width / 2.0,
                             745 + 12.0, font_size, start, duration,
                             family="Space Mono", color=LIME if i == 0 else PAPER,
                             spacing=0.0, alignment="center")
            pill_x += pill_width + 12.0
        self.common_footer("framing", start, duration)

    def closing_chapter(self):
        start, duration = 280.0, 18.0
        self.add_ink("closing", start, duration)
        light_track = self.track("closing-light-track", "Closing / additive RGB light")
        light = self.generator("closing-light", light_track, "desktop-light",
                               start, duration,
                               {"timeOffsetSeconds": start, "timeScale": 1.0,
                                "gather": 0.0}, opacity=0.65)
        self.frame("closing-light-frame", light, fit="stretch", opacity=0.65)
        self.animate("closing-light-gather", light,
                     {"generator:gather": lambda at: smooth(at - start, 6.0, 15.0)},
                     start, start + duration)

        hero_x = 65.0 if self.portrait else 85.0
        hero_top = 170.0 if self.portrait else 90.0
        hero_label_track = self.track("closing-hero-label-track", "Closing / campaign label")
        label = self.title("closing-hero-label", "PHOTO. DESIGN. FILM.",
                           hero_label_track, hero_x, hero_top, 21.0, start, duration,
                           family="Space Mono", color=LIME, spacing=1.8)
        headline_size = 135.0 if self.portrait else 125.0
        heading_y = hero_top + 25.0 + 25.0
        hero_title_track = self.track("closing-hero-title-track", "Closing / campaign title")
        hero_heading = "Make the whole\nthing." if self.portrait else "Make the whole thing."
        heading = self.title("closing-hero-headline", hero_heading,
                             hero_title_track, hero_x, heading_y, headline_size,
                             start, duration, family="Anton", color=PAPER,
                             spacing=-1.8)
        detail_y = heading_y + headline_size * 1.01 + 26.0
        hero_detail_track = self.track("closing-hero-detail-track", "Closing / campaign detail")
        detail = self.title("closing-hero-detail",
                            "One desktop studio. Your work stays yours to refine.",
                            hero_detail_track, hero_x, detail_y,
                            38.0 if self.portrait else 36.0,
                            start, duration, family="Space Grotesk",
                            color=PAPER, spacing=0.0)
        hero_opacity = lambda at: 1.0 - smooth(at - start, 7.0, 8.5)
        for i, clip in enumerate((label, heading, detail)):
            self.animate("closing-hero-fade-" + str(i), clip,
                         {"opacity": hero_opacity}, start, start + duration)

        # Actual desktop UI source, not a flattened trailer frame. This source
        # wrapper uses the active responsive geometry and centered 0.96→1 zoom.
        card_x, card_y, card_w, card_h = (
            (40.0, 770.0, 1000.0, 585.0) if self.portrait
            else (310.0, 390.0, 1300.0, 760.0)
        )
        start_scale = 0.96
        card_cx, card_cy = card_x + card_w / 2.0, card_y + card_h / 2.0
        transform_x = (1.0 - start_scale) * (card_cx - self.width / 2.0)
        transform_y = (1.0 - start_scale) * (card_cy - self.height / 2.0)
        card_track = self.track("closing-desktop-card-track", "Closing / desktop shell")
        card = self.shape("closing-desktop-card", card_track,
                          "rounded-rectangle", card_x, card_y, card_w, card_h,
                          start, duration, "#11151a", stroke="#78849955",
                          stroke_width=1.0, radius=13.0, opacity=1.0)
        self.frame("closing-desktop-card-transform", card,
                   card_x, card_y, card_w, card_h, fit="stretch",
                   opacity=1.0, scale=start_scale,
                   offset_x_px=transform_x, offset_y_px=transform_y,
                   radius=13.0)
        desktop_track = self.track("closing-desktop-image-track", "Closing / native desktop source")
        desktop = self.image("closing-desktop-image", "desktop-launch/raw-editor.png",
                             desktop_track, start, duration)
        self.frame("closing-desktop-image-frame", desktop, card_x + 1.0,
                   card_y + 1.0, card_w - 2.0, card_h - 2.0,
                   fit="fit", opacity=1.0, scale=start_scale,
                   offset_x_px=transform_x, offset_y_px=transform_y,
                   radius=13.0)
        for index, clip in enumerate((card, desktop)):
            self.animate("closing-desktop-motion-" + str(index), clip, {
                "frameScale": lambda at: 0.96 + 0.04 * smooth(at - start, 0.0, 7.0),
                "frameOffsetX": lambda at: 4.0 * (card_cx / self.width - 0.5) +
                4.0 * (card_cx - self.width / 2.0)
                * (1.0 - (0.96 + 0.04 * smooth(at - start, 0.0, 7.0))) / self.width,
                "frameOffsetY": lambda at: 4.0 * (card_cy / self.height - 0.5) +
                4.0 * (card_cy - self.height / 2.0)
                * (1.0 - (0.96 + 0.04 * smooth(at - start, 0.0, 7.0))) / self.height,
                "opacity": hero_opacity,
            }, start, start + duration)

        # BuyerClosing AppWordmark is the exact manifest asset, with source
        # widths and its 1795:454 intrinsic ratio.
        cta_start_y = self.height * (0.30 if self.portrait else 0.26)
        logo_width = 800.0 if self.portrait else 900.0
        logo_height = logo_width * 454.0 / 1795.0
        logo_x = (self.width - logo_width) / 2.0
        cta_track = self.track("closing-cta-logo-track", "Closing / Looksy wordmark")
        logo = self.image("closing-app-wordmark", "looksy-logo-dark.png",
                          cta_track, start, duration)
        self.frame("closing-app-wordmark-frame", logo, logo_x, cta_start_y,
                   logo_width, logo_height, fit="fit", opacity=0.0)
        logo_base_offset_y = 4.0 * ((cta_start_y + logo_height / 2.0) / self.height - 0.5)
        self.closing_cta_motion("logo", logo, start, cta_start_y,
                                base_frame_offset_y=logo_base_offset_y)

        tagline_size = 55.0 if self.portrait else 57.0
        tagline_y = cta_start_y + logo_height + 32.0
        tagline_track = self.track("closing-cta-tagline-track", "Closing / delivery promise")
        tagline_text = ("From your first edit to\nyour final delivery."
                        if self.portrait else "From your first edit to your final delivery.")
        tagline = self.title("closing-cta-tagline", tagline_text,
                             tagline_track, self.width / 2.0, tagline_y,
                             tagline_size, start, duration,
                             family="Instrument Serif", color=PAPER,
                             spacing=-1.0, alignment="center", italic=True,
                             opacity=0.0)
        self.closing_cta_motion("tagline", tagline, start, tagline_y)

        button_font = 38.0 if self.portrait else 35.0
        button_height = 23.0 * 2.0 + button_font * 1.25
        button_width = 38.0 * 2.0 + (button_font * 7.3) + 30.0 + button_font * 0.6
        button_x = (self.width - button_width) / 2.0
        button_y = tagline_y + tagline_size * 1.2 + 60.0
        button_track = self.track("closing-cta-button-track", "Closing / website call to action")
        button = self.shape("closing-website-button", button_track,
                            "rounded-rectangle", button_x, button_y,
                            button_width, button_height, start, duration,
                            LIME, radius=8.0, opacity=0.0)
        button_base_offset_y = 4.0 * (
            (button_y + button_height / 2.0) / self.height - 0.5)
        button_text_track = self.track("closing-cta-button-label-track", "Closing / website label")
        button_text = self.title("closing-website-button-text",
                                 "Visit the website  ↗", button_text_track,
                                 self.width / 2.0,
                                 button_y + button_height / 2.0 - button_font * 0.6,
                                 button_font, start, duration,
                                 family="Space Grotesk", color="#101820",
                                 spacing=0.0, alignment="center", weight=600,
                                 opacity=0.0)
        self.closing_cta_motion("button", button, start, button_y,
                                base_frame_offset_y=button_base_offset_y)
        self.closing_cta_motion("button-text", button_text, start, button_y)

        footer_left_track = self.track("closing-footer-left-track", "Closing / footer left")
        footer_right_track = self.track("closing-footer-right-track", "Closing / footer right")
        left_x = 62.0 if self.portrait else 82.0
        right_x = self.width - left_x
        footer_y = self.height - (130.0 if self.portrait else 22.0) - (20.0 if self.portrait else 17.0) * 0.6
        self.title("closing-footer-left", "FOR WORK. FOR YOURSELF. FOR WHAT COMES NEXT.",
                   footer_left_track, left_x, footer_y,
                   20.0 if self.portrait else 17.0, start, duration,
                   family="Space Mono", color=MUTED, spacing=1.0,
                   line_height=1.2)
        self.title("closing-footer-right", "LOOKSY / DESKTOP", footer_right_track,
                   right_x, footer_y, 20.0 if self.portrait else 17.0,
                   start, duration, family="Space Mono", color=MUTED,
                   spacing=1.0, line_height=1.2, alignment="right")

    def closing_cta_motion(self, name, clip, start, _y,
                           base_frame_offset_y=0.0):
        self.animate("closing-cta-" + name + "-entry", clip, {
            "opacity": lambda at: smooth(at - start, 8.5, 10.0),
            "frameOffsetY": lambda at: base_frame_offset_y + 4.0 * 30.0 *
            (1.0 - smooth(at - start, 8.5, 10.0)) / self.height,
        }, start, start + 18.0)


def build(width, height, format_name):
    return FilmClosingPlan(width, height, format_name).build()


if __name__ == "__main__":
    outputs = [
        (1920, 1080, "landscape", "film-closing-v3-landscape.commands.json"),
        (1080, 1920, "portrait", "film-closing-v3-portrait.commands.json"),
    ]
    for width, height, name, filename in outputs:
        builder = FilmClosingPlan(width, height, name)
        plan = builder.build()
        serialized = json.dumps(plan, indent=2) + "\n"
        roundtrip_plan = json.loads(serialized)
        builder.validate_waveform_partitions(roundtrip_plan["steps"])
        roundtrip_audit = builder.audit_native_requests(roundtrip_plan["steps"])
        if roundtrip_audit != plan["nativeRequestAudit"]:
            raise AssertionError("Serialized plan changed native request sizes")
        path = ROOT / filename
        path.write_text(serialized)
        audit = plan["nativeRequestAudit"]
        print(f"{path}: {len(plan['steps'])} native commands, "
              f"{len(plan['sourceAssets'])} source assets, "
              f"max full MCP request {audit['maxRequestBytes']} bytes "
              f"({audit['maxRequestStepId']}), "
              f"{plan['waveformPartition']['waveformClipCount']} waveform clips")
