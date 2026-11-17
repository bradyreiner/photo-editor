#!/usr/bin/env python3
"""Compare one-frame native full-canvas affine transforms with sparse ROI transforms.

This is an isolated experiment over the preserved P8 title fixtures. It does
not modify app sources or the project. Only transparent, full-frame stretch
titles without effects/masks/rotation/perspective are eligible.
"""
from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "fixtures"
PROJECT = Path(__file__).resolve().parents[2] / "native-projects" / "looksy-polish-pass8.json"
OUT = Path(__file__).resolve().parent / "affine-probe"
W, H, FPS = 1920, 1080, 30
TARGETS = [
    (1337, "c78c3eda-188a-4a75-85b0-e8212b0f1869"),  # moving scale and offsets
    (1349, "c78c3eda-188a-4a75-85b0-e8212b0f1869"),  # near-unity scale plateau
    (1350, "50b2aaf6-e39a-43c1-b296-a104fe3119ac"),  # translation at scale 1
    (1440, "50b2aaf6-e39a-43c1-b296-a104fe3119ac"),  # translated plateau
    (1440, "cb769444-6a5e-4188-9f45-2ed4aa2223a4"),  # static title plateau
]
STATIC_CLIP = "cb769444-6a5e-4188-9f45-2ed4aa2223a4"


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as f:
        head = f.read(24)
    if len(head) != 24 or head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        raise ValueError(f"invalid PNG: {path}")
    return struct.unpack(">II", head[16:24])


def parse_fixture():
    clips = json.loads((FIXTURES / "clips.json").read_text())
    sources = [ROOT / line for line in (FIXTURES / "source-order.txt").read_text().splitlines()]
    sparse_rows = []
    for line in (FIXTURES / "sparse-map.tsv").read_text().splitlines():
        rel, x, y, canvas_w, canvas_h, is_sparse = line.split("\t")
        sparse_rows.append((ROOT / rel, int(x), int(y), int(canvas_w), int(canvas_h), is_sparse == "1"))
    if not (len(clips) == len(sources) == len(sparse_rows) == 112):
        raise ValueError("the preserved 112-title fixture is incomplete")
    if any(Path(source).name != clip["name"] for clip, source in zip(clips, sources)):
        raise ValueError("fixture clip/source ordering does not match")
    return clips, sources, sparse_rows


def get_clip_rows():
    clips, sources, sparse_rows = parse_fixture()
    by_id = {}
    for c, source, sparse in zip(clips, sources, sparse_rows):
        by_id[c["clipId"]] = {
            "fixture": c,
            "source": source,
            "sparse": sparse[0],
            "alpha_x": sparse[1],
            "alpha_y": sparse[2],
            "canvas_w": sparse[3],
            "canvas_h": sparse[4],
            "is_sparse": sparse[5],
        }
    project = json.loads(PROJECT.read_text())
    found = {}
    for track in project.get("tracks", []):
        for clip in track.get("clips", []):
            clip_id = clip.get("clipId")
            if clip_id in by_id:
                found[clip_id] = (track, clip)
    if set(found) != set(by_id):
        raise ValueError("not every preserved fixture clip exists in P8 project")
    for clip_id, (track, clip) in found.items():
        by_id[clip_id]["track"] = track
        by_id[clip_id]["clip"] = clip
    return by_id


def parameter_value(clip: dict, parameter: str, local_seconds: float, base_field: str, default: float) -> float:
    keys = sorted(
        (key for key in clip.get("keyframes", []) if key.get("parameter") == parameter),
        key=lambda key: float(key["seconds"]),
    )
    if not keys:
        return float(clip.get(base_field, default))
    if local_seconds <= float(keys[0]["seconds"]):
        return float(keys[0]["value"])
    if local_seconds >= float(keys[-1]["seconds"]):
        return float(keys[-1]["value"])
    for left, right in zip(keys, keys[1:]):
        start, end = float(left["seconds"]), float(right["seconds"])
        if start <= local_seconds <= end:
            span = max(1e-9, end - start)
            progress = (local_seconds - start) / span
            mode = left.get("interpolation", "linear")
            if mode == "hold":
                progress = 0.0
            elif mode == "ease-in":
                progress *= progress
            elif mode == "ease-out":
                progress = 1.0 - (1.0 - progress) * (1.0 - progress)
            elif mode == "ease-in-out":
                progress = progress * progress * (3.0 - 2.0 * progress)
            elif mode == "bezier":
                # The selected P8 clips/frames are checked below; this probe
                # rejects custom Bezier handles rather than approximate them.
                if left.get("bezierOutCustom") or right.get("bezierInCustom"):
                    raise ValueError("custom Bezier evaluation is outside this probe")
            return float(left["value"]) + (float(right["value"]) - float(left["value"])) * progress
    return float(keys[-1]["value"])


