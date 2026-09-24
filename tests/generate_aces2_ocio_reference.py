#!/usr/bin/env python3
"""Generate the pinned ACES 2 OCIO SDR reference corpus.

The resulting CSV is consumed by a dependency-free native test. Regeneration
requires PyOpenColorIO 2.5+ because ACES 2 built-in transforms first shipped in
that configuration generation.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
import math
import random
from pathlib import Path

import PyOpenColorIO as ocio


CONFIG_NAME = "cg-config-v4.0.0_aces-v2.0_ocio-v2.5"
SOURCE_COLOR_SPACE = "ACES2065-1"
DISPLAY = "sRGB - Display"
VIEW = "ACES 2.0 - SDR 100 nits (Rec.709)"


def srgb_decode(value: float) -> float:
    value = max(0.0, min(1.0, value))
    if value <= 0.04045:
        return value / 12.92
    return ((value + 0.055) / 1.055) ** 2.4


def reference_patches() -> list[tuple[str, tuple[float, float, float]]]:
    patches: list[tuple[str, tuple[float, float, float]]] = []

    for index in range(33):
        stop = -12.0 + 24.0 * index / 32.0
        value = 0.18 * (2.0**stop)
        patches.append((f"neutral_{index:02d}", (value, value, value)))

    cube_levels = (-0.05, 0.0, 0.01, 0.18, 1.0, 4.0, 16.0)
    for index, rgb in enumerate(itertools.product(cube_levels, repeat=3)):
        patches.append((f"cube_{index:03d}", rgb))

    for index in range(96):
        hue = 2.0 * math.pi * index / 96.0
        exposure = 2.0 ** (-4.0 + 10.0 * ((index % 17) / 16.0))
        rgb = tuple(
            exposure
            * (0.18 + 0.82 * max(0.0, math.cos(hue - phase)))
            for phase in (0.0, 2.0 * math.pi / 3.0, 4.0 * math.pi / 3.0)
        )
        patches.append((f"hue_{index:02d}", rgb))

    random_generator = random.Random(0xACE520)
    for index in range(128):
        rgb = tuple(
            (2.0 ** random_generator.uniform(-8.0, 7.0))
            * random_generator.uniform(-0.05, 1.0)
            for _ in range(3)
        )
        patches.append((f"random_{index:03d}", rgb))

    return patches


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--csv", type=Path, required=True)
    parser.add_argument("--provenance", type=Path, required=True)
    arguments = parser.parse_args()

    configuration = ocio.Config.CreateFromBuiltinConfig(CONFIG_NAME)
    processor = configuration.getProcessor(
        SOURCE_COLOR_SPACE,
        DISPLAY,
        VIEW,
        ocio.TRANSFORM_DIR_FORWARD,
    )
    cpu_processor = processor.getDefaultCPUProcessor()

    arguments.csv.parent.mkdir(parents=True, exist_ok=True)
    with arguments.csv.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output, lineterminator="\n")
        writer.writerow(
            (
                "patch_id",
                "aces2065_r",
                "aces2065_g",
                "aces2065_b",
                "ocio_srgb_encoded_r",
                "ocio_srgb_encoded_g",
                "ocio_srgb_encoded_b",
                "ocio_srgb_linear_r",
                "ocio_srgb_linear_g",
                "ocio_srgb_linear_b",
            )
        )
        for patch_id, input_rgb in reference_patches():
            encoded = tuple(cpu_processor.applyRGB(list(input_rgb)))
            linear = tuple(srgb_decode(channel) for channel in encoded)
            writer.writerow(
                (patch_id,)
                + tuple(f"{channel:.17g}" for channel in input_rgb)
                + tuple(f"{channel:.17g}" for channel in encoded)
                + tuple(f"{channel:.17g}" for channel in linear)
            )

    csv_sha256 = hashlib.sha256(arguments.csv.read_bytes()).hexdigest()
    provenance = {
        "schema": "aces2-ocio-reference:v1",
        "generator": "tests/generate_aces2_ocio_reference.py",
        "ocio_version": ocio.__version__,
        "built_in_config": CONFIG_NAME,
        "config_cache_id": configuration.getCacheID(),
        "processor_cache_id": processor.getCacheID(),
        "source_color_space": SOURCE_COLOR_SPACE,
        "display": DISPLAY,
        "view": VIEW,
        "patch_count": len(reference_patches()),
        "csv_sha256": csv_sha256,
        "comparison_encoding": "linear sRGB decoded from the OCIO display-view output",
    }
    arguments.provenance.write_text(
        json.dumps(provenance, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
