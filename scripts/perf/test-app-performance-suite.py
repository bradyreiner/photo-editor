#!/usr/bin/env python3
"""Focused tests for the app-wide performance suite wrapper."""

from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = REPO_ROOT / "scripts/perf/run-app-performance-suite.py"


def load_module():
    spec = importlib.util.spec_from_file_location("run_app_performance_suite", SCRIPT_PATH)
    assert spec is not None
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def assert_true(value: bool, message: str) -> None:
    if not value:
        raise AssertionError(message)


def test_profiles_and_dry_run() -> None:
    with tempfile.TemporaryDirectory() as temp_dir:
        result = subprocess.run(
            [
                sys.executable,
                "-B",
                str(SCRIPT_PATH),
                "--profile",
                "smoke",
                "--output-root",
                temp_dir,
                "--dry-run",
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
        artifact = Path(artifact_line.partition("=")[2])
        manifest = json.loads((artifact / "manifest.json").read_text(encoding="utf-8"))
        assert_true(manifest["status"] == "dry-run", "dry-run status was not recorded")
        assert_true(
            manifest["selected_suites"]
            == ["startup", "preview-raster", "thumbnail-cold-warm"],
            "smoke profile selected the wrong suites",
        )
        assert_true((artifact / "operability-report.md").is_file(), "missing markdown report")
        assert_true((artifact / "operability-report.json").is_file(), "missing JSON report")


def test_artifact_parsing_and_report_payload() -> None:
    module = load_module()
    output = "\n".join(
        [
            "artifact_dir=/tmp/photo-editor/a",
            "one_tile_artifact=/tmp/photo-editor/b",
            "ignored=value",
            "two-by-two_artifact=/tmp/photo-editor/b",
        ]
    )
    expected = [
        str(Path("/tmp/photo-editor/a").resolve()),
        str(Path("/tmp/photo-editor/b").resolve()),
    ]
    assert_true(module.parse_artifact_dirs(output) == expected, "artifact parser did not dedupe expected lines")

    with tempfile.TemporaryDirectory() as temp_dir:
        artifact = Path(temp_dir) / "bench"
        artifact.mkdir()
        (artifact / "manifest.json").write_text('{"scenario": "demo"}\n', encoding="utf-8")
        (artifact / "summary.json").write_text(
            json.dumps(
                {
                    "events": {
                        "preview.render.completed": {
                            "count": 2,
                            "severities": {"info": 2},
                            "timings": {
                                "elapsed_ms": {
                                    "count": 2,
                                    "p50": 10,
                                    "p95": 20,
                                    "max": 25,
                                }
                            },
                        },
                        "job.failed": {
                            "count": 1,
                            "severities": {"error": 1},
                            "timings": {"elapsed_ms": {"count": 1, "p50": 5, "p95": 5, "max": 5}},
                        },
                    }
                }
            )
            + "\n",
            encoding="utf-8",
        )
        result = module.SuiteResult(
            name="demo",
            description="demo suite",
            command=["true"],
            returncode=0,
            log_file=None,
            artifact_dirs=[str(artifact)],
            status="passed",
        )
        report = module.build_report_payload([result], None)
        suite = report["suites"][0]
        assert_true(suite["artifacts"][0]["warning_error_counts"]["error"] == 1, "missing error count")
        assert_true(suite["artifacts"][0]["attention_event_count"] == 1, "missing attention count")
        assert_true(
            suite["artifacts"][0]["timings"][0]["event"] == "job.failed",
            "timing rows should be sorted by event name",
        )


def main() -> int:
    test_profiles_and_dry_run()
    test_artifact_parsing_and_report_payload()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
