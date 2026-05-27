#!/usr/bin/env python3
"""Create a deterministic local preview benchmark corpus."""

from __future__ import annotations

import argparse
import json
import math
import struct
import zlib
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = REPO_ROOT / "perf-corpora" / "preview-synthetic-v1"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate deterministic synthetic raster assets and edit recipes for preview benchmarks."
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=DEFAULT_OUTPUT,
        help="Corpus directory to create.",
    )
    parser.add_argument(
        "--count",
        type=int,
        default=100,
        help="Number of synthetic PNG raster assets to generate.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing generated files in the output directory.",
    )
    return parser.parse_args()


def png_chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def write_png(path: Path, width: int, height: int, pixels: Iterable[tuple[int, int, int]]) -> None:
    raw = bytearray()
    iterator = iter(pixels)
    for _y in range(height):
        raw.append(0)
        for _x in range(width):
            red, green, blue = next(iterator)
            raw.extend((red & 0xFF, green & 0xFF, blue & 0xFF))
    payload = b"".join(
        (
            b"\x89PNG\r\n\x1a\n",
            png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)),
            png_chunk(b"IDAT", zlib.compress(bytes(raw), level=6)),
            png_chunk(b"IEND", b""),
        )
    )
    path.write_bytes(payload)


def synthetic_pixels(index: int, width: int, height: int) -> Iterable[tuple[int, int, int]]:
    hue_offset = (index * 37) % 256
    contrast = 0.72 + (index % 9) * 0.035
    for y in range(height):
        yn = y / max(1, height - 1)
        for x in range(width):
            xn = x / max(1, width - 1)
            wave = math.sin((xn * 5.0 + yn * 3.0 + index * 0.11) * math.pi)
            detail = ((x * 13 + y * 17 + index * 19) & 15) if (x % 17 == 0 or y % 19 == 0) else 0
            red = int(255 * min(1.0, max(0.0, (xn * contrast) + 0.12 * wave))) + detail
            green = int(255 * min(1.0, max(0.0, (yn * contrast) + 0.10 * math.cos(wave)))) + detail
            blue = int((hue_offset + x * 3 + y * 2 + detail) % 256)
            yield min(255, red), min(255, green), min(255, blue)


def recipe_payload(index: int) -> dict[str, object]:
    return {
        "version": 1,
        "exposure": round(((index % 9) - 4) * 0.18, 3),
        "contrast": float((index % 7) * 8 - 20),
        "highlights": float(12 - (index % 5) * 8),
        "shadows": float((index % 6) * 7 - 14),
        "temperature": float((index % 11) * 8 - 40),
        "tint": float((index % 9) * 5 - 20),
        "vibrance": float((index % 8) * 6),
        "saturation": float((index % 5) * 4 - 8),
        "texture": float((index % 4) * 5),
        "clarity": float((index % 6) * 4),
        "dehaze": float((index % 5) * 3),
        "sharpening": float(20 + (index % 5) * 8),
        "luminanceNr": float((index % 4) * 6),
        "colorNr": float((index % 3) * 7),
    }


def main() -> int:
    args = parse_args()
    if args.count <= 0:
        raise SystemExit("--count must be positive")

    output = args.output.resolve()
    assets_dir = output / "assets"
    recipes_dir = output / "recipes"
    external_dir = output / "external-required"
    for directory in (assets_dir, recipes_dir, external_dir):
        directory.mkdir(parents=True, exist_ok=True)

    if not args.force and any(assets_dir.glob("*.png")):
        raise SystemExit(f"{assets_dir} already contains PNG files. Use --force to overwrite.")

    assets: list[dict[str, object]] = []
    recipes: list[dict[str, object]] = []
    sizes = ((960, 640), (1280, 720), (1600, 900), (1920, 1280))
    for index in range(args.count):
        width, height = sizes[index % len(sizes)]
        asset_id = f"synthetic-raster-{index + 1:04d}"
        asset_path = assets_dir / f"{asset_id}.png"
        recipe_path = recipes_dir / f"{asset_id}.json"
        write_png(asset_path, width, height, synthetic_pixels(index, width, height))
        recipe = recipe_payload(index)
        recipe_path.write_text(json.dumps(recipe, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        assets.append(
            {
                "asset_id": asset_id,
                "path": str(asset_path),
                "media_type": "photo",
                "format": "png",
                "width": width,
                "height": height,
                "recipe": str(recipe_path),
            }
        )
        recipes.append({"asset_id": asset_id, "recipe": str(recipe_path)})

    (external_dir / "README.md").write_text(
        "\n".join(
            (
                "# External Preview Corpus Inputs",
                "",
                "Synthetic PNGs exercise deterministic raster preview paths.",
                "For full performance sign-off, add real JPEG/HEIC and camera RAW files here",
                "and reference them from a copied manifest before recording release baselines.",
                "",
            )
        ),
        encoding="utf-8",
    )
    manifest = {
        "name": "preview-synthetic-v1",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "asset_count": len(assets),
        "assets": assets,
        "recipes": recipes,
        "external_required": {
            "jpeg_heic_mix": "Add real files under external-required/jpeg-heic/",
            "raw_mix": "Add real files under external-required/raw/",
        },
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"corpus={output}")
    print(f"assets={len(assets)}")
    print(f"manifest={output / 'manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
