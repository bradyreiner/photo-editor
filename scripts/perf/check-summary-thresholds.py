#!/usr/bin/env python3
"""Validate performance thresholds from a benchmark summary artifact."""

from __future__ import annotations

import argparse
import json
import math
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
    parser.add_argument("--max-count", type=int, help="Maximum number of samples allowed.")
    parser.add_argument("--p50-max", type=float, help="Maximum allowed p50 in milliseconds.")
    parser.add_argument("--p95-max", type=float, help="Maximum allowed p95 in milliseconds.")
    parser.add_argument("--max", dest="max_value", type=float, help="Maximum allowed max in milliseconds.")
    parser.add_argument(
        "--where",
        action="append",
        default=[],
        metavar="FIELD=VALUE",
        help="Filter raw event records before checking. May be repeated.",
    )
    parser.add_argument(
        "--metric-max",
        action="append",
        default=[],
        metavar="FIELD=VALUE",
        help="Require the maximum numeric value for FIELD on the event to be <= VALUE.",
    )
    parser.add_argument(
        "--metric-min",
        action="append",
        default=[],
        metavar="FIELD=VALUE",
        help="Require the minimum numeric value for FIELD on the event to be >= VALUE.",
    )
    parser.add_argument(
        "--metric-eq",
        action="append",
        default=[],
        metavar="FIELD=VALUE",
        help="Require every observed FIELD value on the event to equal VALUE.",
    )
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


def artifact_root_for_summary(summary_path: Path) -> Path:
    return summary_path.parent if summary_path.name == "summary.json" else summary_path


def jsonl_files_for_artifact(summary_path: Path) -> list[Path]:
    root = artifact_root_for_summary(summary_path)
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


def value_matches_expected(value: Any, expected: str) -> bool:
    normalized_expected = expected.lower()
    if isinstance(value, bool):
        return ("true" if value else "false") == normalized_expected
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        expected_number = coerce_number(expected)
        return expected_number is not None and float(value) == expected_number
    return str(value).lower() == normalized_expected


def record_matches_filters(record: dict[str, Any], filters: list[tuple[str, str]]) -> bool:
    for field_name, expected in filters:
        if field_name not in record or not value_matches_expected(record[field_name], expected):
            return False
    return True


def raw_event_records(
    summary_path: Path,
    event_name: str,
    filters: list[tuple[str, str]] | None = None,
) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    active_filters = filters or []
    for jsonl_path in jsonl_files_for_artifact(summary_path):
        with jsonl_path.open("r", encoding="utf-8") as handle:
            for line in handle:
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if not isinstance(record, dict):
                    continue
                event, fields = normalize_record(record)
                if event == event_name and record_matches_filters(fields, active_filters):
                    records.append(fields)
    return records


def number(value: Any, label: str) -> float:
    if not isinstance(value, (int, float)):
        raise SystemExit(f"{label} is missing or non-numeric: {value!r}")
    return float(value)


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


def parse_metric_check(value: str) -> tuple[str, str]:
    field, separator, expected = value.partition("=")
    if not separator or not field.strip() or not expected.strip():
        raise SystemExit(f"Metric check must be FIELD=VALUE, got {value!r}")
    return field.strip(), expected.strip()


def metric_values(
    summary: dict[str, Any],
    summary_path: Path,
    event_name: str,
    field_name: str,
    filters: list[tuple[str, str]],
) -> list[Any]:
    if filters:
        return [
            record[field_name]
            for record in raw_event_records(summary_path, event_name, filters)
            if field_name in record
        ]

    event = summary.get("events", {}).get(event_name, {})
    for container_name in ("numeric_fields", "timings"):
        field_summary = event.get(container_name, {}).get(field_name)
        if isinstance(field_summary, dict) and "min" in field_summary and "max" in field_summary:
            count = int(number(field_summary.get("count"), f"{field_name}.count"))
            if count <= 0:
                return []
            if count == 1:
                return [field_summary.get("max")]
            return [field_summary.get("min"), field_summary.get("max")]

    values: list[Any] = []
    for record in raw_event_records(summary_path, event_name):
        if field_name in record:
            values.append(record[field_name])
    return values


