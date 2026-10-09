#!/usr/bin/env python3
"""Validate the photographic review matrix for the creative-tools plan.

The corpus itself is checked by check-real-image-corpus.py before this tool
accepts its metadata. This tool enforces the plan's category mix and review
coverage/score gates; it does not replace a human's visual judgment.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
CORPUS_CHECKER = ROOT / "scripts" / "perf" / "check-real-image-corpus.py"
FAMILIES = ("F1", "F2", "F3", "T1", "T2", "T3", "B1", "B2", "B3")
STRENGTHS = ("low", "default", "expressive")
ZOOMS = ("fit", "100%")
DIMENSIONS = (
    "photographic_integration",
    "material_credibility",
    "restraint",
    "typographic_readability",
    "artifact_freedom",
)
REQUIRED_DIMENSIONS = {
    family: tuple(d for d in DIMENSIONS if d != "typographic_readability")
    for family in FAMILIES
}
for _family in ("T1", "T2", "T3"):
    REQUIRED_DIMENSIONS[_family] = DIMENSIONS
CATEGORY_MINIMUMS = {
    "portrait": 3,
    "night": 2,
    "architecture": 2,
    "product_glass": 2,
    "landscape": 2,
    "monochrome": 1,
}
RELEVANT_CATEGORIES = {
    "F1": {"portrait", "night", "product_glass", "landscape"},
    "F2": {"architecture", "product_glass", "landscape", "monochrome"},
    "F3": {"portrait", "night", "product_glass"},
    "T1": set(CATEGORY_MINIMUMS),
    "T2": {"portrait", "architecture", "landscape", "monochrome"},
    "T3": {"portrait", "night", "product_glass"},
    "B1": {"portrait", "architecture", "landscape", "product_glass"},
    "B2": {"night", "architecture", "product_glass", "landscape"},
    "B3": {"portrait", "architecture", "landscape", "product_glass"},
}
REQUIRED_VISUAL_TAGS = {
    "hair_detail": "portrait",
    "skin_tone": "portrait",
    "glass_transparency": "product_glass",
}


def read_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be a JSON object")
    return value


def validate(corpus: dict[str, Any], packet: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    assets = corpus.get("assets")
    if not isinstance(assets, list):
        return ["corpus.assets must be an array"]
    if len(assets) != 12:
        errors.append(f"corpus has {len(assets)} photos; the fixed review corpus must contain exactly 12")

    categories_by_asset: dict[str, set[str]] = {}
    tags_by_asset: dict[str, set[str]] = {}
    category_counts = {category: 0 for category in CATEGORY_MINIMUMS}
    for index, asset in enumerate(assets):
        if not isinstance(asset, dict):
            errors.append(f"corpus.assets[{index}] must be an object")
            continue
        asset_id = asset.get("asset_id")
        if not isinstance(asset_id, str) or not asset_id:
            errors.append(f"corpus.assets[{index}].asset_id is required")
            continue
        if asset_id in categories_by_asset:
            errors.append(f"duplicate corpus asset_id: {asset_id}")
            continue
        if asset.get("media_type") != "photo" or asset.get("license_verified") is not True:
            errors.append(f"{asset_id}: requires a real photo with verified license metadata")
        categories = asset.get("creative_categories")
        if not isinstance(categories, list) or not categories:
            errors.append(f"{asset_id}: creative_categories must be a non-empty array")
            categories = []
        elif len(categories) != 1:
            errors.append(f"{asset_id}: assign exactly one primary creative category")
        category_values = [category for category in categories if isinstance(category, str)]
        if len(category_values) != len(categories):
            errors.append(f"{asset_id}: creative categories must be strings")
        normalized = {category for category in category_values if category in CATEGORY_MINIMUMS}
        for unknown in set(category_values) - set(CATEGORY_MINIMUMS):
            errors.append(f"{asset_id}: unknown creative category {unknown!r}")
        categories_by_asset[asset_id] = normalized
        for category in normalized:
            category_counts[category] += 1
        raw_tags = asset.get("creative_visual_tags", [])
        if not isinstance(raw_tags, list) or any(not isinstance(tag, str) for tag in raw_tags):
            errors.append(f"{asset_id}: creative_visual_tags must be an array of strings")
            raw_tags = []
        tags_by_asset[asset_id] = set(raw_tags)
    for category, minimum in CATEGORY_MINIMUMS.items():
        if category_counts[category] < minimum:
            errors.append(
                f"corpus category {category} has {category_counts[category]} photos; {minimum} required"
            )
    for tag, required_category in REQUIRED_VISUAL_TAGS.items():
        if not any(
            tag in tags_by_asset.get(asset_id, set()) and
            required_category in categories_by_asset.get(asset_id, set())
            for asset_id in categories_by_asset
        ):
            errors.append(
                f"corpus must identify a {required_category} photo with creative_visual_tags={tag}"
            )

    rows = packet.get("reviews")
    if not isinstance(rows, list):
        return errors + ["review packet.reviews must be an array"]
    seen: set[tuple[str, str, str, str, str]] = set()
    covered_cells: set[tuple[str, str, str, str]] = set()
    by_family: dict[str, set[str]] = {family: set() for family in FAMILIES}
    scores: dict[str, list[int]] = {family: [] for family in FAMILIES}
    for index, row in enumerate(rows):
        prefix = f"reviews[{index}]"
        if not isinstance(row, dict):
            errors.append(f"{prefix} must be an object")
            continue
        family = row.get("family")
        asset_id = row.get("asset_id")
        strength = row.get("strength")
        zoom = row.get("zoom")
        reviewer = row.get("reviewer")
        if family not in FAMILIES:
            errors.append(f"{prefix}.family must be one of {', '.join(FAMILIES)}")
            continue
        if not isinstance(asset_id, str) or asset_id not in categories_by_asset:
            errors.append(f"{prefix}: unknown asset_id {asset_id!r}")
            continue
        if strength not in STRENGTHS or zoom not in ZOOMS:
            errors.append(f"{prefix}: strength/zoom must be one of the plan's required values")
            continue
        if not isinstance(reviewer, str) or not reviewer.strip():
            errors.append(f"{prefix}.reviewer must identify the reviewer")
        reviewer_id = reviewer.strip() if isinstance(reviewer, str) else ""
        key = (family, asset_id, strength, zoom, reviewer_id)
        if key in seen:
            errors.append(f"{prefix}: duplicate review for {key}")
            continue
        seen.add(key)
        covered_cells.add((family, asset_id, strength, zoom))
        by_family[family].add(asset_id)
        if row.get("artifacts") != []:
            errors.append(f"{prefix}.artifacts must be an empty array for a passing review")
        rating_map = row.get("ratings")
        if not isinstance(rating_map, dict):
            errors.append(f"{prefix}.ratings must be an object")
            continue
        required = REQUIRED_DIMENSIONS[family]
        row_scores: list[int] = []
        for dimension in required:
            score = rating_map.get(dimension)
            if not isinstance(score, int) or isinstance(score, bool) or not 1 <= score <= 5:
                errors.append(f"{prefix}.ratings.{dimension} must be an integer from 1 to 5")
                continue
            if score < 3:
                errors.append(f"{prefix}.ratings.{dimension}={score} is below the minimum 3")
            row_scores.append(score)
        for dimension in set(rating_map) - set(required):
            if rating_map[dimension] is not None:
                errors.append(f"{prefix}.ratings.{dimension} is not applicable to {family}; use null")
        scores[family].extend(row_scores)

    for family in FAMILIES:
        asset_ids = by_family[family]
        if len(asset_ids) < 4:
            errors.append(f"{family}: reviewed on {len(asset_ids)} distinct photos; at least 4 required")
        for asset_id in asset_ids:
            asset_categories = categories_by_asset.get(asset_id, set())
            if not asset_categories.intersection(RELEVANT_CATEGORIES[family]):
                errors.append(f"{family}/{asset_id}: photo is not in a relevant category for {family}")
            missing = [
                (strength, zoom)
                for strength in STRENGTHS
                for zoom in ZOOMS
                if (family, asset_id, strength, zoom) not in covered_cells
            ]
            if missing:
                errors.append(f"{family}/{asset_id}: missing strength/zoom reviews {missing}")
        if scores[family] and sum(scores[family]) / len(scores[family]) < 4.0:
            mean = sum(scores[family]) / len(scores[family])
            errors.append(f"{family}: mean score {mean:.3f} is below 4.0")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("corpus_manifest", type=Path)
    parser.add_argument("review_packet", type=Path)
    parser.add_argument(
        "--skip-corpus-file-check",
        action="store_true",
        help="Skip byte/license verification for schema development only; never use for qualification.",
    )
    args = parser.parse_args()
    try:
        corpus = read_json(args.corpus_manifest)
        packet = read_json(args.review_packet)
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"invalid creative-tools review input: {exc}", file=sys.stderr)
        return 2
    if not args.skip_corpus_file_check:
        checked = subprocess.run(
            [sys.executable, str(CORPUS_CHECKER), str(args.corpus_manifest), "--creative-tools"],
            check=False,
            text=True,
            capture_output=True,
        )
        if checked.returncode:
            print("licensed photo corpus verification failed:", file=sys.stderr)
            print(checked.stdout, end="", file=sys.stderr)
            print(checked.stderr, end="", file=sys.stderr)
            return checked.returncode
    errors = validate(corpus, packet)
    if errors:
        for error in errors:
            print(f"FAIL {error}")
        print(f"creative_tools_review=FAIL errors={len(errors)}")
        return 1
    print("corpus_photos=12 category_mix=PASS")
    print("family_coverage=9/9 four_photos_each low_default_expressive fit_100_percent")
    print("visual_scores=PASS mean_at_least_4 no_dimension_below_3 artifacts=0")
    print("creative_tools_review=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
