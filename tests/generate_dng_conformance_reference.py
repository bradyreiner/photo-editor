#!/usr/bin/env python3
"""Generate project-authored DNG profile-stage conformance fixtures.

The implementation below is intentionally self-contained.  It translates the
interpolation and rendering equations published by the DNG specification into
plain Python and does not load, build, or execute the Adobe DNG SDK.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import struct
from pathlib import Path
from typing import Callable, Iterable, Sequence


PROFILE_STAGE_BASENAME = "dng-conformance-profile-stages-v1"
GAIN_TABLE_BASENAME = "dng-conformance-profile-gain-table-map-v1"
CAMERA_TRANSFORM_BASENAME = "dng-conformance-camera-transform-v1"
RGB_TABLES_BASENAME = "dng-conformance-rgb-tables-v1"
SPECIFICATION = "Digital Negative (DNG) Specification 1.7.1.0"

SAMPLES = (
    (0.0, 0.0, 0.0),
    (1.0, 1.0, 1.0),
    (0.18, 0.18, 0.18),
    (1.0, 0.0, 0.0),
    (0.0, 1.0, 0.0),
    (0.0, 0.0, 1.0),
    (0.91, 0.12, 0.07),
    (0.07, 0.62, 0.94),
    (0.42, 0.31, 0.73),
)

# Robertson/Wyszecki and Stiles isotemperature-line data, published in
# Wyszecki & Stiles, Color Science, 2nd edition, Table 1(3.11).
TEMPERATURE_TABLE = (
    (0.0, 0.18006, 0.26352, -0.24341),
    (10.0, 0.18066, 0.26589, -0.25479),
    (20.0, 0.18133, 0.26846, -0.26876),
    (30.0, 0.18208, 0.27119, -0.28539),
    (40.0, 0.18293, 0.27407, -0.30470),
    (50.0, 0.18388, 0.27709, -0.32675),
    (60.0, 0.18494, 0.28021, -0.35156),
    (70.0, 0.18611, 0.28342, -0.37915),
    (80.0, 0.18740, 0.28668, -0.40955),
    (90.0, 0.18880, 0.28997, -0.44278),
    (100.0, 0.19032, 0.29326, -0.47888),
    (125.0, 0.19462, 0.30141, -0.58204),
    (150.0, 0.19962, 0.30921, -0.70471),
    (175.0, 0.20525, 0.31647, -0.84901),
    (200.0, 0.21142, 0.32312, -1.0182),
    (225.0, 0.21807, 0.32909, -1.2168),
    (250.0, 0.22511, 0.33439, -1.4512),
    (275.0, 0.23247, 0.33904, -1.7298),
    (300.0, 0.24010, 0.34308, -2.0637),
    (325.0, 0.24702, 0.34655, -2.4681),
    (350.0, 0.25591, 0.34951, -2.9641),
    (375.0, 0.26400, 0.35200, -3.5814),
    (400.0, 0.27218, 0.35407, -4.3633),
    (425.0, 0.28039, 0.35577, -5.3762),
    (450.0, 0.28863, 0.35714, -6.7262),
    (475.0, 0.29685, 0.35823, -8.5955),
    (500.0, 0.30505, 0.35907, -11.324),
    (525.0, 0.31320, 0.35968, -15.628),
    (550.0, 0.32129, 0.36011, -23.325),
    (575.0, 0.32931, 0.36038, -40.770),
    (600.0, 0.33724, 0.36051, -116.45),
)

# CIE 1931 2-degree standard observer sampled at the same 20 nm interval as
# this deliberately compact spectrum fixture (CIE 15 / ISO 11664-1).
CIE_1931_2DEG_20NM = (
    (0.001368, 0.000039, 0.006450001),
    (0.01431, 0.000396, 0.06785001),
    (0.13438, 0.004, 0.6456),
    (0.34828, 0.023, 1.74706),
    (0.2908, 0.06, 1.6692),
    (0.09564, 0.13902, 0.8129501),
    (0.0049, 0.323, 0.272),
    (0.06327, 0.71, 0.07824999),
    (0.2904, 0.954, 0.0203),
    (0.5945, 0.995, 0.0039),
    (0.9163, 0.87, 0.001650001),
    (1.0622, 0.631, 0.0008),
    (0.8544499, 0.381, 0.00019),
    (0.4479, 0.175, 0.00002),
    (0.1649, 0.061, 0.0),
    (0.04677, 0.017, 0.0),
    (0.01135916, 0.004102, 0.0),
    (0.002899327, 0.001047, 0.0),
    (0.0006900786, 0.0002492, 0.0),
    (0.0001661505, 0.00006, 0.0),
    (0.00004150994, 0.00001499, 0.0),
    (0.00001025398, 0.0000037029, 0.0),
    (0.000002522525, 0.00000091093, 0.0),
)

SPECTRUM_SAMPLES = (
    0.35, 0.41, 0.52, 0.65, 0.78, 0.90, 0.98, 1.00,
    0.97, 0.92, 0.88, 0.85, 0.83, 0.82, 0.81, 0.80,
    0.78, 0.75, 0.72, 0.69, 0.65, 0.61, 0.57,
)

COLOR_MATRIX_1 = (
    1.0, 0.012, 0.012,
    0.012, 1.0, 0.012,
    0.012, 0.012, 1.0,
)
COLOR_MATRIX_2 = (
    1.0, -0.008, -0.008,
    -0.008, 1.0, -0.008,
    -0.008, -0.008, 1.0,
)
COLOR_MATRIX_3 = (
    1.0, 0.003, 0.003,
    0.003, 1.0, 0.003,
    0.003, 0.003, 1.0,
)
FORWARD_MATRIX_1 = (
    0.9, 0.0321, 0.0321,
    0.02, 0.96, 0.02,
    0.0126, 0.0126, 0.8,
)
FORWARD_MATRIX_2 = (
    0.94, 0.0121, 0.0121,
    0.01, 0.98, 0.01,
    0.0051, 0.0051, 0.815,
)
FORWARD_MATRIX_3 = (
    0.92, 0.0221, 0.0221,
    0.015, 0.97, 0.015,
    0.0096, 0.0096, 0.806,
)


def f32(value: float) -> float:
    """Round a value to IEEE-754 binary32 without third-party dependencies."""
    return struct.unpack("=f", struct.pack("=f", value))[0]


def clamp(value: float, minimum: float, maximum: float) -> float:
    return min(max(value, minimum), maximum)


def lerp(first: float, second: float, amount: float) -> float:
    return first + amount * (second - first)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def text_number(value: float) -> str:
    if not math.isfinite(value):
        raise ValueError("fixtures may not contain non-finite values")
    return format(value, ".17g")


def rgb_to_hsv(rgb: Sequence[float]) -> tuple[float, float, float]:
    maximum = max(rgb)
    minimum = min(rgb)
    chroma = maximum - minimum
    saturation = chroma / maximum if maximum > 0.0 else 0.0
    if chroma <= 1.0e-15:
        return 0.0, saturation, maximum
    if maximum == rgb[0]:
        hue = (rgb[1] - rgb[2]) / chroma
        if hue < 0.0:
            hue += 6.0
    elif maximum == rgb[1]:
        hue = 2.0 + (rgb[2] - rgb[0]) / chroma
    else:
        hue = 4.0 + (rgb[0] - rgb[1]) / chroma
    return hue / 6.0, saturation, maximum


def hsv_to_rgb(hsv: Sequence[float]) -> tuple[float, float, float]:
    hue = hsv[0] - math.floor(hsv[0])
    saturation = clamp(hsv[1], 0.0, 1.0)
    hue_six = hue * 6.0
    sector = math.floor(hue_six) % 6
    fraction = hue_six - math.floor(hue_six)
    value = hsv[2]
    p = value * (1.0 - saturation)
    q = value * (1.0 - saturation * fraction)
    t = value * (1.0 - saturation * (1.0 - fraction))
    return (
        (value, t, p),
        (q, value, p),
        (p, value, t),
        (p, q, value),
        (t, p, value),
        (value, p, q),
    )[sector]


def srgb_encode(value: float) -> float:
    value = clamp(value, 0.0, 1.0)
    return 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055


def srgb_decode(value: float) -> float:
    value = clamp(value, 0.0, 1.0)
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def make_hsv_table(variant: int = 0) -> tuple[tuple[float, float, float], ...]:
    entries: list[tuple[float, float, float]] = []
    for value in range(2):
        for hue in range(6):
            for saturation in range(3):
                entries.append(
                    (
                        f32((f32(float(hue)) - f32(2.5)) * f32(1.75) + f32(float(saturation)) * f32(0.5) + f32(float(value)) * f32(0.25) + f32(float(variant)) * f32(0.6)),
                        f32(f32(0.82) + f32(float(hue)) * f32(0.04) + f32(float(saturation)) * f32(0.07) + f32(float(value)) * f32(0.03) + f32(float(variant)) * f32(0.025)),
                        1.0 if saturation == 0 else f32(f32(0.91) + f32(float(hue)) * f32(0.02) + f32(float(saturation)) * f32(0.025) + f32(float(value)) * f32(0.04) + f32(float(variant)) * f32(0.015)),
                    )
                )
    return tuple(entries)


def sample_hsv_table(
    table: Sequence[Sequence[float]],
    hsv: Sequence[float],
    srgb_value_encoding: bool,
) -> tuple[float, float, float]:
    hue_scaled = (hsv[0] - math.floor(hsv[0])) * 6.0
    saturation_scaled = clamp(hsv[1], 0.0, 1.0) * 2.0
    value_for_lookup = clamp(hsv[2], 0.0, 1.0)
    if srgb_value_encoding:
        value_for_lookup = srgb_encode(value_for_lookup)
    value_scaled = value_for_lookup
    hue0 = math.floor(hue_scaled)
    hue1 = hue0 + 1
    if hue0 >= 5:
        hue0 = 5
        hue1 = 0
    saturation0 = min(math.floor(saturation_scaled), 1)
    saturation1 = saturation0 + 1
    value0 = min(math.floor(value_scaled), 0)
    value1 = value0 + 1
    hue_fraction = hue_scaled - hue0
    saturation_fraction = saturation_scaled - saturation0
    value_fraction = value_scaled - value0

    def index(value: int, hue: int, saturation: int) -> int:
        return (value * 6 + hue) * 3 + saturation

    def component(component_index: int) -> float:
        def at(value: int, hue: int, saturation: int) -> float:
            return table[index(value, hue, saturation)][component_index]

        hue_sat0_value0 = lerp(at(value0, hue0, saturation0), at(value0, hue1, saturation0), hue_fraction)
        hue_sat1_value0 = lerp(at(value0, hue0, saturation1), at(value0, hue1, saturation1), hue_fraction)
        result0 = lerp(hue_sat0_value0, hue_sat1_value0, saturation_fraction)
        hue_sat0_value1 = lerp(at(value1, hue0, saturation0), at(value1, hue1, saturation0), hue_fraction)
        hue_sat1_value1 = lerp(at(value1, hue0, saturation1), at(value1, hue1, saturation1), hue_fraction)
        result1 = lerp(hue_sat0_value1, hue_sat1_value1, saturation_fraction)
        return lerp(result0, result1, value_fraction)

    return component(0), component(1), component(2)


def apply_hsv_delta(
    rgb: Sequence[float],
    delta: Sequence[float],
    srgb_value_encoding: bool,
) -> tuple[float, float, float]:
    proxy = tuple(clamp(channel, 0.0, 1.0) for channel in rgb)
    hue, saturation, value = rgb_to_hsv(proxy)
    hue += delta[0] / 360.0
    saturation = min(saturation * delta[1], 1.0)
    encoded_value = srgb_encode(value) if srgb_value_encoding else value
    encoded_value = clamp(encoded_value * delta[2], 0.0, 1.0)
    value = srgb_decode(encoded_value) if srgb_value_encoding else encoded_value
    transformed = hsv_to_rgb((hue, saturation, value))
    if all(0.0 <= channel <= 1.0 for channel in rgb):
        return transformed
    return tuple(rgb[index] + transformed[index] - proxy[index] for index in range(3))


def apply_hsv_table(
    rgb: Sequence[float],
    table: Sequence[Sequence[float]],
    srgb_value_encoding: bool = False,
    weights_and_tables: Sequence[tuple[float, Sequence[Sequence[float]]]] = (),
) -> tuple[float, float, float]:
    hsv = rgb_to_hsv(tuple(clamp(channel, 0.0, 1.0) for channel in rgb))
    if weights_and_tables:
        sampled = [
            (weight, sample_hsv_table(candidate, hsv, srgb_value_encoding))
            for weight, candidate in weights_and_tables
        ]
        delta = tuple(sum(weight * value[component] for weight, value in sampled) for component in range(3))
    else:
        delta = sample_hsv_table(table, hsv, srgb_value_encoding)
    return apply_hsv_delta(rgb, delta, srgb_value_encoding)


def solve_spline_slopes(points: Sequence[Sequence[float]]) -> list[float]:
    count = len(points)
    slopes = [0.0] * count
    interval = points[1][0] - points[0][0]
    secant = (points[1][1] - points[0][1]) / interval
    slopes[0] = secant
    for index in range(2, count):
        next_interval = points[index][0] - points[index - 1][0]
        next_secant = (points[index][1] - points[index - 1][1]) / next_interval
        slopes[index - 1] = (secant * next_interval + next_secant * interval) / (interval + next_interval)
        interval = next_interval
        secant = next_secant
    slopes[-1] = 2.0 * secant - slopes[-2]
    slopes[0] = 2.0 * slopes[0] - slopes[1]
    if count <= 2:
        return slopes
    lower = [0.0] * count
    upper = [0.0] * count
    rhs = [0.0] * count
    upper[0] = 0.5
    lower[-1] = 0.5
    rhs[0] = 0.75 * (slopes[0] + slopes[1])
    rhs[-1] = 0.75 * (slopes[-2] + slopes[-1])
    for index in range(1, count - 1):
        span = (points[index + 1][0] - points[index - 1][0]) * 2.0
        lower[index] = (points[index + 1][0] - points[index][0]) / span
        upper[index] = (points[index][0] - points[index - 1][0]) / span
        rhs[index] = 1.5 * slopes[index]
    for index in range(1, count):
        divisor = 1.0 - upper[index - 1] * lower[index]
        if index + 1 < count:
            upper[index] /= divisor
        rhs[index] = (rhs[index] - rhs[index - 1] * lower[index]) / divisor
    for index in range(count - 2, -1, -1):
        rhs[index] -= upper[index] * rhs[index + 1]
    return rhs


def evaluate_spline(points: Sequence[Sequence[float]], slopes: Sequence[float], value: float) -> float:
    if value <= points[0][0]:
        return points[0][1] + slopes[0] * (value - points[0][0])
    if value >= points[-1][0]:
        return points[-1][1] + slopes[-1] * (value - points[-1][0])
    upper = next(index for index, point in enumerate(points) if point[0] >= value)
    lower = upper - 1
    x0, y0 = points[lower]
    x1, y1 = points[upper]
    span = x1 - x0
    toward_upper = (value - x0) / span
    toward_lower = (x1 - value) / span
    return (
        (y0 * (2.0 - toward_lower + toward_upper) + slopes[lower] * span * toward_upper) * toward_lower * toward_lower
        + (y1 * (2.0 - toward_upper + toward_lower) - slopes[upper] * span * toward_lower) * toward_upper * toward_upper
    )


def apply_tone_curve(rgb: Sequence[float]) -> tuple[float, float, float]:
    points = tuple((f32(x), f32(y)) for x, y in ((0.0, 0.0), (0.2, 0.1), (0.5, 0.65), (0.8, 0.9), (1.0, 1.0)))
    slopes = solve_spline_slopes(points)
    minimum = min(range(3), key=lambda index: rgb[index])
    maximum = max(range(3), key=lambda index: rgb[index])
    result = [0.0, 0.0, 0.0]
    if abs(rgb[maximum] - rgb[minimum]) <= 1.0e-15:
        mapped = evaluate_spline(points, slopes, rgb[0])
        return mapped, mapped, mapped
    middle = 3 - minimum - maximum
    result[minimum] = evaluate_spline(points, slopes, rgb[minimum])
    result[maximum] = evaluate_spline(points, slopes, rgb[maximum])
    amount = (rgb[middle] - rgb[minimum]) / (rgb[maximum] - rgb[minimum])
    result[middle] = lerp(result[minimum], result[maximum], amount)
    return result[0], result[1], result[2]


def temperature_tint_to_xy(temperature: float, tint: float) -> tuple[float, float]:
    reciprocal = 1.0e6 / temperature
    tint_offset = tint / -3000.0
    for index in range(len(TEMPERATURE_TABLE) - 1):
        first = TEMPERATURE_TABLE[index]
        second = TEMPERATURE_TABLE[index + 1]
        if reciprocal >= second[0] and index + 2 < len(TEMPERATURE_TABLE):
            continue
        denominator = second[0] - first[0]
        first_weight = (second[0] - reciprocal) / denominator if denominator > 0.0 else 1.0
        first_weight = clamp(first_weight, 0.0, 1.0)
        u = lerp(second[1], first[1], first_weight)
        v = lerp(second[2], first[2], first_weight)
        first_direction = (1.0 / math.hypot(1.0, first[3]), first[3] / math.hypot(1.0, first[3]))
        second_direction = (1.0 / math.hypot(1.0, second[3]), second[3] / math.hypot(1.0, second[3]))
        u_direction = lerp(second_direction[0], first_direction[0], first_weight)
        v_direction = lerp(second_direction[1], first_direction[1], first_weight)
        length = math.hypot(u_direction, v_direction)
        u += u_direction / length * tint_offset
        v += v_direction / length * tint_offset
        denominator = u - 4.0 * v + 2.0
        return 1.5 * u / denominator, v / denominator
    raise AssertionError("temperature table is incomplete")


def xy_to_temperature_tint(xy: Sequence[float]) -> tuple[float, float]:
    denominator = 1.5 - xy[0] + 6.0 * xy[1]
    u = 2.0 * xy[0] / denominator
    v = 3.0 * xy[1] / denominator
    last_distance = 0.0
    last_direction = (0.0, 0.0)
    for index in range(1, len(TEMPERATURE_TABLE)):
        entry = TEMPERATURE_TABLE[index]
        length = math.hypot(1.0, entry[3])
        direction = (1.0 / length, entry[3] / length)
        u_offset = u - entry[1]
        v_offset = v - entry[2]
        distance = -u_offset * direction[1] + v_offset * direction[0]
        if distance > 0.0 and index + 1 < len(TEMPERATURE_TABLE):
            last_distance = distance
            last_direction = direction
            continue
        distance = max(-distance, 0.0)
        first_weight = 0.0 if index == 1 else distance / (last_distance + distance)
        reciprocal = lerp(entry[0], TEMPERATURE_TABLE[index - 1][0], first_weight)
        u_offset = u - lerp(entry[1], TEMPERATURE_TABLE[index - 1][1], first_weight)
        v_offset = v - lerp(entry[2], TEMPERATURE_TABLE[index - 1][2], first_weight)
        u_direction = lerp(direction[0], last_direction[0], first_weight)
        v_direction = lerp(direction[1], last_direction[1], first_weight)
        length = math.hypot(u_direction, v_direction)
        u_direction /= length
        v_direction /= length
        tint = (u_offset * u_direction + v_offset * v_direction) * -3000.0
        return 1.0e6 / reciprocal, tint
    raise AssertionError("chromaticity lies outside the temperature table")


def triple_weights(temperature: float, tint: float) -> tuple[float, float, float]:
    # Round-trip the requested source white because that is how a DNG profile
    # consumes the chromaticity, then compare it with the standard-light xy data.
    source = xy_to_temperature_tint(temperature_tint_to_xy(temperature, tint))
    calibration = tuple(
        xy_to_temperature_tint(xy)
        for xy in ((0.4476, 0.4074), (0.3127, 0.3290), (0.3457, 0.3585))
    )

    def mapped(value: Sequence[float]) -> tuple[float, float]:
        return value[1] / 200.0, min(1500.0 / value[0], 1.0)

    source_point = mapped(source)
    weights = []
    for value in calibration:
        point = mapped(value)
        delta_tint = source_point[0] - point[0]
        delta_temperature = source_point[1] - point[1]
        weights.append(1.0 / (delta_tint * delta_tint + delta_temperature * delta_temperature + 1.0e-8))
    total = sum(weights)
    weights = [value / total for value in weights]
    weights = [value * value * (3.0 - 2.0 * value) for value in weights]
    weights = [clamp((value - 0.02) / 0.98, 0.0, 1.0) for value in weights]
    total = sum(weights)
    first = weights[0] / total
    second = weights[1] / total
    return first, second, max(1.0 - first - second, 0.0)


def matrix_vector(matrix: Sequence[float], vector: Sequence[float]) -> tuple[float, float, float]:
    return tuple(
        sum(matrix[row * 3 + column] * vector[column] for column in range(3))
        for row in range(3)
    )


def matrix_product(first: Sequence[float], second: Sequence[float]) -> tuple[float, ...]:
    return tuple(
        sum(first[row * 3 + inner] * second[inner * 3 + column] for inner in range(3))
        for row in range(3)
        for column in range(3)
    )


def xyz_from_xy(xy: Sequence[float]) -> tuple[float, float, float]:
    return xy[0] / xy[1], 1.0, (1.0 - xy[0] - xy[1]) / xy[1]


def dng_round(value: float, factor: float = 10000.0) -> float:
    scaled = value * factor
    integral = math.floor(scaled + 0.5) if scaled >= 0.0 else math.ceil(scaled - 0.5)
    return integral / factor


def canonical_color_matrix(matrix: Sequence[float]) -> tuple[float, ...]:
    mapped_white = matrix_vector(matrix, xyz_from_xy((0.3457, 0.3585)))
    maximum = max(mapped_white)
    scale = 1.0 / maximum if maximum < 0.99 or maximum > 1.01 else 1.0
    return tuple(dng_round(value * scale) for value in matrix)


def canonical_forward_matrix(matrix: Sequence[float]) -> tuple[float, ...]:
    rounded = tuple(dng_round(value) for value in matrix)
    mapped_one = matrix_vector(rounded, (1.0, 1.0, 1.0))
    d50 = xyz_from_xy((0.3457, 0.3585))
    return tuple(
        rounded[row * 3 + column] * d50[row] / mapped_one[row]
        for row in range(3)
        for column in range(3)
    )


def interpolate_values(first: Sequence[float], second: Sequence[float], weight: float) -> tuple[float, ...]:
    return tuple(lerp(first[index], second[index], weight) for index in range(len(first)))


def weighted_values(
    first: Sequence[float],
    second: Sequence[float],
    third: Sequence[float],
    weights: Sequence[float],
) -> tuple[float, ...]:
    return tuple(
        first[index] * weights[0] + second[index] * weights[1] + third[index] * weights[2]
        for index in range(len(first))
    )


def reciprocal_temperature_weight(source: float, first: float, second: float) -> float:
    return clamp(
        (1.0 / source - 1.0 / first) / (1.0 / second - 1.0 / first),
        0.0,
        1.0,
    )


def spectrum_white_xy() -> tuple[float, float]:
    xyz = tuple(
        sum(sample * observer[channel] for sample, observer in zip(SPECTRUM_SAMPLES, CIE_1931_2DEG_20NM))
        for channel in range(3)
    )
    total = sum(xyz)
    return xyz[0] / total, xyz[1] / total


def camera_transform(
    profile_model: str,
    temperature: float,
    tint: float,
    calibration_white: Sequence[float],
) -> tuple[tuple[float, float], tuple[float, float], tuple[float, float, float], tuple[float, ...]]:
    source_white = temperature_tint_to_xy(temperature, tint)
    resolved_temperature, _ = xy_to_temperature_tint(source_white)
    first_temperature = (
        2850.0
        if profile_model in {"standard", "triple"}
        else xy_to_temperature_tint(calibration_white)[0]
    )
    second_weight = reciprocal_temperature_weight(resolved_temperature, first_temperature, 6500.0)
    weights = (1.0 - second_weight, second_weight, 0.0)
    if profile_model == "triple":
        weights = triple_weights(temperature, tint)

    color1 = canonical_color_matrix(COLOR_MATRIX_1)
    color2 = canonical_color_matrix(COLOR_MATRIX_2)
    if profile_model == "triple":
        color_matrix = weighted_values(
            color1,
            color2,
            canonical_color_matrix(COLOR_MATRIX_3),
            weights,
        )
    else:
        color_matrix = interpolate_values(color1, color2, second_weight)

    camera_white_unscaled = matrix_vector(color_matrix, xyz_from_xy(source_white))
    camera_white_maximum = max(camera_white_unscaled)
    camera_white = tuple(
        clamp(value / camera_white_maximum, 0.001, 1.0)
        for value in camera_white_unscaled
    )

    forward1 = canonical_forward_matrix(FORWARD_MATRIX_1)
    forward2 = canonical_forward_matrix(FORWARD_MATRIX_2)
    if profile_model == "triple":
        forward = weighted_values(
            forward1,
            forward2,
            canonical_forward_matrix(FORWARD_MATRIX_3),
            weights,
        )
    else:
        forward = interpolate_values(forward1, forward2, second_weight)
    inverse_camera_white = (
        1.0 / camera_white[0], 0.0, 0.0,
        0.0, 1.0 / camera_white[1], 0.0,
        0.0, 0.0, 1.0 / camera_white[2],
    )
    camera_to_xyz_d50 = matrix_product(forward, inverse_camera_white)
    calibration_temperature_tint = xy_to_temperature_tint(calibration_white)
    return source_white, calibration_temperature_tint, camera_white, camera_to_xyz_d50


def camera_transform_rows() -> list[list[str]]:
    spectrum_white = spectrum_white_xy()
    cases = (
        *(('standard', temperature, 12.0, (0.4476, 0.4074)) for temperature in (2856.0, 4309.89247311828, 5000.0, 6504.0)),
        *(('custom-xy', temperature, 12.0, (0.3457, 0.3585)) for temperature in (5000.0, 5800.0)),
        ('triple', 3200.0, -20.0, (0.4476, 0.4074)),
        ('triple', 5000.0, 12.0, (0.4476, 0.4074)),
        ('triple', 7000.0, 30.0, (0.4476, 0.4074)),
        ('spectrum', 4800.0, 5.0, spectrum_white),
        ('spectrum', 6200.0, -10.0, spectrum_white),
    )
    rows = []
    for profile_model, temperature, tint, calibration_white in cases:
        source_white, calibration_temperature_tint, camera_white, matrix = camera_transform(
            profile_model,
            temperature,
            tint,
            calibration_white,
        )
        case_id = f"{profile_model}-temperature-{temperature:g}-tint-{tint:g}"
        values = (
            temperature,
            tint,
            *source_white,
            *calibration_white,
            *calibration_temperature_tint,
            *camera_white,
            *matrix,
        )
        rows.append([case_id, profile_model, *(text_number(value) for value in values)])
    if len(rows) != 11:
        raise AssertionError(f"expected 11 camera-transform cases, produced {len(rows)}")
    return rows


def profile_stage_rows() -> list[list[str]]:
    table = make_hsv_table()
    rows: list[list[str]] = []
    stages: tuple[tuple[str, Callable[[Sequence[float]], tuple[float, float, float]]], ...] = (
        ("hue-sat-linear", lambda rgb: apply_hsv_table(rgb, table)),
        ("hue-sat-srgb", lambda rgb: apply_hsv_table(rgb, table, True)),
        ("look-linear", lambda rgb: apply_hsv_table(rgb, table)),
        ("look-srgb", lambda rgb: apply_hsv_table(rgb, table, True)),
        ("tone", apply_tone_curve),
    )
    for stage, operation in stages:
        for case_id, sample in enumerate(SAMPLES):
            rgb = tuple(f32(value) for value in sample)
            output = operation(rgb)
            rows.append([stage, str(case_id), *(text_number(value) for value in (*rgb, *output))])
    tables = (make_hsv_table(0), make_hsv_table(1), make_hsv_table(2))
    for white_index, white in enumerate(((3200.0, -20.0), (5000.0, 12.0), (7000.0, 30.0))):
        weights = triple_weights(*white)
        weighted_tables = tuple(zip(weights, tables))
        for case_id, sample in enumerate(SAMPLES):
            rgb = tuple(f32(value) for value in sample)
            output = apply_hsv_table(rgb, tables[0], weights_and_tables=weighted_tables)
            rows.append([f"hue-sat-triple-{white_index}", str(case_id), *(text_number(value) for value in (*rgb, *output))])
    if len(rows) != 72:
        raise AssertionError(f"expected 72 profile-stage cases, produced {len(rows)}")
    return rows


def f32_lerp(first: float, second: float, amount: float) -> float:
    return f32(f32(first) + f32(amount) * f32(f32(second) - f32(first)))


def gain_table_output(normalized_x: float, normalized_y: float, exposure_gain: float, rgb: Sequence[float]) -> tuple[float, float, float]:
    gains = (
        0.8, 1.0, 1.2, 1.4,
        1.1, 1.3, 1.5, 1.7,
        0.9, 1.15, 1.35, 1.6,
        1.2, 1.4, 1.8, 2.0,
    )
    gains = tuple(f32(value) for value in gains)
    x_map = clamp(f32(f32(f32(normalized_x) - f32(0.05)) / f32(0.75)), 0.0, 1.0)
    y_map = clamp(f32(f32(f32(normalized_y) - f32(0.10)) / f32(0.75)), 0.0, 1.0)
    x0 = int(x_map)
    y0 = int(y_map)
    x1 = min(x0 + 1, 1)
    y1 = min(y0 + 1, 1)
    xf = f32(x_map - f32(float(x0)))
    yf = f32(y_map - f32(float(y0)))
    red, green, blue = (f32(value) for value in rgb)
    minimum = min(red, green, blue)
    maximum = max(red, green, blue)
    weights = tuple(f32(value) for value in (0.25, 0.50, 0.10, 0.05, 0.10))
    weight = f32(weights[0] * red + weights[1] * green + weights[2] * blue + weights[3] * minimum + weights[4] * maximum)
    weight = f32(weight * f32(exposure_gain))
    weight = clamp(weight, 0.0, 1.0)
    weight = f32(math.pow(weight, f32(0.8)))
    weight_scaled = f32(weight * f32(4.0))
    w0 = min(int(weight_scaled), 3)
    w1 = min(w0 + 1, 3)
    wf = f32(weight_scaled - f32(float(w0)))

    def gain_at(y: int, x: int, w: int) -> float:
        return gains[(y * 2 + x) * 4 + w]

    gain00 = f32_lerp(gain_at(y0, x0, w0), gain_at(y0, x0, w1), wf)
    gain01 = f32_lerp(gain_at(y0, x1, w0), gain_at(y0, x1, w1), wf)
    gain10 = f32_lerp(gain_at(y1, x0, w0), gain_at(y1, x0, w1), wf)
    gain11 = f32_lerp(gain_at(y1, x1, w0), gain_at(y1, x1, w1), wf)
    gain0 = f32_lerp(gain00, gain01, xf)
    gain1 = f32_lerp(gain10, gain11, xf)
    gain = f32_lerp(gain0, gain1, yf)
    return f32(red * gain), f32(green * gain), f32(blue * gain)


def gain_table_rows() -> list[list[str]]:
    cases = (
        (0, 0, 1.0, (0.10, 0.20, 0.05)),
        (25, 10, 0.5, (0.80, 0.30, 0.10)),
        (50, 40, 1.25, (0.20, 0.40, 0.10)),
        (75, 65, 2.0, (0.05, 0.15, 0.90)),
        (99, 79, 1.0, (1.20, -0.10, 0.50)),
        (42, 31, 1.0, (0.0, 0.0, 0.0)),
    )
    rows = []
    for case_id, (x, y, exposure_gain, sample) in enumerate(cases):
        normalized_x = (x + 0.5) / 100.0
        normalized_y = (y + 0.5) / 80.0
        rgb = tuple(f32(value) for value in sample)
        output = gain_table_output(normalized_x, normalized_y, exposure_gain, rgb)
        rows.append([str(case_id), *(text_number(value) for value in (normalized_x, normalized_y, f32(exposure_gain), *rgb, *output))])
    return rows


def rgb_table_gamma_encode(gamma: int, value: float) -> float:
    value = clamp(value, 0.0, 1.0)
    if gamma == 0:
        return value
    if gamma == 1:
        return srgb_encode(value)
    if gamma == 2:
        return value ** (1.0 / 1.8)
    if gamma == 3:
        return value ** (1.0 / 2.2)
    alpha = 1.09929682680944
    beta = 0.018053968510807
    return 4.5 * value if value < beta else alpha * value ** 0.45 - (alpha - 1.0)


def rgb_table_gamma_decode(gamma: int, value: float) -> float:
    value = clamp(value, 0.0, 1.0)
    if gamma == 0:
        return value
    if gamma == 1:
        return srgb_decode(value)
    if gamma == 2:
        return value ** 1.8
    if gamma == 3:
        return value ** 2.2
    alpha = 1.09929682680944
    beta = 0.018053968510807
    return value / 4.5 if value < 4.5 * beta else ((value + alpha - 1.0) / alpha) ** (1.0 / 0.45)


def rgb_table_hdr_encode(value: float) -> float:
    sign = -1.0 if value < 0.0 else 1.0
    magnitude = abs(value)
    return sign * magnitude * (256.0 + magnitude) / (256.0 * (1.0 + magnitude))


def rgb_table_hdr_decode(value: float) -> float:
    sign = -1.0 if value < 0.0 else 1.0
    x = abs(value)
    return sign * 16.0 * (8.0 * x - 8.0 + math.sqrt(max(64.0 * x * x - 127.0 * x + 64.0, 0.0)))


def sample_rgb_table(encoded_rgb: Sequence[float], swap: str) -> tuple[float, float, float]:
    # A two-division table whose vertex outputs are either an R/B or R/G swap.
    # Trilinear interpolation of these vertices is exactly the same permutation.
    red, green, blue = encoded_rgb
    return (blue, green, red) if swap == "rb" else (green, red, blue)


def apply_rgb_table(
    rgb: Sequence[float],
    gamma: int,
    swap: str,
    hdr: bool,
    gamut_extension: bool,
) -> tuple[float, float, float]:
    encoded = tuple(rgb_table_hdr_encode(value) for value in rgb) if hdr else tuple(rgb)
    clipped = tuple(clamp(value, 0.0, 1.0) for value in encoded)
    delta = tuple(encoded[index] - clipped[index] if gamut_extension else 0.0 for index in range(3))
    lookup = tuple(rgb_table_gamma_encode(gamma, value) for value in clipped)
    sampled = sample_rgb_table(lookup, swap)
    decoded = tuple(rgb_table_gamma_decode(gamma, sampled[index]) + delta[index] for index in range(3))
    return tuple(rgb_table_hdr_decode(value) for value in decoded) if hdr else decoded


def rgb_tables_output(
    composite: str,
    gamma: int,
    hdr: bool,
    gamut_extension: bool,
    mask_weight: float,
    rgb: Sequence[float],
) -> tuple[float, float, float]:
    mask_weight = clamp(mask_weight, 0.0, 1.0)
    background_weight = 1.0 - mask_weight
    foreground = apply_rgb_table(rgb, gamma, "rb", hdr, gamut_extension)
    if composite == "weighted":
        background = apply_rgb_table(rgb, gamma, "rg", hdr, gamut_extension)
        return tuple(mask_weight * foreground[index] + background_weight * background[index] for index in range(3))
    first = tuple(lerp(rgb[index], foreground[index], mask_weight) for index in range(3))
    background = apply_rgb_table(first, gamma, "rg", hdr, gamut_extension)
    return tuple(lerp(first[index], background[index], background_weight) for index in range(3))


def rgb_tables_rows() -> list[list[str]]:
    samples = (
        (0.2, 0.4, 0.8, 0.25, False),
        (0.91, 0.12, 0.07, 0.0, False),
        (0.07, 0.62, 0.94, 1.0, False),
        (0.42, 0.31, 0.73, 0.6, False),
        (1.2, -0.1, 0.5, 0.4, True),
    )
    rows: list[list[str]] = []
    for composite in ("weighted", "sequential"):
        for gamma in range(5):
            for sample_index, (red, green, blue, mask_weight, hdr) in enumerate(samples):
                rgb = red, green, blue
                output = rgb_tables_output(composite, gamma, hdr, True, mask_weight, rgb)
                rows.append([
                    f"{composite}-gamma-{gamma}-sample-{sample_index}",
                    composite,
                    str(gamma),
                    "1" if hdr else "0",
                    "1",
                    text_number(mask_weight),
                    *(text_number(value) for value in (*rgb, *output)),
                ])
    if len(rows) != 50:
        raise AssertionError(f"expected 50 RGBTables cases, produced {len(rows)}")
    return rows


def write_csv(path: Path, header: Iterable[str], rows: Iterable[Sequence[str]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(header)
        writer.writerows(rows)


def write_metadata(path: Path, content: dict[str, object]) -> None:
    path.write_text(json.dumps(content, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=Path(__file__).resolve().parent / "fixtures" / "color")
    arguments = parser.parse_args()
    output_dir = arguments.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    script_path = Path(__file__).resolve()
    repository_root = script_path.parent.parent
    generator = str(script_path.relative_to(repository_root))
    generator_sha256 = sha256(script_path)

    camera_csv = output_dir / f"{CAMERA_TRANSFORM_BASENAME}.csv"
    write_csv(camera_csv, (
        "case_id", "profile_model", "temperature_kelvin", "tint",
        "white_x", "white_y", "calibration_white_x", "calibration_white_y",
        "calibration_temperature_kelvin", "calibration_tint",
        "camera_white_r", "camera_white_g", "camera_white_b",
        "camera_to_xyz_d50_0_0", "camera_to_xyz_d50_0_1", "camera_to_xyz_d50_0_2",
        "camera_to_xyz_d50_1_0", "camera_to_xyz_d50_1_1", "camera_to_xyz_d50_1_2",
        "camera_to_xyz_d50_2_0", "camera_to_xyz_d50_2_1", "camera_to_xyz_d50_2_2",
    ), camera_transform_rows())
    camera_metadata = output_dir / f"{CAMERA_TRANSFORM_BASENAME}.json"
    write_metadata(camera_metadata, {
        "authority": SPECIFICATION,
        "case_count": 11,
        "comparison_tolerance": {
            "camera_to_xyz_d50_absolute": 2e-8,
            "custom_calibration_temperature_kelvin_absolute": 1e-3,
            "white_xy_absolute": 1e-12,
        },
        "fixture_csv": camera_csv.name,
        "fixture_csv_sha256": sha256(camera_csv),
        "generator": generator,
        "generator_sha256": generator_sha256,
        "license": "Project-authored test vectors generated from public DNG, CIE, and color-science formulas",
        "method": "Independent reciprocal-temperature interpolation, DNG matrix normalization, camera-white normalization, forward-matrix execution, and triple-illuminant weighting",
        "profile": {
            "analog_balance": [1.0, 1.0, 1.0],
            "calibration_illuminant1": "Standard Light A (17)",
            "calibration_illuminant2": "D65 (21)",
            "camera_calibration_signature": "shared-calibration",
            "matrix_values": "defined verbatim in the generator source",
            "profile_calibration_signature": "shared-calibration",
            "profile_models": ["standard illuminants", "custom xy IlluminantData1", "DNG 1.6 triple illuminants", "DNG 1.6 spectrum-form IlluminantData1"],
            "spectrum": {
                "minimum_wavelength_nm": 380,
                "observer": "CIE 1931 2-degree standard observer at 20 nm",
                "sample_denominator": 10000,
                "sample_numerators": [3500, 4100, 5200, 6500, 7800, 9000, 9800, 10000, 9700, 9200, 8800, 8500, 8300, 8200, 8100, 8000, 7800, 7500, 7200, 6900, 6500, 6100, 5700],
                "wavelength_spacing_nm": 20,
            },
        },
        "schema": "dng-conformance-camera-transform:v1",
    })

    profile_csv = output_dir / f"{PROFILE_STAGE_BASENAME}.csv"
    write_csv(profile_csv, ("stage", "case_id", "input_r", "input_g", "input_b", "output_r", "output_g", "output_b"), profile_stage_rows())
    profile_metadata = output_dir / f"{PROFILE_STAGE_BASENAME}.json"
    write_metadata(profile_metadata, {
        "authority": SPECIFICATION,
        "case_count": 72,
        "comparison_tolerance": {"rgb_absolute": 2e-6},
        "domain": "standard RGB [0, 1]",
        "fixture_csv": profile_csv.name,
        "fixture_csv_sha256": sha256(profile_csv),
        "generator": generator,
        "generator_sha256": generator_sha256,
        "license": "Project-authored test vectors generated from public DNG and color-science formulas",
        "method": "Independent HSV trilinear interpolation, sRGB value encoding, natural cubic spline tone curve, and triple-illuminant weighting",
        "schema": "dng-conformance-profile-stages:v1",
        "stage_case_count": 9,
        "stages": ["hue-sat-linear", "hue-sat-srgb", "look-linear", "look-srgb", "tone", "hue-sat-triple-0", "hue-sat-triple-1", "hue-sat-triple-2"],
        "table_dimensions": [6, 3, 2],
        "tone_curve": [[0.0, 0.0], [0.2, 0.1], [0.5, 0.65], [0.8, 0.9], [1.0, 1.0]],
    })

    gain_csv = output_dir / f"{GAIN_TABLE_BASENAME}.csv"
    write_csv(gain_csv, ("case_id", "normalized_x", "normalized_y", "exposure_gain", "input_r", "input_g", "input_b", "output_r", "output_g", "output_b"), gain_table_rows())
    gain_metadata = output_dir / f"{GAIN_TABLE_BASENAME}.json"
    write_metadata(gain_metadata, {
        "authority": SPECIFICATION,
        "case_count": 6,
        "comparison_tolerance": {"rgb_absolute": 2e-7},
        "fixture_csv": gain_csv.name,
        "fixture_csv_sha256": sha256(gain_csv),
        "gain_values": [[[0.8, 1.0, 1.2, 1.4], [1.1, 1.3, 1.5, 1.7]], [[0.9, 1.15, 1.35, 1.6], [1.2, 1.4, 1.8, 2.0]]],
        "gamma": 0.8,
        "generator": generator,
        "generator_sha256": generator_sha256,
        "image_dimensions": [100, 80],
        "license": "Project-authored test vectors generated from public DNG and color-science formulas",
        "map_input_weights": [0.25, 0.50, 0.10, 0.05, 0.10],
        "map_origin_vh": [0.10, 0.05],
        "map_points": [2, 2, 4],
        "map_spacing_vh": [0.75, 0.75],
        "method": "Independent float32 spatial and weight-axis interpolation following ProfileGainTableMap2",
        "schema": "dng-conformance-profile-gain-table-map:v1",
        "support_overrange": True,
    })
    rgb_tables_csv = output_dir / f"{RGB_TABLES_BASENAME}.csv"
    write_csv(rgb_tables_csv, (
        "case_id", "composite", "gamma", "hdr", "gamut_extension",
        "mask_weight", "input_r", "input_g", "input_b",
        "output_r", "output_g", "output_b",
    ), rgb_tables_rows())
    rgb_tables_metadata = output_dir / f"{RGB_TABLES_BASENAME}.json"
    write_metadata(rgb_tables_metadata, {
        "authority": SPECIFICATION,
        "case_count": 50,
        "comparison_tolerance": {"rgb_absolute": 2e-7},
        "composite_methods": ["weighted sum", "sequential"],
        "fixture_csv": rgb_tables_csv.name,
        "fixture_csv_sha256": sha256(rgb_tables_csv),
        "gamma_encodings": ["linear", "sRGB", "gamma 1.8", "gamma 2.2", "Rec. 2020"],
        "generator": generator,
        "generator_sha256": generator_sha256,
        "license": "Project-authored test vectors generated from public DNG and color-science formulas",
        "method": "Independent RGBTables trilinear interpolation, gamma, HDR, gamut-extension, semantic-mask, background, weighted-sum, and sequential composition",
        "primaries": "linear ProPhoto RGB profile and table space",
        "schema": "dng-conformance-rgb-tables:v1",
        "table_dimensions": [2, 2, 2],
    })
    print(camera_csv)
    print(camera_metadata)
    print(profile_csv)
    print(profile_metadata)
    print(gain_csv)
    print(gain_metadata)
    print(rgb_tables_csv)
    print(rgb_tables_metadata)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
