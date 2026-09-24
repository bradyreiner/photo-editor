#!/usr/bin/env python3
"""Validate a complete Photo Editor/direct-OCIO chart render evidence set."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any


SCHEMA = "photo-editor-independent-chart-render-set:v1"
TAGGED_SET_SCHEMA = "photo-editor-independent-chart-tagged-set:v1"
CANDIDATE_SCHEMA = "photo-editor-independent-chart-candidate-report:v1"
CANDIDATE_VERSION = "photo-editor-independent-chart-candidate-render:v1"
REFERENCE_SCHEMA = "photo-editor-independent-chart-ocio-reference-report:v2"
REFERENCE_VERSION = "photo-editor-independent-chart-ocio-reference:v2"
SET_ID = "middlebury-checker24-aces2-ocio-srgb-render-set-v1"
HASH_ALGORITHM = (
    "sha256(sorted(logical_path + NUL + byte_size + NUL + file_sha256 + LF))"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
SHA1_PATTERN = re.compile(r"^[0-9A-F]{40}$")
ROOT_KEYS = {
    "schema", "set_id", "description", "promotion_eligible",
    "promotion_blockers", "source", "tools", "recipe", "output_profile",
    "matrix", "artifact_summary", "cells",
}
SOURCE_KEYS = {"tagged_set_id", "manifest_sha256", "artifact_set_sha256"}
TOOLS_KEYS = {
    "candidate_renderer_sha256", "reference_generator_sha256",
    "candidate_tool_version", "reference_tool_version", "qt_version",
    "ocio_version", "openimageio_version",
}
RECIPE_KEYS = {"sha256", "render_path", "photographic_rendering_model"}
PROFILE_KEYS = {"name", "icc_sha256", "storage"}
MATRIX_KEYS = {"cameras", "illuminants", "exposures", "cell_count"}
SUMMARY_KEYS = {
    "artifact_count", "byte_size", "artifact_set_sha256", "hash_algorithm",
}
CELL_KEYS = {
    "camera_directory", "manufacturer", "model", "illuminant_code", "kelvin",
    "exposure_code", "stops", "source_id", "source_sha256", "candidate_id",
    "candidate_sha256", "candidate_report_id", "candidate_report_sha256",
    "reference_id", "reference_sha256", "reference_report_id",
    "reference_report_sha256",
}
CANDIDATE_REPORT_KEYS = {
    "schema", "tool_version", "renderer", "qt_version", "source_id",
    "output_id", "source_file_sha256", "source_pixel_sha256",
    "source_icc_sha256", "output_file_sha256", "output_pixel_sha256",
    "output_icc_sha256", "output_color_space", "output_storage", "width",
    "height", "recipe", "recipe_sha256", "backend_requested", "backend_used",
    "fallback_reason",
}
CANDIDATE_RECIPE_KEYS = {
    "schema", "input_color_profile", "working_color_profile",
    "output_color_profile", "photographic_rendering_model", "render_intent",
    "output_gamut_mapping_policy", "black_point_compensation",
    "source_black_point", "output_black_point", "output_headroom", "backend",
    "render_path", "output_storage",
}
REFERENCE_REPORT_KEYS = {
    "schema", "tool_version", "renderer", "independent_of_photo_editor",
    "source_id", "output_id", "source_file_sha256", "output_file_sha256",
    "output_pixel_sha1", "output_color_space", "processing_storage",
    "output_storage", "width", "height", "ocio_version",
    "openimageio_version", "built_in_config", "config_cache_id",
    "processor_cache_id", "source_color_space", "display", "view",
    "generator_sha256", "png_color_metadata",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Validate the complete independent chart render set."
  )
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--artifact-root", type=Path)
  parser.add_argument("--tagged-set-manifest", type=Path, required=True)
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


def require_sha256(value: Any, label: str, errors: list[str]) -> None:
  if not valid_sha256(value):
    errors.append(f"{label} must be a lowercase SHA-256 digest")


def safe_path(value: Any, prefix: str) -> bool:
  if not isinstance(value, str) or not value or "\\" in value or ":" in value:
    return False
  path = PurePosixPath(value)
  return (
      not path.is_absolute() and str(path) == value and value.startswith(prefix) and
      all(part not in ("", ".", "..") for part in path.parts)
  )


def validate_candidate_report(
    report: dict[str, Any], cell: dict[str, Any], manifest: dict[str, Any],
    label: str, errors: list[str],
) -> None:
  exact_keys(report, CANDIDATE_REPORT_KEYS, label, errors)
  recipe = exact_keys(
      report.get("recipe"), CANDIDATE_RECIPE_KEYS, f"{label}.recipe", errors
  ) or {}
  expected = {
      "schema": CANDIDATE_SCHEMA,
      "tool_version": CANDIDATE_VERSION,
      "renderer": "shipping-desktop-render-output-image-with-backend",
      "qt_version": manifest["tools"].get("qt_version"),
      "source_id": cell.get("source_id"),
      "output_id": cell.get("candidate_id"),
      "source_file_sha256": cell.get("source_sha256"),
      "output_file_sha256": cell.get("candidate_sha256"),
      "output_icc_sha256": manifest["output_profile"].get("icc_sha256"),
      "output_color_space": "sRGB",
      "output_storage": "rgba16-png",
      "width": 606,
      "height": 404,
      "recipe_sha256": manifest["recipe"].get("sha256"),
      "backend_requested": "cpu-only",
      "backend_used": "cpu",
      "fallback_reason": "cpu_requested",
  }
  for key, expected_value in expected.items():
    if report.get(key) != expected_value:
      errors.append(f"{label}.{key} does not match the render-set contract")
  expected_recipe = {
      "schema": "photo-editor-chart-recipe:v1",
      "input_color_profile": "Embedded",
      "working_color_profile": "Linear sRGB",
      "output_color_profile": "sRGB",
      "photographic_rendering_model": "aces2-ocio-v1",
      "render_intent": "Perceptual",
      "output_gamut_mapping_policy": "Perceptual",
      "black_point_compensation": True,
      "source_black_point": 0,
      "output_black_point": 0,
      "output_headroom": 1,
      "backend": "cpu-only",
      "render_path": "desktop-export",
      "output_storage": "rgba16-png",
  }
  if recipe != expected_recipe:
    errors.append(f"{label}.recipe is not the pinned shipping render recipe")
  for key in (
      "source_pixel_sha256", "source_icc_sha256", "output_pixel_sha256",
      "output_icc_sha256",
  ):
    require_sha256(report.get(key), f"{label}.{key}", errors)


def validate_reference_report(
    report: dict[str, Any], cell: dict[str, Any], manifest: dict[str, Any],
    label: str, errors: list[str],
) -> None:
  exact_keys(report, REFERENCE_REPORT_KEYS, label, errors)
  expected = {
      "schema": REFERENCE_SCHEMA,
      "tool_version": REFERENCE_VERSION,
      "renderer": "OpenImageIO ImageBufAlgo.ociodisplay",
      "independent_of_photo_editor": True,
      "source_id": cell.get("source_id"),
      "output_id": cell.get("reference_id"),
      "source_file_sha256": cell.get("source_sha256"),
      "output_file_sha256": cell.get("reference_sha256"),
      "output_color_space": "sRGB",
      "processing_storage": "rgba32-float",
      "output_storage": "rgba16-png",
      "width": 606,
      "height": 404,
      "ocio_version": manifest["tools"].get("ocio_version"),
      "openimageio_version": manifest["tools"].get("openimageio_version"),
      "built_in_config": "cg-config-v4.0.0_aces-v2.0_ocio-v2.5",
      "source_color_space": "Linear Rec.709 (sRGB)",
      "display": "sRGB - Display",
      "view": "ACES 2.0 - SDR 100 nits (Rec.709)",
      "generator_sha256": manifest["tools"].get("reference_generator_sha256"),
      "png_color_metadata": ["sRGB", "gAMA", "cHRM"],
  }
  for key, expected_value in expected.items():
    if report.get(key) != expected_value:
      errors.append(f"{label}.{key} does not match the independent oracle contract")
  if not isinstance(report.get("config_cache_id"), str) or not report["config_cache_id"]:
    errors.append(f"{label}.config_cache_id must be non-empty")
  if not isinstance(report.get("processor_cache_id"), str) or not report["processor_cache_id"]:
    errors.append(f"{label}.processor_cache_id must be non-empty")
  if not isinstance(report.get("output_pixel_sha1"), str) or not SHA1_PATTERN.fullmatch(
      report["output_pixel_sha1"]
  ):
    errors.append(f"{label}.output_pixel_sha1 must be an uppercase SHA-1 digest")


def validate_manifest(
    manifest_path: Path, *, tagged_set_manifest_path: Path,
    artifact_root: Path | None = None,
) -> list[str]:
  errors: list[str] = []
  manifest = load_json(manifest_path, "render-set manifest", errors)
  tagged = load_json(tagged_set_manifest_path, "tagged-set manifest", errors)
  if manifest is None or tagged is None:
    return errors
  exact_keys(manifest, ROOT_KEYS, "manifest", errors)
  if manifest.get("schema") != SCHEMA:
    errors.append(f"manifest.schema must be {SCHEMA!r}")
  if manifest.get("set_id") != SET_ID:
    errors.append("manifest.set_id is not the pinned render-set identity")
  if manifest.get("promotion_eligible") is not False:
    errors.append("independent chart render set must not claim promotion eligibility")
  if tagged.get("schema") != TAGGED_SET_SCHEMA:
    errors.append("source tagged-set schema is unsupported")
  if manifest.get("promotion_blockers") != tagged.get("promotion_blockers"):
    errors.append("render set must retain every tagged-set promotion blocker")

  source = exact_keys(manifest.get("source"), SOURCE_KEYS, "source", errors) or {}
  tools = exact_keys(manifest.get("tools"), TOOLS_KEYS, "tools", errors) or {}
  recipe = exact_keys(manifest.get("recipe"), RECIPE_KEYS, "recipe", errors) or {}
  profile = exact_keys(
      manifest.get("output_profile"), PROFILE_KEYS, "output_profile", errors
  ) or {}
  matrix = exact_keys(manifest.get("matrix"), MATRIX_KEYS, "matrix", errors) or {}
  summary = exact_keys(
      manifest.get("artifact_summary"), SUMMARY_KEYS, "artifact_summary", errors
  ) or {}
  expected_source = {
      "tagged_set_id": tagged.get("set_id"),
      "manifest_sha256": sha256_file(tagged_set_manifest_path),
      "artifact_set_sha256": (tagged.get("artifact_summary") or {}).get(
          "artifact_set_sha256"
      ),
  }
  if source != expected_source:
    errors.append("source does not identify the exact tagged reference set")
  for key in ("candidate_renderer_sha256", "reference_generator_sha256"):
    require_sha256(tools.get(key), f"tools.{key}", errors)
  expected_tools = {
      "candidate_tool_version": CANDIDATE_VERSION,
      "reference_tool_version": REFERENCE_VERSION,
  }
  for key, value in expected_tools.items():
    if tools.get(key) != value:
      errors.append(f"tools.{key} does not match the pinned implementation")
  for key in ("qt_version", "ocio_version", "openimageio_version"):
    if not isinstance(tools.get(key), str) or not tools.get(key):
      errors.append(f"tools.{key} must be non-empty")
  require_sha256(recipe.get("sha256"), "recipe.sha256", errors)
  if recipe.get("render_path") != "desktop-export" or recipe.get(
      "photographic_rendering_model"
  ) != "aces2-ocio-v1":
    errors.append("recipe must identify the shipping ACES 2 desktop export path")
  if profile.get("name") != "sRGB" or profile.get("storage") != "rgba16-png":
    errors.append("output_profile must be the pinned 16-bit sRGB publication")
  require_sha256(profile.get("icc_sha256"), "output_profile.icc_sha256", errors)
  if matrix.get("cameras") != (tagged.get("matrix") or {}).get("cameras"):
    errors.append("matrix.cameras differs from the tagged source set")
  if matrix.get("illuminants") != (tagged.get("matrix") or {}).get("illuminants"):
    errors.append("matrix.illuminants differs from the tagged source set")
  if matrix.get("exposures") != (tagged.get("matrix") or {}).get("exposures"):
    errors.append("matrix.exposures differs from the tagged source set")
  if matrix.get("cell_count") != 48:
    errors.append("matrix.cell_count must be 48")

  tagged_cells = {
      (
          item.get("camera_directory"), item.get("illuminant_code"),
          item.get("exposure_code"),
      ): item
      for item in tagged.get("references", []) if isinstance(item, dict)
  }
  cells = manifest.get("cells")
  if not isinstance(cells, list) or len(cells) != 48:
    errors.append("cells must contain exactly 48 render comparisons")
    cells = []
  seen: set[tuple[Any, Any, Any]] = set()
  artifact_paths: list[str] = []
  for index, value in enumerate(cells):
    label = f"cells[{index}]"
    cell = exact_keys(value, CELL_KEYS, label, errors) or {}
    identity = (
        cell.get("camera_directory"), cell.get("illuminant_code"),
        cell.get("exposure_code"),
    )
    if identity in seen:
      errors.append(f"{label} duplicates a matrix cell")
    seen.add(identity)
    tagged_cell = tagged_cells.get(identity)
    if tagged_cell is None:
      errors.append(f"{label} does not resolve to the tagged set")
    else:
      expected_meta = {
          "manufacturer": tagged_cell.get("manufacturer"),
          "model": tagged_cell.get("model"),
          "kelvin": tagged_cell.get("kelvin"),
          "stops": tagged_cell.get("stops"),
          "source_id": tagged_cell.get("output_id"),
          "source_sha256": tagged_cell.get("output_sha256"),
      }
      for key, expected_value in expected_meta.items():
        if cell.get(key) != expected_value:
          errors.append(f"{label}.{key} does not match the tagged source cell")
    for key in (
        "source_sha256", "candidate_sha256", "candidate_report_sha256",
        "reference_sha256", "reference_report_sha256",
    ):
      require_sha256(cell.get(key), f"{label}.{key}", errors)
    path_contracts = {
        "candidate_id": "candidates/checker24/",
        "candidate_report_id": "candidate-reports/checker24/",
        "reference_id": "references/checker24/",
        "reference_report_id": "reference-reports/checker24/",
    }
    for key, prefix in path_contracts.items():
      logical_path = cell.get(key)
      if not safe_path(logical_path, prefix):
        errors.append(f"{label}.{key} is unsafe or outside {prefix}")
      elif isinstance(logical_path, str):
        artifact_paths.append(logical_path)

    if artifact_root is None:
      continue
    pairs = (
        ("candidate_id", "candidate_sha256"),
        ("candidate_report_id", "candidate_report_sha256"),
        ("reference_id", "reference_sha256"),
        ("reference_report_id", "reference_report_sha256"),
    )
    for path_key, hash_key in pairs:
      logical_path = cell.get(path_key)
      if not isinstance(logical_path, str):
        continue
      path = artifact_root / logical_path
      if not path.is_file():
        errors.append(f"{label}: missing artifact {logical_path}")
      elif sha256_file(path) != cell.get(hash_key):
        errors.append(f"{label}: artifact hash mismatch for {logical_path}")
    candidate_report_id = cell.get("candidate_report_id")
    if isinstance(candidate_report_id, str):
      report = load_json(
          artifact_root / candidate_report_id, f"{label}.candidate_report", errors
      )
      if report is not None:
        validate_candidate_report(
            report, cell, manifest, f"{label}.candidate_report", errors
        )
    reference_report_id = cell.get("reference_report_id")
    if isinstance(reference_report_id, str):
      report = load_json(
          artifact_root / reference_report_id, f"{label}.reference_report", errors
      )
      if report is not None:
        validate_reference_report(
            report, cell, manifest, f"{label}.reference_report", errors
        )

  if seen != set(tagged_cells):
    errors.append("render-set cells do not equal the complete tagged-set matrix")
  if len(artifact_paths) != len(set(artifact_paths)):
    errors.append("render-set artifact paths must be globally unique")
  if summary.get("artifact_count") != 192:
    errors.append("artifact_summary.artifact_count must be 192")
  if summary.get("hash_algorithm") != HASH_ALGORITHM:
    errors.append("artifact_summary.hash_algorithm is unsupported")
  if not isinstance(summary.get("byte_size"), int) or summary.get("byte_size", 0) <= 0:
    errors.append("artifact_summary.byte_size must be positive")
  require_sha256(
      summary.get("artifact_set_sha256"),
      "artifact_summary.artifact_set_sha256", errors,
  )
  if artifact_root is not None and len(artifact_paths) == 192 and all(
      (artifact_root / path).is_file() for path in artifact_paths
  ):
    byte_size, digest = artifact_set_digest(artifact_root, artifact_paths)
    if byte_size != summary.get("byte_size"):
      errors.append("artifact_summary.byte_size does not match artifact bytes")
    if digest != summary.get("artifact_set_sha256"):
      errors.append("artifact_summary.artifact_set_sha256 does not match artifacts")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(
      args.manifest.resolve(),
      tagged_set_manifest_path=args.tagged_set_manifest.resolve(),
      artifact_root=(args.artifact_root.resolve() if args.artifact_root else None),
  )
  if errors:
    for error in errors:
      print(f"ERROR: {error}", file=sys.stderr)
    return 1
  print(f"valid independent chart render set: {args.manifest.resolve()}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
