#!/usr/bin/env python3
"""Measure shipping camera-linear renders against the portable LibRaw baseline.

This is diagnostic evidence, not a promotion gate: the two paths intentionally
use different camera-characterization implementations. The report makes large
or regressing differences visible without pretending the portable path is an
independent colorimetric reference.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path


SCHEMA = "photo-editor-real-raw-decoder-comparison:v1"


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def load_json(path: Path) -> dict:
  with path.open("r", encoding="utf-8") as handle:
    value = json.load(handle)
  if not isinstance(value, dict):
    raise ValueError(f"{path} must contain a JSON object")
  return value


def render_for(cell: dict, decode_path: str) -> dict:
  matches = [
      render for render in cell.get("renders", [])
      if render.get("decode_path") == decode_path
  ]
  if len(matches) != 1:
    raise ValueError(
        f"{cell.get('candidate_id')}: expected one {decode_path} render")
  return matches[0]


def metric(report: dict, name: str, field: str) -> float:
  value = report.get("metrics", {}).get(name, {}).get(field)
  if not isinstance(value, (int, float)):
    raise ValueError(f"comparison metric {name}.{field} is missing")
  return float(value)


def main() -> int:
  parser = argparse.ArgumentParser(
      description="Compare both decoder paths in a real-RAW technical render set.")
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--comparison-tool", required=True, type=Path)
  parser.add_argument("--output", required=True, type=Path)
  args = parser.parse_args()

  manifest_path = args.manifest.resolve()
  tool_path = args.comparison_tool.resolve()
  output = args.output.resolve()
  if not manifest_path.is_file() or not tool_path.is_file():
    raise SystemExit("manifest and comparison tool must exist")
  if output.exists():
    raise SystemExit("output must be a new path")

  manifest = load_json(manifest_path)
  if manifest.get("schema") != "photo-editor-real-raw-render-set:v1":
    raise SystemExit("unsupported real-RAW render-set schema")
  cells = manifest.get("cells")
  if not isinstance(cells, list) or len(cells) != 6:
    raise SystemExit("the technical comparison requires exactly six cells")

  reports_dir = output / "reports"
  heatmaps_dir = output / "heatmaps"
  reports_dir.mkdir(parents=True)
  heatmaps_dir.mkdir()
  root = manifest_path.parent
  results = []

  for cell in cells:
    candidate_id = cell.get("candidate_id")
    if not isinstance(candidate_id, str) or not candidate_id:
      raise ValueError("every render-set cell needs a candidate_id")
    shipping = render_for(cell, "shipping-default")
    portable = render_for(cell, "portable-libraw")
    shipping_path = root / shipping["output_path"]
    portable_path = root / portable["output_path"]
    shipping_report = load_json(root / shipping["report_path"])
    report_path = reports_dir / f"{candidate_id}.json"
    heatmap_path = heatmaps_dir / f"{candidate_id}.png"
    command = [
        str(tool_path), "--diagnostic",
        "--candidate", str(shipping_path),
        "--reference", str(portable_path),
        "--report", str(report_path),
        "--heatmap", str(heatmap_path),
        "--corpus-manifest", str(manifest_path),
        "--capture-id", candidate_id,
        "--execution-path", "camera-input",
        "--candidate-rendering-model", "aces2-ocio-v1",
        "--reference-rendering-model", "libraw-characterized-linear-srgb",
        "--recipe-sha256", shipping_report["recipe_sha256"],
        "--output-profile-sha256", shipping_report["output_icc_sha256"],
    ]
    subprocess.run(command, check=True)
    comparison = load_json(report_path)
    if comparison.get("diagnostic_only") is not True:
      raise ValueError(f"{candidate_id}: comparison is not diagnostic-only")
    if comparison.get("candidate", {}).get("sha256") != shipping["output_sha256"]:
      raise ValueError(f"{candidate_id}: shipping hash mismatch")
    if comparison.get("reference", {}).get("sha256") != portable["output_sha256"]:
      raise ValueError(f"{candidate_id}: portable hash mismatch")
    results.append({
        "candidate_id": candidate_id,
        "camera_make": cell.get("camera_make"),
        "camera_model": cell.get("camera_model"),
        "illuminant": cell.get("illuminant"),
        "report_path": str(report_path.relative_to(output)),
        "report_sha256": sha256_file(report_path),
        "heatmap_path": str(heatmap_path.relative_to(output)),
        "heatmap_sha256": sha256_file(heatmap_path),
        "mean_delta_e_00": metric(comparison, "delta_e_00_max", "mean"),
        "p95_delta_e_00_upper": metric(
            comparison, "delta_e_00_p95", "p95_upper_bound"),
        "max_delta_e_00": metric(comparison, "delta_e_00_max", "max"),
        "mean_delta_e_itp": metric(comparison, "delta_e_itp_max", "mean"),
        "p95_delta_e_itp_upper": metric(
            comparison, "delta_e_itp_p95", "p95_upper_bound"),
        "max_delta_e_itp": metric(comparison, "delta_e_itp_max", "max"),
    })

  artifact_rows = []
  for path in sorted(reports_dir.glob("*.json")) + sorted(heatmaps_dir.glob("*.png")):
    artifact_rows.append(
        f"{path.relative_to(output)}\0{path.stat().st_size}\0{sha256_file(path)}\n")
  artifact_set_sha256 = hashlib.sha256("".join(artifact_rows).encode()).hexdigest()
  summary = {
      "schema": SCHEMA,
      "diagnostic_only": True,
      "promotion_evidence": False,
      "reason": (
          "Portable LibRaw is a regression baseline, not an independent "
          "colorimetric reference."),
      "source_manifest": str(manifest_path),
      "source_manifest_sha256": sha256_file(manifest_path),
      "cell_count": len(results),
      "cells": results,
      "worst_mean_delta_e_00": max(row["mean_delta_e_00"] for row in results),
      "worst_p95_delta_e_00_upper": max(
          row["p95_delta_e_00_upper"] for row in results),
      "worst_max_delta_e_00": max(row["max_delta_e_00"] for row in results),
      "artifact_count": len(artifact_rows),
      "artifact_set_sha256": artifact_set_sha256,
  }
  summary_path = output / "decoder-path-comparison.json"
  with summary_path.open("x", encoding="utf-8") as handle:
    json.dump(summary, handle, indent=2, sort_keys=True)
    handle.write("\n")
  print(f"status=created-real-raw-decoder-comparison:{summary_path}")
  print(f"cell_count={len(results)}")
  print(f"artifact_set_sha256={artifact_set_sha256}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
