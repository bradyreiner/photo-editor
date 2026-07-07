#!/usr/bin/env python3
"""Run or validate the accepted max-preview brush frontier gates."""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_APP = REPO_ROOT / "build/dev-macos-debug/tests/preview_benchmark_driver"
DEFAULT_CORPUS = REPO_ROOT / "perf-corpora/preview-synthetic-v1/manifest.json"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"


@dataclass(frozen=True)
class FrontierCase:
    name: str
    tile_span: int
    p95_max_ms: int
    p99_max_ms: int
    parity_max_delta: int
    texture_upload_pixels: int


FRONTIER_CASES = (
    FrontierCase(
        name="one-tile",
        tile_span=1,
        p95_max_ms=20,
        p99_max_ms=25,
        parity_max_delta=3,
        texture_upload_pixels=28_561,
    ),
    FrontierCase(
        name="two-by-two",
        tile_span=2,
        p95_max_ms=65,
        p99_max_ms=75,
        parity_max_delta=4,
        texture_upload_pixels=463_761,
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run or validate accepted preview brush frontier performance gates."
    )
    parser.add_argument("--app", type=Path, default=DEFAULT_APP, help="preview_benchmark_driver path.")
    parser.add_argument("--corpus", type=Path, default=DEFAULT_CORPUS, help="Preview corpus manifest.")
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--iterations", type=int, default=30)
    parser.add_argument("--timeout", type=int, default=180, help="Outer benchmark timeout seconds.")
    parser.add_argument("--timeout-ms", type=int, default=30000, help="Driver per-render timeout.")
    parser.add_argument("--one-tile-artifact", type=Path, help="Validate an existing one-tile artifact.")
    parser.add_argument("--two-by-two-artifact", type=Path, help="Validate an existing 2x2 artifact.")
    return parser.parse_args()


def run_command(command: list[str]) -> subprocess.CompletedProcess[str]:
    print("+ " + " ".join(command), flush=True)
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n")
    return result


def parse_artifact_dir(output: str) -> Path:
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key == "artifact_dir" and value.strip():
            return Path(value.strip()).resolve()
    raise SystemExit("Benchmark completed without printing artifact_dir")


def run_frontier_case(args: argparse.Namespace, frontier_case: FrontierCase) -> Path:
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-preview-raster",
        "--app",
        str(args.app),
        "--output-root",
        str(args.output_root),
        "--corpus",
        str(args.corpus),
        "--event",
        "benchmark.preview_brush_tile_cache.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--corpus",
        str(args.corpus),
        "--limit",
        "1",
        "--brush-tile-cache",
        "--brush-tile-span",
        str(frontier_case.tile_span),
        "--brush-texture-patch-only",
        "--iterations",
        str(args.iterations),
        "--timeout-ms",
        str(args.timeout_ms),
    ]
    result = run_command(command)
    if result.returncode != 0:
        raise SystemExit(result.returncode)
    return parse_artifact_dir(result.stdout)


def validate_frontier_case(artifact: Path, frontier_case: FrontierCase, iterations: int) -> None:
    checker = [
        sys.executable,
        "-B",
        "scripts/perf/check-summary-thresholds.py",
        str(artifact),
        "--event",
        "benchmark.preview_brush_tile_cache.completed",
        "--field",
        "brush_p95_ms",
        "--min-count",
        "1",
        "--p50-max",
        str(frontier_case.p95_max_ms),
        "--max",
        str(frontier_case.p95_max_ms),
        "--metric-min",
        f"iterations={iterations}",
        "--metric-max",
        f"parity_max_channel_delta={frontier_case.parity_max_delta}",
        "--metric-max",
        "texture_patch_base_copy_pixels=0",
        "--metric-max",
        "brush_max_decode_ms=0",
        "--metric-max",
        f"texture_upload_pixels={frontier_case.texture_upload_pixels}",
        "--metric-min",
        f"texture_upload_pixels={frontier_case.texture_upload_pixels}",
        "--metric-eq",
        "texture_upload_image=true",
    ]
    result = run_command(checker)
    if result.returncode != 0:
        raise SystemExit(result.returncode)

    p99_checker = [
        sys.executable,
        "-B",
        "scripts/perf/check-summary-thresholds.py",
        str(artifact),
        "--event",
        "benchmark.preview_brush_tile_cache.completed",
        "--field",
        "brush_p99_ms",
        "--min-count",
        "1",
        "--max",
        str(frontier_case.p99_max_ms),
    ]
    result = run_command(p99_checker)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def main() -> int:
    args = parse_args()
    if args.iterations <= 0:
        raise SystemExit("--iterations must be positive")

    supplied_artifacts = {
        "one-tile": args.one_tile_artifact,
        "two-by-two": args.two_by_two_artifact,
    }
    artifacts: dict[str, Path] = {}
    for frontier_case in FRONTIER_CASES:
        supplied = supplied_artifacts[frontier_case.name]
        artifacts[frontier_case.name] = (
            supplied.expanduser().resolve()
            if supplied is not None
            else run_frontier_case(args, frontier_case)
        )
        validate_frontier_case(artifacts[frontier_case.name], frontier_case, args.iterations)

    print("preview_frontier_gate=passed")
    for name, artifact in artifacts.items():
        print(f"{name}_artifact={artifact}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
