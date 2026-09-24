#!/usr/bin/env python3
"""Self-test immutable public download and real-image corpus verification."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
CHECKER_PATH = REPO_ROOT / "scripts/perf/check-real-image-corpus.py"
FETCHER_PATH = REPO_ROOT / "scripts/perf/fetch-real-image-corpus.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def load_fetcher() -> Any:
    spec = importlib.util.spec_from_file_location("real_image_fetcher", FETCHER_PATH)
    if spec is None or spec.loader is None:
        raise SystemExit(f"unable to import {FETCHER_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules["real_image_fetcher"] = module
    spec.loader.exec_module(module)
    return module


def run_checker(
    manifest: Path,
    *,
    schema_only: bool = False,
    seed_manifest: bool = False,
    require_agent_cases: bool = False,
    require_human_ratings: bool = False,
) -> subprocess.CompletedProcess[str]:
    command = [sys.executable, "-B", str(CHECKER_PATH), str(manifest)]
    if schema_only:
        command.append("--schema-only")
    if seed_manifest:
        command.append("--seed-manifest")
    if require_agent_cases:
        command.append("--require-agent-cases")
    if require_human_ratings:
        command.append("--require-human-ratings")
    return subprocess.run(command, capture_output=True, check=False, text=True)


def build_manifest(root: Path) -> dict[str, Any]:
    tag_sets = [
        ["dark_raw", "textured_detail"],
        ["clipped_highlights"],
        ["smooth_sky"],
        ["skin"],
        ["foliage"],
        ["textured_detail"],
    ]
    assets: list[dict[str, Any]] = []
    for index, tags in enumerate(tag_sets):
        asset_id = f"fixture-photo-{index}"
        asset_path = root / f"{asset_id}.raw"
        recipe_path = root / f"{asset_id}-recipe.json"
        content = f"immutable-real-image:{asset_id}\n".encode("utf-8")
        asset_path.write_bytes(content)
        write_json(recipe_path, {"version": 1})
        assets.append(
            {
                "asset_id": asset_id,
                "path": asset_path.name,
                "media_type": "photo",
                "format": "raw",
                "license": "Internal QA fixture",
                "license_verified": True,
                "license_evidence": f"fixture-license-{index}",
                "sha256": hashlib.sha256(content).hexdigest(),
                "size_bytes": len(content),
                "tags": tags,
                "recipe": recipe_path.name,
                "review_regions": [],
                "agent_cases": [
                    {
                        "case_id": f"agent-case-{index}",
                        "instructions": "Make this photo brighter.",
                        "scope": "current-photo",
                        "expected_pixel_checks": ["brighter"],
                        "protected_regions": [],
                        "human_review": {"status": "pending"},
                    }
                ],
            }
        )
    assets[2]["agent_cases"].append(
        {
            "case_id": "agent-retouch-case",
            "kind": "retouch",
            "instructions": "Remove the small isolated spot from the smooth background.",
            "scope": "current-photo",
            "retouch": {
                "operation": "remove",
                "target_label": "small isolated spot",
                "region": {
                    "center_x": 0.72,
                    "center_y": 0.24,
                    "radius_x": 0.02,
                    "radius_y": 0.02,
                },
            },
            "expected_pixel_checks": [
                "artifactMaterialized",
                "fullResolutionArtifact",
                "targetChanged",
                "outsidePreserved",
                "backgroundContinuity",
            ],
            "protected_regions": [],
            "human_review": {"status": "pending"},
        }
    )
    return {
        "manifest_version": 1,
        "agent_evaluation_version": 1,
        "asset_count": len(assets),
        "assets": assets,
    }


def main() -> int:
    fetcher = load_fetcher()
    with tempfile.TemporaryDirectory(prefix="photo-editor-real-corpus-") as temp:
        root = Path(temp)
        manifest_payload = build_manifest(root)
        manifest_path = root / "manifest.json"
        write_json(manifest_path, manifest_payload)

        result = run_checker(manifest_path)
        require(result.returncode == 0, f"valid corpus was rejected: {result.stderr}")
        result = run_checker(
            manifest_path,
            schema_only=True,
            require_agent_cases=True,
        )
        require(result.returncode == 0, f"valid agent cases were rejected: {result.stderr}")
        result = run_checker(
            manifest_path,
            schema_only=True,
            require_agent_cases=True,
            require_human_ratings=True,
        )
        require(
            result.returncode == 1 and "completed passing human review is required" in result.stderr,
            f"pending human review was not rejected by the human gate: {result.stderr}",
        )

        reviewed = copy.deepcopy(manifest_payload)
        for asset in reviewed["assets"]:
            for agent_case in asset["agent_cases"]:
                agent_case["human_review"] = {
                    "status": "pass",
                    "rating": 4,
                    "reviewer": "QA Photographer",
                    "reviewed_at": "2026-08-21T12:00:00Z",
                    "notes": "Direction and protected areas are acceptable.",
                }
        reviewed_path = root / "reviewed.json"
        write_json(reviewed_path, reviewed)
        result = run_checker(
            reviewed_path,
            schema_only=True,
            require_agent_cases=True,
            require_human_ratings=True,
        )
        require(result.returncode == 0, f"completed human reviews were rejected: {result.stderr}")

        rejected = copy.deepcopy(reviewed)
        rejected["assets"][0]["agent_cases"][0]["human_review"]["status"] = "fail"
        rejected_path = root / "human-rejected.json"
        write_json(rejected_path, rejected)
        result = run_checker(
            rejected_path,
            schema_only=True,
            require_agent_cases=True,
            require_human_ratings=True,
        )
        require(
            result.returncode == 1 and "completed passing human review is required" in result.stderr,
            f"failed human review was not rejected by the human gate: {result.stderr}",
        )

        invalid_region = copy.deepcopy(manifest_payload)
        invalid_region["assets"][0]["agent_cases"][0]["protected_regions"] = [
            {
                "label": "background",
                "x": 0.8,
                "y": 0.2,
                "width": 0.4,
                "height": 0.3,
                "max_mean_rgb_delta": 0.02,
            }
        ]
        invalid_region_path = root / "invalid-region.json"
        write_json(invalid_region_path, invalid_region)
        result = run_checker(
            invalid_region_path,
            schema_only=True,
            require_agent_cases=True,
        )
        require(
            result.returncode == 1 and "bounds must stay within" in result.stderr,
            f"out-of-bounds protected region was not rejected: {result.stderr}",
        )

        invalid_retouch = copy.deepcopy(manifest_payload)
        invalid_retouch["assets"][2]["agent_cases"][1]["retouch"]["region"][
            "radius_x"
        ] = 0.9
        invalid_retouch_path = root / "invalid-retouch.json"
        write_json(invalid_retouch_path, invalid_retouch)
        result = run_checker(
            invalid_retouch_path,
            schema_only=True,
            require_agent_cases=True,
        )
        require(
            result.returncode == 1
            and "retouch region must stay within" in result.stderr,
            f"out-of-bounds retouch region was not rejected: {result.stderr}",
        )

        first_path = root / manifest_payload["assets"][0]["path"]
        original = first_path.read_bytes()
        first_path.write_bytes(b"X" + original[1:])
        result = run_checker(manifest_path)
        require(
            result.returncode == 1 and "sha256 mismatch" in result.stderr,
            f"same-size byte tampering was not rejected: {result.stderr}",
        )
        first_path.write_bytes(original)

        wrong_count = copy.deepcopy(manifest_payload)
        wrong_count["asset_count"] += 1
        wrong_count_path = root / "wrong-count.json"
        write_json(wrong_count_path, wrong_count)
        result = run_checker(wrong_count_path, schema_only=True)
        require(
            result.returncode == 1 and "asset_count mismatch" in result.stderr,
            f"stale asset count was not rejected: {result.stderr}",
        )

        duplicate = copy.deepcopy(manifest_payload)
        duplicate["assets"][1]["asset_id"] = duplicate["assets"][0]["asset_id"]
        duplicate_path = root / "duplicate.json"
        write_json(duplicate_path, duplicate)
        result = run_checker(duplicate_path, schema_only=True)
        require(
            result.returncode == 1 and "duplicate asset_id" in result.stderr,
            f"duplicate asset IDs were not rejected: {result.stderr}",
        )

        uppercase = copy.deepcopy(manifest_payload)
        uppercase["assets"][0]["sha256"] = uppercase["assets"][0]["sha256"].upper()
        uppercase_path = root / "uppercase-hash.json"
        write_json(uppercase_path, uppercase)
        result = run_checker(uppercase_path, schema_only=True)
        require(
            result.returncode == 1 and "lowercase 64-character" in result.stderr,
            f"noncanonical digest was not rejected: {result.stderr}",
        )

        assets_dir = root / "downloaded-assets"
        recipes_dir = root / "downloaded-recipes"
        assets_dir.mkdir()
        recipes_dir.mkdir()
        downloaded = b"pinned-public-source-bytes\n"
        seed = {
            "asset_id": "pinned-download",
            "source_url": "https://example.invalid/pinned.raw",
            "file_name": "pinned.raw",
            "sha256": hashlib.sha256(downloaded).hexdigest(),
            "size_bytes": len(downloaded),
            "recipe": {},
        }

        original_download = fetcher.download_file
        fetcher.download_file = lambda _url, destination, _timeout: destination.write_bytes(
            downloaded
        )
        try:
            materialized = fetcher.materialize_asset(
                seed, assets_dir, recipes_dir, 1.0
            )
            require(
                materialized["sha256"] == seed["sha256"]
                and materialized["size_bytes"] == seed["size_bytes"],
                "pinned download did not preserve verified identity",
            )
            wrong_seed = dict(seed)
            wrong_seed["sha256"] = "0" * 64
            try:
                fetcher.materialize_asset(
                    wrong_seed, assets_dir, recipes_dir, 1.0
                )
            except ValueError as exc:
                require(
                    "downloaded SHA-256 mismatch" in str(exc),
                    f"wrong download failure reason: {exc}",
                )
            else:
                raise SystemExit("changed upstream bytes were not rejected by fetcher")
        finally:
            fetcher.download_file = original_download

        public_seed = (
            REPO_ROOT
            / "perf-corpora"
            / "real-image-required"
            / "public-seed-manifest.json"
        )
        result = run_checker(
            public_seed,
            schema_only=True,
            seed_manifest=True,
            require_agent_cases=True,
        )
        require(result.returncode == 0, f"public agent seed was rejected: {result.stderr}")

    print("real_image_corpus_contract_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
