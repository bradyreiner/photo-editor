"""Duration- and range-safe v3 Canvas/Agent native Timeline plans.

Canvas Stage motion on titles is converted from animated ``titlePositionY`` to
the native absolute ``frameOffsetY`` channel around each title's fixed base
anchor. Existing frame-offset motion is composed into that channel. The
builder is offline and writes only v3 plan artifacts.
"""
from __future__ import annotations

import argparse
import copy
import json
import math
from pathlib import Path

import canvas_agent_v2_plan as v2

ROOT = Path(__file__).resolve().parent


def _clip_ref(value):
    if isinstance(value, dict) and set(value) == {"result", "field"}:
        return str(value["result"])
    if isinstance(value, str):
        return value
    return ""


def _keyframe_value(keyframes, local_seconds, default):
    """Evaluate the linear/hold curves emitted by the native plan builder."""
    if not keyframes:
        return float(default)
    keys = sorted(keyframes, key=lambda key: float(key["localSeconds"]))
    for key in keys:
        mode = key.get("interpolation", "linear")
        if mode not in ("linear", "hold", ""):
            raise ValueError(f"Unsupported interpolation in position oracle: {mode}")
    t = float(local_seconds)
    if t <= float(keys[0]["localSeconds"]):
        return float(keys[0]["value"])
    if t >= float(keys[-1]["localSeconds"]):
        return float(keys[-1]["value"])
    for left, right in zip(keys, keys[1:]):
        t0 = float(left["localSeconds"])
        t1 = float(right["localSeconds"])
        if t <= t1:
            v0 = float(left["value"])
            if left.get("interpolation", "linear") == "hold":
                return v0
            v1 = float(right["value"])
            if t1 <= t0:
                raise ValueError("Position track contains duplicate keyframe times")
            progress = (t - t0) / (t1 - t0)
            return v0 + (v1 - v0) * progress
    return float(keys[-1]["value"])


def _channel_for_parameter(step, parameter):
    found = [channel for channel in step.get("values", {}).get("channels", [])
             if channel.get("parameter") == parameter]
    if len(found) > 1:
        raise ValueError(f"Action {step.get('id')} duplicates channel {parameter}")
    return found[0] if found else None


def _clip_context(plan):
    creators = {}
    frames = {}
    animation_steps = {}
    for index, step in enumerate(plan["steps"]):
        command = step.get("commandId", "")
        if command in ("timeline.action.createTimelineTitle",
                       "timeline.action.createTimelineGraphic"):
            creators[step["id"]] = step
        elif command == "timeline.action.setTimelineClipFrame":
            clip_id = _clip_ref(step.get("values", {}).get("clipId"))
            if clip_id:
                frames.setdefault(clip_id, []).append((index, step))
        elif command == "timeline.action.setTimelineClipAnimation":
            clip_id = _clip_ref(step.get("values", {}).get("clipId"))
            if clip_id:
                animation_steps.setdefault(clip_id, []).append((index, step))
    return creators, frames, animation_steps


