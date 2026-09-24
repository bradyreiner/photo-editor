#!/usr/bin/env python3
"""Self-test for code-duplication-report.py."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("code-duplication-report.py")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        source_root = Path(temporary)
        first = source_root / "first.cpp"
        second = source_root / "second.rs"
        shared = [f"shared_operation_{index} = input_{index} + output_{index};" for index in range(8)]
        first.write_text("\n".join(["unique_before();", *shared, "unique_after();"]) + "\n", encoding="utf-8")
        second.write_text("\n".join(["other_before();", *shared, "other_after();"]) + "\n", encoding="utf-8")
        report_path = source_root / "report.json"
        command = [
            sys.executable,
            "-B",
            str(SCRIPT),
            "--source-root",
            str(source_root),
            "--root",
            "first.cpp",
            "--root",
            "second.rs",
            "--output",
            str(report_path),
            "--minimum-lines",
            "5",
            "--minimum-characters",
            "100",
        ]
        result = subprocess.run(command, check=False, capture_output=True, text=True)
        require(result.returncode == 0, result.stderr)
        report = json.loads(report_path.read_text(encoding="utf-8"))
        require(report["summary"]["cloneCount"] == 1, "one maximal clone should be reported")
        finding = report["findings"][0]
        require(finding["logicalLines"] == 8, "overlapping windows should merge into the maximal clone")
        require(
            report["summary"]["scopeCounts"]["production"] == 1
            and report["summary"]["languagePairCounts"]["c-cpp -> rust"] == 1,
            "scope and language-pair counts should cover every finding",
        )
        require(
            finding["left"]["language"] == "c-cpp" and finding["right"]["language"] == "rust",
            "cross-language source should remain classified",
        )

        failed = subprocess.run([*command, "--fail-above", "0"], check=False, capture_output=True, text=True)
        require(failed.returncode == 1, "clone budget should fail closed")
        production_failed = subprocess.run(
            [*command, "--fail-production-above", "0"],
            check=False,
            capture_output=True,
            text=True,
        )
        require(
            production_failed.returncode == 1
            and "production_clones=1" in production_failed.stderr,
            "production clone budget should fail independently",
        )

    print("code-duplication self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
