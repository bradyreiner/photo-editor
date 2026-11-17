#!/usr/bin/env python3
"""Compare 8 authored title layers through full and sparse affine graphs."""
from __future__ import annotations

import gzip
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import tempfile
import time

from affine_roi_probe import get_clip_rows, parameter_value, png_size
from dynamic_crop_probe import native_curve_expression

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(__file__).resolve().parent / "affine-probe"
FRAME_FIRST, FRAME_LAST, FPS = 1323, 1352, 30
W, H = 1920, 1080
SUPPORT = 8


def eligible(entry: dict, first_seconds: float, last_seconds: float) -> bool:
    clip, track = entry["clip"], entry["track"]
    start = float(clip.get("startSeconds", 0.0))
    end = start + float(clip.get("durationSeconds", 0.0))
    if end <= first_seconds or start > last_seconds:
        return False
    if clip.get("mediaType") != "title" or not entry.get("is_sparse"):
        return False
    if clip.get("effectStack") or clip.get("clipMask") or clip.get("framePerspectiveQuad"):
        return False
    if clip.get("frameShadowEnabled") or clip.get("frameShadow"):
        return False
    if clip.get("frameMode", "stretch") != "stretch":
        return False
    if abs(float(clip.get("frameRotationDegrees", 0.0))) > 1e-9:
        return False
    if any(k.get("parameter") in {
        "frameRotationDegrees", "framePerspectiveX", "framePerspectiveY",
        "frameCornerRadius", "titlePositionX", "titlePositionY", "titleFontSize",
    } for k in clip.get("keyframes", [])):
        return False
    if abs(float(clip.get("frameWidth", 1.0)) - 1.0) > 1e-9 or abs(float(clip.get("frameHeight", 1.0)) - 1.0) > 1e-9:
        return False
    if any(abs(float(clip.get(key, default)) - default) > 1e-9 for key, default in (
        ("sourceCropLeft", 0.0), ("sourceCropTop", 0.0),
        ("sourceCropRight", 1.0), ("sourceCropBottom", 1.0),
    )):
        return False
    if clip.get("blendMode", "normal") not in ("normal", "source-over"):
        return False
    if track.get("blendMode", "normal") not in ("normal", "source-over"):
        return False
    if any(k.get("parameter") == "opacity" for k in track.get("keyframes", [])):
        return False
    if any(abs(float(track.get(key, default)) - default) > 1e-9 for key, default in (
        ("groupTransformScale", 1.0), ("groupTransformOffsetX", 0.0),
        ("groupTransformOffsetY", 0.0), ("groupTransformRotationDegrees", 0.0),
    )):
        return False
    return bool(entry["source"].is_file())


def quote(expr: str) -> str:
    return f"'{expr}'"


def native_opacity_filter(clip: dict, sequence_start: float, instance: str) -> str:
    opacity = native_curve_expression(clip, "opacity", sequence_start, "opacity", 1.0)
    opacity = re.sub(r"\bt\b", "T", opacity).replace(",", "\\\\,")
    initial = parameter_value(
        clip, "opacity", sequence_start - float(clip.get("startSeconds", 0.0)), "opacity", 1.0
    )
    initial = min(1.0, max(0.0, initial))
    return (
        f"sendcmd=c='0 [expr] {instance} aa {opacity};',"
        f"format=rgba,colorchannelmixer@{instance}=aa={initial:.8f}"
    )


