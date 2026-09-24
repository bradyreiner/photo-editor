#!/usr/bin/env python3
"""Generate the committed Looksy application and website brand assets."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as error:  # pragma: no cover - exercised by the operator environment
    raise SystemExit("Pillow is required to regenerate Looksy assets: python3 -m pip install Pillow") from error


PAPER = "#F4F6F8"
BLUE = "#3282FF"
BACKGROUND = "#050505"
MUTED = "#9C9CAF"
SUPERSAMPLE = 4


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = (
        "/System/Library/Fonts/HelveticaNeue.ttc",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    )
    for candidate in candidates:
        path = Path(candidate)
        if path.is_file():
            try:
                return ImageFont.truetype(path, size=size, index=1 if bold and path.suffix == ".ttc" else 0)
            except OSError:
                continue
    return ImageFont.load_default()


def icon(size: int, master: Image.Image) -> Image.Image:
    return master.resize((size, size), Image.Resampling.LANCZOS)


def icns_chunk(kind: str, payload: bytes) -> bytes:
    return kind.encode("ascii") + struct.pack(">I", len(payload) + 8) + payload


def build_icns(iconset: Path, output: Path) -> None:
    chunks = (
        ("icp4", "icon_16x16.png"),
        ("icp5", "icon_32x32.png"),
        ("icp6", "icon_32x32@2x.png"),
        ("ic07", "icon_128x128.png"),
        ("ic08", "icon_256x256.png"),
        ("ic09", "icon_512x512.png"),
        ("ic10", "icon_512x512@2x.png"),
    )
    payload = b"".join(
        icns_chunk(kind, (iconset / filename).read_bytes())
        for kind, filename in chunks
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"icns" + struct.pack(">I", len(payload) + 8) + payload)


def build_social_card(output: Path, master: Image.Image) -> None:
    scale = SUPERSAMPLE
    image = Image.new("RGB", (1200 * scale, 630 * scale), BACKGROUND)
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle(
        (36 * scale, 36 * scale, 1164 * scale, 594 * scale),
        radius=36 * scale,
        fill="#101113",
        outline="#2B2A29",
        width=2 * scale,
    )
    icon_art = master.resize((320 * scale, 320 * scale), Image.Resampling.LANCZOS)
    image.paste(icon_art, (70 * scale, 120 * scale), icon_art)
    draw.text((430 * scale, 180 * scale), "Looksy", fill=PAPER, font=font(94 * scale, bold=True))
    draw.text((438 * scale, 302 * scale), "SEE MORE. NOTICE MORE.", fill=BLUE, font=font(28 * scale, bold=True))
    draw.text(
        (438 * scale, 360 * scale),
        "AI visual companion for photography and video.",
        fill=MUTED,
        font=font(27 * scale),
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    image.resize((1200, 630), Image.Resampling.LANCZOS).save(output, optimize=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--repository-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
    )
    parser.add_argument(
        "--desktop-only",
        action="store_true",
        help="Regenerate desktop icons without changing website assets.",
    )
    args = parser.parse_args()
    root = args.repository_root.resolve()
    desktop_assets = root / "apps/desktop/assets"
    iconset = desktop_assets / "macos/Looksy.iconset"
    windows = desktop_assets / "windows"
    iconset.mkdir(parents=True, exist_ok=True)
    windows.mkdir(parents=True, exist_ok=True)

    master = Image.open(desktop_assets / "looksy-app-icon-master.png").convert("RGBA")
    icon(1024, master).save(desktop_assets / "app-icon-1024.png", optimize=True)

    iconset_outputs = {
        "icon_16x16.png": 16,
        "icon_16x16@2x.png": 32,
        "icon_32x32.png": 32,
        "icon_32x32@2x.png": 64,
        "icon_128x128.png": 128,
        "icon_128x128@2x.png": 256,
        "icon_256x256.png": 256,
        "icon_256x256@2x.png": 512,
        "icon_512x512.png": 512,
        "icon_512x512@2x.png": 1024,
    }
    for filename, size in iconset_outputs.items():
        icon(size, master).save(iconset / filename, optimize=True)
    build_icns(iconset, desktop_assets / "macos/Looksy.icns")

    ico = icon(256, master)
    ico.save(
        windows / "Looksy.ico",
        format="ICO",
        sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    icon(44, master).save(windows / "Square44x44Logo.png", optimize=True)
    icon(44, master).save(windows / "Square44x44Logo.targetsize-44_altform-unplated.png", optimize=True)
    icon(150, master).save(windows / "Square150x150Logo.png", optimize=True)
    icon(50, master).save(windows / "StoreLogo.png", optimize=True)

    if not args.desktop_only:
        site_assets = root / "site/assets"
        site_assets.mkdir(parents=True, exist_ok=True)
        icon(512, master).save(site_assets / "looksy-icon.png", optimize=True)
        build_social_card(site_assets / "og-looksy.png", master)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
