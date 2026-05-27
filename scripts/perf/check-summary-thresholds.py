#!/usr/bin/env python3
"""Validate loose performance thresholds from a benchmark summary artifact."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Check event timing thresholds in scripts/perf/summarize-jsonl.py output."
    )
    parser.add_argument(
        "summary",
        type=Path,
        help="A summary.json file, one benchmark artifact directory, or an output root.",
    )
    parser.add_argument("--event", required=True, help="Event name to validate.")
    parser.add_argument(
        "--field",
        default="elapsed_ms",
        help="Timing field to validate. Defaults to elapsed_ms.",
    )
    parser.add_argument(
        "--min-count",
        type=int,
        default=1,
        help="Minimum number of samples required for the timing field.",
    )
    parser.add_argument("--p50-max", type=float, help="Maximum allowed p50 in milliseconds.")
    parser.add_argument("--p95-max", type=float, help="Maximum allowed p95 in milliseconds.")
    parser.add_argument("--max", dest="max_value", type=float, help="Maximum allowed max in milliseconds.")
    return parser.parse_args()


def summary_json_path(path: Path) -> Path:
    path = path.expanduser().resolve()
    if path.is_file():
        return path
    direct = path / "summary.json"
    if direct.is_file():
        return direct
    matches = sorted(path.glob("*/summary.json"), key=lambda candidate: candidate.stat().st_mtime)
    if matches:
        return matches[-1]
    raise SystemExit(f"No summary.json found under {path}")


def number(value: Any, label: str) -> float:
    if not isinstance(value, (int, float)):
        raise SystemExit(f"{label} is missing or non-numeric: {value!r}")
    return float(value)


def check_upper_bound(actual: float, limit: float | None, label: str) -> None:
    if limit is None:
        return
    if actual > limit:
        raise SystemExit(f"{label} {actual:.1f} ms exceeds threshold {limit:.1f} ms")


def main() -> int:
    args = parse_args()
    summary_path = summary_json_path(args.summary)
    with summary_path.open("r", encoding="utf-8") as handle:
        summary = json.load(handle)

    try:
        timing = summary["events"][args.event]["timings"][args.field]
    except KeyError as error:
        raise SystemExit(
            f"Missing timing field {args.event}.{args.field} in {summary_path}"
        ) from error

    count = int(number(timing.get("count"), "count"))
    if count < args.min_count:
        raise SystemExit(
            f"{args.event}.{args.field} has {count} samples; expected at least {args.min_count}"
        )

    p50 = number(timing.get("p50"), "p50")
    p95 = number(timing.get("p95"), "p95")
    max_value = number(timing.get("max"), "max")
    check_upper_bound(p50, args.p50_max, f"{args.event}.{args.field} p50")
    check_upper_bound(p95, args.p95_max, f"{args.event}.{args.field} p95")
    check_upper_bound(max_value, args.max_value, f"{args.event}.{args.field} max")

    print(
        f"{args.event}.{args.field}: n={count} p50={p50:.1f} ms "
        f"p95={p95:.1f} ms max={max_value:.1f} ms"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
