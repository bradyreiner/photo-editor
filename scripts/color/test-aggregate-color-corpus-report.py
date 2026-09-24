#!/usr/bin/env python3
"""Self-test the complete color-corpus report matrix gate."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import sys
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
AGGREGATOR_PATH = REPO_ROOT / "scripts/color/aggregate-color-corpus-report.py"
VALIDATOR_PATH = REPO_ROOT / "scripts/color/validate-color-corpus.py"
VALIDATOR_TEST_PATH = REPO_ROOT / "scripts/color/test-validate-color-corpus.py"


def load_module(path: Path, name: str) -> Any:
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"unable to import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def file_record(path: Path) -> dict[str, Any]:
    content = path.read_bytes()
    return {
        "path": str(path.resolve()),
        "byte_size": len(content),
        "sha256": hashlib.sha256(content).hexdigest(),
    }


def metric(
    threshold: float, count: int = 16, *, percentile_gate: bool = False
) -> dict[str, Any]:
    value = {
        "count": count,
        "mean": 0.0,
        "p50_upper_bound": 0.0,
        "p95_upper_bound": 0.0,
        "p99_upper_bound": 0.0,
        "max": 0.0,
        "threshold": threshold,
        "gate_passed": True,
    }
    if percentile_gate:
        value["above_threshold_count"] = 0
    return value


def materialize_reports(
    root: Path,
    manifest_path: Path,
    payload: dict[str, Any],
) -> Path:
    report_dir = root / "comparison-reports"
    artifact_dir = root / "comparison-artifacts"
    report_dir.mkdir()
    artifact_dir.mkdir()
    manifest_sha = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
    assets = {asset["asset_id"]: asset for asset in payload["assets"]}
    output_profile = next(
        asset
        for asset in payload["assets"]
        if asset["kind"] == "profile" and asset["profile_role"] == "output"
    )
    for capture in (asset for asset in payload["assets"] if asset["kind"] == "capture"):
        recipe = assets[capture["recipe_id"]]
        for execution_path in sorted(capture["execution_paths"]):
            stem = f"{capture['asset_id']}--{execution_path}"
            candidate = artifact_dir / f"{stem}--candidate.bin"
            reference = artifact_dir / f"{stem}--reference.bin"
            heatmap = artifact_dir / f"{stem}--heatmap.png"
            candidate.write_bytes(f"candidate:{stem}\n".encode("utf-8"))
            reference.write_bytes(f"reference:{stem}\n".encode("utf-8"))
            heatmap.write_bytes(f"heatmap:{stem}\n".encode("utf-8"))
            gate = payload["quality_gates"][execution_path]
            report = {
                "schema": "photo-editor-color-corpus-comparison:v1",
                "tool_version": "1",
                "passed": True,
                "failure": "",
                "comparison_space": "linear Rec.2020 D65",
                "percentile_method": "deterministic test fixture",
                "linear_one_nits": gate["linear_one_nits"],
                "dimensions": {"width": 4, "height": 4},
                "provenance": {
                    "execution_path": execution_path,
                    "capture_id": capture["asset_id"],
                    "candidate_rendering_model": payload["candidate_rendering_model"],
                    "reference_rendering_model": "independent-reference-v1",
                    "recipe_sha256": recipe["sha256"],
                    "output_profile_sha256": output_profile["sha256"],
                    "corpus_manifest_sha256": manifest_sha,
                },
                "candidate": file_record(candidate),
                "reference": file_record(reference),
                "heatmap": file_record(heatmap),
                "metrics": {
                    "delta_e_00_p95": metric(
                        gate["p95_delta_e_00"], percentile_gate=True
                    ),
                    "delta_e_00_max": metric(gate["max_delta_e_00"]),
                    "delta_e_itp_p95": metric(
                        gate["p95_delta_e_itp"], percentile_gate=True
                    ),
                    "delta_e_itp_max": metric(gate["max_delta_e_itp"]),
                    "alpha_error_max": metric(gate["max_alpha_error"]),
                },
                "worst_pixel": {
                    "delta_e_00": {"value": 0.0},
                    "delta_e_itp": {"value": 0.0},
                    "alpha_error": {"value": 0.0},
                },
                "regions": [
                    {"label": region["label"]}
                    for region in capture["review_regions"]
                ],
            }
            write_json(report_dir / f"{stem}.json", report)
    return report_dir


def contains(errors: list[str], fragment: str) -> bool:
    return any(fragment in error for error in errors)


def main() -> int:
    aggregator = load_module(AGGREGATOR_PATH, "color_corpus_aggregator")
    validator = load_module(VALIDATOR_PATH, "color_corpus_validator_fixture")
    fixture_helper = load_module(VALIDATOR_TEST_PATH, "color_corpus_fixture_helper")

    with tempfile.TemporaryDirectory(prefix="photo-editor-color-aggregate-") as temp:
        root = Path(temp)
        manifest_path, payload = fixture_helper.materialize_fixture(root, validator)
        report_dir = materialize_reports(root, manifest_path, payload)
        output_path = root / "aggregate.json"

        errors, summary = aggregator.aggregate(manifest_path, report_dir, output_path)
        require(not errors, f"complete report matrix was rejected: {errors}")
        require(
            summary["passed"] is True
            and summary["expected_report_count"] == 28
            and summary["observed_report_count"] == 28,
            f"complete aggregate summary is incorrect: {summary}",
        )

        first_report_path = sorted(report_dir.glob("*.json"))[0]
        first_report = json.loads(first_report_path.read_text(encoding="utf-8"))
        first_report_path.unlink()
        errors, _ = aggregator.aggregate(manifest_path, report_dir, output_path)
        require(
            contains(errors, "missing report for"),
            f"missing matrix entry was not rejected: {errors}",
        )
        write_json(first_report_path, first_report)

        wrong_threshold = json.loads(first_report_path.read_text(encoding="utf-8"))
        wrong_threshold["metrics"]["delta_e_00_p95"]["threshold"] += 0.25
        write_json(first_report_path, wrong_threshold)
        errors, _ = aggregator.aggregate(manifest_path, report_dir, output_path)
        require(
            contains(errors, "threshold does not match the manifest gate"),
            f"post-hoc threshold change was not rejected: {errors}",
        )
        write_json(first_report_path, first_report)

        heatmap_path = Path(first_report["heatmap"]["path"])
        original_heatmap = heatmap_path.read_bytes()
        heatmap_path.write_bytes(b"X" + original_heatmap[1:])
        errors, _ = aggregator.aggregate(manifest_path, report_dir, output_path)
        require(
            contains(errors, "heatmap.sha256 mismatch"),
            f"tampered heatmap was not rejected: {errors}",
        )
        heatmap_path.write_bytes(original_heatmap)

        duplicate_path = report_dir / "duplicate.json"
        write_json(duplicate_path, first_report)
        errors, _ = aggregator.aggregate(manifest_path, report_dir, output_path)
        require(
            contains(errors, "duplicate report for"),
            f"duplicate path report was not rejected: {errors}",
        )

    print("color_corpus_aggregate_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
