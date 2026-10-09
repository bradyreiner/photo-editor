#!/usr/bin/env python3
"""Build a compact, linked visual-review sheet from an F1 capture receipt."""

from __future__ import annotations

import argparse
import html
import json
import subprocess
from pathlib import Path


STYLES = ("restrained", "default", "expressive")
REQUIRED_ASSETS = (
    "public-obama-portrait.jpg",
    "night-chicago-building.jpg",
    "product-glass-bottle.jpg",
    "landscape-mountain.jpg",
)


def make_preview(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            "sips",
            "-Z",
            "800",
            "-s",
            "format",
            "jpeg",
            "-s",
            "formatOptions",
            "70",
            str(source),
            "--out",
            str(destination),
        ],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packet_dir", type=Path, help="Full-resolution-review-release directory")
    args = parser.parse_args()
    packet = args.packet_dir.expanduser().resolve()
    receipt_path = packet / "optical-diffusion-review-capture.json"
    if not receipt_path.is_file():
        parser.error(f"capture receipt is missing: {receipt_path}")
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if receipt.get("fullResolutionInputs") is not True:
        parser.error("contact sheet requires full-resolution source captures")

    assets = list(REQUIRED_ASSETS)
    records = {
        (str(record["filename"]), str(record["style"])): record
        for record in receipt.get("assets", [])
    }
    missing = [
        f"{asset}/{style}"
        for asset in assets
        for style in STYLES
        if (asset, style) not in records
    ]
    if not assets or missing:
        parser.error(f"capture receipt lacks review variants: {', '.join(missing)}")

    previews = packet / "contact-sheet-previews"
    rows: list[str] = []
    for asset in assets:
        stem = Path(asset).stem
        source_relative = Path("default") / stem / "source.png"
        source_path = packet / source_relative
        if not source_path.is_file():
            parser.error(f"source capture is missing: {source_path}")
        columns: list[str] = []
        variants = [("source", source_path, source_relative)]
        for style in STYLES:
            record = records[(asset, style)]
            output_relative = Path(str(record["output"]))
            variants.append((style, packet / output_relative, output_relative))
        for label, full_image, full_relative in variants:
            preview_name = f"{stem}-{label}.jpg"
            preview_path = previews / preview_name
            if not full_image.is_file():
                parser.error(f"review image is missing: {full_image}")
            make_preview(full_image, preview_path)
            columns.append(
                "<figure>"
                f'<a href="{html.escape(full_relative.as_posix())}">'
                f'<img loading="lazy" src="contact-sheet-previews/{html.escape(preview_name)}" '
                f'alt="{html.escape(asset)} — {html.escape(label)}">'
                "</a>"
                f"<figcaption>{html.escape(label.title())}</figcaption>"
                "</figure>"
            )
        rows.append(
            f'<section><h2>{html.escape(asset)}</h2><div class="row">'
            + "".join(columns)
            + "</div></section>"
        )

    document = """<!doctype html>
<html lang="en">
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Halation Study — visual review</title>
<style>
body { margin: 0; padding: 24px; color: #eee; background: #151515; font: 15px/1.5 system-ui, sans-serif; }
h1 { margin: 0 0 6px; font-size: 25px; }
.note { max-width: 900px; color: #b7b7b7; margin: 0 0 24px; }
section { margin: 0 0 26px; padding: 16px; background: #202020; border: 1px solid #373737; border-radius: 10px; }
h2 { margin: 0 0 12px; font-size: 17px; font-weight: 550; }
.row { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 12px; }
figure { margin: 0; min-width: 0; text-align: center; }
img { display: block; width: 100%; height: 300px; object-fit: contain; background: #101010; border-radius: 4px; }
figcaption { padding-top: 6px; color: #ccc; font-size: 13px; }
a { color: inherit; }
@media (max-width: 900px) { body { padding: 12px; } .row { grid-template-columns: repeat(2, minmax(0, 1fr)); } img { height: 240px; } }
</style>
<body>
<h1>Halation Study — visual review sheet</h1>
<p class="note">Preliminary image comparison only. This sheet is not scored and does not qualify F1. Each preview is resized and JPEG-compressed for quick scanning; click any image to open its full-resolution PNG, then review at fit and 100% before scoring.</p>
""" + "\n".join(rows) + "\n</body>\n</html>\n"
    output_path = packet / "contact-sheet.html"
    output_path.write_text(document, encoding="utf-8")
    print(f"contact sheet={output_path}")
    print(f"photos={len(assets)} variants={len(assets) * 4} previews={previews}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
