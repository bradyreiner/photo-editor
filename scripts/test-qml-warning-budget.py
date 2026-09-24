#!/usr/bin/env python3
"""Self-test for qml-warning-budget.py."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("qml-warning-budget.py")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    spec = importlib.util.spec_from_file_location("qml_warning_budget", SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError("unable to load qml warning budget")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    sample = "\n".join(
        [
            "Warning: Example.qml:1:1: Unqualified access [unqualified]",
            "Warning: Example.qml:2:1: Bad layout [Quick.layout-positioning]",
            "Info: unrelated",
        ]
    )
    counts = module.warning_counts(sample)
    require(counts["unqualified"] == 1, "unqualified warning should be counted")
    require(
        counts["Quick.layout-positioning"] == 1,
        "category names containing punctuation should be preserved",
    )

    with tempfile.TemporaryDirectory() as temporary:
        output_path = Path(temporary) / "qmllint.log"
        output_path.write_text(sample, encoding="utf-8")
        passing = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--input",
                str(output_path),
                "--max-category",
                "unqualified=1",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        require(passing.returncode == 0, passing.stderr)
        failing = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--input",
                str(output_path),
                "--max-category",
                "Quick.layout-positioning=0",
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        require(failing.returncode == 1, "an exceeded category budget should fail")

    print("qml-warning-budget self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
