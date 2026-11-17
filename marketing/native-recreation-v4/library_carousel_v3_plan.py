"""Build duration-safe v3 native plans for Library and Carousel.

This layer keeps the established v2 composition intact while correcting
clip-local animation timing and ensuring generated keyframes end at or before
their target clip's duration. It writes only the v3 command plans and never
contacts Looksy or writes Timeline project internals.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import library_carousel_v2_plan as v2

ROOT = Path(__file__).resolve().parent
base = v2.base

_ORIGINAL_ANIMATE = base.animate


def _step_reference(value):
    if isinstance(value, dict) and set(value) == {"result", "field"}:
        return str(value["result"])
    if isinstance(value, str):
        return value
    return ""


def _effective_clip_metadata(clip_id):
    """Read the latest authored duration/start for a clip built so far."""
    create_actions = {
        "timeline.action.createTimelineClip",
        "timeline.action.createTimelineTitle",
        "timeline.action.createTimelineGraphic",
        "timeline.action.createTimelineGenerator",
    }
    metadata = None
    for step in base.STEPS:
        if step.get("id") == clip_id and step.get("commandId") in create_actions:
            values = step.get("values", {})
            metadata = {
                "durationSeconds": float(values.get("durationSeconds", 0.0)),
                "startSeconds": float(values.get("startSeconds", 0.0)),
            }
        elif step.get("commandId") == "timeline.action.setTimelineClipTiming":
            values = step.get("values", {})
            if _step_reference(values.get("clipId")) == clip_id and metadata:
                metadata["durationSeconds"] = float(values.get(
                    "durationSeconds", metadata["durationSeconds"]))
                metadata["startSeconds"] = float(values.get(
                    "startSeconds", metadata["startSeconds"]))
    return metadata


def _frame_safe_compact_keys(duration, value_at, fps=None):
    """Sample at frame boundaries and include the exact endpoint once."""
    if fps is None:
        fps = base.FPS
    duration = float(duration)
    fps = float(fps)
    if not math.isfinite(duration) or duration <= 0 or not math.isfinite(fps) or fps <= 0:
        raise ValueError(f"Invalid animation duration/fps: {duration!r}/{fps!r}")

    frame_position = duration * fps
    nearest_frame = round(frame_position)
    if math.isclose(frame_position, nearest_frame, rel_tol=0.0, abs_tol=1e-9):
        frame_count = int(nearest_frame)
    else:
        frame_count = math.floor(frame_position)

    times = [frame / fps for frame in range(frame_count + 1)]
    if not times:
        times = [0.0]
    if math.isclose(times[-1], duration, rel_tol=0.0, abs_tol=1e-10):
        times[-1] = duration
    else:
        times.append(duration)

    keys = [{"localSeconds": t, "value": value_at(t), "interpolation": "linear"}
            for t in times]
    compact = []
    for index, key in enumerate(keys):
        if (index == 0 or index == len(keys) - 1 or
                keys[index - 1]["value"] != key["value"] or
                keys[index + 1]["value"] != key["value"]):
            compact.append(key)
    return compact


def _phase_offset(step_id, target_metadata):
    """Map source cull-relative time onto each clip's local time."""
    if ("lib-cull-" not in step_id and "p-lib-cull-" not in step_id):
        return 0.0
    if not step_id.endswith(("-selected-zoom", "-deemphasis")):
        return 0.0
    if target_metadata is None:
        return 0.0
    # Library culling starts at 67s. Status text starts at 69s and the
    # selected outline at 68.8s, so their local timelines begin at those
    # corresponding source phases (2.0s and 1.8s).
    return max(0.0, target_metadata["startSeconds"] - 67.0)


def _duration_safe_animate(step_id, clip, requested_duration, channels):
    clip_id = _step_reference(clip)
    metadata = _effective_clip_metadata(clip_id)
    duration = float(requested_duration)
    phase = _phase_offset(step_id, metadata)
    if metadata is not None:
        actual_duration = metadata["durationSeconds"]
        if actual_duration <= 0:
            raise ValueError(f"Animation {step_id} targets a nonpositive clip duration")
        duration = min(duration, actual_duration)

    shifted = {
        parameter: (lambda t, fn=value_at, offset=phase: fn(t + offset))
        for parameter, value_at in channels.items()
    }
    _ORIGINAL_ANIMATE(step_id, clip, duration, shifted)


def _resolve_plan_clip_id(value):
    return _step_reference(value)


