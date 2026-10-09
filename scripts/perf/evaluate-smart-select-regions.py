#!/usr/bin/env python3
"""Score a prompted mask against manually annotated positive/negative image regions."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


def polygon_contains(x: float, y: float, polygon: list[list[float]]) -> bool:
    inside = False
    previous = polygon[-1]
    for current in polygon:
        x0, y0 = previous
        x1, y1 = current
        if (y0 > y) != (y1 > y):
            crossing_x = (x1 - x0) * (y - y0) / (y1 - y0) + x0
            if x < crossing_x:
                inside = not inside
        previous = current
    return inside


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("annotation", type=Path, help="JSON annotation with normalized polygons")
    parser.add_argument("mask", type=Path, help="8-bit mask PNG produced for the annotated source")
    parser.add_argument(
        "--ffmpeg", default="ffmpeg", help="ffmpeg executable used to decode grayscale PNG pixels"
    )
    args = parser.parse_args()

    annotation: dict[str, Any] = json.loads(args.annotation.read_text(encoding="utf-8"))
    if annotation.get("schema") != "looksy-smart-select-region-annotation-v1":
        parser.error("unsupported region-annotation schema")
    source = annotation["source"]
    width = int(source["width"])
    height = int(source["height"])
    decoded = subprocess.run(
        [args.ffmpeg, "-v", "error", "-i", str(args.mask), "-f", "rawvideo", "-pix_fmt", "gray", "-"],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if decoded.returncode:
        parser.error(f"ffmpeg could not decode mask: {decoded.stderr.decode(errors='replace')}")
    if len(decoded.stdout) != width * height:
        parser.error(
            f"mask dimensions do not match annotation: expected {width * height} bytes, "
            f"received {len(decoded.stdout)}"
        )

    threshold = int(annotation["maskThreshold"])
    all_pass = True
    for region in annotation["regions"]:
        polygon = region["polygon"]
        min_x = max(0, int(min(point[0] for point in polygon) * width))
        max_x = min(width, int(max(point[0] for point in polygon) * width + 0.999999))
        min_y = max(0, int(min(point[1] for point in polygon) * height))
        max_y = min(height, int(max(point[1] for point in polygon) * height + 0.999999))
        selected = 0
        total = 0
        for y in range(min_y, max_y):
            normalized_y = (y + 0.5) / height
            for x in range(min_x, max_x):
                if polygon_contains((x + 0.5) / width, normalized_y, polygon):
                    total += 1
                    selected += decoded.stdout[y * width + x] >= threshold
        if total == 0:
            parser.error(f"annotation region {region['id']} contains no pixels")
        coverage = selected / total
        if region["label"] == "positive":
            passed = coverage >= float(annotation["minimumTargetCoverage"])
        elif region["label"] == "negative":
            passed = coverage <= float(annotation["maximumExcludedCoverage"])
        else:
            parser.error(f"unsupported label in region {region['id']}")
        all_pass = all_pass and passed
        print(
            f"region={region['id']} label={region['label']} coverage={coverage:.6f} "
            f"pixels={selected}/{total} passed={str(passed).lower()}"
        )
    print(f"region_quality={'passed' if all_pass else 'failed'}")
    return 0 if all_pass else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError, KeyError, TypeError, ValueError) as error:
        print(f"region quality evaluation failed: {error}", file=sys.stderr)
        raise SystemExit(2)
