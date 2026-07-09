#!/usr/bin/env python3
"""Run a repeatable PhotoEditor performance scenario and archive its logs."""

from __future__ import annotations

import argparse
import json
import os
import signal
import subprocess
import sys
import time
import uuid
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_APP = REPO_ROOT / "build/dev-macos-debug/apps/desktop/PhotoEditor.app/Contents/MacOS/PhotoEditor"
DEFAULT_OUTPUT_ROOT = REPO_ROOT / "perf-artifacts"
DEFAULT_SCENARIOS = (
    "perf-startup",
    "perf-library-scroll",
    "perf-preview-raster",
    "perf-color-adjustment",
    "perf-edit-global-sliders",
    "perf-edit-color-tools",
    "perf-edit-tone-curves",
    "perf-layer-adjustments",
    "perf-retouch",
    "perf-crop-geometry",
    "perf-preview-raw",
    "perf-thumbnail-cold-warm",
    "perf-mask-brush",
    "perf-mask-lasso",
    "perf-mask-smart-select",
    "perf-interaction-workflow",
    "perf-actual-app-workflow",
    "perf-export-batch",
    "perf-import-large-folder",
    "perf-timeline-scroll",
    "perf-timeline-playback",
    "perf-timeline-edit",
    "perf-carousel-preview",
    "perf-workspace-switch",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Launch PhotoEditor with isolated observability and save a benchmark artifact."
    )
    parser.add_argument(
        "--scenario",
        choices=DEFAULT_SCENARIOS,
        default="perf-startup",
        help="Named benchmark scenario to record in the artifact.",
    )
    parser.add_argument(
        "--app",
        type=Path,
        default=DEFAULT_APP,
        help="Path to the PhotoEditor executable.",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=DEFAULT_OUTPUT_ROOT,
        help="Directory where benchmark artifacts are written.",
    )
    parser.add_argument(
        "--runtime-root",
        type=Path,
        help="Optional app data root. Defaults to an isolated directory inside the artifact.",
    )
    parser.add_argument(
        "--corpus",
        type=Path,
        help="Optional benchmark corpus manifest or directory to record in the artifact manifest.",
    )
    parser.add_argument(
        "--event",
        action="append",
        dest="target_events",
        help="Event that must appear before the run is considered ready. Defaults to desktop.startup.ready.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="Seconds to wait for target events before failing.",
    )
    parser.add_argument(
        "--settle-seconds",
        type=float,
        default=1.0,
        help="Additional seconds to keep the app running after target events are observed.",
    )
    parser.add_argument(
        "--keep-open",
        action="store_true",
        help="Leave the app running after target events are observed.",
    )
    parser.add_argument(
        "--memory-growth-threshold-kb",
        type=int,
        default=65536,
        help="RSS delta above this threshold marks workflow.resource_summary as growth detected.",
    )
    parser.add_argument(
        "--allow-gui-unavailable",
        action="store_true",
        help=(
            "Return success with a skipped status when a macOS GUI session is unavailable "
            "before observability can start."
        ),
    )
    parser.add_argument(
        "app_args",
        nargs=argparse.REMAINDER,
        help="Arguments passed through to the app after `--`.",
    )
    return parser.parse_args()


def timestamp_slug() -> str:
    return datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ")


def read_events(jsonl_path: Path) -> set[str]:
    events: set[str] = set()
    if not jsonl_path.exists():
        return events
    with jsonl_path.open("r", encoding="utf-8") as handle:
        for line in handle:
            try:
                record = json.loads(line)
            except json.JSONDecodeError:
                continue
            fields = record.get("fields")
            if isinstance(fields, dict):
                event = fields.get("event")
                if isinstance(event, str) and event:
                    events.add(event)
            event = record.get("event")
            if isinstance(event, str) and event:
                events.add(event)
    return events


def process_rss_kb(pid: int) -> int | None:
    try:
        result = subprocess.run(
            ["ps", "-o", "rss=", "-p", str(pid)],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
        )
    except OSError:
        return None
    if result.returncode != 0:
        return None
    value = result.stdout.strip()
    if not value:
        return None
    try:
        return int(value)
    except ValueError:
        return None


