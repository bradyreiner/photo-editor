#!/usr/bin/env python3
"""Self-test the generated performance-artifact retention boundary."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path


SCRIPT = Path(__file__).with_name("prune-performance-artifacts.py")


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-B", str(SCRIPT), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )


def create_artifact(root: Path, name: str, age_days: int, pinned: bool = False) -> Path:
    artifact = root / name
    artifact.mkdir()
    (artifact / "report.json").write_text("{}\n", encoding="utf-8")
    if pinned:
        (artifact / ".retain").write_text("release review\n", encoding="utf-8")
    modified_at = time.time() - float(age_days) * 24.0 * 60.0 * 60.0
    os.utime(artifact, (modified_at, modified_at))
    return artifact


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary) / "perf-artifacts"
        root.mkdir()
        root = root.resolve()
        oldest = create_artifact(
            root,
            "20260101T000000.000000Z-perf-preview-raster-aaaaaaaa",
            30,
        )
        excess = create_artifact(
            root,
            "20260102T000000.000000Z-perf-preview-raster-bbbbbbbb",
            2,
        )
        newest = create_artifact(
            root,
            "20260103T000000.000000Z-perf-preview-raster-cccccccc",
            1,
        )
        pinned = create_artifact(
            root,
            "20260104T000000.000000Z-perf-preview-raster-dddddddd",
            90,
            pinned=True,
        )
        unmanaged = root / "notes"
        unmanaged.mkdir()
        planned = run(
            "--root",
            str(root),
            "--max-age-days",
            "14",
            "--max-per-category",
            "1",
        )
        if planned.returncode != 0:
            print(planned.stderr, file=sys.stderr)
            return 1
        plan = json.loads(planned.stdout)
        if (
            plan["mode"] != "plan"
            or str(oldest) not in plan["planned_removals"]
            or str(excess) not in plan["planned_removals"]
            or str(newest) not in plan["retained"]
            or str(pinned) not in plan["retained"]
            or str(unmanaged) not in plan["skipped_unmanaged"]
            or not oldest.exists()
            or not excess.exists()
        ):
            print(
                "dry-run retention plan is incomplete or mutated input: "
                + json.dumps(plan, sort_keys=True),
                file=sys.stderr,
            )
            return 1
        applied = run(
            "--root",
            str(root),
            "--max-age-days",
            "14",
            "--max-per-category",
            "1",
            "--apply",
        )
        if applied.returncode != 0:
            print(applied.stderr, file=sys.stderr)
            return 1
        result = json.loads(applied.stdout)
        if (
            oldest.exists()
            or excess.exists()
            or not newest.exists()
            or not pinned.exists()
            or not unmanaged.exists()
            or sorted(result["removed"])
            != sorted([str(oldest), str(excess)])
        ):
            print("applied retention did not preserve exact managed boundaries", file=sys.stderr)
            return 1
        unsafe = run("--root", str(Path(temporary) / "other"))
        if unsafe.returncode != 2:
            print("unsafe retention roots must fail closed", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
