#!/usr/bin/env python3
"""Self-test color adjustment sample distribution comparison policy."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
COMPARE = REPO_ROOT / "scripts/perf/compare-color-adjustment-samples.py"
EVENT = "benchmark.color_adjustment_sample.completed"
STAGES = ("preview-960", "full-1920")


def write_artifact(root: Path, name: str, values: dict[str, list[float]]) -> Path:
    artifact = root / name
    log_dir = artifact / "logs/runs/run"
    log_dir.mkdir(parents=True)
    with (log_dir / "events.jsonl").open("w", encoding="utf-8") as handle:
        for stage, stage_values in values.items():
            for index, elapsed_ms in enumerate(stage_values, start=1):
                handle.write(
                    json.dumps(
                        {
                            "event": EVENT,
                            "fields": {
                                "state": "spatial-detail",
                                "stage": stage,
                                "iteration": index,
                                "elapsed_ms": elapsed_ms,
                            },
                        },
                        sort_keys=True,
                    )
                    + "\n"
                )
    return artifact


def run_compare(
    baseline: Path,
    candidate: Path,
    *extra_args: str,
    expected_code: int = 0,
) -> subprocess.CompletedProcess[str]:
    command = [
        sys.executable,
        "-B",
        str(COMPARE),
        "--baseline",
        str(baseline),
        "--candidate",
        str(candidate),
        "--state",
        "spatial-detail",
        "--min-count",
        "4",
        "--bootstrap-iterations",
        "200",
    ]
    command.extend(extra_args)
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    if result.returncode != expected_code:
        raise SystemExit(
            f"Unexpected compare exit {result.returncode}; expected {expected_code}\n"
            f"Command: {' '.join(command)}\n"
            f"Output:\n{result.stdout}"
        )
    return result


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="color-adjustment-compare-") as temporary:
        root = Path(temporary)
        baseline_values = {
            "preview-960": [100.0, 101.0, 99.0, 102.0],
            "full-1920": [400.0, 401.0, 399.0, 402.0],
        }
        baseline = write_artifact(root, "baseline", baseline_values)
        identical = write_artifact(root, "identical", baseline_values)
        faster = write_artifact(
            root,
            "faster",
            {
                "preview-960": [96.0, 97.0, 95.0, 98.0],
                "full-1920": [396.0, 397.0, 395.0, 398.0],
            },
        )
        slower = write_artifact(
            root,
            "slower",
            {
                "preview-960": [105.0, 106.0, 104.0, 107.0],
                "full-1920": [405.0, 406.0, 404.0, 407.0],
            },
        )
        too_few = write_artifact(
            root,
            "too-few",
            {
                "preview-960": [100.0, 101.0, 99.0],
                "full-1920": [400.0, 401.0, 399.0],
            },
        )

        identical_result = run_compare(baseline, identical)
        require(
            "color_adjustment_sample_compare=passed" in identical_result.stdout,
            f"Identical samples should pass:\n{identical_result.stdout}",
        )

        faster_result = run_compare(
            baseline,
            faster,
            "--min-improvement-ms",
            "1",
        )
        require(
            "color_adjustment_sample_compare=passed" in faster_result.stdout,
            f"Faster samples should satisfy the improvement bound:\n{faster_result.stdout}",
        )

        slower_result = run_compare(baseline, slower, expected_code=1)
        require(
            "regression bound failed" in slower_result.stdout,
            f"Slower samples should fail the regression bound:\n{slower_result.stdout}",
        )

        too_few_result = run_compare(baseline, too_few, expected_code=1)
        require(
            "candidate has 3 samples; expected at least 4" in too_few_result.stdout,
            f"Insufficient sample count should fail:\n{too_few_result.stdout}",
        )

    print("color_adjustment_sample_compare_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
