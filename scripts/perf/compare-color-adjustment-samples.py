#!/usr/bin/env python3
"""Compare color-adjustment sample distributions between two artifacts."""

from __future__ import annotations

import argparse
import json
import math
import random
import statistics
from dataclasses import dataclass
from pathlib import Path
from typing import Any


EVENT_NAME = "benchmark.color_adjustment_sample.completed"
DEFAULT_STAGES = ("preview-960", "full-1920")


@dataclass(frozen=True)
class SampleSet:
    state: str
    stage: str
    field: str
    values: tuple[float, ...]
    iterations: tuple[int, ...]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare raw benchmark.color_adjustment_sample.completed events. "
            "The gate uses a deterministic bootstrap over mean deltas where "
            "delta = candidate - baseline."
        )
    )
    parser.add_argument("--baseline", required=True, type=Path, help="Accepted baseline artifact.")
    parser.add_argument("--candidate", required=True, type=Path, help="Candidate artifact.")
    parser.add_argument("--state", required=True, help="State filter, for example spatial-detail.")
    parser.add_argument(
        "--stage",
        action="append",
        choices=DEFAULT_STAGES,
        help="Stage to compare. May be repeated. Defaults to preview-960 and full-1920.",
    )
    parser.add_argument(
        "--field",
        action="append",
        default=[],
        help="Numeric sample field to compare. May be repeated. Defaults to elapsed_ms.",
    )
    parser.add_argument("--min-count", type=int, default=30)
    parser.add_argument("--confidence", type=float, default=0.95)
    parser.add_argument("--bootstrap-iterations", type=int, default=5000)
    parser.add_argument("--seed", type=int, default=20260529)
    parser.add_argument(
        "--max-regression-ms",
        type=float,
        default=0.0,
        help="Maximum allowed upper-confidence mean regression in milliseconds.",
    )
    parser.add_argument(
        "--max-regression-pct",
        type=float,
        default=0.0,
        help="Maximum allowed upper-confidence mean regression as percent of baseline mean.",
    )
    parser.add_argument(
        "--min-improvement-ms",
        type=float,
        default=0.0,
        help="Required lower-bound mean improvement in milliseconds.",
    )
    parser.add_argument(
        "--min-improvement-pct",
        type=float,
        default=0.0,
        help="Required lower-bound mean improvement as percent of baseline mean.",
    )
    return parser.parse_args()


def artifact_root(path: Path) -> Path:
    resolved = path.expanduser().resolve()
    if resolved.is_file() and resolved.name == "summary.json":
        return resolved.parent
    if resolved.is_dir():
        return resolved
    raise SystemExit(f"Artifact does not exist: {resolved}")


def jsonl_files_for_artifact(path: Path) -> list[Path]:
    root = artifact_root(path)
    manifest_path = root / "manifest.json"
    if manifest_path.is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            manifest = {}
        desktop_log = manifest.get("desktop_log")
        if isinstance(desktop_log, str):
            candidate = Path(desktop_log)
            if candidate.is_file():
                return [candidate]
    logs_root = root / "logs"
    if logs_root.is_dir():
        return sorted(logs_root.glob("runs/*/*.jsonl"))
    return []


def normalize_record(record: dict[str, Any]) -> tuple[str | None, dict[str, Any]]:
    raw_fields = record.get("fields")
    fields = raw_fields if isinstance(raw_fields, dict) else {}
    event = record.get("event")
    if not isinstance(event, str) or not event:
        event = fields.get("event")
    if not isinstance(event, str) or not event:
        return None, fields
    return event, fields


def coerce_number(value: Any) -> float | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, (int, float)) and math.isfinite(value):
        return float(value)
    if isinstance(value, str):
        try:
            parsed = float(value.strip())
        except ValueError:
            return None
        return parsed if math.isfinite(parsed) else None
    return None


def coerce_iteration(value: Any) -> int | None:
    number = coerce_number(value)
    if number is None or not number.is_integer():
        return None
    return int(number)


def load_samples(artifact: Path, state: str, stage: str, field: str) -> SampleSet:
    values_by_iteration: dict[int, float] = {}
    for jsonl_path in jsonl_files_for_artifact(artifact):
        with jsonl_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not isinstance(record, dict):
                    continue
                event, fields = normalize_record(record)
                if event != EVENT_NAME:
                    continue
                if fields.get("state") != state or fields.get("stage") != stage:
                    continue
                iteration = coerce_iteration(fields.get("iteration"))
                value = coerce_number(fields.get(field))
                if iteration is None or value is None:
                    continue
                if iteration in values_by_iteration:
                    raise SystemExit(
                        f"Duplicate {state}/{stage} iteration {iteration} in {artifact}"
                    )
                values_by_iteration[iteration] = value
    ordered_iterations = tuple(sorted(values_by_iteration))
    ordered_values = tuple(values_by_iteration[index] for index in ordered_iterations)
    return SampleSet(
        state=state,
        stage=stage,
        field=field,
        values=ordered_values,
        iterations=ordered_iterations,
    )


def percentile(values: tuple[float, ...], fraction: float) -> float:
    if not values:
        raise ValueError("percentile requires at least one value")
    ordered = sorted(values)
    index = min(len(ordered) - 1, math.ceil(fraction * len(ordered)) - 1)
    return ordered[index]


