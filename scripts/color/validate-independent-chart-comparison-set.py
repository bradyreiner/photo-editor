#!/usr/bin/env python3
"""Validate the aggregated 48-cell independent chart comparison evidence."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any


RENDER_SET_VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-render-set.py"
)
SCHEMA = "photo-editor-independent-chart-comparison-set:v1"
TOOL_VERSION = "photo-editor-independent-chart-comparison-runner:v1"
CONTRACT_SCHEMA = "photo-editor-independent-chart-comparison-contract:v1"
NATIVE_REPORT_SCHEMA = "photo-editor-color-corpus-comparison:v1"
HASH_ALGORITHM = (
    "sha256(sorted(logical_path + NUL + byte_size + NUL + file_sha256 + LF))"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
GATES = {
    "linear_one_nits": 100,
    "p95_delta_e_00": 0.25,
    "max_delta_e_00": 0.75,
    "p95_delta_e_itp": 0.5,
    "max_delta_e_itp": 1.5,
    "max_alpha_error": 0.0001,
}
ROOT_KEYS = {
    "schema", "tool_version", "passed", "failures", "source",
    "quality_gates", "matrix", "summary", "artifact_summary", "cells",
}
SOURCE_KEYS = {
    "render_set_manifest_sha256", "render_set_artifact_set_sha256",
    "comparison_contract_sha256", "comparison_tool_sha256",
}
SUMMARY_KEYS = {
    "cell_count", "passed_cell_count", "pixel_count",
    "worst_p95_delta_e_00_upper", "worst_max_delta_e_00",
    "worst_p95_delta_e_itp_upper", "worst_max_delta_e_itp",
    "worst_max_alpha_error",
}
CELL_KEYS = {
    "cell_id", "camera_directory", "illuminant_code", "exposure_code",
    "candidate_sha256", "reference_sha256", "report_id", "report_sha256",
    "heatmap_id", "heatmap_sha256", "passed", "pixel_count",
    "p95_delta_e_00_upper", "max_delta_e_00", "p95_delta_e_itp_upper",
    "max_delta_e_itp", "max_alpha_error",
}
SUMMARY_FIELDS = (
    "p95_delta_e_00_upper", "max_delta_e_00", "p95_delta_e_itp_upper",
    "max_delta_e_itp", "max_alpha_error",
)
WORST_KEYS = {
    "p95_delta_e_00_upper": "worst_p95_delta_e_00_upper",
    "max_delta_e_00": "worst_max_delta_e_00",
    "p95_delta_e_itp_upper": "worst_p95_delta_e_itp_upper",
    "max_delta_e_itp": "worst_max_delta_e_itp",
    "max_alpha_error": "worst_max_alpha_error",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Validate independent chart comparison evidence."
  )
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--artifact-root", type=Path, required=True)
  parser.add_argument("--render-set-root", type=Path, required=True)
  parser.add_argument("--tagged-set-manifest", type=Path, required=True)
  return parser.parse_args()


def load_module(path: Path, name: str) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load module: {path}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def load_json(path: Path, label: str, errors: list[str]) -> dict[str, Any] | None:
  try:
    with path.open("r", encoding="utf-8") as handle:
      value = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    errors.append(f"unable to read {label}: {exc}")
    return None
  if not isinstance(value, dict):
    errors.append(f"{label} root must be an object")
    return None
  return value


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def artifact_set_digest(root: Path, paths: list[str]) -> tuple[int, str]:
  total = 0
  digest = hashlib.sha256()
  for logical_path in sorted(paths):
    path = root / logical_path
    size = path.stat().st_size
    digest.update(logical_path.encode("utf-8"))
    digest.update(b"\0")
    digest.update(str(size).encode("ascii"))
    digest.update(b"\0")
    digest.update(sha256_file(path).encode("ascii"))
    digest.update(b"\n")
    total += size
  return total, digest.hexdigest()


def exact_keys(
    value: Any, keys: set[str], label: str, errors: list[str]
) -> dict[str, Any] | None:
  if not isinstance(value, dict):
    errors.append(f"{label} must be an object")
    return None
  for key in sorted(set(value) - keys):
    errors.append(f"{label}: unknown field {key!r}")
  for key in sorted(keys - set(value)):
    errors.append(f"{label}: missing required field {key!r}")
  return value


def require_sha256(value: Any, label: str, errors: list[str]) -> None:
  if not isinstance(value, str) or SHA256_PATTERN.fullmatch(value) is None:
    errors.append(f"{label} must be a lowercase SHA-256 digest")


def safe_path(value: Any, prefix: str) -> bool:
  if not isinstance(value, str) or not value or "\\" in value or ":" in value:
    return False
  path = PurePosixPath(value)
  return (
      not path.is_absolute() and str(path) == value and value.startswith(prefix) and
      all(part not in ("", ".", "..") for part in path.parts)
  )


def expected_cell_id(cell: dict[str, Any]) -> str:
  return (
      f"chart-{cell['camera_directory']}-{cell['illuminant_code']}-"
      f"{cell['exposure_code']}"
  )


def native_metric(
    report: dict[str, Any], key: str, field: str, label: str,
    errors: list[str],
) -> float | None:
  value = (report.get("metrics") or {}).get(key, {}).get(field)
  if not isinstance(value, (int, float)):
    errors.append(f"{label}.{key}.{field} is missing")
    return None
  return float(value)


def validate_manifest(
    manifest_path: Path, *, artifact_root: Path, render_set_root: Path,
    tagged_set_manifest_path: Path,
) -> list[str]:
  errors: list[str] = []
  render_validator = load_module(
      RENDER_SET_VALIDATOR_PATH, "independent_chart_render_set_validator"
  )
  render_set_path = render_set_root / "render-set.json"
  render_errors = render_validator.validate_manifest(
      render_set_path, tagged_set_manifest_path=tagged_set_manifest_path,
      artifact_root=render_set_root,
  )
  errors.extend(f"render-set: {error}" for error in render_errors)
  manifest = load_json(manifest_path, "comparison-set manifest", errors)
  render_set = load_json(render_set_path, "render-set manifest", errors)
  if manifest is None or render_set is None:
    return errors
  exact_keys(manifest, ROOT_KEYS, "manifest", errors)
  if manifest.get("schema") != SCHEMA:
    errors.append(f"manifest.schema must be {SCHEMA!r}")
  if manifest.get("tool_version") != TOOL_VERSION:
    errors.append("manifest.tool_version is unsupported")
  source = exact_keys(manifest.get("source"), SOURCE_KEYS, "source", errors) or {}
  expected_source = {
      "render_set_manifest_sha256": sha256_file(render_set_path),
      "render_set_artifact_set_sha256": render_set["artifact_summary"][
          "artifact_set_sha256"
      ],
  }
  for key, value in expected_source.items():
    if source.get(key) != value:
      errors.append(f"source.{key} does not match the render set")
  for key in SOURCE_KEYS:
    require_sha256(source.get(key), f"source.{key}", errors)
  if manifest.get("quality_gates") != GATES:
    errors.append("quality_gates must equal the precommitted chart thresholds")
  if manifest.get("matrix") != render_set.get("matrix"):
    errors.append("comparison matrix differs from the render set")

  contract_path = artifact_root / "comparison-contract.json"
  contract = load_json(contract_path, "comparison contract", errors)
  if contract is not None:
    if contract.get("schema") != CONTRACT_SCHEMA:
      errors.append("comparison contract schema mismatch")
    if contract.get("render_set_manifest_sha256") != sha256_file(render_set_path):
      errors.append("comparison contract does not bind the render-set manifest")
    if contract.get("quality_gates") != {"sdr": GATES}:
      errors.append("comparison contract gates differ from the aggregate")
    assets = contract.get("assets")
    if not isinstance(assets, list) or len(assets) != 50:
      errors.append("comparison contract must contain 48 captures plus recipe/profile")
    if sha256_file(contract_path) != source.get("comparison_contract_sha256"):
      errors.append("comparison contract hash mismatch")

  render_cells = {
      expected_cell_id(cell): cell for cell in render_set.get("cells", [])
      if isinstance(cell, dict)
  }
  cells = manifest.get("cells")
  if not isinstance(cells, list) or len(cells) != 48:
    errors.append("cells must contain exactly 48 comparisons")
    cells = []
  seen: set[str] = set()
  artifact_paths = ["comparison-contract.json"]
  for index, value in enumerate(cells):
    label = f"cells[{index}]"
    cell = exact_keys(value, CELL_KEYS, label, errors) or {}
    identifier = cell.get("cell_id")
    if not isinstance(identifier, str) or identifier in seen:
      errors.append(f"{label}.cell_id is invalid or duplicated")
    elif identifier not in render_cells:
      errors.append(f"{label}.cell_id is outside the render set")
    seen.add(identifier)
    render_cell = render_cells.get(identifier, {})
    expected_identity = {
        "camera_directory": render_cell.get("camera_directory"),
        "illuminant_code": render_cell.get("illuminant_code"),
        "exposure_code": render_cell.get("exposure_code"),
        "candidate_sha256": render_cell.get("candidate_sha256"),
        "reference_sha256": render_cell.get("reference_sha256"),
        "pixel_count": 606 * 404,
    }
    for key, expected in expected_identity.items():
      if cell.get(key) != expected:
        errors.append(f"{label}.{key} does not match its render-set cell")
    for key in (
        "candidate_sha256", "reference_sha256", "report_sha256",
        "heatmap_sha256",
    ):
      require_sha256(cell.get(key), f"{label}.{key}", errors)
    report_id = cell.get("report_id")
    heatmap_id = cell.get("heatmap_id")
    if not safe_path(report_id, "reports/"):
      errors.append(f"{label}.report_id is unsafe")
    if not safe_path(heatmap_id, "heatmaps/"):
      errors.append(f"{label}.heatmap_id is unsafe")
    if isinstance(report_id, str):
      artifact_paths.append(report_id)
    if isinstance(heatmap_id, str):
      artifact_paths.append(heatmap_id)
    if not isinstance(report_id, str) or not isinstance(heatmap_id, str):
      continue
    report_path = artifact_root / report_id
    heatmap_path = artifact_root / heatmap_id
    if not report_path.is_file():
      errors.append(f"{label}: comparison report is missing")
      continue
    if not heatmap_path.is_file():
      errors.append(f"{label}: heatmap is missing")
      continue
    if sha256_file(report_path) != cell.get("report_sha256"):
      errors.append(f"{label}: report hash mismatch")
    if sha256_file(heatmap_path) != cell.get("heatmap_sha256"):
      errors.append(f"{label}: heatmap hash mismatch")
    report = load_json(report_path, f"{label}.report", errors)
    if report is None:
      continue
    if report.get("schema") != NATIVE_REPORT_SCHEMA:
      errors.append(f"{label}: native report schema mismatch")
    if report.get("passed") is not True or cell.get("passed") is not True:
      errors.append(f"{label}: comparison is not passing")
    candidate = report.get("candidate") or {}
    reference = report.get("reference") or {}
    if candidate.get("sha256") != cell.get("candidate_sha256"):
      errors.append(f"{label}: native candidate hash mismatch")
    if reference.get("sha256") != cell.get("reference_sha256"):
      errors.append(f"{label}: native reference hash mismatch")
    provenance = report.get("provenance") or {}
    expected_provenance = {
        "execution_path": "sdr", "capture_id": identifier,
        "candidate_rendering_model": "aces2-ocio-v1",
        "reference_rendering_model": (
            "ocio-2.5.1-direct-display-view-float-v2"
        ),
        "recipe_sha256": render_set["recipe"]["sha256"],
        "output_profile_sha256": render_set["output_profile"]["icc_sha256"],
        "corpus_manifest_sha256": source.get("comparison_contract_sha256"),
    }
    if provenance != expected_provenance:
      errors.append(f"{label}: native provenance mismatch")
    heatmap = report.get("heatmap") or {}
    if heatmap.get("sha256") != cell.get("heatmap_sha256"):
      errors.append(f"{label}: native heatmap hash mismatch")
    metric_mapping = {
        "p95_delta_e_00_upper": ("delta_e_00_p95", "p95_upper_bound"),
        "max_delta_e_00": ("delta_e_00_max", "max"),
        "p95_delta_e_itp_upper": ("delta_e_itp_p95", "p95_upper_bound"),
        "max_delta_e_itp": ("delta_e_itp_max", "max"),
        "max_alpha_error": ("alpha_error_max", "max"),
    }
    for field, (metric, metric_field) in metric_mapping.items():
      value = native_metric(report, metric, metric_field, label, errors)
      if value is not None and cell.get(field) != value:
        errors.append(f"{label}.{field} differs from the native report")

  if seen != set(render_cells):
    errors.append("comparison cells do not equal the complete render-set matrix")
  if len(artifact_paths) != len(set(artifact_paths)):
    errors.append("comparison artifact paths must be globally unique")
  failures = manifest.get("failures")
  if not isinstance(failures, list):
    errors.append("failures must be an array")
    failures = []
  passed_cells = sum(1 for cell in cells if cell.get("passed") is True)
  expected_passed = len(cells) == 48 and passed_cells == 48 and not failures
  if manifest.get("passed") is not expected_passed:
    errors.append("manifest.passed contradicts cell and failure evidence")
  summary = exact_keys(manifest.get("summary"), SUMMARY_KEYS, "summary", errors) or {}
  expected_counts = {
      "cell_count": len(cells), "passed_cell_count": passed_cells,
      "pixel_count": sum(
          cell.get("pixel_count", 0) for cell in cells
          if isinstance(cell.get("pixel_count"), int)
      ),
  }
  for key, expected in expected_counts.items():
    if summary.get(key) != expected:
      errors.append(f"summary.{key} contradicts cell evidence")
  for field in SUMMARY_FIELDS:
    summary_key = WORST_KEYS[field]
    worst = exact_keys(
        summary.get(summary_key), {"cell_id", "value"}, summary_key, errors
    ) or {}
    if cells:
      expected = max(cells, key=lambda item: item.get(field, float("-inf")))
      if worst != {"cell_id": expected.get("cell_id"), "value": expected.get(field)}:
        errors.append(f"summary.{summary_key} contradicts cell evidence")
  artifact_summary = exact_keys(
      manifest.get("artifact_summary"),
      {"artifact_count", "byte_size", "artifact_set_sha256", "hash_algorithm"},
      "artifact_summary", errors,
  ) or {}
  if artifact_summary.get("artifact_count") != 97:
    errors.append("artifact_summary.artifact_count must be 97")
  if artifact_summary.get("hash_algorithm") != HASH_ALGORITHM:
    errors.append("artifact_summary.hash_algorithm is unsupported")
  require_sha256(
      artifact_summary.get("artifact_set_sha256"),
      "artifact_summary.artifact_set_sha256", errors,
  )
  if len(artifact_paths) == 97 and all(
      (artifact_root / path).is_file() for path in artifact_paths
  ):
    byte_size, digest = artifact_set_digest(artifact_root, artifact_paths)
    if byte_size != artifact_summary.get("byte_size"):
      errors.append("artifact_summary.byte_size does not match artifacts")
    if digest != artifact_summary.get("artifact_set_sha256"):
      errors.append("artifact_summary.artifact_set_sha256 does not match artifacts")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(
      args.manifest.resolve(), artifact_root=args.artifact_root.resolve(),
      render_set_root=args.render_set_root.resolve(),
      tagged_set_manifest_path=args.tagged_set_manifest.resolve(),
  )
  if errors:
    for error in errors:
      print(f"ERROR: {error}", file=sys.stderr)
    return 1
  print(f"valid independent chart comparison set: {args.manifest.resolve()}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
