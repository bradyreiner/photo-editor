#!/usr/bin/env python3
"""Summarize photo-editor observability JSONL runs.

Current logs write the event name at the record root with details under
`fields`. Older desktop logs kept the event in `fields.event`; this script
accepts either shape, groups timing fields by event, and prints p50/p95 latency
summaries suitable for before/after performance notes.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


DEFAULT_EVENTS = {
    "desktop.startup.ready",
    "preview.render.completed",
    "preview.histogram.completed",
    "thumbnail.completed",
    "benchmark.thumbnail_corpus.completed",
    "benchmark.color_adjustment_pass.completed",
    "benchmark.color_adjustment_suite.completed",
    "benchmark.preview_asset.completed",
    "benchmark.preview_corpus.completed",
    "job.completed",
    "job.failed",
}

PRIMARY_TIMING_FIELDS = (
    "elapsed_ms",
    "uptime_ms",
    "worker_elapsed_ms",
    "queue_wait_ms",
    "decode_ms",
    "edit_ms",
    "encode_ms",
)

CATEGORY_FIELDS = (
    "workflow",
    "backend",
    "decode_backend",
    "fallback_reason",
    "cancellation_reason",
    "reason",
    "render_reason",
    "interaction_mode",
    "queue_priority",
    "pass",
    "reason",
    "job_kind",
    "media_type",
    "draft_interactive",
    "full_resolution",
    "cache_hit",
    "decoded_cache_hit",
    "decoded_cache_lookup",
    "linear_cache_hit",
    "interactive",
    "thread",
)


@dataclass
class EventStats:
    count: int = 0
    severities: Counter[str] = field(default_factory=Counter)
    timing_values: dict[str, list[float]] = field(default_factory=lambda: defaultdict(list))
    categories: dict[str, Counter[str]] = field(default_factory=lambda: defaultdict(Counter))
    files: Counter[str] = field(default_factory=Counter)


@dataclass
class ParseStats:
    files: int = 0
    lines: int = 0
    records: int = 0
    skipped: int = 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize structured photo-editor JSONL performance logs."
    )
    parser.add_argument(
        "inputs",
        nargs="+",
        help="Run directories, logs/runs roots, or .jsonl files to summarize.",
    )
    parser.add_argument(
        "--event",
        action="append",
        dest="events",
        help="Event name to include. May be repeated. Defaults to key performance events.",
    )
    parser.add_argument(
        "--all-events",
        action="store_true",
        help="Include every event that has a timing field.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON instead of tables.",
    )
    parser.add_argument(
        "--compare",
        action="append",
        dest="compare_inputs",
        help="Baseline run directory or .jsonl file to compare against. May be repeated.",
    )
    return parser.parse_args()


def jsonl_files_for_input(path: Path) -> list[Path]:
    if path.is_file():
        return [path] if path.suffix == ".jsonl" else []
    if not path.is_dir():
        return []

    direct_jsonl = sorted(child for child in path.iterdir() if child.suffix == ".jsonl")
    if direct_jsonl:
        return direct_jsonl

    runs_root = path / "runs"
    if runs_root.is_dir():
        return sorted(runs_root.glob("*/*.jsonl"))

    return sorted(path.glob("*/*.jsonl"))


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


def percentile(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[int(position)]
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def normalize_record(record: dict[str, Any]) -> tuple[str | None, dict[str, Any]]:
    raw_fields = record.get("fields")
    fields = raw_fields if isinstance(raw_fields, dict) else {}

    event = record.get("event")
    if not isinstance(event, str) or not event:
        event = fields.get("event")
    if not isinstance(event, str) or not event:
        return None, fields

    merged = dict(fields)
    for key in ("severity", "category", "component", "timestamp", "pid", "thread_id"):
        if key in record and key not in merged:
            merged[key] = record[key]
    if "thread" not in merged and "thread_id" in merged:
        merged["thread"] = merged["thread_id"]
    if "reason" not in merged and "render_reason" in merged:
        merged["reason"] = merged["render_reason"]
    if "interactive" not in merged and isinstance(merged.get("full_resolution"), bool):
        merged["interactive"] = not merged["full_resolution"]
    if "interactive" not in merged and "draft_interactive" in merged:
        merged["interactive"] = merged["draft_interactive"]
    if "cache_hit" not in merged:
        for alias in ("decoded_cache_hit", "linear_cache_hit", "decoded_cache_lookup"):
            if alias in merged:
                merged["cache_hit"] = merged[alias]
                break
    return event, merged


def has_timing(fields: dict[str, Any]) -> bool:
    return any(coerce_number(value) is not None for key, value in fields.items() if key.endswith("_ms"))


def add_record(
    summaries: dict[str, EventStats],
    event: str,
    fields: dict[str, Any],
    source: Path,
) -> None:
    stats = summaries[event]
    stats.count += 1
    severity = str(fields.get("severity", "info"))
    stats.severities[severity] += 1
    stats.files[source.name] += 1

    for key, value in fields.items():
        if key in PRIMARY_TIMING_FIELDS or key.endswith("_ms"):
            number = coerce_number(value)
            if number is not None:
                stats.timing_values[key].append(number)
        if key in CATEGORY_FIELDS and value is not None:
            stats.categories[key][str(value)] += 1


def summarize_file(
    path: Path,
    summaries: dict[str, EventStats],
    parse_stats: ParseStats,
    selected_events: set[str],
    all_events: bool,
) -> None:
    parse_stats.files += 1
    try:
        handle = path.open("r", encoding="utf-8")
    except OSError:
        parse_stats.skipped += 1
        return

    with handle:
        for line in handle:
            parse_stats.lines += 1
            stripped = line.strip()
            if not stripped:
                continue
            try:
                record = json.loads(stripped)
            except json.JSONDecodeError:
                parse_stats.skipped += 1
                continue
            if not isinstance(record, dict):
                parse_stats.skipped += 1
                continue

            event, fields = normalize_record(record)
            if event is None:
                parse_stats.skipped += 1
                continue
            if not all_events and event not in selected_events:
                continue
            if all_events and not has_timing(fields):
                continue

            parse_stats.records += 1
            add_record(summaries, event, fields, path)


def timing_summary(values: list[float]) -> dict[str, float | int | None]:
    return {
        "count": len(values),
        "min": min(values) if values else None,
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "max": max(values) if values else None,
        "avg": (sum(values) / len(values)) if values else None,
    }


def build_output(summaries: dict[str, EventStats], parse_stats: ParseStats) -> dict[str, Any]:
    events: dict[str, Any] = {}
    for event in sorted(summaries):
        stats = summaries[event]
        events[event] = {
            "count": stats.count,
            "severities": dict(stats.severities),
            "timings": {
                key: timing_summary(values)
                for key, values in sorted(stats.timing_values.items())
            },
            "categories": {
                key: dict(counter.most_common())
                for key, counter in sorted(stats.categories.items())
            },
            "files": dict(stats.files.most_common()),
        }
    return {
        "parse": {
            "files": parse_stats.files,
            "lines": parse_stats.lines,
            "records": parse_stats.records,
            "skipped": parse_stats.skipped,
        },
        "events": events,
    }


def collect_files(inputs: list[str]) -> list[Path]:
    files: list[Path] = []
    for input_path in inputs:
        files.extend(jsonl_files_for_input(Path(input_path).expanduser()))
    return sorted(set(files))


def summarize_inputs(
    files: list[Path],
    selected_events: set[str],
    all_events: bool,
) -> dict[str, Any]:
    summaries: dict[str, EventStats] = defaultdict(EventStats)
    parse_stats = ParseStats()
    for file_path in files:
        summarize_file(file_path, summaries, parse_stats, selected_events, all_events)
    return build_output(summaries, parse_stats)


def add_comparison(output: dict[str, Any], baseline: dict[str, Any]) -> None:
    comparisons: dict[str, Any] = {}
    for event, stats in output["events"].items():
        baseline_stats = baseline["events"].get(event)
        if baseline_stats is None:
            continue
        event_comparison: dict[str, Any] = {}
        for field_name, summary in stats["timings"].items():
            baseline_summary = baseline_stats["timings"].get(field_name)
            if baseline_summary is None:
                continue
            current_p50 = summary.get("p50")
            current_p95 = summary.get("p95")
            baseline_p50 = baseline_summary.get("p50")
            baseline_p95 = baseline_summary.get("p95")
            event_comparison[field_name] = {
                "baseline_count": baseline_summary.get("count", 0),
                "current_count": summary.get("count", 0),
                "p50_delta_ms": None
                if current_p50 is None or baseline_p50 is None
                else current_p50 - baseline_p50,
                "p95_delta_ms": None
                if current_p95 is None or baseline_p95 is None
                else current_p95 - baseline_p95,
                "p50_delta_pct": percent_delta(current_p50, baseline_p50),
                "p95_delta_pct": percent_delta(current_p95, baseline_p95),
            }
        if event_comparison:
            comparisons[event] = event_comparison
    output["comparison"] = {
        "baseline_parse": baseline["parse"],
        "timings": comparisons,
    }


def percent_delta(current: Any, baseline: Any) -> float | None:
    if current is None or baseline in (None, 0):
        return None
    return ((current - baseline) / baseline) * 100.0


def format_ms(value: float | int | None) -> str:
    if value is None:
        return "-"
    return f"{float(value):.1f}"


def print_table(output: dict[str, Any]) -> None:
    parse = output["parse"]
    print(
        "Parsed "
        f"{parse['records']} matching records from {parse['files']} files "
        f"({parse['lines']} lines, {parse['skipped']} skipped)."
    )
    print()

    rows: list[tuple[str, str, int, str, str, str, str]] = []
    for event, stats in output["events"].items():
        timings = stats["timings"]
        primary_field = "elapsed_ms" if "elapsed_ms" in timings else next(iter(timings), "")
        if not primary_field:
            rows.append((event, "-", stats["count"], "-", "-", "-", "-"))
            continue
        summary = timings[primary_field]
        rows.append(
            (
                event,
                primary_field,
                summary["count"],
                format_ms(summary["p50"]),
                format_ms(summary["p95"]),
                format_ms(summary["max"]),
                format_ms(summary["avg"]),
            )
        )

    print("Event summary")
    print("event | field | n | p50 ms | p95 ms | max ms | avg ms")
    print("--- | --- | ---: | ---: | ---: | ---: | ---:")
    for row in rows:
        print(f"{row[0]} | {row[1]} | {row[2]} | {row[3]} | {row[4]} | {row[5]} | {row[6]}")

    detail_rows: list[tuple[str, str, int, str, str, str]] = []
    for event, stats in output["events"].items():
        for field_name, summary in stats["timings"].items():
            detail_rows.append(
                (
                    event,
                    field_name,
                    summary["count"],
                    format_ms(summary["p50"]),
                    format_ms(summary["p95"]),
                    format_ms(summary["max"]),
                )
            )

    if detail_rows:
        print()
        print("Timing fields")
        print("event | field | n | p50 ms | p95 ms | max ms")
        print("--- | --- | ---: | ---: | ---: | ---:")
        for event, field_name, count, p50, p95, max_value in detail_rows:
            print(f"{event} | {field_name} | {count} | {p50} | {p95} | {max_value}")

    category_lines: list[str] = []
    for event, stats in output["events"].items():
        for field_name, counter in stats["categories"].items():
            values = ", ".join(f"{key}={value}" for key, value in list(counter.items())[:5])
            if values:
                category_lines.append(f"{event} {field_name}: {values}")
    if category_lines:
        print()
        print("Categorical fields")
        for line in category_lines:
            print(f"- {line}")

    comparison = output.get("comparison")
    if comparison:
        comparison_rows: list[tuple[str, str, str, str, str, str]] = []
        for event, fields in comparison["timings"].items():
            for field_name, summary in fields.items():
                comparison_rows.append(
                    (
                        event,
                        field_name,
                        format_ms(summary["p50_delta_ms"]),
                        format_percent(summary["p50_delta_pct"]),
                        format_ms(summary["p95_delta_ms"]),
                        format_percent(summary["p95_delta_pct"]),
                    )
                )
        if comparison_rows:
            print()
            print("Comparison vs baseline")
            print("event | field | p50 delta ms | p50 delta % | p95 delta ms | p95 delta %")
            print("--- | --- | ---: | ---: | ---: | ---:")
            for row in comparison_rows:
                print(f"{row[0]} | {row[1]} | {row[2]} | {row[3]} | {row[4]} | {row[5]}")


def format_percent(value: float | None) -> str:
    if value is None:
        return "-"
    return f"{value:+.1f}%"


def main() -> int:
    args = parse_args()
    selected_events = set(args.events) if args.events else set(DEFAULT_EVENTS)

    files = collect_files(args.inputs)
    if not files:
        print("No JSONL files found for the provided inputs.", file=sys.stderr)
        return 2

    output = summarize_inputs(files, selected_events, args.all_events)

    if args.compare_inputs:
        baseline_files = collect_files(args.compare_inputs)
        if not baseline_files:
            print("No JSONL files found for --compare inputs.", file=sys.stderr)
            return 2
        baseline_output = summarize_inputs(baseline_files, selected_events, args.all_events)
        add_comparison(output, baseline_output)

    if args.json:
        json.dump(output, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
    else:
        print_table(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
