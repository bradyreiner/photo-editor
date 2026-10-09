#!/usr/bin/env python3
"""Focused tests for the benchmark artifact runner."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts/perf/run-benchmark.py"
GOAL8_SCENARIOS = {
    "perf-edit-global-sliders",
    "perf-edit-color-tools",
    "perf-edit-tone-curves",
    "perf-mask-brush",
    "perf-mask-lasso",
    "perf-mask-smart-select",
    "perf-layer-adjustments",
    "perf-retouch",
    "perf-crop-geometry",
    "perf-export-batch",
    "perf-import-large-folder",
    "perf-library-scroll",
    "perf-timeline-edit",
    "perf-carousel-preview",
    "perf-workspace-switch",
}


def load_module():
    spec = importlib.util.spec_from_file_location("run_benchmark", SCRIPT_PATH)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def assert_true(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def test_goal8_scenarios_are_first_class_choices() -> None:
    module = load_module()
    scenarios = set(module.DEFAULT_SCENARIOS)
    missing = sorted(GOAL8_SCENARIOS - scenarios)
    assert_true(not missing, f"missing benchmark scenarios: {missing}")


def test_gui_unavailable_skip_allows_runner_resource_log() -> None:
    module = load_module()
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        desktop_log = temp_root / "desktop.jsonl"
        desktop_log.write_text(
            '{"event":"workflow.resource_sampled","fields":{"event":"workflow.resource_sampled"}}\n',
            encoding="utf-8",
        )
        process_output = temp_root / "process-output.log"
        process_output.write_text(
            "\n".join(
                [
                    "PasteBoard: Error creating pasteboard: com.apple.pasteboard.clipboard [-4960]",
                    "Connection Invalid error for service com.apple.hiservices-xpcservice.",
                ]
            ),
            encoding="utf-8",
        )
        assert_true(
            module.gui_unavailable_abort(-6, desktop_log, process_output),
            "runner-created resource logs should not suppress GUI-unavailable skip detection",
        )


def test_rss_manifest_fields() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        temp_root = Path(temp_dir)
        probe = temp_root / "rss_probe_app.py"
        probe.write_text(
            "\n".join(
                [
                    "import json",
                    "import os",
                    "import time",
                    "from pathlib import Path",
                    "run_dir = Path(os.environ['PHOTO_EDITOR_OBSERVABILITY_RUN_DIR'])",
                    "run_dir.mkdir(parents=True, exist_ok=True)",
                    "desktop_log = run_dir / 'desktop.jsonl'",
                    "with desktop_log.open('a', encoding='utf-8') as handle:",
                    "    handle.write(json.dumps({'event': 'benchmark.rss_probe.completed', 'fields': {'elapsed_ms': 1}}) + '\\n')",
                    "    handle.flush()",
                    "time.sleep(0.5)",
                ]
            )
            + "\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                str(SCRIPT_PATH),
                "--scenario",
                "perf-startup",
                "--app",
                sys.executable,
                "--output-root",
                str(temp_root / "artifacts"),
                "--event",
                "benchmark.rss_probe.completed",
                "--timeout",
                "5",
                "--settle-seconds",
                "0",
                "--memory-growth-threshold-kb",
                "999999999",
                "--",
                str(probe),
            ],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert_true(result.returncode == 0, result.stdout)
        artifact_line = next(
            line for line in result.stdout.splitlines() if line.startswith("artifact_dir=")
        )
        artifact_dir = Path(artifact_line.partition("=")[2])
        manifest = json.loads((artifact_dir / "manifest.json").read_text(encoding="utf-8"))
        for field in (
            "rss_start_kb",
            "rss_peak_kb",
            "rss_end_kb",
            "rss_delta_kb",
            "rss_bytes",
            "rss_start_bytes",
            "rss_peak_bytes",
            "rss_end_bytes",
            "rss_delta_bytes",
        ):
            assert_true(field in manifest, f"missing {field}")
        if manifest["process_max_rss_source"] is not None:
            assert_true(
                isinstance(manifest["process_max_rss_kb"], int) and
                manifest["process_max_rss_kb"] > 0,
                "child-process peak RSS should be reported when getrusage is available",
            )
            assert_true(
                manifest["process_max_rss_source"] == "getrusage.RUSAGE_CHILDREN.ru_maxrss",
                "child-process peak RSS source should be identified",
            )
        assert_true("workflow_id" in manifest, "missing workflow_id")
        assert_true(
            manifest["memory_growth_threshold_kb"] == 999999999,
            "memory growth threshold was not recorded",
        )
        assert_true(
            manifest["memory_growth_detected"] is False,
            "large threshold should not flag memory growth",
        )
        desktop_log = Path(manifest["desktop_log"])
        events = []
        with desktop_log.open("r", encoding="utf-8") as handle:
            for line in handle:
                record = json.loads(line)
                events.append(record.get("event") or record.get("fields", {}).get("event"))
        assert_true("workflow.resource_sampled" in events, "missing workflow.resource_sampled")
        assert_true("workflow.resource_summary" in events, "missing workflow.resource_summary")
        assert_true("worker.queue.summary" in events, "missing worker.queue.summary")
        assert_true("benchmark.artifact.summary" in events, "missing benchmark.artifact.summary")
        summary = json.loads((artifact_dir / "summary.json").read_text(encoding="utf-8"))
        assert_true(
            "workflow.resource_summary" in summary["events"],
            "summary should include workflow.resource_summary",
        )
        assert_true(
            "worker.queue.summary" in summary["events"],
            "summary should include worker.queue.summary",
        )
        assert_true(
            "benchmark.artifact.summary" in summary["events"],
            "summary should include benchmark.artifact.summary",
        )
        worker_fields = summary["events"]["worker.queue.summary"]["numeric_fields"]
        for field in (
            "active_worker_count",
            "pending_worker_count",
            "queue_wait_ms",
            "completed_count",
            "cancelled_count",
        ):
            assert_true(field in worker_fields, f"summary should include worker field {field}")
        worker_pools = summary["groups"].get("worker_pool", {})
        assert_true("benchmark-process" in worker_pools, "summary should group by benchmark worker pool")
        sampled_values = [
            manifest["rss_start_kb"],
            manifest["rss_peak_kb"],
            manifest["rss_end_kb"],
            manifest["rss_delta_kb"],
        ]
        if any(value is not None for value in sampled_values):
            assert_true(isinstance(manifest["rss_start_kb"], int), "rss_start_kb should be numeric when sampled")
            assert_true(isinstance(manifest["rss_peak_kb"], int), "rss_peak_kb should be numeric when sampled")
            assert_true(isinstance(manifest["rss_end_kb"], int), "rss_end_kb should be numeric when sampled")
            assert_true(isinstance(manifest["rss_delta_kb"], int), "rss_delta_kb should be numeric when sampled")
            assert_true(isinstance(manifest["rss_bytes"], int), "rss_bytes should be numeric when sampled")
            assert_true(
                manifest["rss_bytes"] == manifest["rss_end_kb"] * 1024,
                "rss_bytes should match end RSS in bytes",
            )
            assert_true(
                manifest["rss_delta_bytes"] == manifest["rss_delta_kb"] * 1024,
                "rss_delta_bytes should match RSS delta in bytes",
            )
            assert_true(
                manifest["rss_peak_kb"] >= manifest["rss_start_kb"],
                "peak RSS should be at least start RSS",
            )


def main() -> int:
    test_goal8_scenarios_are_first_class_choices()
    test_gui_unavailable_skip_allows_runner_resource_log()
    test_rss_manifest_fields()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
