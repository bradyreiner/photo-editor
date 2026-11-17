#!/usr/bin/env python3
"""Benchmark the saved P8 112-title graph against affine sparse-output paths.

The baseline keeps the native full transparent pad/crop for every title. The
candidate uses pre-cropped inputs at scale 1 and crops only after the exact
native full-canvas scale for other affine titles; the opaque source and the
animated-corner title stay on the original full path. This harness is isolated
to the two frames covered by the preserved 112-input render snapshot.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import struct
import time

from affine_roi_probe import get_clip_rows, parameter_value, png_size

EXPERIMENT = Path(__file__).resolve().parents[1]
OUT = Path(__file__).resolve().parent / "112-affine-benchmark"
PROJECT = Path(__file__).resolve().parents[2] / "native-projects" / "looksy-polish-pass8.json"
W, H, FPS = 1920, 1080, 30
SUPPORTED_FRAMES = {1337, 1349}


def rounded_crop_origin(value: float) -> int:
    # FFmpeg crop uses nearest integer coordinates; values here are positive.
    return math.floor(value + 0.5)


def ff_num(value: float) -> str:
    return f"{value:.8f}"


def radius_filter(radius: float) -> str:
    r = ff_num(radius)
    return (
        "format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':"
        f"a='alpha(X,Y)*clip(0.5-(sqrt(pow(max(abs(X+0.5-W/2)-W/2+({r})*min(W,H),0),2)+"
        f"pow(max(abs(Y+0.5-H/2)-H/2+({r})*min(W,H),0),2))+"
        f"min(max(abs(X+0.5-W/2)-W/2+({r})*min(W,H),"
        f"abs(Y+0.5-H/2)-H/2+({r})*min(W,H)),0)-({r})*min(W,H)),0,1)',"
    )


def load_rows():
    entries = get_clip_rows()
    fixture_clips = json.loads((EXPERIMENT / "fixtures" / "clips.json").read_text())
    rows = [entries[c["clipId"]] for c in fixture_clips]
    if len(rows) != 112:
        raise ValueError("expected the exact saved 112-title P8 fixture")
    return rows


def row_values(entry: dict, frame: int) -> dict:
    clip, track = entry["clip"], entry["track"]
    sec = frame / FPS
    local = sec - float(clip.get("startSeconds", 0.0))
    scale = parameter_value(clip, "frameScale", local, "frameScale", 1.0)
    offset_x = parameter_value(clip, "frameOffsetX", local, "frameOffsetX", 0.0)
    offset_y = parameter_value(clip, "frameOffsetY", local, "frameOffsetY", 0.0)
    opacity = parameter_value(clip, "opacity", local, "opacity", 1.0)
    track_opacity = float(track.get("opacity", 1.0))
    return {
        "scale": scale,
        "offsetX": offset_x,
        "offsetY": offset_y,
        "clipOpacity": opacity,
        "trackOpacity": track_opacity,
        "opacity": min(1.0, max(0.0, opacity * track_opacity)),
        "localSeconds": local,
    }


def fallback_reason(entry: dict) -> str | None:
    clip, track = entry["clip"], entry["track"]
    if not entry["is_sparse"]:
        return "opaque-or-full-canvas-source"
    if clip.get("frameMode", "stretch") != "stretch":
        return "non-stretch-frame-mode"
    if abs(float(clip.get("frameWidth", 1.0)) - 1.0) > 1e-9 or abs(float(clip.get("frameHeight", 1.0)) - 1.0) > 1e-9:
        return "non-full-frame-geometry"
    if [float(clip.get(k, default)) for k, default in (
        ("sourceCropLeft", 0.0), ("sourceCropTop", 0.0),
        ("sourceCropRight", 1.0), ("sourceCropBottom", 1.0),
    )] != [0.0, 0.0, 1.0, 1.0]:
        return "source-crop"
    if clip.get("effectStack") or clip.get("clipMask") or clip.get("videoInputTransform") or clip.get("colorGradeRecipeJson"):
        return "effects-mask-or-grade"
    if clip.get("framePerspectiveQuad") or clip.get("frameShadowEnabled") or clip.get("frameShadow"):
        return "perspective-or-shadow"
    if abs(float(clip.get("frameRotationDegrees", 0.0))) > 1e-9:
        return "rotation"
    forbidden = {
        "frameRotationDegrees", "framePerspectiveX", "framePerspectiveY",
        "frameCornerRadius", "titlePositionX", "titlePositionY", "titleFontSize",
    }
    if any(k.get("parameter") in forbidden for k in clip.get("keyframes", [])):
        return "unsupported-geometry-curve"
    if any(k.get("parameter") == "opacity" for k in track.get("keyframes", [])):
        return "animated-track-opacity"
    if any(k.get("parameter") == "opacity" for k in track.get("automationKeyframes", [])):
        return "animated-track-opacity"
    if any(abs(float(track.get(k, default)) - default) > 1e-9 for k, default in (
        ("groupTransformScale", 1.0), ("groupTransformOffsetX", 0.0),
        ("groupTransformOffsetY", 0.0), ("groupTransformRotationDegrees", 0.0),
    )):
        return "track-group-transform"
    if track.get("blendMode", "normal") not in ("normal", "source-over") or clip.get("blendMode", "normal") not in ("normal", "source-over"):
        return "non-normal-blend"
    if clip.get("mediaType") not in ("title", "caption"):
        return "unsupported-media-type"
    return None


def crop_geometry(entry: dict, values: dict, frame: int) -> dict:
    clip = entry["clip"]
    scale = values["scale"]
    # FFmpeg scale rounds down for these rgba dimensions; verify the scale
    # expression and explicit dimensions were identical in affine probes.
    scaled_w, scaled_h = math.floor(W * scale), math.floor(H * scale)
    if scaled_w <= 0 or scaled_h <= 0:
        raise ValueError(f"clip {clip['clipId']} scales to a nonpositive frame")
    animated_scale = any(k.get("parameter") == "frameScale" for k in clip.get("keyframes", []))
    ref_w, ref_h = (math.floor(W * scale), math.floor(H * scale)) if animated_scale else (scaled_w, scaled_h)
    offset_x_px = W * values["offsetX"] * 0.25
    offset_y_px = H * values["offsetY"] * 0.25
    crop_x = (ref_w + W) / 2.0 - offset_x_px
    crop_y = (ref_h + H) / 2.0 - offset_y_px

    alpha_w, alpha_h = png_size(entry["sparse"])
    # Use exact source alpha bounds with eight scaled pixels of support. This
    # crop is downstream of the native scale, so it does not change sampling
    # phase or coefficient rounding.
    roi_x = max(0, math.floor(entry["alpha_x"] * scaled_w / W) - 8)
    roi_y = max(0, math.floor(entry["alpha_y"] * scaled_h / H) - 8)
    roi_end_x = min(scaled_w, math.ceil((entry["alpha_x"] + alpha_w) * scaled_w / W) + 8)
    roi_end_y = min(scaled_h, math.ceil((entry["alpha_y"] + alpha_h) * scaled_h / H) + 8)
    return {
        "scaledWidth": scaled_w,
        "scaledHeight": scaled_h,
        "cropX": crop_x,
        "cropY": crop_y,
        "cropPixelX": rounded_crop_origin(crop_x),
        "cropPixelY": rounded_crop_origin(crop_y),
        "offsetXPixels": offset_x_px,
        "offsetYPixels": offset_y_px,
        "roiX": roi_x,
        "roiY": roi_y,
        "roiWidth": max(1, roi_end_x - roi_x),
        "roiHeight": max(1, roi_end_y - roi_y),
        "overlayX": W + roi_x - rounded_crop_origin(crop_x),
        "overlayY": H + roi_y - rounded_crop_origin(crop_y),
    }


def baseline_filter(entry: dict, values: dict, geom: dict, output_label: str, input_index: int) -> str:
    clip = entry["clip"]
    parts = [f"[{input_index}:v]scale={W}:{H},format=rgba,"]
    radius_keys = any(k.get("parameter") == "frameCornerRadius" for k in clip.get("keyframes", []))
    if radius_keys or float(clip.get("frameCornerRadius", 0.0)) > 1e-8:
        radius = parameter_value(clip, "frameCornerRadius", values["localSeconds"], "frameCornerRadius", 0.0)
        parts.append(radius_filter(radius))
    parts.append(
        f"scale={geom['scaledWidth']}:{geom['scaledHeight']}:eval=frame:flags=bicubic,"
        f"pad=iw+{2*W}:ih+{2*H}:{W}:{H}:color=black@0:eval=frame,"
        f"crop={W}:{H}:'{geom['cropX']:.12f}':'{geom['cropY']:.12f}',"
        f"setsar=1,colorchannelmixer=aa={values['opacity']:.8f},format=rgba[{output_label}]"
    )
    return "".join(parts)


def direct_candidate_filter(entry: dict, values: dict, geom: dict, output_label: str, input_index: int) -> tuple[str, int, int]:
    # Full-scale==1 is a no-op on the authored RGBA pixels; use the exact saved
    # alpha-cropped PNG and retain its source-space origin.
    filter_text = (
        f"[{input_index}:v]format=rgba,colorchannelmixer=aa={values['opacity']:.8f},"
        f"format=rgba[{output_label}]"
    )
    return filter_text, entry["alpha_x"] + W - geom["cropPixelX"], entry["alpha_y"] + H - geom["cropPixelY"]


def post_scale_candidate_filter(entry: dict, values: dict, geom: dict, output_label: str, input_index: int) -> tuple[str, int, int]:
    filter_text = (
        f"[{input_index}:v]scale={W}:{H},format=rgba,"
        f"scale={geom['scaledWidth']}:{geom['scaledHeight']}:eval=frame:flags=bicubic,"
        f"crop={geom['roiWidth']}:{geom['roiHeight']}:{geom['roiX']}:{geom['roiY']},"
        f"setsar=1,colorchannelmixer=aa={values['opacity']:.8f},format=rgba[{output_label}]"
    )
    return filter_text, geom["overlayX"], geom["overlayY"]


def command(frame: int, kind: str):
    rows = load_rows()
    args = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-filter_complex_threads", "1"]
    values_by_row, geometry_by_row, candidate_paths = [], [], []
    counts = {"directSparse": 0, "postScaleCrop": 0, "fullFallback": 0}
    estimated = {"baselinePaddedFrameBytes": 0, "candidateScaleFrameBytes": 0, "candidateTileBytes": 0}

    for entry in rows:
        values = row_values(entry, frame)
        geom = crop_geometry(entry, values, frame)
        reason = fallback_reason(entry)
        if kind == "baseline":
            path = entry["source"]
        elif reason is not None:
            path = entry["source"]
            counts["fullFallback"] += 1
        elif abs(values["scale"] - 1.0) <= 1e-9:
            path = entry["sparse"]
            counts["directSparse"] += 1
        else:
            path = entry["source"]
            counts["postScaleCrop"] += 1
        if not path.is_file():
            raise ValueError(f"missing input fixture: {path}")
        args += ["-loop", "1", "-framerate", str(FPS), "-i", str(path)]
        values_by_row.append(values)
        geometry_by_row.append(geom)
        candidate_paths.append(path)
        estimated["baselinePaddedFrameBytes"] += (geom["scaledWidth"] + 2 * W) * (geom["scaledHeight"] + 2 * H) * 4
        if kind == "candidate":
            if reason is not None:
                estimated["candidateScaleFrameBytes"] += (geom["scaledWidth"] + 2 * W) * (geom["scaledHeight"] + 2 * H) * 4
            elif abs(values["scale"] - 1.0) <= 1e-9:
                cw, ch = png_size(entry["sparse"])
                estimated["candidateTileBytes"] += cw * ch * 4
            else:
                estimated["candidateScaleFrameBytes"] += geom["scaledWidth"] * geom["scaledHeight"] * 4
                estimated["candidateTileBytes"] += geom["roiWidth"] * geom["roiHeight"] * 4

    chains, overlays = [], []
    for index, (entry, values, geom) in enumerate(zip(rows, values_by_row, geometry_by_row)):
        label = f"t{index}"
        if kind == "baseline":
            chains.append(baseline_filter(entry, values, geom, label, index))
            overlay_x = overlay_y = 0
        else:
            reason = fallback_reason(entry)
            if reason is not None:
                chains.append(baseline_filter(entry, values, geom, label, index))
                overlay_x = overlay_y = 0
            elif abs(values["scale"] - 1.0) <= 1e-9:
                chain, overlay_x, overlay_y = direct_candidate_filter(entry, values, geom, label, index)
                chains.append(chain)
            else:
                chain, overlay_x, overlay_y = post_scale_candidate_filter(entry, values, geom, label, index)
                chains.append(chain)
        if index == 0:
            chains.append("[t0]null[base0]")
            continue
        previous, output = f"base{index-1}", f"base{index}"
        overlays.append(
            f"[{previous}][{label}]overlay=x={overlay_x}:y={overlay_y}:shortest=1:eof_action=pass:format=auto[{output}]"
        )
    graph = ";".join(chains + overlays)
    result_path = OUT / f"frame-{frame}-{kind}.framemd5"
    args += [
        "-filter_complex", graph, "-map", f"[base{len(rows)-1}]", "-frames:v", "1",
        "-pix_fmt", "rgba", "-f", "framemd5", "-y", str(result_path),
    ]
    return args, result_path, counts, estimated


def frame_record(path: Path) -> str | None:
    lines = [line for line in path.read_text().splitlines() if line and not line.startswith("#")]
    return lines[-1] if lines else None


def run(frame: int, kind: str) -> dict:
    args, result_path, counts, estimates = command(frame, kind)
    OUT.mkdir(parents=True, exist_ok=True)
    stderr_path = OUT / f"frame-{frame}-{kind}.stderr.txt"
    start = time.perf_counter()
    pid = os.fork()
    if pid == 0:
        with stderr_path.open("wb") as stderr_file:
            os.dup2(stderr_file.fileno(), 2)
            os.execvp(args[0], args)
    _pid, status, usage = os.wait4(pid, 0)
    elapsed = time.perf_counter() - start
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
        raise RuntimeError(f"{kind} frame {frame} failed; inspect {stderr_path}")
    result = {
        "kind": kind,
        "frame": frame,
        "seconds": frame / FPS,
        "wallSeconds": round(elapsed, 3),
        "maxRssBytes": int(usage.ru_maxrss),
        "frameRecord": frame_record(result_path),
        "counts": counts,
        "estimatedWorkingBytes": estimates,
        "stderrPath": str(stderr_path),
    }
    (OUT / f"frame-{frame}-{kind}.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--frame", type=int, action="append", choices=sorted(SUPPORTED_FRAMES))
    parser.add_argument("--kind", choices=("both", "baseline", "candidate"), default="both")
    args = parser.parse_args()
    frames = args.frame or [1337, 1349]
    OUT.mkdir(parents=True, exist_ok=True)
    all_runs = []
    for frame in frames:
        kinds = ("baseline", "candidate") if args.kind == "both" else (args.kind,)
        frame_runs = [run(frame, kind) for kind in kinds]
        if len(frame_runs) == 1:
            for existing_kind in ("baseline", "candidate"):
                path = OUT / f"frame-{frame}-{existing_kind}.json"
                if path.is_file() and not any(item.get("kind") == existing_kind and item.get("frame") == frame for item in frame_runs):
                    existing = json.loads(path.read_text())
                    if existing not in frame_runs:
                        frame_runs.append(existing)
        frame_runs.sort(key=lambda item: item["kind"])
        if len(frame_runs) == 2:
            frame_runs[0]["exactFrameHashMatch"] = frame_runs[0]["frameRecord"] == frame_runs[1]["frameRecord"]
            frame_runs[1]["exactFrameHashMatch"] = frame_runs[0]["exactFrameHashMatch"]
        all_runs.extend(frame_runs)
        print(json.dumps({"frame": frame, "runs": frame_runs}, sort_keys=True))
    result = {
        "projectSha256": __import__("hashlib").sha256(PROJECT.read_bytes()).hexdigest(),
        "snapshotPath": "/private/tmp/looksy-dense-timeline-v7iaxqgh/data/timelines/documents/render-snapshots/3db4e4fb-6737-415c-b6ee-dde766e7c237-3ade052f-994e-4a3e-a669-7413f4927782.json",
        "snapshotSha256": "16e930e2e2d74566185eb464cf30884fc11547b2a10710df8c8edf9b71dcf860",
        "scope": "112 title/caption inputs active in the preserved [1320,1350) render snapshot. At 1350 and later the project has 114 active titles, outside this fixture.",
        "runs": all_runs,
        "limitations": [
            "This title-only graph uses the exact saved 112 PNGs/order and P8 curves; it is not a full native project render.",
            "Single-frame comparisons specialize each curve at that global frame; dynamic 30-frame crop parity is separately tested on one representative layer.",
            "The animated-corner clip and fully opaque source use the original full-canvas path as explicit fallbacks.",
            "The byte estimates are per-input graph-frame totals, not a predicted process peak; max RSS is measured per FFmpeg child.",
        ],
    }
    (OUT / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    if any(run.get("exactFrameHashMatch") is False for run in all_runs):
        raise SystemExit(2)


if __name__ == "__main__":
    main()
