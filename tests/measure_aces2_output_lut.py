#!/usr/bin/env python3
"""Measure AP1 log-shaper plus 3D-LUT approximations of the ACES 2 output.

This is an engineering sweep, not a fixture generator. It evaluates candidate
cube sizes and shaper offsets against the pinned OpenColorIO reference corpus
before any representation is allowed into the Metal preview path.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path

import PyOpenColorIO as ocio


AP0_TO_AP1 = (
    (1.4514393161, -0.2365107469, -0.2149285693),
    (-0.0765537734, 1.1762296998, -0.0996759264),
    (0.0083161484, -0.0060324498, 0.9977163014),
)
AP1_TO_AP0 = (
    (0.6954522414, 0.1406786965, 0.1638690622),
    (0.0447945634, 0.8596711185, 0.0955343182),
    (-0.0055258826, 0.0040252103, 1.0015006723),
)
D65 = (0.95047, 1.0, 1.08883)


def multiply(matrix: tuple[tuple[float, ...], ...], vector: tuple[float, ...]) -> tuple[float, ...]:
    return tuple(sum(row[index] * vector[index] for index in range(3)) for row in matrix)


def linear_srgb_to_lab(rgb: tuple[float, float, float]) -> tuple[float, float, float]:
    xyz = (
        0.4124564 * rgb[0] + 0.3575761 * rgb[1] + 0.1804375 * rgb[2],
        0.2126729 * rgb[0] + 0.7151522 * rgb[1] + 0.0721750 * rgb[2],
        0.0193339 * rgb[0] + 0.1191920 * rgb[1] + 0.9503041 * rgb[2],
    )

    def f(value: float) -> float:
        delta = 6.0 / 29.0
        return value ** (1.0 / 3.0) if value > delta**3 else value / (3.0 * delta * delta) + 4.0 / 29.0

    fx, fy, fz = (f(xyz[index] / D65[index]) for index in range(3))
    return (116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz))


def delta_e_2000(left: tuple[float, float, float], right: tuple[float, float, float]) -> float:
    l1, a1, b1 = left
    l2, a2, b2 = right
    c1 = math.hypot(a1, b1)
    c2 = math.hypot(a2, b2)
    mean_c = (c1 + c2) * 0.5
    mean_c7 = mean_c**7
    g = 0.5 * (1.0 - math.sqrt(mean_c7 / (mean_c7 + 25.0**7)))
    a1p = (1.0 + g) * a1
    a2p = (1.0 + g) * a2
    c1p = math.hypot(a1p, b1)
    c2p = math.hypot(a2p, b2)

    def hue(a: float, b: float) -> float:
        if abs(a) < 1.0e-15 and abs(b) < 1.0e-15:
            return 0.0
        value = math.degrees(math.atan2(b, a))
        return value + 360.0 if value < 0.0 else value

    h1p = hue(a1p, b1)
    h2p = hue(a2p, b2)
    delta_l = l2 - l1
    delta_c = c2p - c1p
    if c1p * c2p == 0.0:
        delta_h_degrees = 0.0
    elif abs(h2p - h1p) <= 180.0:
        delta_h_degrees = h2p - h1p
    elif h2p <= h1p:
        delta_h_degrees = h2p - h1p + 360.0
    else:
        delta_h_degrees = h2p - h1p - 360.0
    delta_h = 2.0 * math.sqrt(c1p * c2p) * math.sin(math.radians(delta_h_degrees * 0.5))
    mean_l = (l1 + l2) * 0.5
    mean_cp = (c1p + c2p) * 0.5
    if c1p * c2p == 0.0:
        mean_h = h1p + h2p
    elif abs(h1p - h2p) <= 180.0:
        mean_h = (h1p + h2p) * 0.5
    elif h1p + h2p < 360.0:
        mean_h = (h1p + h2p + 360.0) * 0.5
    else:
        mean_h = (h1p + h2p - 360.0) * 0.5
    t = (
        1.0
        - 0.17 * math.cos(math.radians(mean_h - 30.0))
        + 0.24 * math.cos(math.radians(2.0 * mean_h))
        + 0.32 * math.cos(math.radians(3.0 * mean_h + 6.0))
        - 0.20 * math.cos(math.radians(4.0 * mean_h - 63.0))
    )
    sl = 1.0 + 0.015 * (mean_l - 50.0) ** 2 / math.sqrt(20.0 + (mean_l - 50.0) ** 2)
    sc = 1.0 + 0.045 * mean_cp
    sh = 1.0 + 0.015 * mean_cp * t
    delta_theta = 30.0 * math.exp(-((mean_h - 275.0) / 25.0) ** 2)
    mean_cp7 = mean_cp**7
    rc = 2.0 * math.sqrt(mean_cp7 / (mean_cp7 + 25.0**7))
    rt = -math.sin(math.radians(2.0 * delta_theta)) * rc
    l_term = delta_l / sl
    c_term = delta_c / sc
    h_term = delta_h / sh
    return math.sqrt(l_term * l_term + c_term * c_term + h_term * h_term + rt * c_term * h_term)


def decode_shaper(value: float, maximum: float, offset: float) -> float:
    return offset * math.expm1(value * math.log1p(maximum / offset))


def encode_shaper(value: float, maximum: float, offset: float) -> float:
    return math.log1p(max(0.0, min(maximum, value)) / offset) / math.log1p(maximum / offset)


def table_index(size: int, red: int, green: int, blue: int) -> int:
    return ((red * size + green) * size + blue) * 3


def tetrahedral_sample(
    table: list[float], size: int, coordinates: tuple[float, float, float]
) -> tuple[float, float, float]:
    scaled = tuple(max(0.0, min(1.0, value)) * (size - 1) for value in coordinates)
    lower = tuple(min(int(value), size - 2) for value in scaled)
    fraction = tuple(scaled[index] - lower[index] for index in range(3))
    r, g, b = lower
    fr, fg, fb = fraction

    def sample(dr: int, dg: int, db: int) -> tuple[float, float, float]:
        index = table_index(size, r + dr, g + dg, b + db)
        return (table[index], table[index + 1], table[index + 2])

    c000 = sample(0, 0, 0)
    c111 = sample(1, 1, 1)
    if fr >= fg:
        if fg >= fb:
            vertices = (c000, sample(1, 0, 0), sample(1, 1, 0), c111)
            weights = (1.0 - fr, fr - fg, fg - fb, fb)
        elif fr >= fb:
            vertices = (c000, sample(1, 0, 0), sample(1, 0, 1), c111)
            weights = (1.0 - fr, fr - fb, fb - fg, fg)
        else:
            vertices = (c000, sample(0, 0, 1), sample(1, 0, 1), c111)
            weights = (1.0 - fb, fb - fr, fr - fg, fg)
    elif fr >= fb:
        vertices = (c000, sample(0, 1, 0), sample(1, 1, 0), c111)
        weights = (1.0 - fg, fg - fr, fr - fb, fb)
    elif fg >= fb:
        vertices = (c000, sample(0, 1, 0), sample(0, 1, 1), c111)
        weights = (1.0 - fg, fg - fb, fb - fr, fr)
    else:
        vertices = (c000, sample(0, 0, 1), sample(0, 1, 1), c111)
        weights = (1.0 - fb, fb - fg, fg - fr, fr)
    return tuple(sum(vertices[index][channel] * weights[index] for index in range(4)) for channel in range(3))


def build_table(processor: ocio.CPUProcessor, size: int, maximum: float, offset: float) -> list[float]:
    values = [decode_shaper(index / (size - 1), maximum, offset) for index in range(size)]
    ap0_samples: list[float] = []
    for red in values:
        for green in values:
            for blue in values:
                ap0_samples.extend(multiply(AP1_TO_AP0, (red, green, blue)))
    # Preserve the smooth transform through interpolation. Physical display-cube
    # containment is applied after sampling, matching the exact engine path.
    return list(processor.applyRGB(ap0_samples))


def load_patches(path: Path) -> list[tuple[str, tuple[float, float, float], tuple[float, float, float]]]:
    patches = []
    with path.open(newline="", encoding="utf-8") as input_file:
        for row in csv.DictReader(input_file):
            patches.append(
                (
                    row["patch_id"],
                    tuple(float(row[f"aces2065_{channel}"]) for channel in "rgb"),
                    tuple(float(row[f"ocio_srgb_linear_{channel}"]) for channel in "rgb"),
                )
            )
    return patches


def percentile(values: list[float], fraction: float) -> float:
    return values[min(len(values) - 1, max(0, math.ceil(len(values) * fraction) - 1))]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--fixture",
        type=Path,
        default=Path(__file__).parent / "fixtures/color/aces2-ocio-reference-v1.csv",
    )
    parser.add_argument("--sizes", default="17,33,65,97")
    parser.add_argument("--offsets", default="0.003,0.01,0.03,0.1,0.18")
    arguments = parser.parse_args()

    transform = ocio.FixedFunctionTransform(
        ocio.FIXED_FUNCTION_ACES_OUTPUT_TRANSFORM_20,
        [100.0, 0.64, 0.33, 0.30, 0.60, 0.15, 0.06, 0.3127, 0.3290],
    )
    processor = ocio.Config.CreateRaw().getProcessor(transform).getDefaultCPUProcessor()
    patches = load_patches(arguments.fixture)
    maximum = 1024.0
    print("size,offset,p50_delta_e_00,p95_delta_e_00,p99_delta_e_00,max_delta_e_00,worst_patch")
    for size in (int(value) for value in arguments.sizes.split(",")):
        for offset in (float(value) for value in arguments.offsets.split(",")):
            table = build_table(processor, size, maximum, offset)
            errors: list[tuple[float, str]] = []
            for patch_id, ap0, expected in patches:
                ap1 = tuple(max(0.0, min(maximum, value)) for value in multiply(AP0_TO_AP1, ap0))
                coordinates = tuple(encode_shaper(value, maximum, offset) for value in ap1)
                actual = tuple(
                    max(0.0, min(1.0, value))
                    for value in tetrahedral_sample(table, size, coordinates)
                )
                errors.append(
                    (delta_e_2000(linear_srgb_to_lab(actual), linear_srgb_to_lab(expected)), patch_id)
                )
            errors.sort()
            values = [error for error, _ in errors]
            print(
                f"{size},{offset:.6g},{percentile(values, 0.50):.9g},"
                f"{percentile(values, 0.95):.9g},{percentile(values, 0.99):.9g},"
                f"{values[-1]:.9g},{errors[-1][1]}"
            )


if __name__ == "__main__":
    main()