def process_rss_kb_with_retry(
    process: subprocess.Popen[Any],
    timeout: float = 1.0,
    interval: float = 0.05,
) -> int | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            return process_rss_kb(process.pid)
        sample = process_rss_kb(process.pid)
        if sample is not None:
            return sample
        time.sleep(interval)
    return process_rss_kb(process.pid)


def sample_peak_rss(process: subprocess.Popen[Any], current_peak_kb: int | None) -> int | None:
    if process.poll() is not None:
        return current_peak_kb
    sample = process_rss_kb(process.pid)
    if sample is None:
        return current_peak_kb
    if current_peak_kb is None:
        return sample
    return max(current_peak_kb, sample)


def rss_delta_kb(start_kb: int | None, end_kb: int | None) -> int | None:
    if start_kb is None or end_kb is None:
        return None
    return end_kb - start_kb


def kb_to_bytes(value: int | None) -> int | None:
    return None if value is None else value * 1024


def max_optional_int(left: int | None, right: int | None) -> int | None:
    if left is None:
        return right
    if right is None:
        return left
    return max(left, right)


def wait_for_events(
    jsonl_path: Path,
    target_events: set[str],
    timeout: float,
    process: subprocess.Popen[Any],
) -> tuple[set[str], int | None]:
    deadline = time.monotonic() + timeout
    seen: set[str] = set()
    peak_rss_kb: int | None = None
    while time.monotonic() < deadline:
        peak_rss_kb = sample_peak_rss(process, peak_rss_kb)
        seen = read_events(jsonl_path)
        if target_events.issubset(seen):
            return seen, peak_rss_kb
        if process.poll() is not None:
            return seen, peak_rss_kb
        time.sleep(0.1)
    return seen, peak_rss_kb


def terminate_process(process: subprocess.Popen[Any], timeout: float = 5.0) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=timeout)
        return
    except subprocess.TimeoutExpired:
        pass
    if process.poll() is None:
        process.send_signal(signal.SIGKILL)
        process.wait(timeout=timeout)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def append_jsonl_event(jsonl_path: Path, event: str, fields: dict[str, Any]) -> None:
    payload = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "event": event,
        "severity": fields.get("severity", "info"),
        "fields": {"event": event, **fields},
    }
    jsonl_path.parent.mkdir(parents=True, exist_ok=True)
    with jsonl_path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(payload, sort_keys=True) + "\n")


def append_resource_sample(
    jsonl_path: Path,
    *,
    workflow_id: str,
    workflow: str,
    stage: str,
    rss_kb: int | None,
    peak_rss_kb: int | None,
) -> None:
    append_jsonl_event(
        jsonl_path,
        "workflow.resource_sampled",
        {
            "workflow_id": workflow_id,
            "workflow": workflow,
            "surface": "benchmark",
            "stage": stage,
            "rss_kb": rss_kb,
            "rss_bytes": kb_to_bytes(rss_kb),
            "peak_rss_kb": peak_rss_kb,
            "peak_rss_bytes": kb_to_bytes(peak_rss_kb),
        },
    )


def append_resource_summary(
    jsonl_path: Path,
    *,
    workflow_id: str,
    workflow: str,
    start_rss_kb: int | None,
    peak_rss_kb: int | None,
    end_rss_kb: int | None,
    memory_growth_threshold_kb: int,
) -> bool:
    delta_kb = rss_delta_kb(start_rss_kb, end_rss_kb)
    memory_growth_detected = (
        delta_kb is not None and delta_kb > max(0, memory_growth_threshold_kb)
    )
    append_jsonl_event(
        jsonl_path,
        "workflow.resource_summary",
        {
            "workflow_id": workflow_id,
            "workflow": workflow,
            "surface": "benchmark",
            "stage": "summary",
            "rss_start_kb": start_rss_kb,
            "rss_peak_kb": peak_rss_kb,
            "rss_end_kb": end_rss_kb,
            "rss_delta_kb": delta_kb,
            "rss_bytes": kb_to_bytes(end_rss_kb),
            "rss_delta_bytes": kb_to_bytes(delta_kb),
            "memory_growth_threshold_kb": memory_growth_threshold_kb,
            "memory_growth_threshold_bytes": kb_to_bytes(memory_growth_threshold_kb),
            "memory_growth_detected": memory_growth_detected,
        },
    )
    return memory_growth_detected


