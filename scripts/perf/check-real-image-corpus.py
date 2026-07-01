#!/usr/bin/env python3
"""Validate the real-image QA corpus manifest."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


REQUIRED_TAGS = {
    "dark_raw": "dark/night RAW recovery",
    "clipped_highlights": "clipped highlight rollback",
    "smooth_sky": "smooth sky or gradient",
    "skin": "portrait skin tone",
    "foliage": "landscape foliage",
    "textured_detail": "fine texture/detail",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate a non-public real-image QA corpus manifest."
    )
    parser.add_argument("manifest", type=Path, help="Path to the corpus manifest JSON.")
    parser.add_argument(
        "--schema-only",
        action="store_true",
        help="Validate manifest structure and coverage without requiring asset files to exist.",
    )
    return parser.parse_args()


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def load_manifest(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    if not isinstance(payload, dict):
        raise ValueError("manifest root must be an object")
    return payload


def resolve_path(manifest_path: Path, value: str) -> Path:
    candidate = Path(value).expanduser()
    if candidate.is_absolute():
        return candidate
    return (manifest_path.parent / candidate).resolve()


def validate_asset(
    manifest_path: Path,
    asset: dict[str, Any],
    covered_tags: set[str],
    schema_only: bool,
    index: int,
) -> list[str]:
    errors: list[str] = []
    asset_id = asset.get("asset_id")
    if not isinstance(asset_id, str) or not asset_id.strip():
        errors.append(f"asset {index}: asset_id is required")

    path_value = asset.get("path")
    if not isinstance(path_value, str) or not path_value.strip():
        errors.append(f"asset {asset_id or index}: path is required")
    elif not schema_only and not resolve_path(manifest_path, path_value).is_file():
        errors.append(f"asset {asset_id or index}: path does not exist: {path_value}")

    media_type = asset.get("media_type")
    if media_type != "photo":
        errors.append(f"asset {asset_id or index}: media_type must be photo")

    image_format = asset.get("format")
    if not isinstance(image_format, str) or not image_format.strip():
        errors.append(f"asset {asset_id or index}: format is required")

    license_name = asset.get("license")
    if not isinstance(license_name, str) or not license_name.strip():
        errors.append(f"asset {asset_id or index}: license is required")
    if asset.get("license_verified") is not True:
        errors.append(f"asset {asset_id or index}: license_verified must be true")

    source_url = asset.get("source_url")
    source_page = asset.get("source_page")
    license_url = asset.get("license_url")
    license_evidence = asset.get("license_evidence")
    public_source_fields = (source_url, source_page, license_url)
    has_public_source = any(isinstance(value, str) and value.strip() for value in public_source_fields)
    has_private_evidence = isinstance(license_evidence, str) and license_evidence.strip()
    if not has_public_source and not has_private_evidence:
        errors.append(
            f"asset {asset_id or index}: source_url/source_page/license_url or "
            "license_evidence is required"
        )
    for field_name, field_value in (
        ("source_url", source_url),
        ("source_page", source_page),
        ("license_url", license_url),
    ):
        if field_value is not None and (not isinstance(field_value, str) or not field_value.strip()):
            errors.append(f"asset {asset_id or index}: {field_name} must be a non-empty string")

    sha256_value = asset.get("sha256")
    if sha256_value is not None:
        if (
            not isinstance(sha256_value, str)
            or len(sha256_value) != 64
            or any(character not in "0123456789abcdefABCDEF" for character in sha256_value)
        ):
            errors.append(f"asset {asset_id or index}: sha256 must be a 64-character hex digest")

    tags = asset.get("tags")
    if not isinstance(tags, list) or not all(isinstance(tag, str) for tag in tags):
        errors.append(f"asset {asset_id or index}: tags must be a string array")
    else:
        covered_tags.update(tag for tag in tags if tag in REQUIRED_TAGS)

    recipe_value = asset.get("recipe")
    if recipe_value is not None:
        if not isinstance(recipe_value, str) or not recipe_value.strip():
            errors.append(f"asset {asset_id or index}: recipe must be a non-empty path string")
        elif not schema_only and not resolve_path(manifest_path, recipe_value).is_file():
            errors.append(f"asset {asset_id or index}: recipe does not exist: {recipe_value}")

    review_regions = asset.get("review_regions", [])
    if review_regions is not None and not isinstance(review_regions, list):
        errors.append(f"asset {asset_id or index}: review_regions must be an array")

    return errors


def main() -> int:
    args = parse_args()
    manifest_path = args.manifest.resolve()
    if not manifest_path.is_file():
        return fail(f"manifest does not exist: {manifest_path}")

    try:
        manifest = load_manifest(manifest_path)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return fail(f"unable to read manifest: {exc}")

    assets = manifest.get("assets")
    if not isinstance(assets, list) or not assets:
        return fail("manifest must contain a non-empty assets array")

    errors: list[str] = []
    covered_tags: set[str] = set()
    for index, asset in enumerate(assets):
        if not isinstance(asset, dict):
            errors.append(f"asset {index}: entry must be an object")
            continue
        errors.extend(validate_asset(manifest_path, asset, covered_tags, args.schema_only, index))

    missing_tags = sorted(set(REQUIRED_TAGS) - covered_tags)
    for tag in missing_tags:
        errors.append(f"missing required scene coverage tag: {tag} ({REQUIRED_TAGS[tag]})")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(
        "real-image corpus manifest ok: "
        f"{len(assets)} assets, required tags covered: {', '.join(sorted(covered_tags))}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
