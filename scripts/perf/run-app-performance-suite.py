#!/usr/bin/env python3
"""Run app-wide performance and operability suites.

This wrapper composes the existing benchmark/proof tools, keeps their artifacts intact, and writes
one app-level manifest/report that makes performance and operability regressions easier to triage.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import uuid
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"
DEFAULT_PREVIEW_CORPUS = REPO_ROOT / "perf-corpora/preview-synthetic-v1/manifest.json"
DEFAULT_PREVIEW_APP = REPO_ROOT / "build/dev-macos-debug/tests/preview_benchmark_driver"
DEFAULT_THUMBNAIL_APP = REPO_ROOT / "build/dev-macos-debug/tests/thumbnail_benchmark_driver"
DEFAULT_COLOR_APP = REPO_ROOT / "build/dev-macos-debug/tests/color_adjustment_benchmark_driver"
DEFAULT_INTERACTION_APP = REPO_ROOT / "build/dev-macos-debug/tests/interaction_workflow_benchmark_driver"
DEFAULT_ACTUAL_APP_WORKFLOW_APP = (
    REPO_ROOT / "build/dev-macos-debug/tests/actual_app_workflow_benchmark_driver"
)
DEFAULT_DESKTOP_APP = (
    REPO_ROOT / "build/dev-macos-debug/apps/desktop/PhotoEditor.app/Contents/MacOS/PhotoEditor"
)


CommandBuilder = Callable[[argparse.Namespace, Path], list[str]]


@dataclass(frozen=True)
class SuiteDefinition:
    name: str
    description: str
    builder: CommandBuilder
    required_for_release: bool = False
    requires_corpus: bool = False


@dataclass
class SuiteResult:
    name: str
    description: str
    command: list[str]
    returncode: int | None
    log_file: str | None
    artifact_dirs: list[str]
    status: str
    started_at: str | None = None
    ended_at: str | None = None


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run an app-wide PhotoEditor performance and operability suite."
    )
    parser.add_argument(
        "--profile",
        choices=sorted(PROFILES),
        default="smoke",
        help="Curated suite profile to run.",
    )
    parser.add_argument(
        "--suite",
        action="append",
        choices=sorted(SUITE_DEFINITIONS),
        help="Run one or more explicit suites instead of the selected profile.",
    )
    parser.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    parser.add_argument("--corpus", type=Path, default=DEFAULT_PREVIEW_CORPUS)
    parser.add_argument("--desktop-app", type=Path, default=DEFAULT_DESKTOP_APP)
    parser.add_argument("--preview-app", type=Path, default=DEFAULT_PREVIEW_APP)
    parser.add_argument("--thumbnail-app", type=Path, default=DEFAULT_THUMBNAIL_APP)
    parser.add_argument("--color-app", type=Path, default=DEFAULT_COLOR_APP)
    parser.add_argument("--interaction-app", type=Path, default=DEFAULT_INTERACTION_APP)
    parser.add_argument("--actual-app-workflow-app", type=Path, default=DEFAULT_ACTUAL_APP_WORKFLOW_APP)
    parser.add_argument("--iterations", type=int, default=10)
    parser.add_argument("--timeout", type=int, default=120)
    parser.add_argument(
        "--baseline",
        type=Path,
        help="Optional prior app-suite artifact to compare at the report level.",
    )
    parser.add_argument("--list-suites", action="store_true", help="List suites and exit.")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Write manifest/report without executing commands.",
    )
    parser.add_argument(
        "--keep-going",
        action="store_true",
        help="Continue running later suites after a suite fails.",
    )
    return parser.parse_args()


def python_command(script: str, *args: str) -> list[str]:
    return [sys.executable, "-B", script, *args]


def build_startup_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-startup",
        "--app",
        str(args.desktop_app),
        "--output-root",
        str(suite_output_root),
        "--event",
        "desktop.startup.ready",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--allow-gui-unavailable",
    )


def build_preview_raster_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-preview-raster",
        "--app",
        str(args.preview_app),
        "--output-root",
        str(suite_output_root),
        "--corpus",
        str(args.corpus),
        "--event",
        "benchmark.preview_corpus.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--corpus",
        str(args.corpus),
        "--limit",
        "2",
        "--timeout-ms",
        str(max(args.timeout * 1000, 15000)),
    )


def build_thumbnail_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-benchmark.py",
        "--scenario",
        "perf-thumbnail-cold-warm",
        "--app",
        str(args.thumbnail_app),
        "--output-root",
        str(suite_output_root),
        "--corpus",
        str(args.corpus),
        "--event",
        "benchmark.thumbnail_corpus.completed",
        "--timeout",
        str(args.timeout),
        "--settle-seconds",
        "0",
        "--",
        "--corpus",
        str(args.corpus),
        "--limit",
        "8",
        "--window-size",
        "6",
        "--timeout-ms",
        str(max(args.timeout * 1000, 15000)),
    )


def build_preview_pipeline_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-preview-pipeline-gate.py",
        "--app",
        str(args.preview_app),
        "--corpus",
        str(args.corpus),
        "--output-root",
        str(suite_output_root),
        "--timeout",
        str(args.timeout),
    )


def build_color_adjustment_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-color-adjustment-gate.py",
        "--app",
        str(args.color_app),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
    )


def build_interaction_workflow_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-interaction-workflow-gate.py",
        "--app",
        str(args.interaction_app),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
    )


def build_actual_app_workflow_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-actual-app-workflow-gate.py",
        "--app",
        str(args.actual_app_workflow_app),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
    )


def build_preview_frontier_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-preview-frontier-gate.py",
        "--app",
        str(args.preview_app),
        "--corpus",
        str(args.corpus),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
    )


def build_mask_performance_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-mask-performance-proof.py",
        "--output-root",
        str(suite_output_root),
    )


def build_metal_proof_command(args: argparse.Namespace, suite_output_root: Path) -> list[str]:
    return python_command(
        "scripts/perf/run-metal-proof-gate.py",
        "--color-app",
        str(args.color_app),
        "--output-root",
        str(suite_output_root),
        "--iterations",
        str(args.iterations),
        "--timeout",
        str(args.timeout),
        "--allow-unavailable",
    )


SUITE_DEFINITIONS: dict[str, SuiteDefinition] = {
    "startup": SuiteDefinition(
        "startup",
        "Desktop launch to usable app readiness.",
        build_startup_command,
    ),
    "preview-raster": SuiteDefinition(
        "preview-raster",
        "Corpus-backed raster preview latency and render-stage telemetry.",
        build_preview_raster_command,
        requires_corpus=True,
    ),
    "thumbnail-cold-warm": SuiteDefinition(
        "thumbnail-cold-warm",
        "Cold and warm visible-window thumbnail readiness.",
        build_thumbnail_command,
        requires_corpus=True,
    ),
    "preview-pipeline": SuiteDefinition(
        "preview-pipeline",
        "Accepted preview pipeline regression gates.",
        build_preview_pipeline_command,
        requires_corpus=True,
    ),
    "color-adjustment": SuiteDefinition(
        "color-adjustment",
        "CPU/GPU color-adjustment stage performance gates.",
        build_color_adjustment_command,
    ),
    "interaction-workflow": SuiteDefinition(
        "interaction-workflow",
        "Open photo, brush page, brush stroke, and tool apply interaction speed gate.",
        build_interaction_workflow_command,
    ),
    "actual-app-workflow": SuiteDefinition(
        "actual-app-workflow",
        "Actual QML shell open-photo, import, masks, brush, and tool readiness speed gate.",
        build_actual_app_workflow_command,
    ),
    "preview-frontier": SuiteDefinition(
        "preview-frontier",
        "Brush tile-cache and max-preview frontier gates.",
        build_preview_frontier_command,
        required_for_release=True,
        requires_corpus=True,
    ),
    "mask-performance": SuiteDefinition(
        "mask-performance",
        "Focused mask interaction performance proof.",
        build_mask_performance_command,
        required_for_release=True,
    ),
    "metal-proof": SuiteDefinition(
        "metal-proof",
        "Hardware-dependent Metal parity and GPU-work proof.",
        build_metal_proof_command,
        required_for_release=True,
    ),
}


PROFILES: dict[str, tuple[str, ...]] = {
    "smoke": ("startup", "preview-raster", "thumbnail-cold-warm"),
    "local-dev": (
        "startup",
        "preview-raster",
        "thumbnail-cold-warm",
        "preview-pipeline",
        "color-adjustment",
        "interaction-workflow",
        "actual-app-workflow",
    ),
    "release-proof": (
        "startup",
        "preview-raster",
        "thumbnail-cold-warm",
        "preview-pipeline",
        "color-adjustment",
        "interaction-workflow",
        "actual-app-workflow",
        "preview-frontier",
        "mask-performance",
        "metal-proof",
    ),
}


def timestamp_slug() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")


def iso_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def suite_names_for_args(args: argparse.Namespace) -> list[str]:
    names = args.suite if args.suite else list(PROFILES[args.profile])
    seen: set[str] = set()
    ordered: list[str] = []
    for name in names:
        if name not in seen:
            seen.add(name)
            ordered.append(name)
    return ordered


def print_suite_listing() -> None:
    for profile, suites in PROFILES.items():
        print(f"{profile}: {', '.join(suites)}")
    print()
    for suite in SUITE_DEFINITIONS.values():
        release = " release-required" if suite.required_for_release else ""
        print(f"{suite.name}:{release} {suite.description}")


def selected_suites_need_corpus(suite_names: list[str]) -> bool:
    return any(SUITE_DEFINITIONS[name].requires_corpus for name in suite_names)


def ensure_synthetic_corpus(corpus: Path) -> None:
    resolved = corpus.expanduser().resolve()
    if resolved.is_file():
        return
    if resolved.name != "manifest.json":
        raise SystemExit(f"Corpus manifest does not exist: {resolved}")
    output_dir = resolved.parent
    command = python_command(
        "scripts/perf/create-preview-corpus.py",
        "--output",
        str(output_dir),
        "--count",
        "8",
        "--force",
    )
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
        raise SystemExit(result.returncode)
    if not resolved.is_file():
        raise SystemExit(f"Corpus generation did not create {resolved}")


def parse_artifact_dirs(output: str) -> list[str]:
    artifacts: list[str] = []
    for line in output.splitlines():
        key, separator, value = line.partition("=")
        if separator and key.endswith("artifact") and value.strip():
            artifacts.append(str(Path(value.strip()).expanduser().resolve()))
        if separator and key == "artifact_dir" and value.strip():
            artifacts.append(str(Path(value.strip()).expanduser().resolve()))
    return sorted(set(artifacts))


def status_for_output(returncode: int, output: str) -> str:
    if returncode != 0:
        return "failed"
    if "benchmark_status=skipped" in output or "metal_proof_gate=skipped" in output:
        return "skipped"
    return "passed"


def run_command(command: list[str], log_file: Path) -> tuple[int, str]:
    with log_file.open("w", encoding="utf-8") as handle:
        handle.write("+ " + " ".join(command) + "\n")
        handle.flush()
        process = subprocess.run(
            command,
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        if process.stdout:
            handle.write(process.stdout)
    return process.returncode, process.stdout or ""


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return data if isinstance(data, dict) else None


def summary_for_artifact(artifact: str) -> dict[str, Any] | None:
    summary_path = Path(artifact) / "summary.json"
    return load_json(summary_path)


def manifest_for_artifact(artifact: str) -> dict[str, Any] | None:
    manifest_path = Path(artifact) / "manifest.json"
    return load_json(manifest_path)


def event_timing_rows(summary: dict[str, Any] | None) -> list[dict[str, Any]]:
    if not summary:
        return []
    rows: list[dict[str, Any]] = []
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return rows
    for event_name, event_summary in sorted(events.items()):
        if not isinstance(event_summary, dict):
            continue
        timings = event_summary.get("timings", {})
        if not isinstance(timings, dict):
            continue
        primary = "elapsed_ms" if "elapsed_ms" in timings else next(iter(timings), None)
        if not primary:
            continue
        values = timings.get(primary, {})
        if not isinstance(values, dict):
            continue
        rows.append(
            {
                "event": event_name,
                "field": primary,
                "count": values.get("count"),
                "p50": values.get("p50"),
                "p95": values.get("p95"),
                "max": values.get("max"),
            }
        )
    return rows


def warning_error_counts(summary: dict[str, Any] | None) -> dict[str, int]:
    counts = {"warning": 0, "error": 0, "fatal": 0}
    if not summary:
        return counts
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return counts
    for event_summary in events.values():
        if not isinstance(event_summary, dict):
            continue
        severities = event_summary.get("severities", {})
        if not isinstance(severities, dict):
            continue
        for severity in counts:
            value = severities.get(severity)
            if isinstance(value, int):
                counts[severity] += value
    return counts


def attention_event_count(summary: dict[str, Any] | None) -> int:
    if not summary:
        return 0
    events = summary.get("events", {})
    if not isinstance(events, dict):
        return 0
    count = 0
    for event_name in events:
        if any(token in event_name for token in ("failed", "failure", "cancelled", "canceled")):
            event_summary = events[event_name]
            if isinstance(event_summary, dict) and isinstance(event_summary.get("count"), int):
                count += event_summary["count"]
    return count


def build_report_payload(results: list[SuiteResult], baseline: dict[str, Any] | None) -> dict[str, Any]:
    suites: list[dict[str, Any]] = []
    for result in results:
        artifacts: list[dict[str, Any]] = []
        for artifact in result.artifact_dirs:
            summary = summary_for_artifact(artifact)
            manifest = manifest_for_artifact(artifact)
            artifacts.append(
                {
                    "path": artifact,
                    "manifest": manifest,
                    "warning_error_counts": warning_error_counts(summary),
                    "attention_event_count": attention_event_count(summary),
                    "timings": event_timing_rows(summary),
                }
            )
        suites.append(
            {
                "name": result.name,
                "description": result.description,
                "status": result.status,
                "returncode": result.returncode,
                "log_file": result.log_file,
                "artifact_dirs": result.artifact_dirs,
                "artifacts": artifacts,
            }
        )
    payload: dict[str, Any] = {"suites": suites}
    if baseline is not None:
        payload["baseline"] = {
            "profile": baseline.get("profile"),
            "suite_count": len(baseline.get("suites", [])) if isinstance(baseline.get("suites"), list) else 0,
        }
    return payload


def fmt_ms(value: Any) -> str:
    return "-" if value is None else f"{float(value):.1f}"


def write_markdown_report(path: Path, manifest: dict[str, Any], report: dict[str, Any]) -> None:
    lines = [
        "# App Performance Operability Report",
        "",
        f"- profile: `{manifest['profile']}`",
        f"- status: `{manifest['status']}`",
        f"- started: `{manifest['started_at']}`",
        f"- ended: `{manifest['ended_at']}`",
        f"- artifact: `{manifest['artifact_dir']}`",
        "",
        "## Suites",
        "",
        "| suite | status | artifacts | warnings | errors | attention events |",
        "| --- | --- | ---: | ---: | ---: | ---: |",
    ]
    for suite in report["suites"]:
        warnings = 0
        errors = 0
        attention = 0
        for artifact in suite["artifacts"]:
            counts = artifact["warning_error_counts"]
            warnings += counts["warning"]
            errors += counts["error"] + counts["fatal"]
            attention += artifact["attention_event_count"]
        lines.append(
            f"| {suite['name']} | {suite['status']} | {len(suite['artifact_dirs'])} | "
            f"{warnings} | {errors} | {attention} |"
        )

    lines.extend(["", "## Timings", ""])
    for suite in report["suites"]:
        lines.extend([f"### {suite['name']}", ""])
        if not suite["artifacts"]:
            lines.extend(["No benchmark artifacts were reported.", ""])
            continue
        lines.extend(["| artifact | event | field | n | p50 ms | p95 ms | max ms |", "| --- | --- | --- | ---: | ---: | ---: | ---: |"])
        for artifact in suite["artifacts"]:
            artifact_name = Path(artifact["path"]).name
            for row in artifact["timings"][:12]:
                lines.append(
                    f"| {artifact_name} | {row['event']} | {row['field']} | {row['count']} | "
                    f"{fmt_ms(row['p50'])} | {fmt_ms(row['p95'])} | {fmt_ms(row['max'])} |"
                )
        lines.append("")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def load_baseline(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    candidate = path.expanduser().resolve()
    if candidate.is_dir():
        candidate = candidate / "manifest.json"
    return load_json(candidate)


def main() -> int:
    args = parse_args()
    if args.list_suites:
        print_suite_listing()
        return 0
    if args.iterations <= 0:
        raise SystemExit("--iterations must be positive")
    if args.timeout <= 0:
        raise SystemExit("--timeout must be positive")

    selected_suites = suite_names_for_args(args)
    if selected_suites_need_corpus(selected_suites) and not args.dry_run:
        ensure_synthetic_corpus(args.corpus)
    session_id = uuid.uuid4().hex[:8]
    artifact_dir = (
        args.output_root.expanduser().resolve()
        / f"app-suite-{timestamp_slug()}-{args.profile}-{session_id}"
    )
    suite_output_root = artifact_dir / "benchmarks"
    log_dir = artifact_dir / "logs"
    artifact_dir.mkdir(parents=True, exist_ok=False)
    suite_output_root.mkdir(parents=True, exist_ok=True)
    log_dir.mkdir(parents=True, exist_ok=True)

    results: list[SuiteResult] = []
    status = "passed"
    started_at = iso_now()
    for suite_name in selected_suites:
        suite = SUITE_DEFINITIONS[suite_name]
        command = suite.builder(args, suite_output_root)
        log_file = log_dir / f"{suite.name}.log"
        result = SuiteResult(
            name=suite.name,
            description=suite.description,
            command=command,
            returncode=None,
            log_file=str(log_file),
            artifact_dirs=[],
            status="dry-run" if args.dry_run else "pending",
            started_at=iso_now(),
        )
        if args.dry_run:
            log_file.write_text("+ " + " ".join(command) + "\n", encoding="utf-8")
            result.ended_at = iso_now()
            results.append(result)
            continue

        returncode, output = run_command(command, log_file)
        result.returncode = returncode
        result.artifact_dirs = parse_artifact_dirs(output)
        result.status = status_for_output(returncode, output)
        result.ended_at = iso_now()
        results.append(result)
        if returncode != 0:
            status = "failed"
            if not args.keep_going:
                break

    ended_at = iso_now()
    if args.dry_run:
        status = "dry-run"
    elif any(result.status == "failed" for result in results):
        status = "failed"

    manifest = {
        "profile": args.profile,
        "selected_suites": selected_suites,
        "status": status,
        "started_at": started_at,
        "ended_at": ended_at,
        "artifact_dir": str(artifact_dir),
        "suite_output_root": str(suite_output_root),
        "baseline": str(args.baseline.expanduser().resolve()) if args.baseline else None,
        "corpus": str(args.corpus.expanduser().resolve()),
        "dry_run": args.dry_run,
        "suites": [result.__dict__ for result in results],
    }
    baseline = load_baseline(args.baseline)
    report = build_report_payload(results, baseline)
    manifest["report_json"] = str(artifact_dir / "operability-report.json")
    manifest["report_markdown"] = str(artifact_dir / "operability-report.md")

    write_json(artifact_dir / "manifest.json", manifest)
    write_json(artifact_dir / "operability-report.json", report)
    write_markdown_report(artifact_dir / "operability-report.md", manifest, report)

    print(f"artifact_dir={artifact_dir}")
    print(f"report={artifact_dir / 'operability-report.md'}")
    print(f"status={status}")
    return 0 if status in {"passed", "dry-run"} else 1


if __name__ == "__main__":
    raise SystemExit(main())
