#!/usr/bin/env python3
"""Contract tests for the creative-tools photographic review gate."""

from __future__ import annotations

import importlib.util
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("validate-creative-tools-review.py")
CORPUS_CHECKER = SCRIPT.with_name("check-real-image-corpus.py")
SPEC = importlib.util.spec_from_file_location("creative_review", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def fixture_inputs():
    assets = []
    categories = [
        "portrait", "portrait", "portrait", "night", "night", "architecture",
        "architecture", "product_glass", "product_glass", "landscape", "landscape", "monochrome",
    ]
    for index, category in enumerate(categories):
        tags = []
        if index == 0:
            tags = ["hair_detail"]
        elif index == 1:
            tags = ["skin_tone"]
        elif index == 7:
            tags = ["glass_transparency"]
        assets.append({
            "asset_id": f"photo-{index}",
            "media_type": "photo",
            "license_verified": True,
            "creative_categories": [category],
            "creative_visual_tags": tags,
        })
    corpus = {"assets": assets}
    reviews = []
    required_dimensions = MODULE.REQUIRED_DIMENSIONS
    # Assign four category-relevant photos to each family; overlap is deliberate.
    family_assets = {
        "F1": ["photo-0", "photo-3", "photo-7", "photo-9"],
        "F2": ["photo-5", "photo-7", "photo-9", "photo-11"],
        "F3": ["photo-0", "photo-1", "photo-3", "photo-7"],
        "T1": ["photo-0", "photo-5", "photo-7", "photo-9"],
        "T2": ["photo-0", "photo-5", "photo-9", "photo-11"],
        "T3": ["photo-0", "photo-3", "photo-7", "photo-8"],
        "B1": ["photo-0", "photo-5", "photo-7", "photo-9"],
        "B2": ["photo-3", "photo-4", "photo-7", "photo-9"],
        "B3": ["photo-0", "photo-5", "photo-7", "photo-9"],
    }
    for family, asset_ids in family_assets.items():
        for asset_id in asset_ids:
            for strength in MODULE.STRENGTHS:
                for zoom in MODULE.ZOOMS:
                    ratings = {dimension: 4 for dimension in required_dimensions[family]}
                    for dimension in set(MODULE.DIMENSIONS) - set(required_dimensions[family]):
                        ratings[dimension] = None
                    reviews.append({
                        "family": family,
                        "asset_id": asset_id,
                        "strength": strength,
                        "zoom": zoom,
                        "reviewer": "Reviewer A",
                        "ratings": ratings,
                        "artifacts": [],
                    })
    return corpus, {"reviews": reviews}


class CreativeReviewGateTest(unittest.TestCase):
    def test_complete_review_matrix_passes(self):
        corpus, packet = fixture_inputs()
        self.assertEqual([], MODULE.validate(corpus, packet))

    def test_category_shortfall_fails(self):
        corpus, packet = fixture_inputs()
        corpus["assets"] = [asset for asset in corpus["assets"] if asset["asset_id"] != "photo-3"]
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any("category night has 1 photos" in error for error in errors))
        self.assertTrue(any("unknown asset_id 'photo-3'" in error for error in errors))

    def test_fixed_corpus_rejects_extra_photos(self):
        corpus, packet = fixture_inputs()
        corpus["assets"].append({
            "asset_id": "photo-extra",
            "media_type": "photo",
            "license_verified": True,
            "creative_categories": ["portrait"],
        })
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any("fixed review corpus must contain exactly 12" in error for error in errors))

    def test_photo_cannot_fill_multiple_corpus_slots(self):
        corpus, packet = fixture_inputs()
        corpus["assets"][0]["creative_categories"] = ["portrait", "night"]
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any("assign exactly one primary creative category" in error for error in errors))

    def test_corpus_requires_difficult_subject_cases(self):
        corpus, packet = fixture_inputs()
        corpus["assets"][0]["creative_visual_tags"] = []
        corpus["assets"][1]["creative_visual_tags"] = []
        corpus["assets"][9]["creative_visual_tags"] = ["skin_tone"]
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any("a portrait photo with creative_visual_tags=hair_detail" in error for error in errors))
        self.assertTrue(any("a portrait photo with creative_visual_tags=skin_tone" in error for error in errors))

    def test_missing_review_cell_fails(self):
        corpus, packet = fixture_inputs()
        packet["reviews"].pop()
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any("missing strength/zoom reviews" in error for error in errors))

    def test_family_requires_relevant_photo_categories(self):
        corpus, packet = fixture_inputs()
        packet["reviews"] = [row for row in packet["reviews"] if row["family"] != "F3"]
        for asset_id in ("photo-5", "photo-9", "photo-11", "photo-6"):
            for strength in MODULE.STRENGTHS:
                for zoom in MODULE.ZOOMS:
                    packet["reviews"].append({
                        "family": "F3",
                        "asset_id": asset_id,
                        "strength": strength,
                        "zoom": zoom,
                        "reviewer": "Reviewer A",
                        "ratings": {dimension: 4 for dimension in MODULE.REQUIRED_DIMENSIONS["F3"]},
                        "artifacts": [],
                    })
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any("not in a relevant category for F3" in error for error in errors))

    def test_low_score_and_artifact_fail_independently(self):
        corpus, packet = fixture_inputs()
        packet["reviews"][0]["ratings"]["restraint"] = 2
        packet["reviews"][1]["artifacts"] = ["visible halo"]
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any("below the minimum 3" in error for error in errors))
        self.assertTrue(any("artifacts must be an empty array" in error for error in errors))

    def test_mean_threshold_fails_even_when_each_dimension_is_at_least_three(self):
        corpus, packet = fixture_inputs()
        for row in packet["reviews"]:
            if row["family"] == "F1":
                row["ratings"] = {key: (3 if value == 4 else value) for key, value in row["ratings"].items()}
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any(error.startswith("F1: mean score") for error in errors))

    def test_cli_schema_development_mode_reports_pass(self):
        corpus, packet = fixture_inputs()
        with tempfile.TemporaryDirectory(prefix="creative-review-") as directory:
            root = Path(directory)
            corpus_path = root / "corpus.json"
            packet_path = root / "review.json"
            corpus_path.write_text(json.dumps(corpus), encoding="utf-8")
            packet_path.write_text(json.dumps(packet), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(corpus_path), str(packet_path), "--skip-corpus-file-check"],
                check=False,
                text=True,
                capture_output=True,
            )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("creative_tools_review=PASS", result.stdout)

    def test_second_human_reviewer_can_rate_same_cell(self):
        corpus, packet = fixture_inputs()
        second_review = dict(packet["reviews"][0])
        second_review["reviewer"] = "Reviewer B"
        packet["reviews"].append(second_review)
        self.assertEqual([], MODULE.validate(corpus, packet))

    def test_same_reviewer_duplicate_cell_fails(self):
        corpus, packet = fixture_inputs()
        packet["reviews"].append(dict(packet["reviews"][0]))
        errors = MODULE.validate(corpus, packet)
        self.assertTrue(any("duplicate review" in error for error in errors))

    def test_creative_corpus_profile_verifies_hashes_and_category_mix(self):
        corpus, _ = fixture_inputs()
        with tempfile.TemporaryDirectory(prefix="creative-corpus-") as directory:
            root = Path(directory)
            entries = []
            for asset in corpus["assets"]:
                relative = Path("assets") / f"{asset['asset_id']}.jpg"
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                content = asset["asset_id"].encode("utf-8")
                path.write_bytes(content)
                entries.append({
                    **asset,
                    "path": str(relative),
                    "format": "jpg",
                    "license": "CC0 1.0",
                    "source_url": f"https://example.invalid/{asset['asset_id']}.jpg",
                    "source_page": f"https://example.invalid/{asset['asset_id']}",
                    "license_url": "https://creativecommons.org/publicdomain/zero/1.0/",
                    "sha256": hashlib.sha256(content).hexdigest(),
                    "size_bytes": len(content),
                    "tags": [],
                })
            manifest = root / "manifest.json"
            manifest.write_text(json.dumps({
                "manifest_version": 1,
                "asset_count": 12,
                "assets": entries,
            }), encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(CORPUS_CHECKER), str(manifest), "--creative-tools"],
                check=False,
                text=True,
                capture_output=True,
            )
        self.assertEqual(0, result.returncode, result.stdout + result.stderr)
        self.assertIn("creative-tools photo corpus ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
