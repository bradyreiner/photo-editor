#!/usr/bin/env python3
"""Focused tests for JSONL performance summarization."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts/perf/summarize-jsonl.py"


def assert_true(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def test_grouped_metrics() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        log_path = Path(temp_dir) / "desktop.jsonl"
        records = [
            {
                "event": "tool.stage.completed",
                "fields": {
                    "event": "tool.stage.completed",
                    "workflow_id": "wf-1",
                    "workflow": "perf-edit-global-sliders",
                    "interaction_id": "interaction-1",
                    "tool_id": "global.exposure",
                    "surface": "edit",
                    "stage": "draft_preview",
                    "backend": "cpu",
                    "elapsed_ms": 12,
                    "queue_wait_ms": 2,
                },
            },
            {
                "event": "workflow.resource_summary",
                "fields": {
                    "event": "workflow.resource_summary",
                    "workflow_id": "wf-1",
                    "workflow": "perf-edit-global-sliders",
                    "surface": "edit",
                    "stage": "summary",
                    "rss_bytes": 1000,
                    "rss_delta_bytes": 100,
                },
            },
        ]
        log_path.write_text(
            "".join(json.dumps(record) + "\n" for record in records),
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                str(SCRIPT_PATH),
                str(log_path),
                "--all-events",
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
                "--json",
            ],
            cwd=REPO_ROOT,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        assert_true(result.returncode == 0, result.stdout)
        output = json.loads(result.stdout)
        groups = output["groups"]
        assert_true(
            "perf-edit-global-sliders" in groups["workflow"],
            "workflow group was not recorded",
        )
        assert_true("global.exposure" in groups["tool"], "tool group was not recorded")
        assert_true("draft_preview" in groups["stage"], "stage group was not recorded")
        assert_true("summary" in groups["stage"], "resource stage group was not recorded")
        assert_true("cpu" in groups["backend"], "backend group was not recorded")
        assert_true("edit" in groups["surface"], "surface group was not recorded")
        workflow_group = groups["workflow"]["perf-edit-global-sliders"]
        assert_true(
            workflow_group["numeric_fields"]["elapsed_ms"]["max"] == 12,
            "grouped timing metric was not promoted to numeric fields",
        )
        assert_true(
            workflow_group["numeric_fields"]["rss_delta_bytes"]["max"] == 100,
            "grouped resource metric was not recorded",
        )


def main() -> int:
    test_grouped_metrics()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