def append_worker_queue_summary(
    jsonl_path: Path,
    *,
    workflow_id: str,
    workflow: str,
    elapsed_ms: int,
    completed: bool,
) -> None:
    append_jsonl_event(
        jsonl_path,
        "worker.queue.summary",
        {
            "workflow_id": workflow_id,
            "workflow": workflow,
            "surface": "benchmark",
            "stage": "summary",
            "worker_pool": "benchmark-process",
            "pool_id": "benchmark-process",
            "active_worker_count": 0,
            "pending_worker_count": 0,
            "worker_limit": 1,
            "queue_wait_ms": 0,
            "worker_elapsed_ms": elapsed_ms,
            "completed_count": 1 if completed else 0,
            "cancelled_count": 0 if completed else 1,
        },
    )


def append_benchmark_artifact_summary(
    jsonl_path: Path,
    *,
    workflow_id: str,
    workflow: str,
    artifact_dir: Path,
    elapsed_ms: int,
    target_met: bool,
    skipped: bool,
    process_returncode: int | None,
    start_rss_kb: int | None,
    peak_rss_kb: int | None,
    end_rss_kb: int | None,
) -> None:
    append_jsonl_event(
        jsonl_path,
        "benchmark.artifact.summary",
        {
            "workflow_id": workflow_id,
            "workflow": workflow,
            "surface": "benchmark",
            "stage": "summary",
            "artifact_dir": str(artifact_dir),
            "elapsed_ms": elapsed_ms,
            "target_events_met": target_met,
            "skipped": skipped,
            "process_returncode": process_returncode,
            "rss_start_kb": start_rss_kb,
            "rss_peak_kb": peak_rss_kb,
            "rss_end_kb": end_rss_kb,
            "rss_delta_kb": rss_delta_kb(start_rss_kb, end_rss_kb),
            "rss_bytes": kb_to_bytes(end_rss_kb),
            "rss_delta_bytes": kb_to_bytes(rss_delta_kb(start_rss_kb, end_rss_kb)),
        },
    )


