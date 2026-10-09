#!/usr/bin/env python3
"""Optional SAM 2 video-mask worker for Looksy's staged timeline frames."""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
import traceback
from pathlib import Path


def emit(payload: dict[str, object]) -> None:
    print(json.dumps(payload, separators=(",", ":")), flush=True)


def fail(status: str, message: str, exit_code: int = 2) -> int:
    emit({"type": "error", "status": status, "message": message})
    return exit_code


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--frames-dir", required=True)
    parser.add_argument("--mask-dir", required=True)
    parser.add_argument("--frame-count", required=True, type=int)
    parser.add_argument("--seed-box", required=True, help="normalized x,y,width,height")
    parser.add_argument("--config", required=True)
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--device", choices=("cpu", "mps"), default="cpu")
    parser.add_argument("--corrections-json", default="", help="optional JSON file containing normalized point corrections")
    return parser.parse_args()


def read_corrections(path: str, frame_count: int) -> tuple[list[dict[str, object]], str | None]:
    if not path:
        return [], None
    try:
        payload = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return [], f"correction prompt JSON could not be read: {error}"
    if not isinstance(payload, dict) or not isinstance(payload.get("frames"), list):
        return [], "correction prompt JSON must contain a frames array"
    frames = payload["frames"]
    if len(frames) > 64:
        return [], "at most 64 correction frames are supported"
    seen: set[int] = set()
    normalized: list[dict[str, object]] = []
    for frame in frames:
        if not isinstance(frame, dict):
            return [], "each correction frame must be an object"
        frame_index = frame.get("frameIndex")
        if isinstance(frame_index, bool) or not isinstance(frame_index, int) or not 0 <= frame_index < frame_count:
            return [], "correction frame indices must be integers inside the staged source range"
        if frame_index in seen:
            return [], "correction frames must not contain duplicate frame indices"
        seen.add(frame_index)
        points = frame.get("points")
        if not isinstance(points, list) or not points or len(points) > 256:
            return [], "each correction frame must contain 1..256 points"
        normalized_points: list[dict[str, float | int]] = []
        for point in points:
            if not isinstance(point, dict):
                return [], "each correction point must be an object"
            x = point.get("x")
            y = point.get("y")
            label = point.get("label")
            if (isinstance(x, bool) or not isinstance(x, (int, float))
                    or isinstance(y, bool) or not isinstance(y, (int, float))
                    or not math.isfinite(x) or not math.isfinite(y)
                    or not 0.0 <= x <= 1.0 or not 0.0 <= y <= 1.0):
                return [], "correction point coordinates must be finite and normalized to 0..1"
            if isinstance(label, bool) or not isinstance(label, int) or label not in (0, 1):
                return [], "correction point labels must be 1 (positive) or 0 (negative)"
            normalized_points.append({"x": float(x), "y": float(y), "label": label})
        normalized.append({"frameIndex": frame_index, "points": normalized_points})
    return normalized, None


