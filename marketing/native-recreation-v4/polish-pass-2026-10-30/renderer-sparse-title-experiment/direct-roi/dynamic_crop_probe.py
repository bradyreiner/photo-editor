#!/usr/bin/env python3
"""Compare 30 animated P8 frames with a fixed-size post-scale ROI crop.

The full branch mirrors the native title geometry (scale, transparent pad,
center/offset crop). The candidate keeps the identical full-canvas scale, then
uses a frame-evaluated crop with a fixed ROI size and places that ROI directly
on the sequence canvas. A difference image over an opaque base must be black.
"""
from __future__ import annotations

import json
import gzip
import hashlib
import math
from pathlib import Path
import subprocess
import tempfile
import time

from affine_roi_probe import get_clip_rows, parameter_value, png_size

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(__file__).resolve().parent / "affine-probe"
FRAME_FIRST, FRAME_LAST, FPS = 1323, 1352, 30
CLIP_ID = "c78c3eda-188a-4a75-85b0-e8212b0f1869"
W, H = 1920, 1080


def n(value: float) -> str:
    return f"{value:.8f}"


def keyframes(clip: dict, parameter: str) -> list[dict]:
    return sorted(
        (k for k in clip.get("keyframes", []) if k.get("parameter") == parameter),
        key=lambda k: float(k["seconds"]),
    )


def native_curve_expression(clip: dict, parameter: str, sequence_start: float, base_field: str, default: float) -> str:
    points = keyframes(clip, parameter)
    if not points:
        return n(float(clip.get(base_field, default)))
    if len(points) == 1:
        return n(float(points[0]["value"]))
    shift = sequence_start - float(clip["startSeconds"])
    clock = f"(t+{n(shift)})"

    def segment(index: int) -> str:
        left, right = points[index], points[index + 1]
        left_time, right_time = float(left["seconds"]), float(right["seconds"])
        span = max(1e-6, right_time - left_time)
        progress = f"(({clock}-{n(left_time)})/{n(span)})"
        mode = left.get("interpolation", "linear")
        if mode == "hold":
            shaped = "0"
        elif mode == "ease-in":
            shaped = f"({progress}*{progress})"
        elif mode == "ease-out":
            shaped = f"(1-(1-{progress})*(1-{progress}))"
        elif mode == "ease-in-out":
            shaped = f"({progress}*{progress}*(3-2*{progress}))"
        elif mode == "linear":
            shaped = progress
        else:
            raise ValueError(f"unsupported interpolation in sample: {mode}")
        a, b = n(float(left["value"])), n(float(right["value"]))
        return f"({a}+({b}-{a})*{shaped})"

    def tree(first: int, last: int) -> str:
        if first == last:
            return segment(first)
        middle = first + (last - first) // 2
        boundary = n(float(points[middle + 1]["seconds"]))
        return f"if(lt({clock},{boundary}),{tree(first,middle)},{tree(middle+1,last)})"

    return (
        f"if(lte({clock},{n(float(points[0]['seconds']))}),{n(float(points[0]['value']))},"
        f"if(gte({clock},{n(float(points[-1]['seconds']))}),{n(float(points[-1]['value']))},"
        f"{tree(0, len(points)-2)}))"
    )


def max_scaled_alpha_extent(entry: dict, clip: dict, first_frame: int, last_frame: int) -> tuple[int, int]:
    scale_points = [
        parameter_value(clip, "frameScale", frame / FPS - float(clip["startSeconds"]), "frameScale", 1.0)
        for frame in range(first_frame, last_frame + 1)
    ]
    max_w = max_h = 0
    alpha_w, alpha_h = png_size(entry["sparse"])
    for scale in scale_points:
        # FFmpeg's rgba scale dimensions round the expression to integer.
        scaled_w = math.floor(W * scale + 0.5)
        scaled_h = math.floor(H * scale + 0.5)
        # Cover the filtered alpha box with four output pixels of support on
        # each edge. The crop width/height stay constant through the segment.
        left = math.floor(entry["alpha_x"] * scaled_w / W) - 4
        top = math.floor(entry["alpha_y"] * scaled_h / H) - 4
        right = math.ceil((entry["alpha_x"] + alpha_w) * scaled_w / W) + 4
        bottom = math.ceil((entry["alpha_y"] + alpha_h) * scaled_h / H) + 4
        max_w = max(max_w, right - left)
        max_h = max(max_h, bottom - top)
    return max_w, max_h


