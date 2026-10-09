#!/usr/bin/env python3
"""Download and hash-check the fixed, openly licensed creative-tools corpus."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any


CC0 = "https://creativecommons.org/publicdomain/zero/1.0/"
CC_BY_4 = "https://creativecommons.org/licenses/by/4.0/"
PUBLIC_DOMAIN = "https://creativecommons.org/publicdomain/mark/1.0/"

# These are published original-file URLs. Hashes pin the exact reviewed bytes;
# a changed upstream file fails closed and requires an explicit corpus review.
SOURCES: tuple[dict[str, Any], ...] = (
    {
        "asset_id": "portrait-obama",
        "filename": "public-obama-portrait.jpg",
        "title": "President Barack Obama official portrait",
        "category": "portrait",
        "visual_tags": ["hair_detail", "skin_tone"],
        "license": "Public domain (U.S. federal government work)",
        "license_url": PUBLIC_DOMAIN,
        "attribution": "Pete Souza / White House; U.S. federal government public-domain work",
        "source_page": "https://commons.wikimedia.org/wiki/File:President_Barack_Obama.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/8/8d/President_Barack_Obama.jpg",
        "sha256": "744dd848fbb0584229169e01c4944664957c62495fb9e8af514a088ebca43e19",
    },
    {
        "asset_id": "portrait-close-woman",
        "filename": "portrait-close-woman.jpg",
        "title": "Woman's close portrait, 3096664",
        "category": "portrait",
        "visual_tags": ["hair_detail", "skin_tone"],
        "license": "CC0 1.0",
        "license_url": CC0,
        "attribution": "Gromovataya (Pixabay); CC0 1.0",
        "source_page": "https://commons.wikimedia.org/wiki/File:Woman%27s_close_portrait,_3096664.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/7/70/Woman%27s_close_portrait%2C_3096664.jpg",
        "sha256": "eb77b3e953813c97978a70892d2646b88e21e227ae5bef9e6e81d310df67869b",
    },
    {
        "asset_id": "portrait-ursula",
        "filename": "portrait-ursula-von-der-leyen.jpg",
        "title": "Official Portrait of Ursula von der Leyen",
        "category": "portrait",
        "visual_tags": ["hair_detail", "skin_tone"],
        "license": "CC BY 4.0",
        "license_url": CC_BY_4,
        "attribution": "© European Union, 2026; CC BY 4.0; changes must be indicated",
        "source_page": "https://commons.wikimedia.org/wiki/File:Official_Portrait_of_Ursula_von_der_Leyen.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/3/3c/Official_Portrait_of_Ursula_von_der_Leyen.jpg",
        "sha256": "9badfe8087428b6bb17c617f5f48e94cdab1cc98441a2fd6067e189b0f97262c",
    },
    {
        "asset_id": "night-chicago",
        "filename": "night-chicago-building.jpg",
        "title": "Gray High-rise Building at Night 373893",
        "category": "night",
        "visual_tags": [],
        "license": "CC0 1.0",
        "license_url": CC0,
        "attribution": "Mitul Shah; CC0 1.0",
        "source_page": "https://commons.wikimedia.org/wiki/File:Gray_High-rise_Building_at_Night_373893.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/0/0f/Gray_High-rise_Building_at_Night_373893.jpg",
        "sha256": "06ed84e2af288c9da2e14888535e51e6cf78955ecff79970b9c6d51266249a7f",
    },
    {
        "asset_id": "night-hong-kong",
        "filename": "night-hong-kong-typhoon.jpg",
        "title": "Hong Kong skyscrapers in a night of typhoon",
        "category": "night",
        "visual_tags": [],
        "license": "CC0 1.0",
        "license_url": CC0,
        "attribution": "Wilfredo R. Rodriguez H.; CC0 1.0",
        "source_page": "https://commons.wikimedia.org/wiki/File:Hong_Kong_skyscrapers_in_a_night_of_typhoon.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/8/8d/Hong_Kong_skyscrapers_in_a_night_of_typhoon.jpg",
        "sha256": "bbf03b141b82e43f8a798db8bf899154791a3bd0195d2a0ca5f6935f0411a241",
    },
    {
        "asset_id": "architecture-academic-building",
        "filename": "architecture-academic-building.jpg",
        "title": "New Academic Building Exterior",
        "category": "architecture",
        "visual_tags": [],
        "license": "Public domain, released by author",
        "license_url": PUBLIC_DOMAIN,
        "attribution": "Ci-Daemon; released into public domain",
        "source_page": "https://commons.wikimedia.org/wiki/File:New_Academic_Building_Exterior.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/c/ce/New_Academic_Building_Exterior.jpg",
        "sha256": "43a75500ddba0d7fc0fca3f3221cb9323caf84112b3ccbdfbdfe1bee6b69a76a",
    },
    {
        "asset_id": "architecture-glass-house",
        "filename": "architecture-glass-house.jpg",
        "title": "Exterior of The Glass House by Bill and Ruth Lucas",
        "category": "architecture",
        "visual_tags": [],
        "license": "CC0 1.0",
        "license_url": CC0,
        "attribution": "CC0 1.0",
        "source_page": "https://commons.wikimedia.org/wiki/File:Exterior_of_The_Glass_House_by_Bill_and_Ruth_Lucas.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/5/5c/Exterior_of_The_Glass_House_by_Bill_and_Ruth_Lucas.jpg",
        "sha256": "fb35be5d83bb47f093aede9786063d3e08652260250b73830ecb97f482475c9a",
    },
    {
        "asset_id": "product-glass-bottle",
        "filename": "product-glass-bottle.jpg",
        "title": "Glass bottle",
        "category": "product_glass",
        "visual_tags": ["glass_transparency"],
        "license": "CC0 1.0",
        "license_url": CC0,
        "attribution": "Yesseruser; CC0 1.0",
        "source_page": "https://commons.wikimedia.org/wiki/File:Glass_bottle.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/f/f1/Glass_bottle.jpg",
        "sha256": "306f9d3a9b15df0099305c85ab5d2995022dc1b4f5db0ea8e16d5e3ed2c904f2",
    },
    {
        "asset_id": "product-mirto-bottle",
        "filename": "product-mirto-bottle.jpg",
        "title": "Mirtoflasche-01",
        "category": "product_glass",
        "visual_tags": [],
        "license": "Public domain, released by author",
        "license_url": PUBLIC_DOMAIN,
        "attribution": "Priwo; released into public domain",
        "source_page": "https://commons.wikimedia.org/wiki/File:Mirtoflasche-01.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/a/a3/Mirtoflasche-01.jpg",
        "sha256": "be2d68ce7a4a998ca6f8bdfefd555943342771e0500b140f7dc8460fc643a00a",
    },
    {
        "asset_id": "landscape-mountain",
        "filename": "landscape-mountain.jpg",
        "title": "Mountain and Landscape",
        "category": "landscape",
        "visual_tags": [],
        "license": "CC0 1.0",
        "license_url": CC0,
        "attribution": "CC0 1.0",
        "source_page": "https://commons.wikimedia.org/wiki/File:Mountain_and_Landscape.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/9/94/Mountain_and_Landscape.jpg",
        "sha256": "841a4fc4c2e4bf8666b9772ceef34182f39a625af5fbd99d40eaff0609bec8df",
    },
    {
        "asset_id": "landscape-panorama",
        "filename": "landscape-mountain-panorama.jpg",
        "title": "Mountain Without Men",
        "category": "landscape",
        "visual_tags": [],
        "license": "CC0 1.0",
        "license_url": CC0,
        "attribution": "Frawiki90; CC0 1.0",
        "source_page": "https://commons.wikimedia.org/wiki/File:Mountain_Without_Men.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/7/76/Mountain_Without_Men.jpg",
        "sha256": "83b649395980cecbece8db6cb292b66001f083e556e3e49dc6ee622e504bbf1e",
    },
    {
        "asset_id": "monochrome-lyskamm",
        "filename": "monochrome-lyskamm.jpg",
        "title": "North face of Swiss mountain Lyskamm, black and white photograph",
        "category": "monochrome",
        "visual_tags": [],
        "license": "CC BY 4.0",
        "license_url": CC_BY_4,
        "attribution": "Giorgio Galeotti; CC BY 4.0; changes must be indicated",
        "source_page": "https://commons.wikimedia.org/wiki/File:North_face_of_Swiss_mountain_Lyskamm_(4.532_m),_with_Grenzgletscher_in_front,_black_and_white_photograph.jpg",
        "source_url": "https://upload.wikimedia.org/wikipedia/commons/b/b0/North_face_of_Swiss_mountain_Lyskamm_%284.532_m%29%2C_with_Grenzgletscher_in_front%2C_black_and_white_photograph.jpg",
        "sha256": "f723f46045cf9331e054677bd04999ef17880b95439eb4e8cef5a12684814ece",
    },
)


def digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch_asset(source: dict[str, Any], path: Path, interval: float) -> None:
    if path.is_file():
        actual = digest_file(path)
        if actual == source["sha256"]:
            print(f"verified existing {source['filename']}")
            return
        raise ValueError(
            f"{path} exists but has sha256 {actual}; expected {source['sha256']}"
        )

    request = urllib.request.Request(
        source["source_url"],
        headers={
            "User-Agent": "LooksyPhotoEditor/1.0 (creative-tools visual QA corpus preparer)",
            "Accept": "image/jpeg,*/*;q=0.8",
        },
    )
    last_error: Exception | None = None
    for attempt in range(4):
        if attempt or interval:
            time.sleep(interval if attempt == 0 else max(interval, 30.0 * (2**attempt)))
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                with path.open("wb") as output:
                    while True:
                        block = response.read(1024 * 1024)
                        if not block:
                            break
                        output.write(block)
            actual = digest_file(path)
            if actual != source["sha256"]:
                path.unlink(missing_ok=True)
                raise ValueError(
                    f"{source['filename']} hash mismatch: expected {source['sha256']}, got {actual}"
                )
            print(f"downloaded and verified {source['filename']}")
            return
        except urllib.error.HTTPError as exc:
            path.unlink(missing_ok=True)
            last_error = exc
            if exc.code not in {429, 500, 502, 503, 504}:
                break
            retry_after = exc.headers.get("Retry-After")
            if retry_after and retry_after.isdigit():
                time.sleep(int(retry_after))
        except Exception:
            path.unlink(missing_ok=True)
            raise
    raise RuntimeError(f"unable to download {source['source_url']}: {last_error}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", type=Path, help="Local folder for assets and manifest")
    parser.add_argument(
        "--interval-seconds",
        type=float,
        default=15.0,
        help="minimum delay between Wikimedia requests (default: 15)",
    )
    parser.add_argument(
        "--verify-only",
        action="store_true",
        help="verify already-present assets and write the manifest without downloading",
    )
    args = parser.parse_args()
    if args.interval_seconds < 0:
        parser.error("--interval-seconds must be non-negative")

    output_dir = args.output_dir.expanduser().resolve()
    assets_dir = output_dir / "assets"
    assets_dir.mkdir(parents=True, exist_ok=True)
    assets: list[dict[str, Any]] = []
    for index, source in enumerate(SOURCES):
        path = assets_dir / source["filename"]
        try:
            if args.verify_only:
                if not path.is_file():
                    raise FileNotFoundError(path)
                actual = digest_file(path)
                if actual != source["sha256"]:
                    raise ValueError(f"{path} hash mismatch: expected {source['sha256']}, got {actual}")
                print(f"verified {source['filename']}")
            else:
                fetch_asset(source, path, args.interval_seconds if index else 0.0)
        except (OSError, ValueError, RuntimeError) as exc:
            print(f"corpus preparation failed: {exc}", file=sys.stderr)
            return 1
        assets.append(
            {
                "asset_id": source["asset_id"],
                "title": source["title"],
                "path": f"assets/{source['filename']}",
                "media_type": "photo",
                "format": "jpg",
                "license": source["license"],
                "license_verified": True,
                "license_evidence": source["attribution"],
                "attribution": source["attribution"],
                "source_url": source["source_url"],
                "source_page": source["source_page"],
                "license_url": source["license_url"],
                "sha256": source["sha256"],
                "size_bytes": path.stat().st_size,
                "tags": [],
                "creative_categories": [source["category"]],
                "creative_visual_tags": source["visual_tags"],
            }
        )

    manifest = {"manifest_version": 1, "asset_count": len(assets), "assets": assets}
    manifest_path = output_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {len(assets)} verified assets to {manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
