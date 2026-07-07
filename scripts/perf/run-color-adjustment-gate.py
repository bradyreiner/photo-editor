#!/usr/bin/env python3
"""Run or validate accepted color-adjustment engine regression gates."""

from __future__ import annotations

import argparse
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_APP = REPO_ROOT / "build/dev-macos-debug/tests/color_adjustment_benchmark_driver"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"


@dataclass(frozen=True)
class ColorAdjustmentCase:
    stage: str
    p95_max_ms: int
    avg_max_ms: int
    spatial_avg_max_ms: int
    post_color_avg_max_ms: int | None = None
    detail_avg_max_ms: int | None = None


TEXTURE_ONLY_CASES = (
    ColorAdjustmentCase(
        stage="preview-960",
        p95_max_ms=160,
        avg_max_ms=140,
        spatial_avg_max_ms=55,
    ),
    ColorAdjustmentCase(
        stage="full-1920",
        p95_max_ms=560,
        avg_max_ms=530,
        spatial_avg_max_ms=180,
    ),
)


SPATIAL_DETAIL_CASES = (
    ColorAdjustmentCase(
        stage="preview-960",
        p95_max_ms=380,
        avg_max_ms=340,
        spatial_avg_max_ms=90,
        post_color_avg_max_ms=130,
        detail_avg_max_ms=130,
    ),
    ColorAdjustmentCase(
        stage="full-1920",
        p95_max_ms=1300,
        avg_max_ms=1200,
        spatial_avg_max_ms=300,
        post_color_avg_max_ms=420,
        detail_avg_max_ms=380,
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run or validate color-adjustment engine performance gates."
    )
    parser.add_argument("--app", type=Path, default=DEFAULT_APP, help="color_adjustment_benchmark_driver path.")
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--iterations", type=int, default=30)
    parser.add_argument("--timeout", type=int, default=120, help="Outer benchmark timeout seconds.")
    parser.add_argument("--texture-only-artifact", type=Path, help="Validate an existing texture-only artifact.")
    parser.add_argument("--spatial-detail-artifact", type=Path, help="Validate an existing spatial/detail artifact.")
    parser.add_argument(
        "--baseline-texture-only-artifact",
        type=Path,
        help="Accepted texture-only artifact to compare the candidate artifact against.",
    )
    parser.add_argument(
        "--baseline-spatial-detail-artifact",
        type=Path,
        help="Accepted spatial/detail artifact to compare the candidate artifact against.",
    )
    parser.add_argument(
        "--compare-field",
        action="append",
        default=[],
        help="Raw sample field to compare against the baseline. Defaults to elapsed_ms.",
    )
    parser.add_argument("--compare-confidence", type=float, default=0.95)
    parser.add_argument("--compare-bootstrap-iterations", type=int, default=5000)
    parser.add_argument("--compare-max-regression-ms", type=float, default=0.0)
    parser.add_argument("--compare-max-regression-pct", type=float, default=0.0)
    parser.add_argument("--compare-min-improvement-ms", type=float, default=0.0)
    parser.add_argument("--compare-min-improvement-pct", type=float, default=0.0)
    parser.add_argument(
        "--require-gpu-work",
        action="store_true",
        help=(
            "Require Metal/GPU dispatch counters to prove the texture-only spatial path ran on GPU. "
            "Leave disabled on CPU-fallback hosts."
        ),
    )
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


def run_color_adjustment_case(args: argparse.Namespace, driver_args: list[str]) -> Path:
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-color-adjustment",
        "--app",
        str(args.app),
        "--output-root",
        str(args.output_root),
        "--event",
        "benchmark.color_adjustment_suite.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--iterations",
        str(args.iterations),
    ]
    command.extend(driver_args)
    result = run_command(command)
    if result.returncode != 0:
        raise SystemExit(result.returncode)
    return parse_artifact_dir(result.stdout)


def run_texture_only_case(args: argparse.Namespace) -> Path:
    return run_color_adjustment_case(args, ["--spatial-texture-only"])