def make_graph(entries: list[dict]) -> tuple[list[str], dict]:
    first_seconds = FRAME_FIRST / FPS
    last_seconds = (FRAME_LAST + 1) / FPS
    args = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-filter_complex_threads", "1"]
    graph = []
    layer_receipts = []

    for index, entry in enumerate(entries):
        clip = entry["clip"]
        start = float(clip.get("startSeconds", 0.0))
        shift = first_seconds - start
        scale_expr = native_curve_expression(clip, "frameScale", first_seconds, "frameScale", 1.0)
        offset_x_expr = native_curve_expression(clip, "frameOffsetX", first_seconds, "frameOffsetX", 0.0)
        offset_y_expr = native_curve_expression(clip, "frameOffsetY", first_seconds, "frameOffsetY", 0.0)
        scaled_w_expr = f"({W}*({scale_expr}))"
        scaled_h_expr = f"({H}*({scale_expr}))"
        offset_x_px = f"({W}*({offset_x_expr})*0.25)"
        offset_y_px = f"({H}*({offset_y_expr})*0.25)"
        native_crop_x = f"((floor({scaled_w_expr})+{W})/2-({offset_x_px}))"
        native_crop_y = f"((floor({scaled_h_expr})+{H})/2-({offset_y_px}))"
        roi_x = f"max(0,floor(({entry['alpha_x']}*({scaled_w_expr})/{W}))-{SUPPORT})"
        roi_y = f"max(0,floor(({entry['alpha_y']}*({scaled_h_expr})/{H}))-{SUPPORT})"

        sampled_scales = [
            parameter_value(clip, "frameScale", frame / FPS - start, "frameScale", 1.0)
            for frame in range(FRAME_FIRST, FRAME_LAST + 1)
        ]
        alpha_w, alpha_h = png_size(entry["sparse"])
        max_roi_w = max_roi_h = 1
        for scale in sampled_scales:
            scaled_w, scaled_h = math.floor(W * scale), math.floor(H * scale)
            x0 = max(0, math.floor(entry["alpha_x"] * scaled_w / W) - SUPPORT)
            y0 = max(0, math.floor(entry["alpha_y"] * scaled_h / H) - SUPPORT)
            x1 = math.ceil((entry["alpha_x"] + alpha_w) * scaled_w / W) + SUPPORT
            y1 = math.ceil((entry["alpha_y"] + alpha_h) * scaled_h / H) + SUPPORT
            max_roi_w = max(max_roi_w, x1 - x0)
            max_roi_h = max(max_roi_h, y1 - y0)
        roi_w, roi_h = max_roi_w, max_roi_h
        # Crop/pad origin and output overlay location use the native full-canvas crop clock.
        overlay_x = f"({W}+({roi_x})-floor(({native_crop_x})+0.5))"
        overlay_y = f"({H}+({roi_y})-floor(({native_crop_y})+0.5))"
        full_opacity_filter = native_opacity_filter(clip, first_seconds, f"roi_full_{index}")
        sparse_opacity_filter = native_opacity_filter(clip, first_seconds, f"roi_sparse_{index}")

        args += ["-loop", "1", "-framerate", str(FPS), "-t", "1", "-threads", "1", "-i", str(entry["source"])]
        graph.append(f"[{index}:v]split=2[full_src{index}][roi_src{index}]")
        graph.append(
            f"[full_src{index}]scale={W}:{H},format=rgba,"
            f"scale={quote(scaled_w_expr)}:{quote(scaled_h_expr)}:eval=frame:flags=bicubic,"
            f"pad='iw+{2*W}':'ih+{2*H}':{W}:{H}:color=black@0:eval=frame,"
            f"crop={W}:{H}:{quote(native_crop_x)}:{quote(native_crop_y)},setsar=1,{full_opacity_filter}[full{index}]"
        )
        graph.append(
            f"[roi_src{index}]scale={W}:{H},format=rgba,"
            f"scale={quote(scaled_w_expr)}:{quote(scaled_h_expr)}:eval=frame:flags=bicubic,"
            f"pad='iw+{roi_w}':'ih+{roi_h}':0:0:color=black@0:eval=frame,"
            f"crop={roi_w}:{roi_h}:{quote(roi_x)}:{quote(roi_y)},setsar=1,{sparse_opacity_filter}[roi{index}]"
        )
        layer_receipts.append({
            "clipId": clip["clipId"], "displayLabel": clip.get("displayLabel", ""),
            "sourceName": entry["fixture"].get("name"),
            "roi": [roi_w, roi_h], "alphaBounds": [entry["alpha_x"], entry["alpha_y"], alpha_w, alpha_h],
            "frameScaleRange": [min(sampled_scales), max(sampled_scales)],
            "frameScaleKeyframes": sum(k.get("parameter") == "frameScale" for k in clip.get("keyframes", [])),
            "frameOffsetXKeyframes": sum(k.get("parameter") == "frameOffsetX" for k in clip.get("keyframes", [])),
            "frameOffsetYKeyframes": sum(k.get("parameter") == "frameOffsetY" for k in clip.get("keyframes", [])),
            "opacityKeyframes": sum(k.get("parameter") == "opacity" for k in clip.get("keyframes", [])),
        })

    background_index = len(entries)
    args += ["-f", "lavfi", "-i", f"color=c=0x172b3c:s={W}x{H}:r={FPS}:d=1"]
    graph.append(f"[{background_index}:v]format=rgba,split=2[fullbase0][roibase0]")
    for index in range(len(entries)):
        full_input = f"fullbase{index}"
        full_output = f"fullbase{index+1}"
        roi_input = f"roibase{index}"
        roi_output = f"roibase{index+1}"
        graph.append(
            f"[{full_input}][full{index}]overlay=shortest=1:eof_action=pass:format=auto[{full_output}]"
        )
        clip = entries[index]["clip"]
        start = float(clip.get("startSeconds", 0.0))
        scale_expr = native_curve_expression(clip, "frameScale", first_seconds, "frameScale", 1.0)
        offset_x_expr = native_curve_expression(clip, "frameOffsetX", first_seconds, "frameOffsetX", 0.0)
        offset_y_expr = native_curve_expression(clip, "frameOffsetY", first_seconds, "frameOffsetY", 0.0)
        scaled_w_expr = f"({W}*({scale_expr}))"
        scaled_h_expr = f"({H}*({scale_expr}))"
        native_crop_x = f"((floor({scaled_w_expr})+{W})/2-({W}*({offset_x_expr})*0.25))"
        native_crop_y = f"((floor({scaled_h_expr})+{H})/2-({H}*({offset_y_expr})*0.25))"
        roi_x = f"max(0,floor(({entries[index]['alpha_x']}*({scaled_w_expr})/{W}))-{SUPPORT})"
        roi_y = f"max(0,floor(({entries[index]['alpha_y']}*({scaled_h_expr})/{H}))-{SUPPORT})"
        roi_w, roi_h = layer_receipts[index]["roi"]
        overlay_x = f"({W}+({roi_x})-floor(({native_crop_x})+0.5))"
        overlay_y = f"({H}+({roi_y})-floor(({native_crop_y})+0.5))"
        graph.append(
            f"[{roi_input}][roi{index}]overlay=x={quote(overlay_x)}:y={quote(overlay_y)}:"
            f"eval=frame:shortest=1:eof_action=pass:format=auto[{roi_output}]"
        )
    graph.extend([
        f"[fullbase{len(entries)}]format=rgb24[fullrgb]",
        f"[roibase{len(entries)}]format=rgb24[roirgb]",
        "[fullrgb][roirgb]blend=all_mode=difference:all_opacity=1,format=rgb24[difference]",
    ])
    args += ["-filter_complex", ";".join(graph), "-map", "[difference]", "-frames:v", "30", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1"]
    return args, layer_receipts


def main() -> None:
    rows = get_clip_rows()
    fixture = json.loads((ROOT / "fixtures" / "clips.json").read_text())
    interval_start, interval_end = FRAME_FIRST / FPS, (FRAME_LAST + 1) / FPS
    eligible_entries = [
        rows[item["clipId"]]
        for item in fixture
        if eligible(rows[item["clipId"]], interval_start, interval_end)
    ]
    entries = [
        entry for entry in eligible_entries
        if any(k.get("parameter") == "frameScale" for k in entry["clip"].get("keyframes", []))
    ][:8]
    if len(entries) != 8:
        raise RuntimeError(f"expected eight sparse affine title layers; found {len(entries)}")
    args, layer_receipts = make_graph(entries)
    OUT.mkdir(parents=True, exist_ok=True)
    stderr_path = OUT / "multilayer-sparse-roi-1323-1352.stderr.txt"
    diff_path = OUT / "multilayer-sparse-roi-1323-1352.rgb.gz"
    start = time.perf_counter()
    with tempfile.TemporaryDirectory(prefix="looksy-multilayer-roi-"):
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
    expected_bytes = 30 * W * H * 3
    result = {
        "method": "Eight authored title inputs; compare native full-canvas affine crop to fixed-size post-scale ROI overlays over the same opaque base.",
        "frameRangeInclusive": [FRAME_FIRST, FRAME_LAST],
        "globalSecondsRange": [interval_start, interval_end],
        "canvas": [W, H], "fps": FPS, "frames": 30,
        "threads": {"filterComplex": 1, "eachPngDecoder": 1},
        "layers": layer_receipts,
        "diffRgbBytes": total_bytes,
        "expectedDiffRgbBytes": expected_bytes,
        "compressedDiffBytes": diff_path.stat().st_size,
        "differenceSha256": digest.hexdigest(),
        "nonzeroDiffBytes": nonzero,
        "maximumChannelDifference": maximum,
        "exactAllFrameMatch": total_bytes == expected_bytes and nonzero == 0,
        "wallSeconds": round(time.perf_counter() - start, 3),
        "limitations": [
            "The fixture is an eight-layer title-only subset, not the full native project or all 112 active layers.",
            "It uses the same full source scale and post-scale ROI method; all input PNGs are full-canvas.",
            "Only full-frame stretch, normal source-over, no-mask/no-effect/no-rotation/no-perspective/no-corner/no-shadow titles are included.",
            "The eight selected layers exercise authored opacity, frameScale, frameOffsetX, and frameOffsetY curves on the real 44.1–45.1s clock.",
            "Decoded RGB is compared after compositing on an opaque background; transparent RGB outside visible alpha is not compared.",
        ],
    }
    result_path = OUT / "multilayer-sparse-roi-1323-1352.json"
    result_path.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if not result["exactAllFrameMatch"]:
        raise SystemExit(2)


if __name__ == "__main__":
    main()
