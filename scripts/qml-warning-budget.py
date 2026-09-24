#!/usr/bin/env python3
"""Fail when qmllint warning categories exceed ratcheted budgets."""

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path


WARNING_PATTERN = re.compile(r"^Warning: .*\[([^\]]+)\]$", re.MULTILINE)


def warning_counts(output: str) -> Counter[str]:
    return Counter(WARNING_PATTERN.findall(output))


def parse_budget(value: str) -> tuple[str, int]:
    category, separator, limit = value.rpartition("=")
    if not separator or not category or not limit.isdigit():
        raise argparse.ArgumentTypeError("warning budgets must use CATEGORY=NONNEGATIVE_INTEGER")
    return category, int(limit)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, help="qmllint output; stdin is used when omitted")
    parser.add_argument(
        "--max-category",
        action="append",
        default=[],
        type=parse_budget,
        metavar="CATEGORY=COUNT",
    )
    args = parser.parse_args()

    output = (
        args.input.read_text(encoding="utf-8", errors="replace")
        if args.input is not None
        else sys.stdin.read()
    )
    counts = warning_counts(output)
    violations = [
        f"{category}={counts[category]} exceeds {limit}"
        for category, limit in args.max_category
        if counts[category] > limit
    ]

    summary = ", ".join(f"{category}={count}" for category, count in counts.most_common())
    print(f"qmllint warnings: total={sum(counts.values())}" + (f" ({summary})" if summary else ""))
    if violations:
        print("qml warning budget failed: " + "; ".join(violations), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