def run_spatial_detail_case(args: argparse.Namespace) -> Path:
    return run_color_adjustment_case(args, [])


def validate_suite_artifact(
    artifact: Path, iterations: int, state_name: str, elapsed_max_ms: int
) -> None:
    suite_checker = [
        sys.executable,
        "-B",
        "scripts/perf/check-summary-thresholds.py",
        str(artifact),
        "--event",
        "benchmark.color_adjustment_suite.completed",
        "--field",
        "elapsed_ms",
        "--min-count",
        "1",
        "--max-count",
        "1",
        "--max",
        str(elapsed_max_ms),
        "--metric-eq",
        f"state={state_name}",
        "--metric-min",
        f"iterations={iterations}",
    ]
    result = run_command(suite_checker)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def validate_sample_records(
    artifact: Path,
    iterations: int,
    state_name: str,
    adjustment_cases: tuple[ColorAdjustmentCase, ...],
) -> None:
    for adjustment_case in adjustment_cases:
        checker = [
            sys.executable,
            "-B",
            "scripts/perf/check-summary-thresholds.py",
            str(artifact),
            "--event",
            "benchmark.color_adjustment_sample.completed",
            "--field",
            "elapsed_ms",
            "--min-count",
            str(iterations),
            "--max-count",
            str(iterations),
            "--where",
            f"stage={adjustment_case.stage}",
            "--where",
            f"state={state_name}",
            "--metric-eq",
            f"iterations={iterations}",
            "--metric-min",
            "iteration=1",
            "--metric-max",
            f"iteration={iterations}",
        ]
        result = run_command(checker)
        if result.returncode != 0:
            raise SystemExit(result.returncode)


def validate_adjustment_cases(
    artifact: Path,
    iterations: int,
    state_name: str,
    adjustment_cases: tuple[ColorAdjustmentCase, ...],
    require_gpu_work: bool = False,
    require_texture_only_shape: bool = False,
) -> None:
    for adjustment_case in adjustment_cases:
        checker = [
            sys.executable,
            "-B",
            "scripts/perf/check-summary-thresholds.py",
            str(artifact),
            "--event",
            "benchmark.color_adjustment_pass.completed",
            "--field",
            "p95_ms",
            "--min-count",
            "1",
            "--max-count",
            "1",
            "--max",
            str(adjustment_case.p95_max_ms),
            "--where",
            f"stage={adjustment_case.stage}",
            "--where",
            f"state={state_name}",
            "--metric-max",
            f"avg_ms={adjustment_case.avg_max_ms}",
            "--metric-max",
            f"adjustment_spatial_avg_ms={adjustment_case.spatial_avg_max_ms}",
            "--metric-min",
            f"iterations={iterations}",
        ]
        if adjustment_case.post_color_avg_max_ms is not None:
            checker.extend(
                [
                    "--metric-max",
                    f"adjustment_post_color_avg_ms={adjustment_case.post_color_avg_max_ms}",
                ]
            )
        if adjustment_case.detail_avg_max_ms is not None:
            checker.extend(
                [
                    "--metric-max",
                    f"adjustment_detail_avg_ms={adjustment_case.detail_avg_max_ms}",
                ]
            )
        if require_texture_only_shape:
            checker.extend(
                [
                    "--metric-max",
                    "adjustment_spatial_clarity_blur_avg_ms=0",
                    "--metric-max",
                    "adjustment_spatial_haze_blur_avg_ms=0",
                    "--metric-max",
                    "adjustment_gpu_spatial_clarity_blur_dispatch_count_avg=0",
                    "--metric-max",
                    "adjustment_gpu_spatial_haze_blur_dispatch_count_avg=0",
                ]
            )
            if require_gpu_work:
                checker.extend(
                    [
                        "--metric-max",
                        "adjustment_spatial_luminance_avg_ms=0",
                        "--metric-max",
                        "adjustment_spatial_texture_blur_avg_ms=0",
                        "--metric-max",
                        "adjustment_spatial_apply_avg_ms=0",
                    ]
                )
            else:
                checker.extend(
                    [
                        "--metric-min",
                        "adjustment_spatial_texture_blur_avg_ms=1",
                    ]
                )
        if require_gpu_work:
            checker.extend(
                [
                    "--metric-min",
                    "adjustment_gpu_command_buffer_count_avg=1",
                    "--metric-min",
                    "adjustment_gpu_kernel_dispatch_count_avg=1",
                    "--metric-min",
                    "adjustment_gpu_cpu_to_gpu_bytes_avg=1",
                    "--metric-min",
                    "adjustment_gpu_gpu_to_cpu_bytes_avg=1",
                    "--metric-min",
                    "adjustment_gpu_spatial_texture_blur_dispatch_count_avg=1",
                ]
            )
        result = run_command(checker)
        if result.returncode != 0:
            raise SystemExit(result.returncode)


