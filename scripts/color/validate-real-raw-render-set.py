#!/usr/bin/env python3
"""Validate non-promotable real-RAW renders produced by the shipping pipeline."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any


SCHEMA = "photo-editor-real-raw-render-set:v1"
INVENTORY_SCHEMA = "photo-editor-color-source-inventory:v1"
REPORT_SCHEMA = "photo-editor-real-raw-candidate-report:v2"
TOOL_VERSION = "photo-editor-real-raw-candidate-render:v3"
RENDER_PATHS = ("portable-libraw", "shipping-default")
HASH_ALGORITHM = (
    "sha256(sorted(logical_path + NUL + byte_size + NUL + file_sha256 + LF))"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ROOT_KEYS = {
    "schema", "set_id", "description", "promotion_eligible",
    "promotion_blockers", "source_inventory", "tools", "render_paths", "matrix",
    "artifact_summary", "cells",
}
SOURCE_KEYS = {"inventory_id", "manifest_sha256"}
TOOLS_KEYS = {"candidate_renderer_sha256", "candidate_tool_version", "qt_version"}
MATRIX_KEYS = {
    "candidate_count", "camera_count", "illuminants", "render_count",
}
SUMMARY_KEYS = {
    "artifact_count", "byte_size", "artifact_set_sha256", "hash_algorithm",
}
CELL_KEYS = {
    "candidate_id", "source_id", "source_sha256", "camera_make",
    "camera_model", "illuminant", "scene_tags", "same_decoder_backend",
    "pixel_identical", "renders",
}
RENDER_KEYS = {
    "decode_path", "output_id", "output_path", "output_sha256",
    "report_path", "report_sha256", "decoder_backend", "output_pixel_sha256",
}
REPORT_KEYS = {
    "schema", "tool_version", "source_id", "output_id",
    "source_file_sha256", "source_byte_size", "decoded_pixel_sha256",
    "decoded_icc_sha256", "decoded_width", "decoded_height",
    "decoded_storage", "decoded_statistics", "decoded_metadata",
    "decoder_capability", "output_file_sha256", "output_pixel_sha256",
    "output_statistics", "output_icc_sha256", "output_color_space",
    "output_storage", "recipe", "recipe_sha256", "backend_requested",
    "backend_used", "fallback_reason", "qt_version",
}
STATISTICS_KEYS = {
    "pixel_count", "nonfinite_sample_count", "red_minimum", "red_maximum",
    "green_minimum", "green_maximum", "blue_minimum", "blue_maximum",
    "alpha_minimum", "alpha_maximum",
}
METADATA_KEYS = {
    "valid", "is_raw", "pixel_encoding", "decoder_backend",
    "decoded_color_space_name", "camera_profile_identity", "demosaic_applied",
    "black_subtraction_applied", "white_normalization_applied",
    "white_balance_applied", "camera_transform_applied",
    "highlight_reconstruction_applied", "lens_correction_applied",
    "baseline_exposure_applied",
    "decoder_rendering_applied", "input_profile_name", "camera_make",
    "camera_model", "as_shot_temperature", "as_shot_tint",
    "baseline_exposure", "highlight_headroom",
}
CAPABILITY_KEYS = {
    "available", "strict_version", "backend", "compiled_version",
    "runtime_version", "validated_version", "pixel_contract",
    "applied_controls", "deferred_controls", "maximum_raw_memory_bytes",
}
RECIPE_KEYS = {
    "schema", "decode_path", "input_color_profile", "working_color_profile",
    "raw_demosaic_quality", "raw_highlight_recovery", "raw_gamut_mapping",
    "raw_lens_correction", "output_color_profile",
    "photographic_rendering_model", "render_intent",
    "output_gamut_mapping_policy", "black_point_compensation", "backend",
    "output_storage",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--artifact-root", type=Path)
  parser.add_argument("--source-inventory", type=Path, required=True)
  return parser.parse_args()


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


def valid_sha256(value: Any) -> bool:
  return isinstance(value, str) and SHA256_PATTERN.fullmatch(value) is not None


def safe_path(value: Any, prefix: str) -> bool:
  if not isinstance(value, str) or not value or "\\" in value or ":" in value:
    return False
  path = PurePosixPath(value)
  return (
      not path.is_absolute() and str(path) == value and value.startswith(prefix)
      and all(part not in ("", ".", "..") for part in path.parts)
  )


def validate_statistics(value: Any, label: str, errors: list[str]) -> None:
  statistics = exact_keys(value, STATISTICS_KEYS, label, errors)
  if statistics is None:
    return
  numeric_keys = STATISTICS_KEYS - {"pixel_count", "nonfinite_sample_count"}
  if not isinstance(statistics.get("pixel_count"), int) or statistics["pixel_count"] <= 0:
    errors.append(f"{label}.pixel_count must be positive")
  if statistics.get("nonfinite_sample_count") != 0:
    errors.append(f"{label} must not contain non-finite samples")
  if any(not isinstance(statistics.get(key), (int, float)) for key in numeric_keys):
    errors.append(f"{label} extrema must be numeric")
    return
  rgb_minimum = min(statistics[f"{channel}_minimum"] for channel in ("red", "green", "blue"))
  rgb_maximum = max(statistics[f"{channel}_maximum"] for channel in ("red", "green", "blue"))
  if rgb_maximum <= 1.0e-6 or rgb_maximum - rgb_minimum <= 1.0e-6:
    errors.append(f"{label} must contain non-degenerate RGB samples")
  if abs(statistics["alpha_minimum"] - 1.0) > 1.0e-6 or abs(
      statistics["alpha_maximum"] - 1.0
  ) > 1.0e-6:
    errors.append(f"{label} must be fully opaque")


def validate_report(
    report: dict[str, Any], render: dict[str, Any], candidate: dict[str, Any],
    tools: dict[str, Any], label: str, errors: list[str],
) -> None:
  exact_keys(report, REPORT_KEYS, label, errors)
  metadata = exact_keys(report.get("decoded_metadata"), METADATA_KEYS, f"{label}.decoded_metadata", errors) or {}
  exact_keys(report.get("decoder_capability"), CAPABILITY_KEYS, f"{label}.decoder_capability", errors)
  recipe = exact_keys(report.get("recipe"), RECIPE_KEYS, f"{label}.recipe", errors) or {}
  validate_statistics(report.get("decoded_statistics"), f"{label}.decoded_statistics", errors)
  validate_statistics(report.get("output_statistics"), f"{label}.output_statistics", errors)
  expected = {
      "schema": REPORT_SCHEMA,
      "tool_version": TOOL_VERSION,
      "qt_version": tools.get("qt_version"),
      "source_id": candidate.get("source_id"),
      "output_id": render.get("output_id"),
      "source_file_sha256": candidate.get("source_sha256"),
      "output_file_sha256": render.get("output_sha256"),
      "output_pixel_sha256": render.get("output_pixel_sha256"),
      "output_color_space": "sRGB",
      "output_storage": "rgba16-png",
      "backend_requested": "cpu-only",
      "backend_used": "cpu",
      "fallback_reason": "cpu_requested",
  }
  for key, expected_value in expected.items():
    if report.get(key) != expected_value:
      errors.append(f"{label}.{key} does not match the render-set contract")
  expected_recipe = {
      "schema": "photo-editor-real-raw-recipe:v1",
      "decode_path": render.get("decode_path"),
      "input_color_profile": "Embedded",
      "working_color_profile": "Linear sRGB",
      "raw_demosaic_quality": "Full",
      "raw_highlight_recovery": True,
      "raw_gamut_mapping": False,
      "raw_lens_correction": True,
      "output_color_profile": "sRGB",
      "photographic_rendering_model": "aces2-ocio-v1",
      "render_intent": "Perceptual",
      "output_gamut_mapping_policy": "Perceptual",
      "black_point_compensation": True,
      "backend": "cpu-only",
      "output_storage": "rgba16-png",
  }
  if recipe != expected_recipe:
    errors.append(f"{label}.recipe is not the pinned real-RAW recipe")
  required_metadata = {
      "valid": True, "is_raw": True,
      "black_subtraction_applied": True,
      "white_normalization_applied": True,
      "demosaic_applied": True,
  }
  required_metadata.update(
      {
          "pixel_encoding": "scene-linear-rgb",
          "camera_transform_applied": True,
          "white_balance_applied": True,
      }
      if render.get("decode_path") == "portable-libraw"
      else {
          "pixel_encoding": "camera-linear-rgb",
          "camera_transform_applied": False,
          "white_balance_applied": False,
      }
  )
  for key, expected_value in required_metadata.items():
    if metadata.get(key) != expected_value:
      errors.append(f"{label}.decoded_metadata.{key} is invalid")
  if metadata.get("decoder_backend") != render.get("decoder_backend"):
    errors.append(f"{label}.decoded_metadata.decoder_backend mismatch")
  if metadata.get("camera_make") != candidate.get("camera_make") or metadata.get(
      "camera_model"
  ) != candidate.get("camera_model"):
    errors.append(f"{label} camera identity does not match the source inventory")
  for key in (
      "decoded_pixel_sha256", "decoded_icc_sha256", "output_icc_sha256",
      "recipe_sha256", "output_pixel_sha256",
  ):
    if not valid_sha256(report.get(key)):
      errors.append(f"{label}.{key} must be a lowercase SHA-256 digest")


def validate_manifest(
    manifest_path: Path, *, source_inventory_path: Path,
    artifact_root: Path | None = None,
) -> list[str]:
  errors: list[str] = []
  manifest = load_json(manifest_path, "real-RAW render-set manifest", errors)
  inventory = load_json(source_inventory_path, "source inventory", errors)
  if manifest is None or inventory is None:
    return errors
  exact_keys(manifest, ROOT_KEYS, "manifest", errors)
  if inventory.get("schema") != INVENTORY_SCHEMA:
    errors.append("source inventory schema is unsupported")
  if manifest.get("schema") != SCHEMA:
    errors.append("manifest schema is unsupported")
  if manifest.get("promotion_eligible") is not False:
    errors.append("real-RAW candidate render sets must remain non-promotable")
  inventory_blockers = inventory.get("promotion_blockers")
  blockers = manifest.get("promotion_blockers")
  if not isinstance(blockers, list) or not isinstance(inventory_blockers, list) or not set(
      inventory_blockers
  ).issubset(set(blockers)):
    errors.append("manifest must retain every source-inventory promotion blocker")
  source = exact_keys(manifest.get("source_inventory"), SOURCE_KEYS, "manifest.source_inventory", errors) or {}
  if source.get("inventory_id") != inventory.get("inventory_id"):
    errors.append("source inventory identity mismatch")
  if source.get("manifest_sha256") != sha256_file(source_inventory_path):
    errors.append("source inventory manifest hash mismatch")
  if manifest.get("render_paths") != list(RENDER_PATHS):
    errors.append("render_paths must pin portable-libraw and shipping-default")
  tools = exact_keys(manifest.get("tools"), TOOLS_KEYS, "manifest.tools", errors) or {}
  if tools.get("candidate_tool_version") != TOOL_VERSION:
    errors.append("candidate tool version is not pinned to the v2 contract")
  if not valid_sha256(tools.get("candidate_renderer_sha256")):
    errors.append("candidate renderer hash must be a SHA-256 digest")
  if not isinstance(tools.get("qt_version"), str) or not tools.get("qt_version"):
    errors.append("Qt version must be non-empty")

  inventory_candidates = inventory.get("candidates")
  cells = manifest.get("cells")
  if not isinstance(inventory_candidates, list) or not isinstance(cells, list):
    errors.append("source candidates and cells must be arrays")
    return errors
  inventory_by_id = {
      value.get("candidate_id"): value for value in inventory_candidates
      if isinstance(value, dict) and isinstance(value.get("candidate_id"), str)
  }
  if len(inventory_by_id) != len(inventory_candidates):
    errors.append("source inventory candidate identities must be unique")
  matrix = exact_keys(manifest.get("matrix"), MATRIX_KEYS, "manifest.matrix", errors) or {}
  expected_illuminants = sorted({
      value.get("illuminant") for value in inventory_candidates
      if isinstance(value, dict) and isinstance(value.get("illuminant"), str)
  })
  expected_cameras = {
      (value.get("camera") or {}).get("make", "") + "\0" +
      (value.get("camera") or {}).get("model", "")
      for value in inventory_candidates if isinstance(value, dict)
  }
  expected_matrix = {
      "candidate_count": len(inventory_candidates),
      "camera_count": len(expected_cameras),
      "illuminants": expected_illuminants,
      "render_count": len(inventory_candidates) * len(RENDER_PATHS),
  }
  if matrix != expected_matrix:
    errors.append("manifest.matrix does not match the source inventory")

  artifact_paths: list[str] = []
  seen_candidates: set[str] = set()
  root = artifact_root or manifest_path.parent
  for index, value in enumerate(cells):
    label = f"manifest.cells[{index}]"
    cell = exact_keys(value, CELL_KEYS, label, errors)
    if cell is None:
      continue
    candidate_id = cell.get("candidate_id")
    source_candidate = inventory_by_id.get(candidate_id)
    if source_candidate is None or candidate_id in seen_candidates:
      errors.append(f"{label}.candidate_id is unknown or duplicated")
      continue
    seen_candidates.add(candidate_id)
    capture = source_candidate.get("capture") or {}
    camera = source_candidate.get("camera") or {}
    expected_cell = {
        "source_id": f"sources/{candidate_id}/capture",
        "source_sha256": capture.get("sha256"),
        "camera_make": camera.get("make"),
        "camera_model": camera.get("model"),
        "illuminant": source_candidate.get("illuminant"),
        "scene_tags": source_candidate.get("scene_tags"),
    }
    for key, expected_value in expected_cell.items():
      if cell.get(key) != expected_value:
        errors.append(f"{label}.{key} does not match the source inventory")
    renders = cell.get("renders")
    if not isinstance(renders, list) or len(renders) != len(RENDER_PATHS):
      errors.append(f"{label}.renders must contain both required paths")
      continue
    render_by_path: dict[str, dict[str, Any]] = {}
    reports: dict[str, dict[str, Any]] = {}
    for render_index, render_value in enumerate(renders):
      render_label = f"{label}.renders[{render_index}]"
      render = exact_keys(render_value, RENDER_KEYS, render_label, errors)
      if render is None:
        continue
      decode_path = render.get("decode_path")
      if decode_path not in RENDER_PATHS or decode_path in render_by_path:
        errors.append(f"{render_label}.decode_path is invalid or duplicated")
        continue
      render_by_path[decode_path] = render
      expected_output = f"renders/{candidate_id}/{decode_path}/candidate.png"
      expected_report = f"reports/{candidate_id}/{decode_path}/candidate.json"
      if render.get("output_id") != expected_output or render.get("output_path") != expected_output:
        errors.append(f"{render_label} output identity is not canonical")
      if render.get("report_path") != expected_report:
        errors.append(f"{render_label} report identity is not canonical")
      for key in ("output_sha256", "report_sha256", "output_pixel_sha256"):
        if not valid_sha256(render.get(key)):
          errors.append(f"{render_label}.{key} must be a SHA-256 digest")
      for logical_path, prefix, expected_hash, kind in (
          (render.get("output_path"), "renders/", render.get("output_sha256"), "output"),
          (render.get("report_path"), "reports/", render.get("report_sha256"), "report"),
      ):
        if not safe_path(logical_path, prefix):
          errors.append(f"{render_label} {kind} path is unsafe")
          continue
        artifact_path = root / logical_path
        if not artifact_path.is_file():
          errors.append(f"{render_label} {kind} artifact is missing")
          continue
        if sha256_file(artifact_path) != expected_hash:
          errors.append(f"{render_label} {kind} artifact hash mismatch")
        artifact_paths.append(logical_path)
      report_path = root / expected_report
      report = load_json(report_path, f"{render_label} report", errors)
      if report is not None:
        reports[decode_path] = report
        validate_report(report, render, cell, tools, f"{render_label}.report", errors)
    if set(render_by_path) != set(RENDER_PATHS):
      errors.append(f"{label} does not cover every required decode path")
    if set(reports) == set(RENDER_PATHS):
      same_backend = reports[RENDER_PATHS[0]]["decoded_metadata"]["decoder_backend"] == reports[RENDER_PATHS[1]]["decoded_metadata"]["decoder_backend"]
      pixel_identical = reports[RENDER_PATHS[0]]["output_pixel_sha256"] == reports[RENDER_PATHS[1]]["output_pixel_sha256"]
      if cell.get("same_decoder_backend") is not same_backend:
        errors.append(f"{label}.same_decoder_backend is inaccurate")
      if cell.get("pixel_identical") is not pixel_identical:
        errors.append(f"{label}.pixel_identical is inaccurate")
      if same_backend and not pixel_identical:
        errors.append(f"{label} same-backend decode paths must be pixel-identical")
  if seen_candidates != set(inventory_by_id):
    errors.append("render set does not cover every source-inventory candidate")

  summary = exact_keys(manifest.get("artifact_summary"), SUMMARY_KEYS, "manifest.artifact_summary", errors) or {}
  if len(set(artifact_paths)) != len(artifact_paths):
    errors.append("artifact logical paths must be unique")
  if len(artifact_paths) == len(inventory_candidates) * len(RENDER_PATHS) * 2:
    byte_size, digest = artifact_set_digest(root, artifact_paths)
    expected_summary = {
        "artifact_count": len(artifact_paths), "byte_size": byte_size,
        "artifact_set_sha256": digest, "hash_algorithm": HASH_ALGORITHM,
    }
    if summary != expected_summary:
      errors.append("artifact_summary does not match the validated artifacts")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(
      args.manifest, source_inventory_path=args.source_inventory,
      artifact_root=args.artifact_root,
  )
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print("status=valid-non-promotable-real-raw-render-set")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