def _base_position_y(creator):
    try:
        return float(creator["values"]["values"]["positionY"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"Positionable clip {creator.get('id')} has no finite base positionY") from exc


def _static_frame_offset_y(frame_steps):
    if not frame_steps:
        return 0.0
    return float(frame_steps[-1][1].get("values", {}).get("offsetY", 0.0))


def _effective_curve(animation_steps, parameter):
    """Return the last authored channel, matching native setter semantics."""
    for _, step in reversed(animation_steps):
        channel = _channel_for_parameter(step, parameter)
        if channel is not None:
            return channel
    return None


def _all_curve_times(*channels):
    times = set()
    for channel in channels:
        if channel is None:
            continue
        for key in channel.get("keyframes", []):
            time = float(key["localSeconds"])
            if not math.isfinite(time) or time < 0:
                raise ValueError("Position channel has a non-finite or negative keyframe time")
            times.add(time)
    return sorted(times)


def _compose_title_y_channels(plan):
    creators, frame_steps, animation_steps = _clip_context(plan)
    transformed = []
    changed_step_ids = set()

    # Capture all effective source curves before mutating the plan. Native
    # title clips and graphic-shape clips share the same QML anchor projection.
    for clip_id, creator in creators.items():
        clip_animations = animation_steps.get(clip_id, [])
        source_y = _effective_curve(clip_animations, "titlePositionY")
        if source_y is None:
            continue

        base_y = _base_position_y(creator)
        source_offset = _effective_curve(clip_animations, "frameOffsetY")
        if source_offset is None:
            static_offset = _static_frame_offset_y(frame_steps.get(clip_id, []))
            offset_keys = None
        else:
            # A keyframed value is absolute and supersedes the clip's static
            # frameOffsetY, so preserve the last effective animation curve.
            static_offset = 0.0
            offset_keys = source_offset.get("keyframes", [])
        y_keys = source_y.get("keyframes", [])
        if not y_keys:
            raise ValueError(f"Title {clip_id} has an empty titlePositionY channel")
        if any(key.get("interpolation", "linear") not in ("linear", "hold", "")
               for key in y_keys + (offset_keys or [])):
            raise ValueError(f"Title {clip_id} uses unsupported position interpolation")

        times = _all_curve_times(source_y, source_offset)
        if not times:
            raise ValueError(f"Title {clip_id} has no position samples")
        composed = []
        for local_seconds in times:
            desired_y = _keyframe_value(y_keys, local_seconds, base_y)
            existing_offset = _keyframe_value(offset_keys, local_seconds, static_offset)
            # QML computes desiredY from the fixed title anchor, then adds
            # frameOffsetY * parent.height * 0.25. Therefore this absolute
            # channel exactly composes Stage movement and any prior offset.
            offset = 4.0 * (desired_y - base_y) + existing_offset
            if not math.isfinite(offset) or offset < -4.0 - 1e-9 or offset > 4.0 + 1e-9:
                raise ValueError(f"Title {clip_id} mapped frameOffsetY outside native limits: {offset}")
            composed.append({"localSeconds": local_seconds, "value": offset,
                             "interpolation": "linear"})

        # Remove all earlier writers for these two parameters and place the
        # composed absolute channel at the last writer. This mirrors native
        # last-channel-wins behavior and prevents a later stale offset action
        # from replacing the converted trajectory.
        relevant_steps = []
        for index, step in clip_animations:
            channels = step.get("values", {}).get("channels", [])
            kept = [channel for channel in channels
                    if channel.get("parameter") not in ("titlePositionY", "frameOffsetY")]
            if len(kept) != len(channels):
                relevant_steps.append((index, step, kept))
        if not relevant_steps:
            raise ValueError(f"Title {clip_id} position channel had no owning action")
        writer_index = max(index for index, _, _ in relevant_steps)
        writer = next(step for index, step, _ in relevant_steps if index == writer_index)

        # Retain non-position channels exactly and remove only now-empty
        # animation actions. Re-index after collection below.
        for _, step, kept in relevant_steps:
            step["values"]["channels"] = kept
            changed_step_ids.add(step["id"])
        writer["values"]["channels"].append({"parameter": "frameOffsetY",
                                               "keyframes": composed})
        transformed.append({"clipId": clip_id, "actionType": creator["commandId"],
                            "basePositionY": base_y,
                            "sourceKeyframeCount": len(y_keys),
                            "existingOffsetKeyframeCount": len(offset_keys or []),
                            "composedKeyframeCount": len(composed)})

    plan["steps"] = [step for step in plan["steps"]
                     if not (step.get("commandId") == "timeline.action.setTimelineClipAnimation"
                             and not step.get("values", {}).get("channels"))]
    plan["frameOffsetYMapping"] = {
        "mapping": "frameOffsetY = 4 * (desiredTitlePositionY - fixedBaseTitlePositionY) + existingFrameOffsetY",
        "nativeQmlProjection": "title y = (titlePositionY * canvasHeight - titleHeight / 2) + frameOffsetY * canvasHeight / 4",
        "transformedPositionableClipCount": len(transformed),
        "transformedClips": transformed,
        "removedEmptyAnimationActions": len(changed_step_ids) - len(transformed),
    }
    return plan


def _evaluate_effective_channel(animation_steps, parameter, local_seconds, default):
    channel = _effective_curve(animation_steps, parameter)
    return _keyframe_value(channel.get("keyframes", []), local_seconds, default) if channel else float(default)


def _sample_frame_times(duration, fps):
    count = math.floor(duration * fps + 1e-10)
    times = [frame / fps for frame in range(count + 1)]
    if times and math.isclose(times[-1], duration, rel_tol=0.0, abs_tol=1e-10):
        times[-1] = duration
    elif not times or times[-1] < duration:
        times.append(duration)
    return times


def validate_title_position_oracle(source_plan, mapped_plan):
    """Compare source desired title centers with native composed centers per frame."""
    source_creators, source_frames, source_animations = _clip_context(source_plan)
    mapped_creators, mapped_frames, mapped_animations = _clip_context(mapped_plan)
    if set(source_creators) != set(mapped_creators):
        raise ValueError("Canvas Agent v3 changed title clip identities")

    fps = float(source_plan.get("composition", {}).get("fps", 30))
    errors = []
    frame_count = 0
    title_count = 0
    for clip_id, source_creator in source_creators.items():
        mapped_creator = mapped_creators[clip_id]
        source_base = _base_position_y(source_creator)
        mapped_base = _base_position_y(mapped_creator)
        if not math.isclose(source_base, mapped_base, rel_tol=0.0, abs_tol=1e-12):
            raise ValueError(f"Title {clip_id} static anchor changed")
        source_steps = source_animations.get(clip_id, [])
        mapped_steps = mapped_animations.get(clip_id, [])
        source_y_channel = _effective_curve(source_steps, "titlePositionY")
        mapped_y_channel = _effective_curve(mapped_steps, "titlePositionY")
        if mapped_y_channel is not None:
            raise ValueError(f"Title {clip_id} still animates titlePositionY")

        source_static_offset = _static_frame_offset_y(source_frames.get(clip_id, []))
        source_offset_channel = _effective_curve(source_steps, "frameOffsetY")
        mapped_static_offset = _static_frame_offset_y(mapped_frames.get(clip_id, []))
        mapped_offset_channel = _effective_curve(mapped_steps, "frameOffsetY")
        if mapped_offset_channel is None:
            # Unanimated titles are unchanged by the transform pass.
            mapped_offset_default = mapped_static_offset
        else:
            mapped_offset_default = 0.0

        duration = float(source_creator.get("values", {}).get("durationSeconds", 0.0))
        for t in _sample_frame_times(duration, fps):
            desired_y = _evaluate_effective_channel(source_steps, "titlePositionY", t, source_base)
            source_offset = _evaluate_effective_channel(
                source_steps, "frameOffsetY", t, source_static_offset)
            actual_offset = _evaluate_effective_channel(
                mapped_steps, "frameOffsetY", t, mapped_offset_default)
            expected_composed = desired_y + source_offset / 4.0
            actual_composed = mapped_base + actual_offset / 4.0
            errors.append(abs(actual_composed - expected_composed))
            frame_count += 1
        title_count += 1

    maximum_error = max(errors, default=0.0)
    if maximum_error > 1e-10:
        raise ValueError(f"Canvas Agent title-position oracle mismatch: {maximum_error}")
    return {"positionableClipsChecked": title_count, "frameSamples": frame_count,
            "maximumNormalizedPositionError": maximum_error}


def _validate_native_ranges(plan):
    ranges = {"titlePositionY": (0.0, 1.0), "frameOffsetY": (-4.0, 4.0),
              "titleFontSize": (1.0, 360.0)}
    counts = {parameter: 0 for parameter in ranges}
    for step in plan["steps"]:
        if step.get("commandId") != "timeline.action.setTimelineClipAnimation":
            continue
        for channel in step.get("values", {}).get("channels", []):
            parameter = channel.get("parameter")
            bounds = ranges.get(parameter)
            if bounds is None:
                continue
            for key in channel.get("keyframes", []):
                value = float(key["value"])
                if not math.isfinite(value) or value < bounds[0] - 1e-9 or value > bounds[1] + 1e-9:
                    raise ValueError(f"{step['id']} {parameter}={value} outside {bounds}")
                counts[parameter] += 1
    return counts


def build(orientation):
    source_plan = v2._write_plan(orientation)
    plan = copy.deepcopy(source_plan)
    plan = _compose_title_y_channels(plan)
    plan["planVersion"] = 3
    plan["frameOffsetYOracle"] = validate_title_position_oracle(source_plan, plan)
    plan["nativeRangeAudit"] = _validate_native_ranges(plan)
    return plan


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--orientation", choices=("landscape", "portrait", "both"), default="both")
    args = parser.parse_args()
    outputs = []
    for orientation in ("landscape", "portrait"):
        if args.orientation not in ("both", orientation):
            continue
        plan = build(orientation)
        path = ROOT / f"canvas-agent-v3-{orientation}.commands.json"
        path.write_text(json.dumps(plan, indent=2, ensure_ascii=False) + "\n")
        outputs.append((path, len(plan["steps"]), plan["frameOffsetYOracle"],
                        plan["nativeRangeAudit"]))
    for path, commands, oracle, ranges in outputs:
        print(path, commands, "commands", oracle["positionableClipsChecked"],
              "positionable clips", oracle["frameSamples"], "frame samples",
              "max position error", oracle["maximumNormalizedPositionError"],
              "range counts", ranges)


if __name__ == "__main__":
    main()