def validate_texture_only_artifact(artifact: Path, iterations: int, require_gpu_work: bool) -> None:
    validate_suite_artifact(artifact, iterations, "spatial-texture-only", 30000)
    validate_sample_records(artifact, iterations, "spatial-texture-only", TEXTURE_ONLY_CASES)
    validate_adjustment_cases(
        artifact,
        iterations,
        "spatial-texture-only",
        TEXTURE_ONLY_CASES,
        require_gpu_work=require_gpu_work,
        require_texture_only_shape=True,
    )


def validate_spatial_detail_artifact(artifact: Path, iterations: int) -> None:
    validate_suite_artifact(artifact, iterations, "spatial-detail", 90000)
    validate_sample_records(artifact, iterations, "spatial-detail", SPATIAL_DETAIL_CASES)
    validate_adjustment_cases(
        artifact,
        iterations,
        "spatial-detail",
        SPATIAL_DETAIL_CASES,
    )


def validate_sample_comparison(
    args: argparse.Namespace,
    baseline_artifact: Path | None,
    candidate_artifact: Path,
    state_name: str,
) -> None:
    if baseline_artifact is None:
        return
    command = [
        sys.executable,
        "-B",
        "scripts/perf/compare-color-adjustment-samples.py",
        "--baseline",
        str(baseline_artifact.expanduser().resolve()),
        "--candidate",
        str(candidate_artifact.expanduser().resolve()),
        "--state",
        state_name,
        "--min-count",
        str(args.iterations),
        "--confidence",
        str(args.compare_confidence),
        "--bootstrap-iterations",
        str(args.compare_bootstrap_iterations),
        "--max-regression-ms",
        str(args.compare_max_regression_ms),
        "--max-regression-pct",
        str(args.compare_max_regression_pct),
        "--min-improvement-ms",
        str(args.compare_min_improvement_ms),
        "--min-improvement-pct",
        str(args.compare_min_improvement_pct),
    ]
    for field_name in args.compare_field or ["elapsed_ms"]:
        command.extend(["--field", field_name])
    result = run_command(command)
    if result.returncode != 0:
        raise SystemExit(result.returncode)


def main() -> int:
    args = parse_args()
    if args.iterations <= 0:
        raise SystemExit("--iterations must be positive")

    texture_only_artifact = (
        args.texture_only_artifact.expanduser().resolve()
        if args.texture_only_artifact is not None
        else run_texture_only_case(args)
    )
    validate_texture_only_artifact(texture_only_artifact, args.iterations, args.require_gpu_work)
    validate_sample_comparison(
        args,
        args.baseline_texture_only_artifact,
        texture_only_artifact,
        "spatial-texture-only",
    )

    spatial_detail_artifact = (
        args.spatial_detail_artifact.expanduser().resolve()
        if args.spatial_detail_artifact is not None
        else run_spatial_detail_case(args)
    )
    validate_spatial_detail_artifact(spatial_detail_artifact, args.iterations)
    validate_sample_comparison(
        args,
        args.baseline_spatial_detail_artifact,
        spatial_detail_artifact,
        "spatial-detail",
    )

    print("color_adjustment_gate=passed")
    print(f"texture-only_artifact={texture_only_artifact}")
    print(f"spatial-detail_artifact={spatial_detail_artifact}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