def evaluate(clip: dict, track: dict, frame: int) -> dict:
    sequence_seconds = frame / FPS
    local = sequence_seconds - float(clip.get("startSeconds", 0.0))
    result = {
        "frameScale": parameter_value(clip, "frameScale", local, "frameScale", 1.0),
        "frameOffsetX": parameter_value(clip, "frameOffsetX", local, "frameOffsetX", 0.0),
        "frameOffsetY": parameter_value(clip, "frameOffsetY", local, "frameOffsetY", 0.0),
        "opacity": parameter_value(clip, "opacity", local, "opacity", 1.0),
        "titlePositionX": parameter_value(clip, "titlePositionX", local, "titlePositionX", 0.5),
        "titlePositionY": parameter_value(clip, "titlePositionY", local, "titlePositionY", 0.5),
        "localSeconds": local,
    }
    return result


def eligible(entry: dict) -> bool:
    clip, track = entry["clip"], entry["track"]
    if not entry["is_sparse"]:
        return False
    if clip.get("frameMode", "stretch") != "stretch":
        return False
    if float(clip.get("frameWidth", 1.0)) != 1.0 or float(clip.get("frameHeight", 1.0)) != 1.0:
        return False
    if [float(clip.get(k, default)) for k, default in (
        ("sourceCropLeft", 0.0), ("sourceCropTop", 0.0),
        ("sourceCropRight", 1.0), ("sourceCropBottom", 1.0),
    )] != [0.0, 0.0, 1.0, 1.0]:
        return False
    if clip.get("clipMask") or clip.get("effectStack") or clip.get("videoInputTransform"):
        return False
    if clip.get("framePerspectiveQuad") or clip.get("frameShadowEnabled") or clip.get("frameShadow"):
        return False
    if abs(float(clip.get("frameRotationDegrees", 0.0))) > 1e-9:
        return False
    forbidden = {"frameRotationDegrees", "framePerspectiveX", "framePerspectiveY", "frameCornerRadius",
                 "titlePositionX", "titlePositionY", "titleFontSize"}
    if any(k.get("parameter") in forbidden for k in clip.get("keyframes", [])):
        return False
    if any(k.get("parameter") == "opacity" for k in track.get("keyframes", [])):
        return False
    group = track.get("groupTransform", {}) or {}
    if any(abs(float(group.get(k, default)) - default) > 1e-9 for k, default in (
        ("scale", 1.0), ("offsetX", 0.0), ("offsetY", 0.0), ("rotationDegrees", 0.0),
    )):
        return False
    return True


def run_ffmpeg(args: list[str]) -> None:
    result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise RuntimeError(result.stderr.decode("utf-8", "replace")[-3000:])


def scaled_dimensions(source: Path, scale: float, temp_dir: Path) -> tuple[int, int]:
    probe = temp_dir / "scale-dimensions.png"
    run_ffmpeg([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
        "-vf", f"format=rgba,scale='{W}*{scale:.12f}':'{H}*{scale:.12f}':eval=frame",
        "-frames:v", "1", str(probe),
    ])
    return png_size(probe)


def raw_frame(source: Path, filter_chain: str, output: Path) -> None:
    run_ffmpeg([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(source),
        "-vf", filter_chain, "-frames:v", "1", "-pix_fmt", "rgba", "-f", "rawvideo", str(output),
    ])


