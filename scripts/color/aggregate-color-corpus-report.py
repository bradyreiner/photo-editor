#!/usr/bin/env python3
"""Aggregate and gate a complete licensed color-corpus execution matrix."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = REPO_ROOT / "scripts/color/validate-color-corpus.py"
REPORT_SCHEMA = "photo-editor-color-corpus-comparison:v1"
SUMMARY_SCHEMA = "photo-editor-color-corpus-aggregate:v1"
REQUIRED_PATHS = {
    "camera-input",
    "icc",
    "sdr",
    "hdr",
    "proof",
    "preview",
    "export",
}
METRIC_GATE_MAP = {
    "delta_e_00_p95": "p95_delta_e_00",
    "delta_e_00_max": "max_delta_e_00",
    "delta_e_itp_p95": "p95_delta_e_itp",
    "delta_e_itp_max": "max_delta_e_itp",
    "alpha_error_max": "max_alpha_error",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Require passing, byte-verified reports for every capture and color execution path."
        )
    )
    parser.add_argument("manifest", type=Path, help="Populated color-corpus manifest.")
    parser.add_argument("report_dir", type=Path, help="Directory containing comparison reports.")
    parser.add_argument("--output", type=Path, required=True, help="Aggregate JSON artifact.")
    return parser.parse_args()


def load_validator() -> Any:
    spec = importlib.util.spec_from_file_location("color_corpus_validator", VALIDATOR_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to import {VALIDATOR_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules["color_corpus_validator"] = module
    spec.loader.exec_module(module)
    return module


def load_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def same_number(left: Any, right: Any) -> bool:
    if (
        not isinstance(left, (int, float))
        or isinstance(left, bool)
        or not isinstance(right, (int, float))
        or isinstance(right, bool)
    ):
        return False
    return math.isclose(float(left), float(right), rel_tol=1.0e-12, abs_tol=1.0e-12)


def verify_file_record(
    value: Any,
    label: str,
    errors: list[str],
) -> None:
    if not isinstance(value, dict):
        errors.append(f"{label} must be an object")
        return
    path_value = value.get("path")
    expected_sha = value.get("sha256")
    expected_size = value.get("byte_size")
    if not isinstance(path_value, str) or not path_value:
        errors.append(f"{label}.path must be a non-empty string")
        return
    path = Path(path_value).expanduser()
    if not path.is_absolute():
        errors.append(f"{label}.path must be absolute")
        return
    if not path.is_file():
        errors.append(f"{label}.path does not exist: {path}")
        return
    actual_size = path.stat().st_size
    if not isinstance(expected_size, int) or isinstance(expected_size, bool):
        errors.append(f"{label}.byte_size must be an integer")
    elif expected_size != actual_size:
        errors.append(
            f"{label}.byte_size mismatch: report={expected_size}, actual={actual_size}"
        )
    actual_sha = sha256_file(path)
    if expected_sha != actual_sha:
        errors.append(
            f"{label}.sha256 mismatch: report={expected_sha}, actual={actual_sha}"
        )


def linked_capture_identity(
    capture: dict[str, Any],
    assets: dict[str, dict[str, Any]],
) -> tuple[str | None, set[str]]:
    recipe = assets.get(capture.get("recipe_id"))
    recipe_sha = recipe.get("sha256") if isinstance(recipe, dict) else None
    profile_hashes: set[str] = set()
    for profile_id in capture.get("profile_ids", []):
        profile = assets.get(profile_id)
        if isinstance(profile, dict) and isinstance(profile.get("sha256"), str):
            profile_hashes.add(profile["sha256"])
    return (recipe_sha if isinstance(recipe_sha, str) else None, profile_hashes)


def validate_report(
    report_path: Path,
    report: Any,
    manifest_sha: str,
    candidate_model: str,
    captures: dict[str, dict[str, Any]],
    assets: dict[str, dict[str, Any]],
    quality_gates: dict[str, dict[str, Any]],
    errors: list[str],
) -> tuple[tuple[str, str] | None, dict[str, Any] | None]:
    label = f"report {report_path}"
    if not isinstance(report, dict):
        errors.append(f"{label}: root must be an object")
        return (None, None)
    if report.get("schema") != REPORT_SCHEMA:
        errors.append(f"{label}: schema must be {REPORT_SCHEMA}")
    provenance = report.get("provenance")
    if not isinstance(provenance, dict):
        errors.append(f"{label}: provenance must be an object")
        return (None, None)
    capture_id = provenance.get("capture_id")
    execution_path = provenance.get("execution_path")
    if not isinstance(capture_id, str) or capture_id not in captures:
        errors.append(f"{label}: provenance.capture_id is unknown")
        return (None, None)
    if not isinstance(execution_path, str) or execution_path not in REQUIRED_PATHS:
        errors.append(f"{label}: provenance.execution_path is invalid")
        return (None, None)
    key = (capture_id, execution_path)
    capture = captures[capture_id]
    recipe_sha, profile_hashes = linked_capture_identity(capture, assets)
    if provenance.get("corpus_manifest_sha256") != manifest_sha:
        errors.append(f"{label}: corpus manifest digest does not match")
    if provenance.get("candidate_rendering_model") != candidate_model:
        errors.append(f"{label}: candidate rendering model does not match")
    if provenance.get("recipe_sha256") != recipe_sha:
        errors.append(f"{label}: recipe digest does not match the linked capture recipe")
    if provenance.get("output_profile_sha256") not in profile_hashes:
        errors.append(f"{label}: output profile digest is not linked to the capture")

    gate = quality_gates.get(execution_path)
    if not isinstance(gate, dict):
        errors.append(f"{label}: execution path has no manifest quality gate")
        return (key, None)
    if not same_number(report.get("linear_one_nits"), gate.get("linear_one_nits")):
        errors.append(f"{label}: linear_one_nits does not match the manifest gate")
    if report.get("comparison_space") != "linear Rec.2020 D65":
        errors.append(f"{label}: comparison_space must be linear Rec.2020 D65")
    if not isinstance(report.get("percentile_method"), str) or not report.get(
        "percentile_method"
    ):
        errors.append(f"{label}: percentile_method must be recorded")
    dimensions = report.get("dimensions")
    expected_pixel_count: int | None = None
    if not isinstance(dimensions, dict):
        errors.append(f"{label}: dimensions must be an object")
    else:
        width = dimensions.get("width")
        height = dimensions.get("height")
        if (
            not isinstance(width, int)
            or isinstance(width, bool)
            or width <= 0
            or not isinstance(height, int)
            or isinstance(height, bool)
            or height <= 0
        ):
            errors.append(f"{label}: dimensions must contain positive integer width and height")
        else:
            expected_pixel_count = width * height
    metrics = report.get("metrics")
    if not isinstance(metrics, dict):
        errors.append(f"{label}: metrics must be an object")
        metrics = {}
    for report_metric, manifest_metric in METRIC_GATE_MAP.items():
        metric = metrics.get(report_metric)
        metric_label = f"{label}: metrics.{report_metric}"
        if not isinstance(metric, dict):
            errors.append(f"{metric_label} must be an object")
            continue
        if not same_number(metric.get("threshold"), gate.get(manifest_metric)):
            errors.append(f"{metric_label}.threshold does not match the manifest gate")
        if metric.get("gate_passed") is not True:
            errors.append(f"{metric_label}.gate_passed must be true")
        for numeric_key in ("count", "mean", "p95_upper_bound", "max"):
            numeric = metric.get(numeric_key)
            if (
                not isinstance(numeric, (int, float))
                or isinstance(numeric, bool)
                or not math.isfinite(float(numeric))
                or float(numeric) < 0.0
            ):
                errors.append(f"{metric_label}.{numeric_key} must be finite and nonnegative")
        count = metric.get("count")
        if expected_pixel_count is not None and count != expected_pixel_count:
            errors.append(
                f"{metric_label}.count must equal width x height ({expected_pixel_count})"
            )
        if report_metric.endswith("_p95") and isinstance(count, int):
            above_count = metric.get("above_threshold_count")
            if (
                not isinstance(above_count, int)
                or isinstance(above_count, bool)
                or above_count < 0
                or above_count > count
            ):
                errors.append(f"{metric_label}.above_threshold_count is invalid")
            else:
                required_at_or_below = math.ceil(0.95 * count)
                if count - above_count < required_at_or_below:
                    errors.append(
                        f"{metric_label} contradicts its exact p95 threshold exceedance count"
                    )
        if report_metric.endswith("_max") and isinstance(metric.get("max"), (int, float)):
            if float(metric["max"]) > float(gate[manifest_metric]):
                errors.append(f"{metric_label}.max exceeds the manifest gate")
    if report.get("passed") is not True:
        errors.append(f"{label}: passed must be true")
    if report.get("failure") not in {"", None}:
        errors.append(f"{label}: failure must be empty")
    for artifact_name in ("candidate", "reference", "heatmap"):
        verify_file_record(report.get(artifact_name), f"{label}: {artifact_name}", errors)

    worst_pixel = report.get("worst_pixel")
    if not isinstance(worst_pixel, dict):
        errors.append(f"{label}: worst_pixel must be an object")
    else:
        for worst_name in ("delta_e_00", "delta_e_itp", "alpha_error"):
            worst = worst_pixel.get(worst_name)
            if not isinstance(worst, dict):
                errors.append(f"{label}: worst_pixel.{worst_name} must be an object")
                continue
            value = worst.get("value")
            if (
                not isinstance(value, (int, float))
                or isinstance(value, bool)
                or not math.isfinite(float(value))
                or float(value) < 0.0
            ):
                errors.append(f"{label}: worst_pixel.{worst_name}.value is invalid")
    regions = report.get("regions")
    expected_region_labels = {
        region.get("label")
        for region in capture.get("review_regions", [])
        if isinstance(region, dict) and isinstance(region.get("label"), str)
    }
    if not isinstance(regions, list):
        errors.append(f"{label}: regions must be an array")
    else:
        observed_region_labels = {
            region.get("label")
            for region in regions
            if isinstance(region, dict) and isinstance(region.get("label"), str)
        }
        if observed_region_labels != expected_region_labels or len(regions) != len(
            expected_region_labels
        ):
            errors.append(f"{label}: regions do not match the capture review regions")

    delta_e_00 = metrics.get("delta_e_00_max", {})
    delta_e_itp = metrics.get("delta_e_itp_max", {})
    row = {
        "capture_id": capture_id,
        "execution_path": execution_path,
        "report_path": str(report_path.resolve()),
        "report_sha256": sha256_file(report_path),
        "delta_e_00_p95_upper_bound": metrics.get("delta_e_00_p95", {}).get(
            "p95_upper_bound"
        ),
        "delta_e_00_max": delta_e_00.get("max"),
        "delta_e_itp_p95_upper_bound": metrics.get("delta_e_itp_p95", {}).get(
            "p95_upper_bound"
        ),
        "delta_e_itp_max": delta_e_itp.get("max"),
        "alpha_error_max": metrics.get("alpha_error_max", {}).get("max"),
    }
    return (key, row)


def aggregate(
    manifest_path: Path,
    report_dir: Path,
    output_path: Path,
) -> tuple[list[str], dict[str, Any]]:
    errors: list[str] = []
    validator = load_validator()
    manifest_path = manifest_path.resolve()
    report_dir = report_dir.resolve()
    output_path = output_path.resolve()
    manifest_errors = validator.validate_manifest(manifest_path)
    errors.extend(f"manifest: {error}" for error in manifest_errors)
    try:
        manifest = load_json(manifest_path)
    except (OSError, json.JSONDecodeError) as exc:
        return ([f"unable to read manifest: {exc}"], {})
    if not isinstance(manifest, dict):
        return (["manifest root must be an object"], {})
    if not report_dir.is_dir():
        errors.append(f"report directory does not exist: {report_dir}")

    assets = {
        asset["asset_id"]: asset
        for asset in manifest.get("assets", [])
        if isinstance(asset, dict) and isinstance(asset.get("asset_id"), str)
    }
    captures = {
        asset_id: asset
        for asset_id, asset in assets.items()
        if asset.get("kind") == "capture"
    }
    expected_pairs: set[tuple[str, str]] = set()
    for capture_id, capture in captures.items():
        paths = set(capture.get("execution_paths", []))
        missing_for_capture = REQUIRED_PATHS - paths
        for path in sorted(missing_for_capture):
            errors.append(f"capture {capture_id} does not declare required path {path}")
        expected_pairs.update((capture_id, path) for path in REQUIRED_PATHS)

    manifest_sha = sha256_file(manifest_path)
    quality_gates = manifest.get("quality_gates", {})
    rows: list[dict[str, Any]] = []
    seen: dict[tuple[str, str], Path] = {}
    if report_dir.is_dir():
        for report_path in sorted(report_dir.rglob("*.json")):
            if report_path.resolve() == output_path:
                continue
            try:
                report = load_json(report_path)
            except (OSError, json.JSONDecodeError) as exc:
                errors.append(f"unable to read report {report_path}: {exc}")
                continue
            key, row = validate_report(
                report_path,
                report,
                manifest_sha,
                manifest.get("candidate_rendering_model", ""),
                captures,
                assets,
                quality_gates,
                errors,
            )
            if key is None:
                continue
            if key in seen:
                errors.append(
                    f"duplicate report for {key[0]} / {key[1]}: "
                    f"{seen[key]} and {report_path}"
                )
            else:
                seen[key] = report_path
            if row is not None:
                rows.append(row)

    for capture_id, execution_path in sorted(expected_pairs - set(seen)):
        errors.append(f"missing report for {capture_id} / {execution_path}")
    for capture_id, execution_path in sorted(set(seen) - expected_pairs):
        errors.append(f"unexpected report for {capture_id} / {execution_path}")

    def finite_values(key: str) -> list[float]:
        return [
            float(row[key])
            for row in rows
            if isinstance(row.get(key), (int, float))
            and not isinstance(row.get(key), bool)
            and math.isfinite(float(row[key]))
        ]

    summary = {
        "schema": SUMMARY_SCHEMA,
        "passed": not errors,
        "manifest_path": str(manifest_path),
        "manifest_sha256": manifest_sha,
        "candidate_rendering_model": manifest.get("candidate_rendering_model"),
        "capture_count": len(captures),
        "required_path_count": len(REQUIRED_PATHS),
        "expected_report_count": len(expected_pairs),
        "observed_report_count": len(seen),
        "errors": errors,
        "global_worst": {
            "delta_e_00_max": max(finite_values("delta_e_00_max"), default=None),
            "delta_e_itp_max": max(finite_values("delta_e_itp_max"), default=None),
            "alpha_error_max": max(finite_values("alpha_error_max"), default=None),
        },
        "reports": sorted(rows, key=lambda row: (row["capture_id"], row["execution_path"])),
    }
    return (errors, summary)


def main() -> int:
    args = parse_args()
    errors, summary = aggregate(args.manifest, args.report_dir, args.output)
    if summary:
        write_json(args.output.resolve(), summary)
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print(
        "color corpus aggregate ok: "
        f"{summary['capture_count']} captures x {summary['required_path_count']} paths, "
        f"{summary['observed_report_count']} byte-verified reports"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