def run_summary(run_dir: Path, artifact_dir: Path) -> None:
    summarizer = Path(__file__).with_name("summarize-jsonl.py")
    group_args = [
        "--group-by",
        "workflow",
        "--group-by",
        "tool",
        "--group-by",
        "stage",
        "--group-by",
        "backend",
        "--group-by",
        "surface",
        "--group-by",
        "worker_pool",
    ]
    text_result = subprocess.run(
        [sys.executable, "-B", str(summarizer), str(run_dir), "--all-events", *group_args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    (artifact_dir / "summary.txt").write_text(text_result.stdout, encoding="utf-8")

    json_result = subprocess.run(
        [sys.executable, "-B", str(summarizer), str(run_dir), "--all-events", "--json", *group_args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    (artifact_dir / "summary.json").write_text(json_result.stdout, encoding="utf-8")


def gui_unavailable_abort(process_returncode: int | None, desktop_log: Path, process_output: Path) -> bool:
    if process_returncode not in {-6, 134}:
        return False
    try:
        output = process_output.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    return (
        "PasteBoard: Error creating pasteboard" in output
        or "Connection Invalid error for service com.apple.hiservices-xpcservice" in output
    )


def main() -> int:
    args = parse_args()
    app = args.app.resolve()
    if not app.exists():
        print(f"PhotoEditor executable not found: {app}", file=sys.stderr)
        return 2

    target_events = set(args.target_events or ["desktop.startup.ready"])
    session_id = uuid.uuid4().hex
    workflow_id = f"{args.scenario}:{session_id}"
    artifact_dir = (args.output_root / f"{timestamp_slug()}-{args.scenario}-{session_id[:8]}").resolve()
    run_dir = artifact_dir / "logs" / "runs" / f"{args.scenario}-{session_id[:8]}"
    runtime_root = (args.runtime_root or artifact_dir / "runtime").resolve()
    corpus = args.corpus.resolve() if args.corpus is not None else None
    backup_root = runtime_root / "backups"
    log_root = artifact_dir / "logs"
    artifact_dir.mkdir(parents=True, exist_ok=False)
    run_dir.mkdir(parents=True, exist_ok=True)
    runtime_root.mkdir(parents=True, exist_ok=True)
    backup_root.mkdir(parents=True, exist_ok=True)
    log_root.mkdir(parents=True, exist_ok=True)

    env = os.environ.copy()
    env.update(
        {
            "PHOTO_EDITOR_APP_DATA_DIR": str(runtime_root),
            "PHOTO_EDITOR_LOG_DIR": str(log_root),
            "PHOTO_EDITOR_BACKUP_DIR": str(backup_root),
            "PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY": "1",
            "PHOTO_EDITOR_LOG_LEVEL": env.get("PHOTO_EDITOR_LOG_LEVEL", "info"),
            "PHOTO_EDITOR_SESSION_ID": session_id,
            "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR": str(run_dir),
        }
    )

    app_args = args.app_args
    if app_args and app_args[0] == "--":
        app_args = app_args[1:]
    command = [str(app), *app_args]
    started_at = datetime.now(timezone.utc).isoformat()
    started_monotonic = time.monotonic()
    process_output = artifact_dir / "process-output.log"
    with process_output.open("w", encoding="utf-8") as process_output_handle:
        process = subprocess.Popen(
            command,
            cwd=REPO_ROOT,
            env=env,
            stdout=process_output_handle,
            stderr=subprocess.STDOUT,
            text=True,
        )
    desktop_log = run_dir / "desktop.jsonl"
    start_rss_kb = process_rss_kb_with_retry(process)
    append_resource_sample(
        desktop_log,
        workflow_id=workflow_id,
        workflow=args.scenario,
        stage="start",
        rss_kb=start_rss_kb,
        peak_rss_kb=start_rss_kb,
    )
    seen_events, peak_rss_kb = wait_for_events(desktop_log, target_events, args.timeout, process)
    peak_rss_kb = max_optional_int(start_rss_kb, peak_rss_kb)
    append_resource_sample(
        desktop_log,
        workflow_id=workflow_id,
        workflow=args.scenario,
        stage="target_events",
        rss_kb=process_rss_kb(process.pid),
        peak_rss_kb=peak_rss_kb,
    )
    target_met = target_events.issubset(seen_events)
    if target_met and args.settle_seconds > 0:
        settle_deadline = time.monotonic() + args.settle_seconds
        while time.monotonic() < settle_deadline:
            peak_rss_kb = sample_peak_rss(process, peak_rss_kb)
            time.sleep(0.1)
        append_resource_sample(
            desktop_log,
            workflow_id=workflow_id,
            workflow=args.scenario,
            stage="settled",
            rss_kb=process_rss_kb(process.pid),
            peak_rss_kb=peak_rss_kb,
        )

    if not args.keep_open:
        end_rss_kb = process_rss_kb(process.pid)
        peak_rss_kb = sample_peak_rss(process, peak_rss_kb)
        append_resource_sample(
            desktop_log,
            workflow_id=workflow_id,
            workflow=args.scenario,
            stage="end",
            rss_kb=end_rss_kb,
            peak_rss_kb=peak_rss_kb,
        )
        terminate_process(process)
    else:
        end_rss_kb = process_rss_kb(process.pid)
        peak_rss_kb = sample_peak_rss(process, peak_rss_kb)
        append_resource_sample(
            desktop_log,
            workflow_id=workflow_id,
            workflow=args.scenario,
            stage="end_keep_open",
            rss_kb=end_rss_kb,
            peak_rss_kb=peak_rss_kb,
        )

    ended_at = datetime.now(timezone.utc).isoformat()
    elapsed_ms = int(round((time.monotonic() - started_monotonic) * 1000.0))
    process_returncode = process.poll()
    skipped = (
        not target_met
        and args.allow_gui_unavailable
        and gui_unavailable_abort(process_returncode, desktop_log, process_output)
    )
    skip_reason = "gui_session_unavailable" if skipped else None
    memory_growth_detected = append_resource_summary(
        desktop_log,
        workflow_id=workflow_id,
        workflow=args.scenario,
        start_rss_kb=start_rss_kb,
        peak_rss_kb=peak_rss_kb,
        end_rss_kb=end_rss_kb,
        memory_growth_threshold_kb=args.memory_growth_threshold_kb,
    )
    benchmark_completed = target_met or skipped
    append_worker_queue_summary(
        desktop_log,
        workflow_id=workflow_id,
        workflow=args.scenario,
        elapsed_ms=elapsed_ms,
        completed=benchmark_completed,
    )
    append_benchmark_artifact_summary(
        desktop_log,
        workflow_id=workflow_id,
        workflow=args.scenario,
        artifact_dir=artifact_dir,
        elapsed_ms=elapsed_ms,
        target_met=target_met,
        skipped=skipped,
        process_returncode=process_returncode,
        start_rss_kb=start_rss_kb,
        peak_rss_kb=peak_rss_kb,
        end_rss_kb=end_rss_kb,
    )
    run_summary(run_dir, artifact_dir)
    manifest = {
        "scenario": args.scenario,
        "session_id": session_id,
        "workflow_id": workflow_id,
        "started_at": started_at,
        "ended_at": ended_at,
        "target_events": sorted(target_events),
        "seen_events": sorted(seen_events),
        "target_events_met": target_met,
        "benchmark_summary_event": "benchmark.artifact.summary",
        "benchmark_worker_pool": "benchmark-process",
        "app": str(app),
        "command": command,
        "runtime_root": str(runtime_root),
        "corpus": str(corpus) if corpus is not None else None,
        "run_dir": str(run_dir),
        "desktop_log": str(desktop_log),
        "summary_text": str(artifact_dir / "summary.txt"),
        "summary_json": str(artifact_dir / "summary.json"),
        "artifact_dir": str(artifact_dir),
        "process_output": str(process_output),
        "process_returncode": process_returncode,
        "start_rss_kb": start_rss_kb,
        "peak_rss_kb": peak_rss_kb,
        "end_rss_kb": end_rss_kb,
        "rss_delta_kb": rss_delta_kb(start_rss_kb, end_rss_kb),
        "rss_start_kb": start_rss_kb,
        "rss_peak_kb": peak_rss_kb,
        "rss_end_kb": end_rss_kb,
        "rss_bytes": kb_to_bytes(end_rss_kb),
        "rss_start_bytes": kb_to_bytes(start_rss_kb),
        "rss_peak_bytes": kb_to_bytes(peak_rss_kb),
        "rss_end_bytes": kb_to_bytes(end_rss_kb),
        "rss_delta_bytes": kb_to_bytes(rss_delta_kb(start_rss_kb, end_rss_kb)),
        "gpu_memory_kb": None,
        "gpu_memory_bytes": None,
        "memory_growth_threshold_kb": args.memory_growth_threshold_kb,
        "memory_growth_threshold_bytes": kb_to_bytes(args.memory_growth_threshold_kb),
        "memory_growth_detected": memory_growth_detected,
        "skipped": skipped,
        "skip_reason": skip_reason,
    }
    write_json(artifact_dir / "manifest.json", manifest)

    print(f"artifact_dir={artifact_dir}")
    print(f"run_dir={run_dir}")
    print(f"target_events_met={str(target_met).lower()}")
    if skipped:
        print("benchmark_status=skipped")
        print(f"skip_reason={skip_reason}")
    return 0 if target_met or skipped else 1


if __name__ == "__main__":
    raise SystemExit(main())