def timing_summary_for_field(
    summary: dict[str, Any],
    summary_path: Path,
    event_name: str,
    field_name: str,
    filters: list[tuple[str, str]],
) -> dict[str, Any]:
    if filters:
        numeric_values = [
            value
            for value in (
                coerce_number(record[field_name])
                for record in raw_event_records(summary_path, event_name, filters)
                if field_name in record
            )
            if value is not None
        ]
        if not numeric_values:
            raise SystemExit(
                f"Missing numeric field {event_name}.{field_name} after filters in {summary_path}"
            )
        ordered = sorted(numeric_values)
        p50_index = min(len(ordered) - 1, math.ceil(0.50 * len(ordered)) - 1)
        p95_index = min(len(ordered) - 1, math.ceil(0.95 * len(ordered)) - 1)
        return {
            "count": len(ordered),
            "p50": ordered[p50_index],
            "p95": ordered[p95_index],
            "max": ordered[-1],
        }

    event = summary.get("events", {}).get(event_name, {})
    timing = event.get("timings", {}).get(field_name)
    if isinstance(timing, dict):
        return timing
    numeric = event.get("numeric_fields", {}).get(field_name)
    if isinstance(numeric, dict):
        return numeric

    numeric_values = [
        value
        for value in (coerce_number(raw) for raw in metric_values(summary, summary_path, event_name, field_name, []))
        if value is not None
    ]
    if not numeric_values:
        raise SystemExit(f"Missing numeric field {event_name}.{field_name} in {summary_path}")
    ordered = sorted(numeric_values)
    p50_index = min(len(ordered) - 1, math.ceil(0.50 * len(ordered)) - 1)
    p95_index = min(len(ordered) - 1, math.ceil(0.95 * len(ordered)) - 1)
    return {
        "count": len(ordered),
        "p50": ordered[p50_index],
        "p95": ordered[p95_index],
        "max": ordered[-1],
    }


def check_upper_bound(actual: float, limit: float | None, label: str) -> None:
    if limit is None:
        return
    if actual > limit:
        raise SystemExit(f"{label} {actual:.1f} ms exceeds threshold {limit:.1f} ms")


def check_metric_max(values: list[Any], limit: str, label: str) -> None:
    numeric_values = [coerce_number(value) for value in values]
    if not numeric_values or any(value is None for value in numeric_values):
        raise SystemExit(f"{label} has no numeric values to compare")
    actual = max(value for value in numeric_values if value is not None)
    threshold = number(coerce_number(limit), label)
    if actual > threshold:
        raise SystemExit(f"{label} max {actual:g} exceeds threshold {threshold:g}")


def check_metric_min(values: list[Any], limit: str, label: str) -> None:
    numeric_values = [coerce_number(value) for value in values]
    if not numeric_values or any(value is None for value in numeric_values):
        raise SystemExit(f"{label} has no numeric values to compare")
    actual = min(value for value in numeric_values if value is not None)
    threshold = number(coerce_number(limit), label)
    if actual < threshold:
        raise SystemExit(f"{label} min {actual:g} is below threshold {threshold:g}")


def check_metric_eq(values: list[Any], expected: str, label: str) -> None:
    normalized_expected = expected.lower()
    for value in values:
        if isinstance(value, bool):
            actual = "true" if value else "false"
        else:
            actual = str(value)
        if actual.lower() != normalized_expected:
            raise SystemExit(f"{label} value {actual!r} does not equal {expected!r}")


def main() -> int:
    args = parse_args()
    summary_path = summary_json_path(args.summary)
    with summary_path.open("r", encoding="utf-8") as handle:
        summary = json.load(handle)

    filters = [parse_metric_check(check) for check in args.where]
    if args.max_count == 0:
        count = len(raw_event_records(summary_path, args.event, filters))
        if count > 0:
            raise SystemExit(
                f"{args.event} has {count} records; expected at most 0"
            )
        print(f"{args.event}.{args.field}: n=0")
        return 0

    timing = timing_summary_for_field(summary, summary_path, args.event, args.field, filters)

    count = int(number(timing.get("count"), "count"))
    if count < args.min_count:
        raise SystemExit(
            f"{args.event}.{args.field} has {count} samples; expected at least {args.min_count}"
        )
    if args.max_count is not None and count > args.max_count:
        raise SystemExit(
            f"{args.event}.{args.field} has {count} samples; expected at most {args.max_count}"
        )

    p50 = number(timing.get("p50"), "p50")
    p95 = number(timing.get("p95"), "p95")
    max_value = number(timing.get("max"), "max")
    check_upper_bound(p50, args.p50_max, f"{args.event}.{args.field} p50")
    check_upper_bound(p95, args.p95_max, f"{args.event}.{args.field} p95")
    check_upper_bound(max_value, args.max_value, f"{args.event}.{args.field} max")

    for check in args.metric_max:
        field_name, expected = parse_metric_check(check)
        values = metric_values(summary, summary_path, args.event, field_name, filters)
        if not values:
            raise SystemExit(f"Missing metric field {args.event}.{field_name} in {summary_path}")
        check_metric_max(values, expected, f"{args.event}.{field_name}")
    for check in args.metric_min:
        field_name, expected = parse_metric_check(check)
        values = metric_values(summary, summary_path, args.event, field_name, filters)
        if not values:
            raise SystemExit(f"Missing metric field {args.event}.{field_name} in {summary_path}")
        check_metric_min(values, expected, f"{args.event}.{field_name}")
    for check in args.metric_eq:
        field_name, expected = parse_metric_check(check)
        values = metric_values(summary, summary_path, args.event, field_name, filters)
        if not values:
            raise SystemExit(f"Missing metric field {args.event}.{field_name} in {summary_path}")
        check_metric_eq(values, expected, f"{args.event}.{field_name}")

    print(
        f"{args.event}.{args.field}: n={count} p50={p50:.1f} ms "
        f"p95={p95:.1f} ms max={max_value:.1f} ms"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
