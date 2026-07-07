#!/usr/bin/env python3
"""Run the real controller interaction workflow speed gate."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_APP = REPO_ROOT / "build/dev-macos-debug/tests/interaction_workflow_benchmark_driver"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"


@dataclass(frozen=True)
class StageBudget:
    stage: str
    p95_max_ms: float
    max_ms: float


DEFAULT_BUDGETS = (
    StageBudget("open_photo_to_first_preview", 1200.0, 1800.0),
    StageBudget("brush_page_to_controls_ready", 150.0, 250.0),
    StageBudget("brush_pointer_down_to_live_feedback", 33.0, 50.0),
    StageBudget("brush_release_to_committed_mask", 500.0, 800.0),
    StageBudget("tool_adjust_to_draft_scheduled", 75.0, 125.0),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the open-photo/brush/tool interaction workflow speed gate."
    )
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--source", type=Path, help="Optional source image for the workflow.")
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=60)
    return parser.parse_args()


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return payload if isinstance(payload, dict) else None


def parse_artifact_dir(output: str) -> Path | None:
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key == "artifact_dir" and value.strip():
            return Path(value.strip()).expanduser().resolve()
    return None


def iter_jsonl_records(path: Path) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    if not path.exists():
        return records
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(record, dict):
                records.append(record)
    return records


def record_event_and_fields(record: dict[str, Any]) -> tuple[str | None, dict[str, Any]]:
    raw_fields = record.get("fields")
    fields = raw_fields if isinstance(raw_fields, dict) else {}
    event = record.get("event")
    if not isinstance(event, str) or not event:
        event = fields.get("event")
    return (event if isinstance(event, str) and event else None, fields)


def collect_stage_summaries(desktop_log: Path) -> dict[str, dict[str, Any]]:
    summaries: dict[str, dict[str, Any]] = {}
    for record in iter_jsonl_records(desktop_log):
        event, fields = record_event_and_fields(record)
        if event != "benchmark.interaction_workflow.stage_summary":
            continue
        stage = fields.get("stage")
        if isinstance(stage, str) and stage:
            summaries[stage] = dict(fields)
    return summaries


def check_budgets(summaries: dict[str, dict[str, Any]]) -> list[str]:
    failures: list[str] = []
    for budget in DEFAULT_BUDGETS:
        fields = summaries.get(budget.stage)
        if fields is None:
            failures.append(f"{budget.stage}: missing stage summary")
            continue
        p95 = fields.get("p95_ms")
        max_value = fields.get("max_ms")
        if not isinstance(p95, (int, float)):
            failures.append(f"{budget.stage}: missing p95_ms")
        elif float(p95) > budget.p95_max_ms:
            failures.append(
                f"{budget.stage}: p95 {float(p95):.1f} ms exceeds {budget.p95_max_ms:.1f} ms"
            )
        if not isinstance(max_value, (int, float)):
            failures.append(f"{budget.stage}: missing max_ms")
        elif float(max_value) > budget.max_ms:
            failures.append(
                f"{budget.stage}: max {float(max_value):.1f} ms exceeds {budget.max_ms:.1f} ms"
            )
    return failures


def write_report(
    path: Path,
    artifact_dir: Path,
    summaries: dict[str, dict[str, Any]],
    failures: list[str],
) -> None:
    payload = {
        "artifact_dir": str(artifact_dir),
        "status": "passed" if not failures else "failed",
        "budgets": [budget.__dict__ for budget in DEFAULT_BUDGETS],
        "stage_summaries": summaries,
        "failures": failures,
    }
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    if args.iterations <= 0:
        raise SystemExit("--iterations must be positive")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")
    app = args.app.expanduser().resolve()
    if not app.exists():
        raise SystemExit(f"Interaction workflow benchmark driver not found: {app}")

    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-interaction-workflow",
        "--app",
        str(app),
        "--output-root",
        str(args.output_root),
        "--event",
        "benchmark.interaction_workflow.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--iterations",
        str(args.iterations),
        "--timeout-ms",
        str(max(args.timeout * 1000, 15000)),
    ]
    if args.source is not None:
        command.extend(["--source", str(args.source.expanduser().resolve())])

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
    if result.returncode != 0:
        return result.returncode

    artifact_dir = parse_artifact_dir(result.stdout)
    if artifact_dir is None:
        print("Unable to find artifact_dir in benchmark output.", file=sys.stderr)
        return 1
    manifest = load_json(artifact_dir / "manifest.json")
    if manifest is None:
        print(f"Unable to read benchmark manifest: {artifact_dir / 'manifest.json'}", file=sys.stderr)
        return 1
    desktop_log_value = manifest.get("desktop_log")
    if not isinstance(desktop_log_value, str) or not desktop_log_value:
        print("Benchmark manifest did not include desktop_log.", file=sys.stderr)
        return 1
    summaries = collect_stage_summaries(Path(desktop_log_value))
    failures = check_budgets(summaries)
    write_report(artifact_dir / "interaction-workflow-report.json", artifact_dir, summaries, failures)

    print(f"interaction_workflow_artifact={artifact_dir}")
    print(f"interaction_workflow_report={artifact_dir / 'interaction-workflow-report.json'}")
    if failures:
        for failure in failures:
            print(f"interaction_workflow_failure={failure}")
        return 1
    print("interaction_workflow_gate=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
