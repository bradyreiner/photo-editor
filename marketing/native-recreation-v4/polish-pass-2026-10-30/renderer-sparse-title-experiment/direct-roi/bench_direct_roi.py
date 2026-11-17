#!/usr/bin/env python3
"""Measure sparse cropped title PNGs composed directly at their alpha origins."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import resource
import shutil
import struct
import time

HERE = Path(__file__).resolve().parent
EXPERIMENT = HERE.parent
FIXTURES = EXPERIMENT / "fixtures"
OUT = HERE
CANVAS_WIDTH = 1920
CANVAS_HEIGHT = 1080


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as source:
        header = source.read(24)
    if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[12:16] != b"IHDR":
        raise ValueError(f"not a valid PNG IHDR: {path}")
    return struct.unpack(">II", header[16:24])


def rows():
    result = []
    for line in (FIXTURES / "sparse-map.tsv").read_text().splitlines():
        path, x, y, width, height, sparse = line.split("\t")
        result.append((EXPERIMENT / path, int(x), int(y), int(width), int(height), sparse == "1"))
    return result


def input_paths():
    source_order = [EXPERIMENT / line for line in (FIXTURES / "source-order.txt").read_text().splitlines()]
    mapped = rows()
    if len(source_order) != 112 or len(mapped) != 112:
        raise ValueError("expected the preserved 112-title input order and placement map")
    if any(not path.is_file() for path in source_order):
        raise ValueError("one or more original title PNG fixtures are missing")
    if any(not path.is_file() for path, *_rest in mapped):
        raise ValueError("one or more sparse title PNG fixtures are missing")
    if any((width, height) != (CANVAS_WIDTH, CANVAS_HEIGHT) for _path, _x, _y, width, height, _sparse in mapped):
        raise ValueError("the recorded source canvas dimensions do not match the experiment")
    fallback_rows = [index for index, row in enumerate(mapped) if not row[5]]
    if fallback_rows != [0]:
        raise ValueError("the fully opaque fallback must remain the first full-canvas input")
    return source_order, mapped


def frame_record(path: Path) -> str | None:
    lines = [line for line in path.read_text().splitlines() if line and not line.startswith("#")]
    return lines[-1] if lines else None


def original_frame_record() -> str:
    path = EXPERIMENT / "original.framemd5"
    record = frame_record(path)
    if record is None:
        raise ValueError("the preserved original frame hash is missing")
    return record


def command(paths, mapped):
    args = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-filter_complex_threads", "1"]
    for path in paths:
        args += ["-loop", "1", "-framerate", "30", "-i", str(path)]

    chains = ["[0:v]format=rgba[base0]"]
    for index in range(1, len(mapped)):
        _path, x, y, _width, _height, _sparse = mapped[index]
        chains.append(f"[{index}:v]format=rgba[t{index}]")
        chains.append(
            f"[base{index - 1}][t{index}]overlay=x={x}:y={y}:shortest=1:"
            f"eof_action=pass:format=auto[base{index}]"
        )
    output = OUT / "direct-roi.framemd5"
    args += [
        "-filter_complex", ";".join(chains),
        "-map", "[base111]", "-frames:v", "1", "-pix_fmt", "rgba",
        "-f", "framemd5", "-y", str(output),
    ]
    return args


def run():
    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg not found on PATH")
    OUT.mkdir(parents=True, exist_ok=True)
    original_paths, mapped = input_paths()

    # Preserve the opaque full-canvas fallback at input zero; crop only the
    # remaining inputs and feed their small raster directly to the final overlay.
    paths = [original_paths[0]] + [row[0] for row in mapped[1:]]
    args = command(paths, mapped)
    stderr_path = OUT / "direct-roi.stderr.txt"
    start = time.perf_counter()
    pid = os.fork()
    if pid == 0:
        with stderr_path.open("wb") as stderr_file:
            os.dup2(stderr_file.fileno(), 2)
            os.execvp(args[0], args)
    _pid, status, usage = os.wait4(pid, 0)
    elapsed = time.perf_counter() - start
    if not os.WIFEXITED(status) or os.WEXITSTATUS(status) != 0:
        raise RuntimeError(f"FFmpeg sparse ROI run failed; inspect {stderr_path}")

    candidate = frame_record(OUT / "direct-roi.framemd5")
    baseline = original_frame_record()
    input_areas = [png_size(path)[0] * png_size(path)[1] for path in paths]
    full_canvas_bytes = len(paths) * CANVAS_WIDTH * CANVAS_HEIGHT * 4
    sparse_input_bytes = sum(area * 4 for area in input_areas)
    result = {
        "method": "direct-cropped-raster-overlay; no scale, rotation, perspective, masks, or effects",
        "inputCount": len(paths),
        "croppedInputCount": sum(1 for row in mapped[1:] if row[5]),
        "fullCanvasFallbackCount": 1,
        "canvas": [CANVAS_WIDTH, CANVAS_HEIGHT],
        "decodedInputBytesEstimate": {
            "fullCanvasBaseline": full_canvas_bytes,
            "sparseRoiInputs": sparse_input_bytes,
            "reductionPercent": round(100 * (1 - sparse_input_bytes / full_canvas_bytes), 3),
        },
        "wallSeconds": round(elapsed, 3),
        "maxRssBytes": int(usage.ru_maxrss),
        "baselineFrameRecord": baseline,
        "candidateFrameRecord": candidate,
        "exactFrameHashMatch": candidate == baseline,
        "baselineMeasurement": json.loads((EXPERIMENT / "measurements.json").read_text())["original"],
        "scope": "One frame through the exact preserved 112-title overlay order, with original authored position baked into PNG pixels. This proves only identity-transform ROI composition.",
    }
    (OUT / "measurements.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    if not result["exactFrameHashMatch"]:
        raise SystemExit(2)


if __name__ == "__main__":
    run()
