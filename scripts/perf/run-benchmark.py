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
    "perf-preview-raw",
    "perf-thumbnail-cold-warm",
    "perf-mask-brush",
    "perf-export-batch",
    "perf-timeline-scroll",
    "perf-timeline-playback",
    "perf-carousel-preview",
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


def sample_peak_rss(process: subprocess.Popen[Any], current_peak_kb: int | None) -> int | None:
    if process.poll() is not None:
        return current_peak_kb
    sample = process_rss_kb(process.pid)
    if sample is None:
        return current_peak_kb
    if current_peak_kb is None:
        return sample
    return max(current_peak_kb, sample)


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


def run_summary(run_dir: Path, artifact_dir: Path) -> None:
    summarizer = Path(__file__).with_name("summarize-jsonl.py")
    text_result = subprocess.run(
        [sys.executable, "-B", str(summarizer), str(run_dir)],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    (artifact_dir / "summary.txt").write_text(text_result.stdout, encoding="utf-8")

    json_result = subprocess.run(
        [sys.executable, "-B", str(summarizer), str(run_dir), "--json"],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    (artifact_dir / "summary.json").write_text(json_result.stdout, encoding="utf-8")


def main() -> int:
    args = parse_args()
    app = args.app.resolve()
    if not app.exists():
        print(f"PhotoEditor executable not found: {app}", file=sys.stderr)
        return 2

    target_events = set(args.target_events or ["desktop.startup.ready"])
    session_id = uuid.uuid4().hex
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
    process = subprocess.Popen(command, cwd=REPO_ROOT, env=env)
    desktop_log = run_dir / "desktop.jsonl"
    seen_events, peak_rss_kb = wait_for_events(desktop_log, target_events, args.timeout, process)
    target_met = target_events.issubset(seen_events)
    if target_met and args.settle_seconds > 0:
        settle_deadline = time.monotonic() + args.settle_seconds
        while time.monotonic() < settle_deadline:
            peak_rss_kb = sample_peak_rss(process, peak_rss_kb)
            time.sleep(0.1)

    if not args.keep_open:
        terminate_process(process)

    ended_at = datetime.now(timezone.utc).isoformat()
    run_summary(run_dir, artifact_dir)
    manifest = {
        "scenario": args.scenario,
        "session_id": session_id,
        "started_at": started_at,
        "ended_at": ended_at,
        "target_events": sorted(target_events),
        "seen_events": sorted(seen_events),
        "target_events_met": target_met,
        "app": str(app),
        "command": command,
        "runtime_root": str(runtime_root),
        "corpus": str(corpus) if corpus is not None else None,
        "run_dir": str(run_dir),
        "desktop_log": str(desktop_log),
        "summary_text": str(artifact_dir / "summary.txt"),
        "summary_json": str(artifact_dir / "summary.json"),
        "artifact_dir": str(artifact_dir),
        "process_returncode": process.poll(),
        "peak_rss_kb": peak_rss_kb,
        "gpu_memory_kb": None,
    }
    write_json(artifact_dir / "manifest.json", manifest)

    print(f"artifact_dir={artifact_dir}")
    print(f"run_dir={run_dir}")
    print(f"target_events_met={str(target_met).lower()}")
    return 0 if target_met else 1


if __name__ == "__main__":
    raise SystemExit(main())