def aligned_roi(entry: dict, scaled_w: int, scaled_h: int, temp_dir: Path) -> dict:
    # Exact sampling requires the ROI origin and extent to preserve the full
    # source-to-scaled-image rational phase. Extend the alpha box by four
    # source pixels, then align it to W/gcd(W, scaledW), H/gcd(H, scaledH).
    qx = W // math.gcd(W, scaled_w)
    qy = H // math.gcd(H, scaled_h)
    tight_w, tight_h = png_size(entry["sparse"])
    x0_tight, y0_tight = entry["alpha_x"], entry["alpha_y"]
    x0 = max(0, ((x0_tight - 4) // qx) * qx)
    y0 = max(0, ((y0_tight - 4) // qy) * qy)
    x1_need = min(W, x0_tight + tight_w + 4)
    y1_need = min(H, y0_tight + tight_h + 4)
    x1 = min(W, ((x1_need + qx - 1) // qx) * qx)
    y1 = min(H, ((y1_need + qy - 1) // qy) * qy)
    roi_w, roi_h = x1 - x0, y1 - y0
    roi_scaled_w = roi_w * scaled_w // W
    roi_scaled_h = roi_h * scaled_h // H
    if roi_w <= 0 or roi_h <= 0 or roi_scaled_w <= 0 or roi_scaled_h <= 0:
        raise ValueError("invalid aligned ROI")
    if x0 % qx or y0 % qy or roi_w % qx or roi_h % qy:
        raise ValueError("ROI cannot preserve rational sampler phase at source boundary")
    roi_path = temp_dir / "aligned-roi.png"
    run_ffmpeg([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(entry["source"]),
        "-vf", f"format=rgba,crop={roi_w}:{roi_h}:{x0}:{y0}", "-frames:v", "1", str(roi_path),
    ])
    return {
        "path": roi_path, "x0": x0, "y0": y0, "width": roi_w, "height": roi_h,
        "scaledWidth": roi_scaled_w, "scaledHeight": roi_scaled_h, "qx": qx, "qy": qy,
    }


def placed_raw(roi_raw: bytes, roi_w: int, roi_h: int, x: int, y: int) -> bytes:
    canvas = bytearray(W * H * 4)
    src_stride, dst_stride = roi_w * 4, W * 4
    for row in range(roi_h):
        dy = y + row
        if not (0 <= dy < H):
            continue
        sx = max(0, -x)
        dx = max(0, x)
        count = min(roi_w - sx, W - dx)
        if count > 0:
            src = row * src_stride + sx * 4
            dst = dy * dst_stride + dx * 4
            canvas[dst:dst + count * 4] = roi_raw[src:src + count * 4]
    return bytes(canvas)


def crop_rgba(raw: bytes, width: int, box: tuple[int, int, int, int]) -> tuple[bytes, int, int]:
    left, top, right, bottom = box
    crop_w, crop_h = right - left, bottom - top
    rows = []
    stride = width * 4
    for y in range(top, bottom):
        start = y * stride + left * 4
        rows.append(raw[start:start + crop_w * 4])
    return b"".join(rows), crop_w, crop_h


def alpha_bbox(raw: bytes, width: int, height: int) -> tuple[int, int, int, int] | None:
    left, top, right, bottom = width, height, -1, -1
    for y in range(height):
        row = y * width * 4
        for x in range(width):
            if raw[row + x * 4 + 3]:
                left, top = min(left, x), min(top, y)
                right, bottom = max(right, x), max(bottom, y)
    return None if right < left else (left, top, right + 1, bottom + 1)


def visible_differences(expected: bytes, actual: bytes, box: tuple[int, int, int, int]) -> tuple[int, int, tuple[int, int, int, int] | None, int]:
    left, top, right, bottom = box
    diff = visible = 0
    diff_left, diff_top, diff_right, diff_bottom = right, bottom, -1, -1
    max_channel_delta = 0
    for y in range(top, bottom):
        row = y * W * 4
        for x in range(left, right):
            i = row + x * 4
            ea, aa = expected[i + 3], actual[i + 3]
            if ea or aa:
                visible += 1
                if expected[i:i + 4] != actual[i:i + 4]:
                    diff += 1
                    diff_left, diff_top = min(diff_left, x), min(diff_top, y)
                    diff_right, diff_bottom = max(diff_right, x), max(diff_bottom, y)
                    max_channel_delta = max(max_channel_delta, *(abs(expected[i + c] - actual[i + c]) for c in range(4)))
    bbox = None if diff == 0 else (diff_left, diff_top, diff_right + 1, diff_bottom + 1)
    return diff, visible, bbox, max_channel_delta


def best_placement(expected: bytes, roi_raw: bytes, roi_w: int, roi_h: int, predicted_x: float, predicted_y: float):
    center_x, center_y = math.floor(predicted_x), math.floor(predicted_y)
    expected_box = alpha_bbox(expected, W, H)
    roi_box = alpha_bbox(roi_raw, roi_w, roi_h)
    if expected_box is None or roi_box is None:
        return 0, center_x, center_y, 0, bytes(W * H * 4), None, None, 0
    results = []
    for y in range(center_y - 3, center_y + 4):
        for x in range(center_x - 3, center_x + 4):
            actual = placed_raw(roi_raw, roi_w, roi_h, x, y)
            shifted_box = (roi_box[0] + x, roi_box[1] + y, roi_box[2] + x, roi_box[3] + y)
            box = (
                max(0, min(expected_box[0], shifted_box[0])),
                max(0, min(expected_box[1], shifted_box[1])),
                min(W, max(expected_box[2], shifted_box[2])),
                min(H, max(expected_box[3], shifted_box[3])),
            )
            diff, visible, diff_box, delta = visible_differences(expected, actual, box)
            results.append((diff, x, y, visible, actual, diff_box, delta, box))
    diff, x, y, visible, actual, diff_box, delta, box = min(
        results, key=lambda row: (row[0], abs(row[1] - center_x) + abs(row[2] - center_y))
    )
    return diff, x, y, visible, actual, diff_box, delta, box


def probe(frame: int, clip_id: str, entries: dict) -> dict:
    entry = entries[clip_id]
    clip, track = entry["clip"], entry["track"]
    if not eligible(entry):
        raise ValueError(f"clip {clip_id} is not eligible for the affine prototype")
    values = evaluate(clip, track, frame)
    scale, offset_x, offset_y, opacity = (
        values["frameScale"], values["frameOffsetX"], values["frameOffsetY"], values["opacity"]
    )
    if abs(values["titlePositionX"] - float(clip.get("titlePositionX", 0.5))) > 1e-9 or \
       abs(values["titlePositionY"] - float(clip.get("titlePositionY", 0.5))) > 1e-9:
        raise ValueError("animated title position is outside this affine probe")
    offset_x_px = W * offset_x * 0.25
    offset_y_px = H * offset_y * 0.25
    with tempfile.TemporaryDirectory(prefix="looksy-affine-roi-") as temp_name:
        temp = Path(temp_name)
        scaled_w, scaled_h = scaled_dimensions(entry["source"], scale, temp)
        roi = aligned_roi(entry, scaled_w, scaled_h, temp)
        full_raw = temp / "full.rgba"
        roi_raw_path = temp / "roi.rgba"
        # All selected clips have a keyframed frameScale curve. The native
        # crop origin uses floor(the scale expression) in that case.
        animated_scale = any(k.get("parameter") == "frameScale" for k in clip.get("keyframes", []))
        if animated_scale:
            crop_reference_w = math.floor(W * scale)
            crop_reference_h = math.floor(H * scale)
        else:
            crop_reference_w, crop_reference_h = scaled_w, scaled_h
        crop_x = (crop_reference_w + W) / 2.0 - offset_x_px
        crop_y = (crop_reference_h + H) / 2.0 - offset_y_px
        full_filter = (
            f"format=rgba,scale={W}:{H},scale={scaled_w}:{scaled_h}:eval=frame:flags=bicubic,"
            f"pad=iw+{2*W}:ih+{2*H}:{W}:{H}:color=black@0:eval=frame,"
            f"crop={W}:{H}:'{crop_x:.12f}':'{crop_y:.12f}',setsar=1,"
            f"format=rgba,colorchannelmixer=aa={opacity:.12f}"
        )
        raw_frame(entry["source"], full_filter, full_raw)
        roi_filter = (
            f"format=rgba,scale={roi['scaledWidth']}:{roi['scaledHeight']}:flags=bicubic,"
            f"format=rgba,colorchannelmixer=aa={opacity:.12f}"
        )
        raw_frame(roi["path"], roi_filter, roi_raw_path)
        expected = full_raw.read_bytes()
        candidate_raw = roi_raw_path.read_bytes()
        # Exact expected origin before FFmpeg's integer crop placement. The
        # crop filter's integer rounding is resolved by searching +/-3 pixels.
        scaled_origin_x = roi["x0"] * scaled_w / W
        scaled_origin_y = roi["y0"] * scaled_h / H
        predicted_x = W + scaled_origin_x - crop_x
        predicted_y = H + scaled_origin_y - crop_y
        diff, place_x, place_y, visible, placed, diff_box, max_channel_delta, comparison_box = best_placement(
            expected, candidate_raw, roi["scaledWidth"], roi["scaledHeight"], predicted_x, predicted_y
        )
        # A second strategy keeps the native full-canvas resampling stage, then
        # crops the scaled result before its enormous transparent pad. This is
        # the exact sampler path and lets the remaining affine/overlay stages
        # carry only the transformed title support.
        scaled_raw_path = temp / "scaled-full-canvas.rgba"
        raw_frame(entry["source"], f"format=rgba,scale={scaled_w}:{scaled_h}:flags=bicubic", scaled_raw_path)
        scaled_raw = scaled_raw_path.read_bytes()
        scaled_box = alpha_bbox(scaled_raw, scaled_w, scaled_h)
        if scaled_box is None:
            raise ValueError("selected title has no scaled alpha pixels")
        scaled_box = (
            max(0, scaled_box[0] - 4), max(0, scaled_box[1] - 4),
            min(scaled_w, scaled_box[2] + 4), min(scaled_h, scaled_box[3] + 4),
        )
        post_scale_raw, post_scale_w, post_scale_h = crop_rgba(scaled_raw, scaled_w, scaled_box)
        post_scale_x = W + scaled_box[0] - crop_x
        post_scale_y = H + scaled_box[1] - crop_y
        post_diff, post_x, post_y, post_visible, post_placed, post_diff_box, post_delta, post_comparison_box = best_placement(
            expected, post_scale_raw, post_scale_w, post_scale_h, post_scale_x, post_scale_y
        )
        if frame == 1337:
            kept = OUT / "frame-1337"
            kept.mkdir(exist_ok=True)
            (kept / "full-canvas-reference.rgba").write_bytes(expected)
            (kept / "scaled-roi.rgba").write_bytes(candidate_raw)
            (kept / "post-scale-roi.rgba").write_bytes(post_scale_raw)
        expected_hash = hashlib.md5(expected).hexdigest()
        candidate_hash = hashlib.md5(placed).hexdigest()
        return {
            "frame": frame,
            "sequenceSeconds": frame / FPS,
            "clipId": clip_id,
            "sourceName": entry["fixture"]["name"],
            "localSeconds": values["localSeconds"],
            "frameScale": scale,
            "offsetNormalized": [offset_x, offset_y],
            "offsetPixels": [offset_x_px, offset_y_px],
            "opacity": opacity,
            "scaledCanvas": [scaled_w, scaled_h],
            "rationalSamplerPeriod": [roi["qx"], roi["qy"]],
            "roiSource": [roi["x0"], roi["y0"], roi["width"], roi["height"]],
            "roiScaled": [roi["scaledWidth"], roi["scaledHeight"]],
            "predictedPlacement": [predicted_x, predicted_y],
            "bestPlacement": [place_x, place_y],
            "visiblePixelsCompared": visible,
            "visiblePixelDifferences": diff,
            "differenceBounds": diff_box,
            "maximumChannelDelta": max_channel_delta,
            "comparisonBounds": comparison_box,
            "fullVisibleRawMd5": expected_hash,
            "placedRoiVisibleRawMd5IncludingTransparentRgb": candidate_hash,
            "exactVisiblePixelMatch": diff == 0,
            "postScaleCrop": {
                "method": "same native full-canvas scale, crop expanded alpha support before pad",
                "scaledRoi": [scaled_box[0], scaled_box[1], post_scale_w, post_scale_h],
                "predictedPlacement": [post_scale_x, post_scale_y],
                "bestPlacement": [post_x, post_y],
                "visiblePixelDifferences": post_diff,
                "differenceBounds": post_diff_box,
                "maximumChannelDelta": post_delta,
                "visiblePixelsCompared": post_visible,
                "exactVisiblePixelMatch": post_diff == 0,
            },
        }


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    entries = get_clip_rows()
    results = []
    for frame, clip_id in TARGETS:
        result = probe(frame, clip_id, entries)
        results.append(result)
        print(json.dumps(result, sort_keys=True))
    (OUT / "results.json").write_text(json.dumps({
        "method": "single-layer full-canvas FFmpeg transform vs phase-aligned cropped-source scale and integer placement",
        "fixtureProject": str(PROJECT),
        "fixtureProjectSha256": __import__("hashlib").sha256(PROJECT.read_bytes()).hexdigest(),
        "samples": results,
        "limitations": [
            "Only selected individual P8 title layers were tested, not the whole native composition.",
            "Only full-frame stretch affine geometry and clip opacity are covered; masks/effects/rotation/perspective are excluded.",
            "Per-sample exact parity does not establish continuous-time ROI bounds or production performance.",
            "Transparent RGB at alpha zero is ignored because it cannot affect normal compositing.",
        ],
    }, indent=2) + "\n")
    if not all(r["postScaleCrop"]["exactVisiblePixelMatch"] for r in results):
        raise SystemExit(2)


if __name__ == "__main__":
    main()