def validate_animation_durations(plan):
    """Fail if any channel targets an unknown clip or exceeds its duration."""
    create_actions = {
        "timeline.action.createTimelineClip",
        "timeline.action.createTimelineTitle",
        "timeline.action.createTimelineGraphic",
        "timeline.action.createTimelineGenerator",
    }
    clips = {}
    for step in plan.get("steps", []):
        if step.get("commandId") not in create_actions:
            continue
        values = step.get("values", {})
        clips[step["id"]] = {
            "durationSeconds": float(values.get("durationSeconds", 0.0)),
            "startSeconds": float(values.get("startSeconds", 0.0)),
        }

    checked = 0
    for step in plan.get("steps", []):
        command_id = step.get("commandId")
        values = step.get("values", {})
        if command_id == "timeline.action.setTimelineClipTiming":
            clip_id = _resolve_plan_clip_id(values.get("clipId"))
            if clip_id not in clips:
                raise ValueError(f"Timing edit {step['id']} references unknown clip {clip_id}")
            if "durationSeconds" in values:
                clips[clip_id]["durationSeconds"] = float(values["durationSeconds"])
            if "startSeconds" in values:
                clips[clip_id]["startSeconds"] = float(values["startSeconds"])
            continue
        if command_id != "timeline.action.setTimelineClipAnimation":
            continue

        clip_id = _resolve_plan_clip_id(values.get("clipId"))
        if clip_id not in clips:
            raise ValueError(f"Animation {step['id']} references unknown clip {clip_id}")
        duration = clips[clip_id]["durationSeconds"]
        if duration <= 0:
            raise ValueError(f"Animation {step['id']} targets invalid duration {duration}")
        for channel in values.get("channels", []):
            frames = channel.get("keyframes", [])
            if not frames:
                raise ValueError(f"Animation {step['id']} has an empty {channel.get('parameter')} channel")
            endpoints = [float(frame["localSeconds"]) for frame in frames]
            if any(not math.isfinite(endpoint) or endpoint < 0 for endpoint in endpoints):
                raise ValueError(f"Animation {step['id']} has an invalid local time")
            endpoint = max(endpoints)
            if endpoint > duration + 1e-10:
                raise ValueError(
                    f"Animation {step['id']} channel {channel.get('parameter')} ends at "
                    f"{endpoint:.9f}s beyond clip {clip_id} duration {duration:.9f}s")
            checked += 1
    return {"animationsChecked": checked, "clipsChecked": len(clips)}


def build_landscape():
    base.animate = _duration_safe_animate
    base.compact_keys = _frame_safe_compact_keys
    plan = v2.build_landscape()
    plan["planVersion"] = 3
    plan["timingAudit"] = validate_animation_durations(plan)
    plan["nativeBlockers"] = list(plan.get("nativeBlockers", [])) + [
        "Library cull animations are authored in clip-local time and capped at the target clip duration; source UI and rendered native timing still require visual review.",
    ]
    return plan


def build_portrait():
    base.animate = _duration_safe_animate
    base.compact_keys = _frame_safe_compact_keys
    plan = v2.build_portrait()
    plan["planVersion"] = 3
    plan["timingAudit"] = validate_animation_durations(plan)
    plan["nativeBlockers"] = list(plan.get("nativeBlockers", [])) + [
        "Library cull animations are authored in clip-local time and capped at the target clip duration; source UI and rendered native timing still require visual review.",
    ]
    return plan


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--orientation", choices=["landscape", "portrait", "both"], default="both")
    args = parser.parse_args()
    outputs = []
    if args.orientation in ("landscape", "both"):
        plan = build_landscape()
        plan["assetAudit"] = v2._audit_references(plan)
        target = ROOT / "library-carousel-v3-landscape.commands.json"
        target.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n")
        outputs.append((target, len(plan["steps"]), plan["timingAudit"],
                        plan["assetAudit"]["uniqueMediaPaths"]))
    if args.orientation in ("portrait", "both"):
        plan = build_portrait()
        plan["assetAudit"] = v2._audit_references(plan)
        target = ROOT / "library-carousel-v3-portrait.commands.json"
        target.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n")
        outputs.append((target, len(plan["steps"]), plan["timingAudit"],
                        plan["assetAudit"]["uniqueMediaPaths"]))
    for path, count, timing, assets in outputs:
        print(path, count, "commands", timing["animationsChecked"],
              "animation channels validated", assets, "exact manifest assets")


if __name__ == "__main__":
    main()
