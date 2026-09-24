#!/usr/bin/env python3
"""Plan or apply bounded retention for generated local performance artifacts."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import sys
import time
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_ROOT = REPO_ROOT / "perf-artifacts"
ARTIFACT_NAME = re.compile(
    r"^\d{8}T\d{6}(?:\.\d+)?Z-(?P<category>.+?)-[0-9a-f]{8,}$"
)
PIN_MARKERS = (".retain", "promotion-manifest.json")


@dataclass(frozen=True)
class Artifact:
    path: Path
    category: str
    modified_at: float
    pinned: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Bound ignored local performance artifacts by age and per-category count. "
            "The default is a non-mutating plan."
        )
    )
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--max-age-days", type=int, default=14)
    parser.add_argument("--max-per-category", type=int, default=20)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--json-report", type=Path)
    return parser.parse_args()


def artifact_category(name: str) -> str | None:
    match = ARTIFACT_NAME.fullmatch(name)
    return match.group("category") if match else None


def inventory(root: Path) -> tuple[list[Artifact], list[str]]:
    artifacts: list[Artifact] = []
    skipped: list[str] = []
    if not root.exists():
        return artifacts, skipped
    for path in sorted(root.iterdir()):
        category = artifact_category(path.name)
        if category is None or not path.is_dir() or path.is_symlink():
            skipped.append(str(path))
            continue
        artifacts.append(
            Artifact(
                path=path,
                category=category,
                modified_at=path.stat().st_mtime,
                pinned=any((path / marker).exists() for marker in PIN_MARKERS),
            )
        )
    return artifacts, skipped


def retention_plan(
    artifacts: list[Artifact],
    *,
    now: float,
    max_age_days: int,
    max_per_category: int,
) -> tuple[list[Artifact], list[Artifact]]:
    cutoff = now - float(max_age_days) * 24.0 * 60.0 * 60.0
    retained: list[Artifact] = []
    removed: list[Artifact] = []
    by_category: dict[str, list[Artifact]] = {}
    for artifact in artifacts:
        by_category.setdefault(artifact.category, []).append(artifact)
    for category_artifacts in by_category.values():
        ordered = sorted(
            category_artifacts,
            key=lambda artifact: (artifact.modified_at, artifact.path.name),
            reverse=True,
        )
        unpinned_rank = 0
        for artifact in ordered:
            if artifact.pinned:
                retained.append(artifact)
                continue
            unpinned_rank += 1
            expired = artifact.modified_at < cutoff
            over_count = unpinned_rank > max_per_category
            (removed if expired or over_count else retained).append(artifact)
    return retained, removed


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    if root.name != "perf-artifacts":
        print(
            "artifact retention root must be a directory named perf-artifacts",
            file=sys.stderr,
        )
        return 2
    if args.max_age_days < 1 or args.max_per_category < 1:
        print("retention limits must be positive", file=sys.stderr)
        return 2
    artifacts, skipped = inventory(root)
    retained, removed = retention_plan(
        artifacts,
        now=time.time(),
        max_age_days=args.max_age_days,
        max_per_category=args.max_per_category,
    )
    removed_paths: list[str] = []
    if args.apply:
        for artifact in removed:
            shutil.rmtree(artifact.path)
            removed_paths.append(str(artifact.path))
    report = {
        "schema": "photo-editor-performance-artifact-retention:v1",
        "mode": "apply" if args.apply else "plan",
        "root": str(root),
        "max_age_days": args.max_age_days,
        "max_per_category": args.max_per_category,
        "inventoried": len(artifacts),
        "retained": [str(artifact.path) for artifact in sorted(retained, key=lambda item: item.path.name)],
        "planned_removals": [
            str(artifact.path) for artifact in sorted(removed, key=lambda item: item.path.name)
        ],
        "removed": removed_paths,
        "skipped_unmanaged": skipped,
    }
    encoded = json.dumps(report, indent=2, sort_keys=True)
    print(encoded)
    if args.json_report is not None:
        args.json_report.parent.mkdir(parents=True, exist_ok=True)
        args.json_report.write_text(encoded + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