def main():
    entries = get_clip_rows()
    entry = entries[CLIP_ID]
    clip, track = entry["clip"], entry["track"]
    if not entry["is_sparse"]:
        raise ValueError("the dynamic affine source must be a sparse title")
    scale_samples = []
    for frame in range(FRAME_FIRST, FRAME_LAST + 1):
        values = parameter_value(clip, "opacity", frame / FPS - float(clip["startSeconds"]), "opacity", 1.0)
        if abs(values - 1.0) > 1e-8:
            raise ValueError(f"opacity is not constant 1 at frame {frame}: {values}")
        scale_samples.append(parameter_value(
            clip, "frameScale", frame / FPS - float(clip["startSeconds"]), "frameScale", 1.0
        ))
    if any(k.get("parameter") in {"titlePositionX", "titlePositionY", "titleFontSize"} for k in clip.get("keyframes", [])):
        raise ValueError("title-position/font-size curves are outside this fixture")
    if clip.get("effectStack") or clip.get("clipMask") or clip.get("framePerspectiveQuad"):
        raise ValueError("clip is not a mask/effect-free affine title")
    if any(k.get("parameter") in {"frameRotationDegrees", "framePerspectiveX", "framePerspectiveY", "frameCornerRadius"} for k in clip.get("keyframes", [])):
        raise ValueError("clip has unsupported geometry curves")

    scale = native_curve_expression(clip, "frameScale", FRAME_FIRST / FPS, "frameScale", 1.0)
    offset_x = native_curve_expression(clip, "frameOffsetX", FRAME_FIRST / FPS, "frameOffsetX", 0.0)
    offset_y = native_curve_expression(clip, "frameOffsetY", FRAME_FIRST / FPS, "frameOffsetY", 0.0)
    scaled_width = f"({W}*({scale}))"
    scaled_height = f"({H}*({scale}))"
    native_crop_x = f"((floor({scaled_width})+{W})/2-({W}*({offset_x})*0.25))"
    native_crop_y = f"((floor({scaled_height})+{H})/2-({H}*({offset_y})*0.25))"
    # Place the cropped scaled-source ROI at the same global position as its
    # pixel in the native padded/cropped clip frame. Keep extra margin around
    # the moving alpha box to cover FFmpeg's bicubic support.
    source_roi_x = f"max(0,floor(({entry['alpha_x']}*({scaled_width})/{W}))-8)"
    source_roi_y = f"max(0,floor(({entry['alpha_y']}*({scaled_height})/{H}))-8)"
    roi_w, roi_h = max_scaled_alpha_extent(entry, clip, FRAME_FIRST, FRAME_LAST)
    roi_w += 8
    roi_h += 8
    candidate_pad_w = f"iw+{roi_w}"
    candidate_pad_h = f"ih+{roi_h}"
    # Native crop coordinates are rounded to integer pixels by FFmpeg's crop
    # filter; subtract that rounded origin from the scaled-source ROI origin.
    overlay_x = f"({W}+({source_roi_x})-floor(({native_crop_x})+0.5))"
    overlay_y = f"({H}+({source_roi_y})-floor(({native_crop_y})+0.5))"

    # FFmpeg's outer filter parser respects quoted AVExpr commas. Inputs are
    # split so both branches see the same deterministic PNG frame sequence.
    graph = ";".join([
        "[0:v]split=2[full_source][roi_source]",
        f"[full_source]scale={W}:{H},format=rgba,scale='{scaled_width}':'{scaled_height}':eval=frame:flags=bicubic,"
        f"pad=iw+{2*W}:ih+{2*H}:{W}:{H}:color=black@0:eval=frame,"
        f"crop={W}:{H}:'{native_crop_x}':'{native_crop_y}',setsar=1[full_layer]",
        f"[roi_source]scale={W}:{H},format=rgba,scale='{scaled_width}':'{scaled_height}':eval=frame:flags=bicubic,"
        f"pad={candidate_pad_w}:{candidate_pad_h}:0:0:color=black@0:eval=frame,"
        f"crop={roi_w}:{roi_h}:'{source_roi_x}':'{source_roi_y}',setsar=1[roi_layer]",
        f"color=c=0x172b3c:s={W}x{H}:r={FPS}:d=2,format=rgba,split=2[full_bg][roi_bg]",
        "[full_bg][full_layer]overlay=shortest=1:eof_action=pass:format=auto[full_composite]",
        f"[roi_bg][roi_layer]overlay=x='{overlay_x}':y='{overlay_y}':eval=frame:shortest=1:eof_action=pass:format=auto[roi_composite]",
        "[full_composite]format=rgb24[full_rgb]",
        "[roi_composite]format=rgb24[roi_rgb]",
        "[full_rgb][roi_rgb]blend=all_mode=difference:all_opacity=1,format=rgb24[difference]",
    ])
    OUT.mkdir(parents=True, exist_ok=True)
    stderr_path = OUT / "dynamic-crop-1323-1352.stderr.txt"
    diff_path = OUT / "dynamic-crop-1323-1352.rgb.gz"
    start = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="looksy-dynamic-roi-") as temp:
        args = [
            "ffmpeg", "-hide_banner", "-loglevel", "error", "-filter_complex_threads", "1",
            "-loop", "1", "-framerate", str(FPS), "-i", str(entry["source"]),
            "-filter_complex", graph, "-map", "[difference]", "-frames:v", str(FRAME_LAST-FRAME_FIRST+1),
            "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1",
        ]
        process = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        digest = hashlib.sha256()
        nonzero = total_bytes = maximum = 0
        with gzip.open(diff_path, "wb", compresslevel=6) as compressed:
            assert process.stdout is not None
            while True:
                chunk = process.stdout.read(1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
                nonzero += sum(value != 0 for value in chunk)
                maximum = max(maximum, max(chunk, default=0))
                total_bytes += len(chunk)
                compressed.write(chunk)
        stderr = process.stderr.read() if process.stderr is not None else b""
        return_code = process.wait()
        stderr_path.write_bytes(stderr)
        if return_code:
            raise RuntimeError(f"FFmpeg failed ({return_code}); see {stderr_path}")
    elapsed = time.perf_counter() - start
    expected_bytes = (FRAME_LAST-FRAME_FIRST+1) * W * H * 3
    if total_bytes != expected_bytes:
        raise RuntimeError(f"unexpected diff size {total_bytes} != {expected_bytes}")
    result = {
        "method": "native full-canvas scale followed by dynamic fixed-size crop before large pad; compare both composites over opaque base",
        "clipId": CLIP_ID,
        "sourceName": entry["fixture"]["name"],
        "frameRangeInclusive": [FRAME_FIRST, FRAME_LAST],
        "globalSecondsRange": [FRAME_FIRST/FPS, FRAME_LAST/FPS],
        "canvas": [W, H],
        "roi": [roi_w, roi_h],
        "evaluatedFrameScaleRange": [min(scale_samples), max(scale_samples)],
        "scaledCanvasWidthRange": [math.floor(W * min(scale_samples)), math.floor(W * max(scale_samples))],
        "scaledCanvasHeightRange": [math.floor(H * min(scale_samples)), math.floor(H * max(scale_samples))],
        "frames": FRAME_LAST-FRAME_FIRST+1,
        "diffRgbBytes": total_bytes,
        "compressedDiffBytes": diff_path.stat().st_size,
        "differenceSha256": digest.hexdigest(),
        "nonzeroDiffBytes": nonzero,
        "maximumChannelDifference": maximum,
        "exactAllFrameMatch": nonzero == 0,
        "wallSeconds": round(elapsed, 3),
        "frameScaleKeyframes": len(keyframes(clip, "frameScale")),
        "frameOffsetXKeyframes": len(keyframes(clip, "frameOffsetX")),
        "frameOffsetYKeyframes": len(keyframes(clip, "frameOffsetY")),
        "limitations": [
            "Single P8 title layer, not the entire 112-title composite or native project render.",
            "Opacity is constant 1 in this selected interval; the same crop must be tested with animated opacity before production use.",
            "Only full-frame stretch geometry without masks, effects, corner radius, shadows, rotation, or perspective is covered.",
            "The ROI width/height are fixed across these 30 actual frames; position changes each frame and a small transparent pad protects the ROI when the scaled source is smaller than it.",
            "This proves parity on these 30 frames, not a complete segment-union bound across arbitrary clips.",
        ],
    }
    (OUT / "dynamic-crop-1337-1349.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if not result["exactAllFrameMatch"]:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
