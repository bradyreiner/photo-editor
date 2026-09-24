#!/usr/bin/env python3
"""Report maximal exact normalized code clones without modifying source files."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from collections import defaultdict
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path


SCHEMA = "framehouse-code-duplication:v1"
LANGUAGES = {
    ".c": "c-cpp",
    ".cc": "c-cpp",
    ".cpp": "c-cpp",
    ".h": "c-cpp",
    ".hpp": "c-cpp",
    ".m": "objective-cpp",
    ".mm": "objective-cpp",
    ".qml": "qml",
    ".rs": "rust",
    ".py": "python",
    ".cmake": "cmake",
    ".sh": "shell",
    ".ps1": "powershell",
}
DEFAULT_ROOTS = ("apps", "engine", "rust", "scripts", "cmake", "tests")
IGNORED_PARTS = {".git", "build", "target", "node_modules", "__pycache__", ".venv"}
COMMENT_ONLY = re.compile(r"^(?://|#|/\*|\*|\*/)")


@dataclass(frozen=True)
class LogicalLine:
    physical_line: int
    text: str


@dataclass(frozen=True)
class Location:
    path: str
    logical_index: int


def normalized_lines(path: Path) -> list[LogicalLine]:
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return []
    result: list[LogicalLine] = []
    for number, raw in enumerate(source.splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or COMMENT_ONLY.match(stripped):
            continue
        normalized = re.sub(r"\s+", " ", stripped)
        result.append(LogicalLine(number, normalized))
    return result


def source_files(source_root: Path, requested_roots: list[str]) -> list[Path]:
    files: set[Path] = set()
    for relative_root in requested_roots:
        scan_root = (source_root / relative_root).resolve()
        if scan_root.is_file():
            if scan_root.suffix.lower() in LANGUAGES or scan_root.name == "CMakeLists.txt":
                files.add(scan_root)
            continue
        if not scan_root.is_dir():
            continue
        for path in scan_root.rglob("*"):
            if (
                path.is_file()
                and (path.suffix.lower() in LANGUAGES or path.name == "CMakeLists.txt")
                and not any(part in IGNORED_PARTS for part in path.parts)
            ):
                files.add(path.resolve())
    for root_cmake in (source_root / "CMakeLists.txt",):
        if root_cmake.is_file():
            files.add(root_cmake.resolve())
    return sorted(files)


def clone_findings(
    source_root: Path,
    files: list[Path],
    minimum_lines: int,
    minimum_characters: int,
) -> tuple[list[dict[str, object]], int]:
    logical_by_path: dict[str, list[LogicalLine]] = {}
    windows: dict[str, list[Location]] = defaultdict(list)
    skipped_common_windows = 0
    for path in files:
        relative = path.relative_to(source_root).as_posix()
        lines = normalized_lines(path)
        logical_by_path[relative] = lines
        for index in range(0, len(lines) - minimum_lines + 1):
            payload = "\n".join(line.text for line in lines[index : index + minimum_lines])
            digest = hashlib.sha256(payload.encode("utf-8")).hexdigest()
            windows[digest].append(Location(relative, index))

    pair_starts: dict[tuple[str, str, int], set[tuple[int, int]]] = defaultdict(set)
    for locations in windows.values():
        if len(locations) < 2:
            continue
        if len(locations) > 40:
            skipped_common_windows += 1
            continue
        ordered = sorted(locations, key=lambda item: (item.path, item.logical_index))
        for left_index, left in enumerate(ordered):
            for right in ordered[left_index + 1 :]:
                if left.path == right.path and right.logical_index - left.logical_index < minimum_lines:
                    continue
                diagonal = right.logical_index - left.logical_index
                pair_starts[(left.path, right.path, diagonal)].add(
                    (left.logical_index, right.logical_index)
                )

    candidates: list[dict[str, object]] = []
    for (left_path, right_path, _), positions in pair_starts.items():
        ordered_positions = sorted(positions)
        run_start = ordered_positions[0]
        previous = run_start
        for position in ordered_positions[1:] + [(sys.maxsize, sys.maxsize)]:
            if position[0] == previous[0] + 1 and position[1] == previous[1] + 1:
                previous = position
                continue
            logical_count = minimum_lines + previous[0] - run_start[0]
            left_lines = logical_by_path[left_path]
            right_lines = logical_by_path[right_path]
            clone_text = "\n".join(
                line.text for line in left_lines[run_start[0] : run_start[0] + logical_count]
            )
            if len(clone_text) >= minimum_characters:
                left_end_index = run_start[0] + logical_count - 1
                right_end_index = run_start[1] + logical_count - 1
                candidates.append(
                    {
                        "fingerprint": hashlib.sha256(clone_text.encode("utf-8")).hexdigest(),
                        "logicalLines": logical_count,
                        "normalizedCharacters": len(clone_text),
                        "left": {
                            "path": left_path,
                            "startLine": left_lines[run_start[0]].physical_line,
                            "endLine": left_lines[left_end_index].physical_line,
                            "language": LANGUAGES.get(Path(left_path).suffix.lower(), "cmake"),
                        },
                        "right": {
                            "path": right_path,
                            "startLine": right_lines[run_start[1]].physical_line,
                            "endLine": right_lines[right_end_index].physical_line,
                            "language": LANGUAGES.get(Path(right_path).suffix.lower(), "cmake"),
                        },
                    }
                )
            run_start = position
            previous = position

    candidates.sort(
        key=lambda item: (
            -int(item["logicalLines"]),
            -int(item["normalizedCharacters"]),
            str(item["left"]["path"]),
            int(item["left"]["startLine"]),
            str(item["right"]["path"]),
            int(item["right"]["startLine"]),
        )
    )
    findings: list[dict[str, object]] = []
    for candidate in candidates:
        contained = False
        for accepted in findings:
            if (
                candidate["left"]["path"] == accepted["left"]["path"]
                and candidate["right"]["path"] == accepted["right"]["path"]
                and candidate["left"]["startLine"] >= accepted["left"]["startLine"]
                and candidate["left"]["endLine"] <= accepted["left"]["endLine"]
                and candidate["right"]["startLine"] >= accepted["right"]["startLine"]
                and candidate["right"]["endLine"] <= accepted["right"]["endLine"]
            ):
                contained = True
                break
        if not contained:
            findings.append(candidate)
    return findings, skipped_common_windows


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--root", action="append", dest="roots")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--minimum-lines", type=int, default=10)
    parser.add_argument("--minimum-characters", type=int, default=240)
    parser.add_argument("--max-findings", type=int, default=500)
    parser.add_argument("--fail-above", type=int)
    parser.add_argument("--fail-production-above", type=int)
    args = parser.parse_args()
    if args.minimum_lines < 3 or args.minimum_characters < 1 or args.max_findings < 1:
        parser.error("minimum-lines must be at least 3 and other numeric limits must be positive")
    if any(
        limit is not None and limit < 0
        for limit in (args.fail_above, args.fail_production_above)
    ):
        parser.error("clone budgets must be non-negative")

    source_root = args.source_root.resolve()
    files = source_files(source_root, args.roots or list(DEFAULT_ROOTS))
    findings, skipped_common_windows = clone_findings(
        source_root, files, args.minimum_lines, args.minimum_characters
    )
    reported = findings[: args.max_findings]
    scope_counts = {"production": 0, "testOnly": 0, "productionTest": 0}
    language_pair_counts: dict[str, int] = defaultdict(int)
    for finding in findings:
        left_test = str(finding["left"]["path"]).startswith("tests/")
        right_test = str(finding["right"]["path"]).startswith("tests/")
        if left_test and right_test:
            scope_counts["testOnly"] += 1
        elif left_test or right_test:
            scope_counts["productionTest"] += 1
        else:
            scope_counts["production"] += 1
        language_pair = f'{finding["left"]["language"]} -> {finding["right"]["language"]}'
        language_pair_counts[language_pair] += 1
    report = {
        "schema": SCHEMA,
        "generatedAt": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "sourceRoot": str(source_root),
        "configuration": {
            "roots": args.roots or list(DEFAULT_ROOTS),
            "minimumLogicalLines": args.minimum_lines,
            "minimumNormalizedCharacters": args.minimum_characters,
            "maxFindings": args.max_findings,
            "maximumCloneCount": args.fail_above,
            "maximumProductionCloneCount": args.fail_production_above,
        },
        "summary": {
            "filesScanned": len(files),
            "cloneCount": len(findings),
            "reportedCloneCount": len(reported),
            "skippedCommonWindowCount": skipped_common_windows,
            "scopeCounts": scope_counts,
            "languagePairCounts": dict(
                sorted(language_pair_counts.items(), key=lambda item: (-item[1], item[0]))
            ),
        },
        "findings": reported,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    failed = False
    if args.fail_above is not None and len(findings) > args.fail_above:
        print(
            f"code-duplication: error: clones={len(findings)} exceeds budget={args.fail_above}",
            file=sys.stderr,
        )
        failed = True
    production_clones = scope_counts["production"]
    if (
        args.fail_production_above is not None
        and production_clones > args.fail_production_above
    ):
        print(
            "code-duplication: error: "
            f"production_clones={production_clones} exceeds "
            f"budget={args.fail_production_above}",
            file=sys.stderr,
        )
        failed = True
    if failed:
        return 1
    print(
        f"code-duplication: files={len(files)} clones={len(findings)} "
        f"reported={len(reported)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
