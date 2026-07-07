#!/usr/bin/env python3
"""Run the actual QML shell workflow speed gate."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_APP = REPO_ROOT / "build/dev-macos-debug/tests/actual_app_workflow_benchmark_driver"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"


@dataclass(frozen=True)
class StageBudget:
    stage: str
    p95_max_ms: float
    max_ms: float


DEFAULT_BUDGETS = (
    StageBudget("qml_shell_load_to_ready", 1000.0, 1500.0),
    StageBudget("edit_workspace_prewarm_ready", 3000.0, 3500.0),
    StageBudget("library_route_to_visible", 150.0, 250.0),
    StageBudget("import_route_to_visible", 100.0, 200.0),
    StageBudget("open_photo_to_edit_preview_ready", 900.0, 1500.0),
    StageBudget("masks_page_to_tool_rail_ready", 100.0, 200.0),
    StageBudget("brush_page_to_hud_ready", 250.0, 400.0),
    StageBudget("brush_pointer_down_to_qml_feedback", 50.0, 80.0),
    StageBudget("brush_release_to_qml_commit_visible", 100.0, 175.0),
    StageBudget("tool_adjust_to_qml_draft_scheduled", 40.0, 80.0),
    StageBudget("carousel_route_to_visible", 350.0, 500.0),
    StageBudget("timeline_route_to_visible", 200.0, 350.0),
    StageBudget("export_route_to_visible", 100.0, 200.0),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the real QML shell open-photo/masks/brush workflow speed gate."
    )
    parser.add_argument("--app", type=Path, default=DEFAULT_APP)
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--source", type=Path, help="Optional source image for the workflow.")
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument(
        "--capture",
        action="store_true",
        help="Capture first-iteration screenshots. Disabled by default to keep timing uncontaminated.",
    )
    return parser.parse_args()


def timestamp_slug() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")


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
        if event != "benchmark.actual_app_workflow.stage_summary":
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
    capture_dir: Path,
    summaries: dict[str, dict[str, Any]],
    failures: list[str],
) -> None:
    payload = {
        "artifact_dir": str(artifact_dir),
        "capture_dir": str(capture_dir),
        "status": "passed" if not failures else "failed",
        "budgets": [budget.__dict__ for budget in DEFAULT_BUDGETS],
        "stage_summaries": summaries,
        "failures": failures,
        "frame_swaps_total": summaries.get("qml_frame_tick", {}).get("frame_swaps_total"),
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
        raise SystemExit(f"Actual app workflow benchmark driver not found: {app}")

    capture_dir = (
        args.output_root.expanduser().resolve()
        / f"actual-app-workflow-captures-{timestamp_slug()}"
        if args.capture
        else None
    )
    command = [
        sys.executable,
        "-B",
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-actual-app-workflow",
        "--app",
        str(app),
        "--output-root",
        str(args.output_root),
        "--event",
        "benchmark.actual_app_workflow.completed",
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
    if capture_dir is not None:
        command.extend(["--capture-dir", str(capture_dir)])
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
    write_report(
        artifact_dir / "actual-app-workflow-report.json",
        artifact_dir,
        capture_dir if capture_dir is not None else Path(),
        summaries,
        failures,
    )

    print(f"actual_app_workflow_artifact={artifact_dir}")
    if capture_dir is not None:
        print(f"actual_app_workflow_capture_dir={capture_dir}")
    print(f"actual_app_workflow_report={artifact_dir / 'actual-app-workflow-report.json'}")
    if failures:
        for failure in failures:
            print(f"actual_app_workflow_failure={failure}")
        return 1
    print("actual_app_workflow_gate=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