def main() -> int:
    args = parse_args()
    frames_dir = Path(args.frames_dir)
    mask_dir = Path(args.mask_dir)
    if args.frame_count < 1 or args.frame_count > 180:
        return fail("invalid_input", "frame count must be in the range 1..180")
    checkpoint = Path(args.checkpoint).expanduser()
    if not checkpoint.is_file():
        return fail("unavailable", f"SAM2 checkpoint does not exist: {checkpoint}")

    frame_paths = [frames_dir / f"{index:06d}.jpg" for index in range(args.frame_count)]
    if not frames_dir.is_dir() or not all(path.is_file() for path in frame_paths):
        return fail("invalid_input", "staged numbered JPEG frames are incomplete")
    staged_jpegs = {
        path.name
        for path in frames_dir.iterdir()
        if path.is_file() and path.suffix.lower() in {".jpg", ".jpeg"}
    }
    if staged_jpegs != {path.name for path in frame_paths}:
        return fail("invalid_input", "staged JPEG directory must contain exactly the requested numbered frames")
    try:
        box_values = [float(value) for value in args.seed_box.split(",")]
    except ValueError:
        return fail("invalid_input", "seed box must contain four numeric values")
    if len(box_values) != 4 or not all(math.isfinite(value) for value in box_values):
        return fail("invalid_input", "seed box must contain four finite values")
    x, y, width, height = box_values
    if width <= 0.0 or height <= 0.0 or x < 0.0 or y < 0.0 or x + width > 1.0 or y + height > 1.0:
        return fail("invalid_input", "seed box must be normalized and inside the first frame")
    if not mask_dir.is_dir():
        return fail("invalid_input", "mask output directory does not exist")
    corrections, correction_error = read_corrections(args.corrections_json, args.frame_count)
    if correction_error:
        return fail("invalid_input", correction_error)

    try:
        import numpy as np
        import torch
        from PIL import Image
        from sam2.build_sam import build_sam2_video_predictor
    except Exception as error:  # missing or broken optional Python environment
        return fail(
            "unavailable",
            f"SAM2 Python dependencies are unavailable ({type(error).__name__}): {error}. "
            "Install the optional SAM2 environment and configure PHOTO_EDITOR_SAM2_PYTHON.",
        )

    device = args.device
    if device == "mps" and not torch.backends.mps.is_available():
        return fail("unavailable", "MPS was requested but torch.backends.mps.is_available() is false")

    try:
        from PIL import ImageFile

        ImageFile.LOAD_TRUNCATED_IMAGES = False
        with Image.open(frame_paths[0]) as first_image:
            frame_width, frame_height = first_image.size
            first_image.verify()
        if frame_width < 2 or frame_height < 2:
            return fail("invalid_input", "staged frame dimensions are too small")
        for path in frame_paths[1:]:
            with Image.open(path) as frame_image:
                if frame_image.size != (frame_width, frame_height):
                    return fail("invalid_input", "staged frames must have matching dimensions")
                frame_image.verify()

        # SAM2's video predictor consumes a numbered JPEG directory and keeps
        # per-frame state for temporal propagation.
        predictor = build_sam2_video_predictor(
            args.config,
            str(checkpoint),
            device=device,
            mode="eval",
        )
        with torch.inference_mode():
            state = predictor.init_state(
                video_path=str(frames_dir),
                offload_video_to_cpu=True,
                offload_state_to_cpu=True,
                async_loading_frames=False,
            )
            box_pixels = np.array(
                [x * frame_width, y * frame_height, (x + width) * frame_width, (y + height) * frame_height],
                dtype=np.float32,
            )
            predictor.add_new_points_or_box(state, frame_idx=0, obj_id=1, box=box_pixels)
            for correction in corrections:
                frame_index = int(correction["frameIndex"])
                points = correction["points"]
                point_pixels = np.asarray(
                    [[float(point["x"]) * frame_width, float(point["y"]) * frame_height]
                     for point in points],
                    dtype=np.float32,
                )
                labels = np.asarray([int(point["label"]) for point in points], dtype=np.int32)
                predictor.add_new_points_or_box(
                    state,
                    frame_idx=frame_index,
                    obj_id=1,
                    points=point_pixels,
                    labels=labels,
                    clear_old_points=True,
                    box=box_pixels if frame_index == 0 else None,
                )

            written: set[int] = set()
            output_total = 0
            any_foreground = False
            for frame_index, object_ids, video_res_masks in predictor.propagate_in_video(state):
                frame_index = int(frame_index)
                if frame_index < 0 or frame_index >= args.frame_count:
                    return fail("output_invalid", f"SAM2 returned an out-of-range frame index: {frame_index}")
                if frame_index in written:
                    return fail("output_invalid", f"SAM2 returned a duplicate frame index: {frame_index}")
                ids = [int(value) for value in object_ids]
                if 1 not in ids:
                    return fail("failed", f"SAM2 lost object id 1 at frame {frame_index}")
                mask_slot = ids.index(1)
                # The pinned predictor returns resized mask logits; logit 0 is
                # equivalent to a sigmoid probability threshold of 0.5.
                mask = video_res_masks[mask_slot, 0].detach().to("cpu").numpy() > 0.0
                alpha = Image.fromarray(mask.astype(np.uint8) * 255, mode="L")
                if max(alpha.size) > 640:
                    scale = 640.0 / max(alpha.size)
                    resized = (max(1, round(alpha.width * scale)), max(1, round(alpha.height * scale)))
                    alpha = alpha.resize(resized, resample=Image.Resampling.BILINEAR)
                any_foreground = any_foreground or bool(np.any(np.asarray(alpha) > 0))
                output = Image.new("RGBA", alpha.size, (255, 255, 255, 0))
                output.putalpha(alpha)
                output_path = mask_dir / f"mask_{frame_index:06d}.png"
                output.save(output_path, format="PNG", optimize=False)
                output_total += output_path.stat().st_size
                if output_total > 256 * 1024 * 1024:
                    return fail("output_invalid", "SAM2 masks exceeded the 256 MiB result limit")
                written.add(int(frame_index))
                emit({"type": "progress", "completed": len(written), "total": args.frame_count})

            if written != set(range(args.frame_count)):
                missing = sorted(set(range(args.frame_count)) - written)
                return fail("output_invalid", f"SAM2 did not produce all requested frame masks: {missing[:8]}")
            if not any_foreground:
                return fail("failed", "SAM2 produced an empty object mask for the entire source shot")
        emit({"type": "complete", "frame_count": args.frame_count})
        return 0
    except Exception as error:
        detail = f"SAM2 inference failed ({type(error).__name__}): {error}"
        emit({"type": "error", "status": "failed", "message": detail})
        if os.environ.get("PHOTO_EDITOR_SAM2_TRACEBACK") == "1":
            traceback.print_exc(file=sys.stderr)
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