def paired_deltas(baseline: SampleSet, candidate: SampleSet) -> tuple[float, ...] | None:
    if baseline.iterations != candidate.iterations or len(baseline.values) != len(candidate.values):
        return None
    return tuple(
        candidate_value - baseline_value
        for baseline_value, candidate_value in zip(baseline.values, candidate.values)
    )


def bootstrap_mean_delta_ci(
    baseline: SampleSet,
    candidate: SampleSet,
    confidence: float,
    iterations: int,
    rng: random.Random,
) -> tuple[float, float]:
    if iterations <= 0:
        raise SystemExit("--bootstrap-iterations must be positive")
    if not 0.0 < confidence < 1.0:
        raise SystemExit("--confidence must be between 0 and 1")
    if not baseline.values or not candidate.values:
        raise SystemExit("Cannot bootstrap empty sample sets")

    paired = paired_deltas(baseline, candidate)
    deltas: list[float] = []
    if paired is not None:
        for _ in range(iterations):
            total = 0.0
            for _ in paired:
                total += paired[rng.randrange(len(paired))]
            deltas.append(total / len(paired))
    else:
        for _ in range(iterations):
            baseline_total = 0.0
            candidate_total = 0.0
            for _ in baseline.values:
                baseline_total += baseline.values[rng.randrange(len(baseline.values))]
            for _ in candidate.values:
                candidate_total += candidate.values[rng.randrange(len(candidate.values))]
            deltas.append(
                (candidate_total / len(candidate.values)) -
                (baseline_total / len(baseline.values))
            )

    deltas.sort()
    tail = (1.0 - confidence) / 2.0
    lower = deltas[min(len(deltas) - 1, max(0, math.floor(tail * len(deltas))))]
    upper = deltas[min(len(deltas) - 1, max(0, math.ceil((1.0 - tail) * len(deltas)) - 1))]
    return lower, upper


def summarize(samples: SampleSet) -> dict[str, float]:
    return {
        "avg": statistics.fmean(samples.values),
        "p50": percentile(samples.values, 0.50),
        "p95": percentile(samples.values, 0.95),
        "max": max(samples.values),
    }


def compare_one(
    baseline: SampleSet,
    candidate: SampleSet,
    args: argparse.Namespace,
    rng: random.Random,
) -> None:
    if len(baseline.values) < args.min_count:
        raise SystemExit(
            f"{baseline.state}/{baseline.stage}/{baseline.field} baseline has "
            f"{len(baseline.values)} samples; expected at least {args.min_count}"
        )
    if len(candidate.values) < args.min_count:
        raise SystemExit(
            f"{candidate.state}/{candidate.stage}/{candidate.field} candidate has "
            f"{len(candidate.values)} samples; expected at least {args.min_count}"
        )

    baseline_summary = summarize(baseline)
    candidate_summary = summarize(candidate)
    lower_ci, upper_ci = bootstrap_mean_delta_ci(
        baseline,
        candidate,
        args.confidence,
        args.bootstrap_iterations,
        rng,
    )
    baseline_mean = baseline_summary["avg"]
    regression_allowance = max(
        args.max_regression_ms,
        baseline_mean * args.max_regression_pct / 100.0,
    )
    improvement_requirement = max(
        args.min_improvement_ms,
        baseline_mean * args.min_improvement_pct / 100.0,
    )
    label = f"{baseline.state}/{baseline.stage}/{baseline.field}"

    if upper_ci > regression_allowance:
        raise SystemExit(
            f"{label} regression bound failed: upper {args.confidence:.0%} CI "
            f"mean delta {upper_ci:.3f} ms exceeds allowance {regression_allowance:.3f} ms"
        )
    if improvement_requirement > 0.0 and upper_ci > -improvement_requirement:
        raise SystemExit(
            f"{label} improvement bound failed: upper {args.confidence:.0%} CI "
            f"mean delta {upper_ci:.3f} ms is not <= {-improvement_requirement:.3f} ms"
        )

    print(
        f"{label}: n={len(candidate.values)} "
        f"baseline_avg={baseline_summary['avg']:.3f} candidate_avg={candidate_summary['avg']:.3f} "
        f"delta_avg={candidate_summary['avg'] - baseline_summary['avg']:.3f} "
        f"delta_p95={candidate_summary['p95'] - baseline_summary['p95']:.3f} "
        f"mean_delta_ci{args.confidence:.0%}=[{lower_ci:.3f}, {upper_ci:.3f}]"
    )


def main() -> int:
    args = parse_args()
    if args.min_count <= 0:
        raise SystemExit("--min-count must be positive")
    stages = tuple(args.stage) if args.stage else DEFAULT_STAGES
    fields = tuple(args.field) if args.field else ("elapsed_ms",)
    rng = random.Random(args.seed)

    for stage in stages:
        for field in fields:
            baseline = load_samples(args.baseline, args.state, stage, field)
            candidate = load_samples(args.candidate, args.state, stage, field)
            compare_one(baseline, candidate, args, rng)

    print("color_adjustment_sample_compare=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
