#!/usr/bin/env python3
"""Materialize the public real-image QA corpus from a seed manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import urllib.request
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SEED = REPO_ROOT / "perf-corpora" / "real-image-required" / "public-seed-manifest.json"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Download public licensed real-image QA assets into a local corpus directory."
    )
    parser.add_argument(
        "--seed",
        type=Path,
        default=DEFAULT_SEED,
        help="Seed manifest listing public source URLs and recipes.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Directory where assets, recipes, and manifest.json will be written.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite an existing output directory.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=120.0,
        help="Per-file download timeout in seconds.",
    )
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    if not isinstance(payload, dict):
        raise ValueError(f"{path} root must be a JSON object")
    return payload


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_file(url: str, destination: Path, timeout: float) -> None:
    request = urllib.request.Request(url, headers={"User-Agent": "PhotoEditorRealImageCorpus/1.0"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        with destination.open("wb") as handle:
            shutil.copyfileobj(response, handle)


def normalize_recipe(seed_asset: dict[str, Any]) -> dict[str, Any]:
    recipe = seed_asset.get("recipe", {})
    if not isinstance(recipe, dict):
        raise ValueError(f"{seed_asset.get('asset_id', '<unknown>')}: recipe must be an object")
    normalized = {"version": 1}
    normalized.update(recipe)
    return normalized


def materialize_asset(
    seed_asset: dict[str, Any],
    assets_dir: Path,
    recipes_dir: Path,
    timeout: float,
) -> dict[str, Any]:
    asset_id = seed_asset.get("asset_id")
    source_url = seed_asset.get("source_url")
    file_name = seed_asset.get("file_name")
    if not isinstance(asset_id, str) or not asset_id:
        raise ValueError("seed asset is missing asset_id")
    if not isinstance(source_url, str) or not source_url:
        raise ValueError(f"{asset_id}: source_url is required")
    if not isinstance(file_name, str) or not file_name:
        raise ValueError(f"{asset_id}: file_name is required")

    asset_path = assets_dir / file_name
    recipe_path = recipes_dir / f"{asset_id}.json"
    print(f"download {asset_id}: {source_url}")
    download_file(source_url, asset_path, timeout)
    write_json(recipe_path, normalize_recipe(seed_asset))

    asset = {
        key: value
        for key, value in seed_asset.items()
        if key not in {"file_name", "recipe"}
    }
    asset.update(
        {
            "path": str(asset_path.resolve()),
            "recipe": str(recipe_path.resolve()),
            "sha256": sha256_file(asset_path),
            "size_bytes": asset_path.stat().st_size,
        }
    )
    return asset


def main() -> int:
    args = parse_args()
    seed_path = args.seed.resolve()
    output = args.output.resolve()
    if output.exists():
        if not args.force:
            print(f"output already exists: {output}; pass --force to replace it", file=sys.stderr)
            return 2
        shutil.rmtree(output)
    output.mkdir(parents=True, exist_ok=False)
    assets_dir = output / "assets"
    recipes_dir = output / "recipes"
    assets_dir.mkdir()
    recipes_dir.mkdir()

    seed = load_json(seed_path)
    seed_assets = seed.get("assets")
    if not isinstance(seed_assets, list) or not seed_assets:
        print("seed manifest must contain a non-empty assets array", file=sys.stderr)
        return 2

    assets: list[dict[str, Any]] = []
    try:
        for seed_asset in seed_assets:
            if not isinstance(seed_asset, dict):
                raise ValueError("seed asset entries must be objects")
            assets.append(materialize_asset(seed_asset, assets_dir, recipes_dir, args.timeout))
    except Exception as exc:  # noqa: BLE001 - script should report failed source plainly.
        print(f"unable to materialize real-image corpus: {exc}", file=sys.stderr)
        return 1

    manifest = {
        "manifest_version": 1,
        "name": "real-image-public-v1",
        "description": "Materialized public licensed real-image QA corpus.",
        "created_at": datetime.now(timezone.utc).isoformat(),
        "seed_manifest": str(seed_path),
        "asset_count": len(assets),
        "assets": assets,
    }
    manifest_path = output / "manifest.json"
    write_json(manifest_path, manifest)
    print(f"corpus={output}")
    print(f"assets={len(assets)}")
    print(f"manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
