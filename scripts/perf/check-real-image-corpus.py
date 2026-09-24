#!/usr/bin/env python3
"""Validate the real-image QA corpus manifest."""

from __future__ import annotations

import argparse
import hashlib
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

ALLOWED_AGENT_PIXEL_CHECKS = {
    "brighter",
    "darker",
    "strongerContrast",
    "softerContrast",
    "moreSaturated",
    "lessSaturated",
    "warmer",
    "cooler",
    "monochrome",
    "quieter",
    "preserveTone",
    "preserveColor",
    "artifactMaterialized",
    "fullResolutionArtifact",
    "targetChanged",
    "outsidePreserved",
    "backgroundContinuity",
}

HUMAN_REVIEW_STATUSES = {"pending", "pass", "fail"}
AGENT_CASE_KINDS = {"global_edit", "retouch"}
RETOUCH_OPERATIONS = {"remove", "heal"}


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
    parser.add_argument(
        "--seed-manifest",
        action="store_true",
        help="Validate a downloadable seed manifest with file_name and inline recipe objects.",
    )
    parser.add_argument(
        "--require-agent-cases",
        action="store_true",
        help="Require every asset to define at least one valid agent evaluation case.",
    )
    parser.add_argument(
        "--require-human-ratings",
        action="store_true",
        help="Require every agent case to have a completed passing human review.",
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


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_asset(
    manifest_path: Path,
    asset: dict[str, Any],
    covered_tags: set[str],
    schema_only: bool,
    seed_manifest: bool,
    require_agent_cases: bool,
    require_human_ratings: bool,
    agent_case_ids: set[str],
    index: int,
) -> list[str]:
    errors: list[str] = []
    asset_id = asset.get("asset_id")
    if not isinstance(asset_id, str) or not asset_id.strip():
        errors.append(f"asset {index}: asset_id is required")

    path_field = "file_name" if seed_manifest else "path"
    path_value = asset.get(path_field)
    if not isinstance(path_value, str) or not path_value.strip():
        errors.append(f"asset {asset_id or index}: {path_field} is required")
    asset_path: Path | None = None
    if isinstance(path_value, str) and path_value.strip():
        asset_path = resolve_path(manifest_path, path_value)
        if not schema_only and not seed_manifest and not asset_path.is_file():
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
    if (
        not isinstance(sha256_value, str)
        or len(sha256_value) != 64
        or any(character not in "0123456789abcdef" for character in sha256_value)
    ):
        errors.append(
            f"asset {asset_id or index}: sha256 must be a lowercase 64-character digest"
        )
    size_bytes = asset.get("size_bytes")
    if not isinstance(size_bytes, int) or isinstance(size_bytes, bool) or size_bytes <= 0:
        errors.append(f"asset {asset_id or index}: size_bytes must be a positive integer")
    if not schema_only and not seed_manifest and asset_path is not None and asset_path.is_file():
        actual_size = asset_path.stat().st_size
        if isinstance(size_bytes, int) and not isinstance(size_bytes, bool) and actual_size != size_bytes:
            errors.append(
                f"asset {asset_id or index}: size_bytes mismatch: "
                f"manifest={size_bytes}, actual={actual_size}"
            )
        if isinstance(sha256_value, str) and len(sha256_value) == 64:
            actual_sha256 = sha256_file(asset_path)
            if actual_sha256 != sha256_value:
                errors.append(
                    f"asset {asset_id or index}: sha256 mismatch: "
                    f"manifest={sha256_value}, actual={actual_sha256}"
                )

    tags = asset.get("tags")
    if not isinstance(tags, list) or not all(isinstance(tag, str) for tag in tags):
        errors.append(f"asset {asset_id or index}: tags must be a string array")
    else:
        covered_tags.update(tag for tag in tags if tag in REQUIRED_TAGS)

    recipe_value = asset.get("recipe")
    if recipe_value is not None:
        if seed_manifest and not isinstance(recipe_value, dict):
            errors.append(f"asset {asset_id or index}: seed recipe must be an object")
        elif not seed_manifest and (not isinstance(recipe_value, str) or not recipe_value.strip()):
            errors.append(f"asset {asset_id or index}: recipe must be a non-empty path string")
        elif (
            not seed_manifest
            and not schema_only
            and not resolve_path(manifest_path, recipe_value).is_file()
        ):
            errors.append(f"asset {asset_id or index}: recipe does not exist: {recipe_value}")

    review_regions = asset.get("review_regions", [])
    if review_regions is not None and not isinstance(review_regions, list):
        errors.append(f"asset {asset_id or index}: review_regions must be an array")

    agent_cases = asset.get("agent_cases", [])
    if not isinstance(agent_cases, list):
        errors.append(f"asset {asset_id or index}: agent_cases must be an array")
        agent_cases = []
    if require_agent_cases and not agent_cases:
        errors.append(f"asset {asset_id or index}: at least one agent case is required")
    for case_index, case in enumerate(agent_cases):
        case_label = f"asset {asset_id or index} agent case {case_index}"
        if not isinstance(case, dict):
            errors.append(f"{case_label}: entry must be an object")
            continue
        case_id = case.get("case_id")
        if not isinstance(case_id, str) or not case_id.strip():
            errors.append(f"{case_label}: case_id is required")
        elif case_id in agent_case_ids:
            errors.append(f"duplicate agent case_id: {case_id}")
        else:
            agent_case_ids.add(case_id)
        instructions = case.get("instructions")
        if not isinstance(instructions, str) or len(instructions.strip()) < 4:
            errors.append(f"{case_label}: instructions must contain an explicit editing request")
        case_kind = case.get("kind", "global_edit")
        if case_kind not in AGENT_CASE_KINDS:
            errors.append(
                f"{case_label}: kind must be global_edit or retouch"
            )
        if case_kind == "retouch":
            retouch = case.get("retouch")
            if not isinstance(retouch, dict):
                errors.append(f"{case_label}: retouch object is required for retouch cases")
            else:
                if retouch.get("operation") not in RETOUCH_OPERATIONS:
                    errors.append(f"{case_label}: retouch.operation must be remove or heal")
                target_label = retouch.get("target_label")
                if not isinstance(target_label, str) or not target_label.strip():
                    errors.append(f"{case_label}: retouch.target_label is required")
                region = retouch.get("region")
                if not isinstance(region, dict):
                    errors.append(f"{case_label}: retouch.region is required")
                else:
                    for field_name in ("center_x", "center_y", "radius_x", "radius_y"):
                        value = region.get(field_name)
                        if (
                            not isinstance(value, (int, float))
                            or isinstance(value, bool)
                            or value <= 0.0
                            or value >= 1.0
                        ):
                            errors.append(
                                f"{case_label}: retouch.region.{field_name} must be greater than 0 and less than 1"
                            )
                    if all(
                        isinstance(region.get(name), (int, float))
                        and not isinstance(region.get(name), bool)
                        for name in ("center_x", "center_y", "radius_x", "radius_y")
                    ):
                        if (
                            region["center_x"] - region["radius_x"] < 0.0
                            or region["center_x"] + region["radius_x"] > 1.0
                            or region["center_y"] - region["radius_y"] < 0.0
                            or region["center_y"] + region["radius_y"] > 1.0
                        ):
                            errors.append(
                                f"{case_label}: retouch region must stay within the normalized image"
                            )
        if case.get("scope", "current-photo") != "current-photo":
            errors.append(f"{case_label}: scope must be current-photo")
        expected_checks = case.get("expected_pixel_checks")
        if (
            not isinstance(expected_checks, list)
            or not expected_checks
            or not all(isinstance(check, str) for check in expected_checks)
        ):
            errors.append(f"{case_label}: expected_pixel_checks must be a non-empty string array")
        else:
            unknown_checks = sorted(set(expected_checks) - ALLOWED_AGENT_PIXEL_CHECKS)
            if unknown_checks:
                errors.append(
                    f"{case_label}: unknown expected pixel checks: {', '.join(unknown_checks)}"
                )
        protected_regions = case.get("protected_regions", [])
        if not isinstance(protected_regions, list):
            errors.append(f"{case_label}: protected_regions must be an array")
        else:
            for region_index, region in enumerate(protected_regions):
                region_label = f"{case_label} protected region {region_index}"
                if not isinstance(region, dict):
                    errors.append(f"{region_label}: entry must be an object")
                    continue
                if not isinstance(region.get("label"), str) or not region["label"].strip():
                    errors.append(f"{region_label}: label is required")
                for field_name in ("x", "y", "width", "height"):
                    value = region.get(field_name)
                    if (
                        not isinstance(value, (int, float))
                        or isinstance(value, bool)
                        or value < 0.0
                        or value > 1.0
                    ):
                        errors.append(f"{region_label}: {field_name} must be normalized to 0..1")
                if all(isinstance(region.get(name), (int, float)) for name in ("x", "y", "width", "height")):
                    if region["width"] <= 0.0 or region["height"] <= 0.0:
                        errors.append(f"{region_label}: width and height must be positive")
                    if region["x"] + region["width"] > 1.0 or region["y"] + region["height"] > 1.0:
                        errors.append(f"{region_label}: bounds must stay within the normalized image")
                max_delta = region.get("max_mean_rgb_delta")
                if (
                    not isinstance(max_delta, (int, float))
                    or isinstance(max_delta, bool)
                    or max_delta <= 0.0
                    or max_delta > 1.0
                ):
                    errors.append(
                        f"{region_label}: max_mean_rgb_delta must be greater than 0 and no more than 1"
                    )

        human_review = case.get("human_review")
        if not isinstance(human_review, dict):
            errors.append(f"{case_label}: human_review is required")
            continue
        review_status = human_review.get("status")
        if review_status not in HUMAN_REVIEW_STATUSES:
            errors.append(f"{case_label}: human_review.status must be pending, pass, or fail")
        if require_human_ratings and review_status != "pass":
            errors.append(f"{case_label}: a completed passing human review is required")
        if review_status in {"pass", "fail"}:
            for field_name in ("reviewer", "reviewed_at", "notes"):
                value = human_review.get(field_name)
                if not isinstance(value, str) or not value.strip():
                    errors.append(f"{case_label}: human_review.{field_name} is required after review")
            rating = human_review.get("rating")
            if not isinstance(rating, int) or isinstance(rating, bool) or rating < 1 or rating > 5:
                errors.append(f"{case_label}: human_review.rating must be an integer from 1 to 5")

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
    if manifest.get("manifest_version") != 1:
        errors.append("manifest_version must be 1")
    if args.require_agent_cases and manifest.get("agent_evaluation_version") != 1:
        errors.append("agent_evaluation_version must be 1 when agent cases are required")
    asset_count = manifest.get("asset_count")
    if args.seed_manifest and asset_count is None:
        asset_count = len(assets)
    if not isinstance(asset_count, int) or isinstance(asset_count, bool):
        errors.append("asset_count must be an integer")
    elif asset_count != len(assets):
        errors.append(
            f"asset_count mismatch: manifest={asset_count}, actual={len(assets)}"
        )
    covered_tags: set[str] = set()
    asset_ids: set[str] = set()
    agent_case_ids: set[str] = set()
    for index, asset in enumerate(assets):
        if not isinstance(asset, dict):
            errors.append(f"asset {index}: entry must be an object")
            continue
        errors.extend(
            validate_asset(
                manifest_path,
                asset,
                covered_tags,
                args.schema_only,
                args.seed_manifest,
                args.require_agent_cases,
                args.require_human_ratings,
                agent_case_ids,
                index,
            )
        )
        asset_id = asset.get("asset_id")
        if isinstance(asset_id, str) and asset_id:
            if asset_id in asset_ids:
                errors.append(f"duplicate asset_id: {asset_id}")
            asset_ids.add(asset_id)

    missing_tags = sorted(set(REQUIRED_TAGS) - covered_tags)
    for tag in missing_tags:
        errors.append(f"missing required scene coverage tag: {tag} ({REQUIRED_TAGS[tag]})")

    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1

    print(
        "real-image corpus manifest ok: "
        f"{len(assets)} assets, {len(agent_case_ids)} agent cases, "
        f"required tags covered: {', '.join(sorted(covered_tags))}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
