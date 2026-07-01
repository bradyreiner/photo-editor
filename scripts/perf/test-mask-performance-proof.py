#!/usr/bin/env python3
"""Self-test the masking performance proof runner."""

from __future__ import annotations

import importlib.util
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
RUNNER = REPO_ROOT / "scripts/perf/run-mask-performance-proof.py"


def load_runner() -> Any:
    spec = importlib.util.spec_from_file_location("mask_performance_proof", RUNNER)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Unable to import {RUNNER}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    runner = load_runner()
    items = runner.checklist_items(
        REPO_ROOT / "docs/masking-performance-improvements.md"
    )
    require(
        len(items) >= 50,
        f"masking performance checklist should include at least 50 implemented items, found {len(items)}",
    )

    markers = runner.source_marker_results(runner.REQUIRED_SOURCE_MARKERS)
    failing_markers = [key for key, value in markers.items() if not value["passed"]]
    require(
        not failing_markers,
        f"required source markers should pass: {failing_markers}",
    )

    item_results = runner.item_evidence_results(items, markers)
    require(
        len(item_results) == len(items),
        f"expected one evidence result per implemented item, got {len(item_results)} for {len(items)} items",
    )
    failing_items = [result for result in item_results if not result["passed"]]
    require(not failing_items, f"implemented checklist evidence should pass: {failing_items}")

    unmapped_item = runner.item_evidence_results(["not mapped"], markers)[0]
    require(
        not unmapped_item["passed"] and "unmapped" in unmapped_item["missing_reason"],
        "unmapped checklist items must fail with an unmapped reason",
    )

    failing_marker_results = dict(markers)
    failing_marker_results["canvas_raster_preview_disabled"] = {
        **failing_marker_results["canvas_raster_preview_disabled"],
        "passed": False,
    }
    failing_item = runner.item_evidence_results([items[0]], failing_marker_results)[0]
    require(
        not failing_item["passed"]
        and "canvas_raster_preview_disabled" in failing_item["missing_reason"],
        "checklist item must fail when its mapped source evidence fails",
    )

    passed_status = runner.proof_status(
        implemented_count=50,
        marker_results={"marker": {"passed": True}},
        item_results=[{"passed": True}],
        test_results={"test": {"passed": True}},
        skip_ctest=False,
    )
    require(passed_status == "passed", f"expected passed status, got {passed_status}")

    source_only_status = runner.proof_status(
        implemented_count=50,
        marker_results={"marker": {"passed": True}},
        item_results=[{"passed": True}],
        test_results={},
        skip_ctest=True,
    )
    require(
        source_only_status == "source_only_passed",
        f"expected source_only_passed status, got {source_only_status}",
    )

    weak_count_status = runner.proof_status(
        implemented_count=49,
        marker_results={"marker": {"passed": True}},
        item_results=[{"passed": True}],
        test_results={"test": {"passed": True}},
        skip_ctest=False,
    )
    require(weak_count_status == "failed", "implemented_count below 50 must fail")

    missing_marker_status = runner.proof_status(
        implemented_count=50,
        marker_results={"marker": {"passed": False}},
        item_results=[{"passed": True}],
        test_results={"test": {"passed": True}},
        skip_ctest=False,
    )
    require(missing_marker_status == "failed", "missing source marker must fail")

    failing_item_status = runner.proof_status(
        implemented_count=50,
        marker_results={"marker": {"passed": True}},
        item_results=[{"passed": False}],
        test_results={"test": {"passed": True}},
        skip_ctest=False,
    )
    require(failing_item_status == "failed", "failing item evidence must fail")

    failing_test_status = runner.proof_status(
        implemented_count=50,
        marker_results={"marker": {"passed": True}},
        item_results=[{"passed": True}],
        test_results={"test": {"passed": False}},
        skip_ctest=False,
    )
    require(failing_test_status == "failed", "failing ctest must fail")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
