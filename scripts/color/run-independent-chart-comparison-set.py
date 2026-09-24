#!/usr/bin/env python3
"""Compare all 48 shipping chart renders with their independent OCIO references."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import shutil
import subprocess
import sys
from pathlib import Path
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
GATES = {
    "linear_one_nits": 100,
    "p95_delta_e_00": 0.25,
    "max_delta_e_00": 0.75,
    "p95_delta_e_itp": 0.5,
    "max_delta_e_itp": 1.5,
    "max_alpha_error": 0.0001,
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Run the strict 48-cell independent chart comparison gate."
  )
  parser.add_argument("--render-set-root", type=Path, required=True)
  parser.add_argument("--tagged-set-manifest", type=Path, required=True)
  parser.add_argument("--comparison-tool", type=Path, required=True)
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument("--force", action="store_true")
  return parser.parse_args()


def load_module(path: Path, name: str) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load module: {path}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def load_json(path: Path) -> dict[str, Any]:
  with path.open("r", encoding="utf-8") as handle:
    value = json.load(handle)
  if not isinstance(value, dict):
    raise ValueError(f"JSON root must be an object: {path}")
  return value


def write_json(path: Path, value: dict[str, Any]) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(
      json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
  )


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


def cell_id(cell: dict[str, Any]) -> str:
  return (
      f"chart-{cell['camera_directory']}-{cell['illuminant_code']}-"
      f"{cell['exposure_code']}"
  )


def comparison_contract(render_set: dict[str, Any]) -> dict[str, Any]:
  assets: list[dict[str, Any]] = [
      {
          "kind": "recipe", "asset_id": "chart-aces2-desktop-export-recipe",
          "sha256": render_set["recipe"]["sha256"],
          "rendering_model": "aces2-ocio-v1",
      },
      {
          "kind": "profile", "asset_id": "chart-srgb-output-profile",
          "sha256": render_set["output_profile"]["icc_sha256"],
      },
  ]
  for cell in render_set["cells"]:
    assets.append({
        "kind": "capture", "asset_id": cell_id(cell),
        "recipe_id": "chart-aces2-desktop-export-recipe",
        "profile_ids": ["chart-srgb-output-profile"],
        "review_regions": [{
            "label": "complete registered ColorChecker",
            "x": 0, "y": 0, "width": 1, "height": 1,
            "expected_content": "All registered chart pixels",
        }],
    })
  return {
      "schema": CONTRACT_SCHEMA,
      "render_set_manifest_sha256": "pending",
      "quality_gates": {"sdr": GATES},
      "assets": assets,
  }


def metric_value(report: dict[str, Any], key: str, field: str) -> float:
  value = report.get("metrics", {}).get(key, {}).get(field)
  if not isinstance(value, (int, float)):
    raise ValueError(f"comparison report metric {key}.{field} is missing")
  return float(value)


def validate_native_report(
    report: dict[str, Any], cell: dict[str, Any], capture_id: str,
    contract_sha256: str, recipe_sha256: str, profile_sha256: str,
    heatmap_path: Path,
) -> list[str]:
  errors: list[str] = []
  if report.get("schema") != NATIVE_REPORT_SCHEMA:
    errors.append("native comparison schema mismatch")
  if report.get("dimensions") != {"width": 606, "height": 404}:
    errors.append("native comparison dimensions mismatch")
  if report.get("linear_one_nits") != 100:
    errors.append("native comparison luminance scale mismatch")
  candidate = report.get("candidate") or {}
  reference = report.get("reference") or {}
  if candidate.get("sha256") != cell["candidate_sha256"]:
    errors.append("candidate hash does not match render set")
  if reference.get("sha256") != cell["reference_sha256"]:
    errors.append("reference hash does not match render set")
  provenance = report.get("provenance") or {}
  expected_provenance = {
      "execution_path": "sdr", "capture_id": capture_id,
      "candidate_rendering_model": "aces2-ocio-v1",
      "reference_rendering_model": "ocio-2.5.1-direct-display-view-float-v2",
      "recipe_sha256": recipe_sha256,
      "output_profile_sha256": profile_sha256,
      "corpus_manifest_sha256": contract_sha256,
  }
  if provenance != expected_provenance:
    errors.append("native comparison provenance mismatch")
  heatmap = report.get("heatmap") or {}
  if not heatmap_path.is_file() or heatmap.get("sha256") != sha256_file(heatmap_path):
    errors.append("heatmap hash mismatch")
  expected_count = 606 * 404
  for key in (
      "delta_e_00_p95", "delta_e_00_max", "delta_e_itp_p95",
      "delta_e_itp_max", "alpha_error_max",
  ):
    metric = report.get("metrics", {}).get(key) or {}
    if metric.get("count") != expected_count:
      errors.append(f"{key} pixel count mismatch")
    if metric.get("gate_passed") is not True:
      errors.append(f"{key} gate failed")
  if report.get("passed") is not True:
    errors.append("native comparison did not pass")
  return errors


def run_set(
    render_set_root: Path, tagged_set_manifest: Path, comparison_tool: Path,
    output: Path, *, force: bool,
) -> dict[str, Any]:
  render_set_path = render_set_root / "render-set.json"
  validator = load_module(
      RENDER_SET_VALIDATOR_PATH, "independent_chart_render_set_validator"
  )
  render_errors = validator.validate_manifest(
      render_set_path, tagged_set_manifest_path=tagged_set_manifest,
      artifact_root=render_set_root,
  )
  if render_errors:
    raise ValueError("render-set preflight failed: " + "; ".join(render_errors))
  if not comparison_tool.is_file():
    raise ValueError(f"native comparison tool is missing: {comparison_tool}")
  if output.exists() and not force:
    raise ValueError(f"output already exists: {output}; pass --force to replace it")
  render_set = load_json(render_set_path)
  staging = output.with_name(f".{output.name}.partial")
  shutil.rmtree(staging, ignore_errors=True)
  staging.mkdir(parents=True)
  contract = comparison_contract(render_set)
  contract["render_set_manifest_sha256"] = sha256_file(render_set_path)
  contract_path = staging / "comparison-contract.json"
  write_json(contract_path, contract)
  contract_sha256 = sha256_file(contract_path)
  results: list[dict[str, Any]] = []
  failures: list[str] = []
  artifact_paths = ["comparison-contract.json"]
  try:
    for cell in render_set["cells"]:
      capture_id = cell_id(cell)
      report_id = f"reports/{capture_id}.json"
      heatmap_id = f"heatmaps/{capture_id}.png"
      report_path = staging / report_id
      heatmap_path = staging / heatmap_id
      report_path.parent.mkdir(parents=True, exist_ok=True)
      heatmap_path.parent.mkdir(parents=True, exist_ok=True)
      process = subprocess.run(
          [
              str(comparison_tool), "--candidate",
              str(render_set_root / cell["candidate_id"]), "--reference",
              str(render_set_root / cell["reference_id"]), "--report",
              str(report_path), "--heatmap", str(heatmap_path),
              "--corpus-manifest", str(contract_path), "--capture-id", capture_id,
              "--execution-path", "sdr", "--candidate-rendering-model",
              "aces2-ocio-v1", "--reference-rendering-model",
              "ocio-2.5.1-direct-display-view-float-v2", "--recipe-sha256",
              render_set["recipe"]["sha256"], "--output-profile-sha256",
              render_set["output_profile"]["icc_sha256"],
          ],
          check=False, capture_output=True, text=True,
      )
      cell_errors: list[str] = []
      report: dict[str, Any] | None = None
      if not report_path.is_file():
        detail = process.stderr.strip() or process.stdout.strip()
        cell_errors.append(f"native tool did not write a report: {detail}")
      else:
        report = load_json(report_path)
        cell_errors.extend(validate_native_report(
            report, cell, capture_id, contract_sha256,
            render_set["recipe"]["sha256"],
            render_set["output_profile"]["icc_sha256"], heatmap_path,
        ))
      if process.returncode != 0:
        cell_errors.append(f"native tool exit code {process.returncode}")
      if cell_errors:
        failures.extend(f"{capture_id}: {error}" for error in cell_errors)
      if report is not None and heatmap_path.is_file():
        result = {
            "cell_id": capture_id,
            "camera_directory": cell["camera_directory"],
            "illuminant_code": cell["illuminant_code"],
            "exposure_code": cell["exposure_code"],
            "candidate_sha256": cell["candidate_sha256"],
            "reference_sha256": cell["reference_sha256"],
            "report_id": report_id, "report_sha256": sha256_file(report_path),
            "heatmap_id": heatmap_id, "heatmap_sha256": sha256_file(heatmap_path),
            "passed": not cell_errors,
            "pixel_count": int(metric_value(report, "delta_e_00_max", "count")),
            "p95_delta_e_00_upper": metric_value(
                report, "delta_e_00_p95", "p95_upper_bound"
            ),
            "max_delta_e_00": metric_value(report, "delta_e_00_max", "max"),
            "p95_delta_e_itp_upper": metric_value(
                report, "delta_e_itp_p95", "p95_upper_bound"
            ),
            "max_delta_e_itp": metric_value(report, "delta_e_itp_max", "max"),
            "max_alpha_error": metric_value(report, "alpha_error_max", "max"),
        }
        results.append(result)
        artifact_paths.extend((report_id, heatmap_id))

    def worst(field: str) -> dict[str, Any]:
      if not results:
        return {"cell_id": "", "value": None}
      selected = max(results, key=lambda item: item[field])
      return {"cell_id": selected["cell_id"], "value": selected[field]}

    artifact_bytes, artifact_digest = artifact_set_digest(staging, artifact_paths)
    aggregate = {
        "schema": SCHEMA, "tool_version": TOOL_VERSION,
        "passed": not failures and len(results) == 48,
        "failures": failures,
        "source": {
            "render_set_manifest_sha256": sha256_file(render_set_path),
            "render_set_artifact_set_sha256": render_set["artifact_summary"][
                "artifact_set_sha256"
            ],
            "comparison_contract_sha256": contract_sha256,
            "comparison_tool_sha256": sha256_file(comparison_tool),
        },
        "quality_gates": GATES,
        "matrix": render_set["matrix"],
        "summary": {
            "cell_count": len(results),
            "passed_cell_count": sum(1 for result in results if result["passed"]),
            "pixel_count": sum(result["pixel_count"] for result in results),
            "worst_p95_delta_e_00_upper": worst("p95_delta_e_00_upper"),
            "worst_max_delta_e_00": worst("max_delta_e_00"),
            "worst_p95_delta_e_itp_upper": worst("p95_delta_e_itp_upper"),
            "worst_max_delta_e_itp": worst("max_delta_e_itp"),
            "worst_max_alpha_error": worst("max_alpha_error"),
        },
        "artifact_summary": {
            "artifact_count": len(artifact_paths), "byte_size": artifact_bytes,
            "artifact_set_sha256": artifact_digest,
            "hash_algorithm": HASH_ALGORITHM,
        },
        "cells": results,
    }
    write_json(staging / "comparison-set.json", aggregate)
    if output.exists():
      shutil.rmtree(output)
    staging.rename(output)
    return aggregate
  except Exception:
    shutil.rmtree(staging, ignore_errors=True)
    raise


def main() -> int:
  args = parse_args()
  try:
    aggregate = run_set(
        args.render_set_root.resolve(), args.tagged_set_manifest.resolve(),
        args.comparison_tool.resolve(), args.output.resolve(), force=args.force,
    )
  except Exception as exc:  # noqa: BLE001 - preserve exact gate failure.
    print(f"unable to run independent chart comparison set: {exc}", file=sys.stderr)
    return 1
  print(f"output={args.output.resolve()}")
  print(f"passed={str(aggregate['passed']).lower()}")
  print(f"cells={aggregate['summary']['cell_count']}")
  print(f"pixels={aggregate['summary']['pixel_count']}")
  for key, value in aggregate["summary"].items():
    if key.startswith("worst_"):
      print(f"{key}={value['value']} cell={value['cell_id']}")
  return 0 if aggregate["passed"] else 2


if __name__ == "__main__":
  raise SystemExit(main())
