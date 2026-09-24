#!/usr/bin/env python3
"""Validate a complete, portable Middlebury tagged-reference evidence set."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any


SCHEMA = "photo-editor-independent-chart-tagged-set:v1"
TAG_REPORT_SCHEMA = "photo-editor-independent-chart-tag-report:v1"
TAGGER_VERSION = "photo-editor-independent-chart-profile-tag:v1"
SOURCE_VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-source.py"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ROOT_KEYS = {
    "schema", "set_id", "description", "promotion_eligible",
    "promotion_blockers", "source", "tagger", "matrix",
    "artifact_summary", "references",
}
SOURCE_KEYS = {
    "inventory_id", "manifest_sha256", "archive_sha256",
    "selected_member_set_sha256",
}
TAGGER_KEYS = {
    "version", "binary_sha256", "qt_version", "icc_profile_sha256",
}
MATRIX_KEYS = {"cameras", "illuminants", "exposures", "reference_count"}
SUMMARY_KEYS = {
    "artifact_count", "byte_size", "artifact_set_sha256", "hash_algorithm",
}
REFERENCE_KEYS = {
    "camera_directory", "manufacturer", "model", "illuminant_code", "kelvin",
    "exposure_code", "stops", "source_id", "source_sha256", "output_id",
    "output_sha256", "report_id", "report_sha256", "source_pixel_sha256",
    "output_pixel_sha256",
}
REPORT_KEYS = {
    "schema", "tagger_version", "qt_version", "source_id", "output_id",
    "source_file_sha256", "output_file_sha256", "source_pixel_sha256",
    "output_pixel_sha256", "icc_profile_sha256", "declared_color_space",
    "profile_primaries", "profile_transfer", "width", "height",
    "pixel_samples_unchanged", "tag_only",
}
REQUIRED_BLOCKERS = {
    "no-sensor-raw-files",
    "eight-bit-registered-chart-crops",
    "no-measured-spectral-patch-values",
    "project-license-review-pending",
    "colorchecker-target-rights-review-pending",
    "no-real-dcp-profile",
    "no-rights-cleared-skin-capture",
    "tagged-reference-set-not-yet-corpus-bound",
}
HASH_ALGORITHM = (
    "sha256(sorted(logical_path + NUL + byte_size + NUL + "
    "file_sha256 + LF))"
)


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Validate an independent tagged chart-reference set."
  )
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--artifact-root", type=Path)
  parser.add_argument("--source-manifest", type=Path)
  parser.add_argument("--source-root", type=Path)
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


def valid_sha(value: Any) -> bool:
  return isinstance(value, str) and SHA256_PATTERN.fullmatch(value) is not None


def require_sha(value: Any, label: str, errors: list[str]) -> None:
  if not valid_sha(value):
    errors.append(f"{label} must be a lowercase SHA-256 digest")


def safe_logical_path(value: Any, prefix: str) -> bool:
  if not isinstance(value, str) or not value or "\\" in value or ":" in value:
    return False
  path = PurePosixPath(value)
  return (
      not path.is_absolute() and str(path) == value and
      all(part not in ("", ".", "..") for part in path.parts) and
      value.startswith(prefix)
  )


def artifact_set_digest(root: Path, paths: list[str]) -> tuple[int, str]:
  digest = hashlib.sha256()
  total = 0
  for logical_path in sorted(paths):
    path = root / logical_path
    size = path.stat().st_size
    file_digest = sha256_file(path)
    digest.update(logical_path.encode("utf-8"))
    digest.update(b"\0")
    digest.update(str(size).encode("ascii"))
    digest.update(b"\0")
    digest.update(file_digest.encode("ascii"))
    digest.update(b"\n")
    total += size
  return total, digest.hexdigest()


def validate_report(
    report: dict[str, Any], reference: dict[str, Any], tagger: dict[str, Any],
    label: str, errors: list[str],
) -> None:
  exact_keys(report, REPORT_KEYS, label, errors)
  expected = {
      "schema": TAG_REPORT_SCHEMA,
      "tagger_version": TAGGER_VERSION,
      "qt_version": tagger.get("qt_version"),
      "source_id": reference.get("source_id"),
      "output_id": reference.get("output_id"),
      "source_file_sha256": reference.get("source_sha256"),
      "output_file_sha256": reference.get("output_sha256"),
      "source_pixel_sha256": reference.get("source_pixel_sha256"),
      "output_pixel_sha256": reference.get("output_pixel_sha256"),
      "icc_profile_sha256": tagger.get("icc_profile_sha256"),
      "declared_color_space": "Linear sRGB",
      "profile_primaries": "sRGB",
      "profile_transfer": "linear",
      "width": 606,
      "height": 404,
      "pixel_samples_unchanged": True,
      "tag_only": True,
  }
  for key, expected_value in expected.items():
    if report.get(key) != expected_value:
      errors.append(f"{label}.{key} does not match the set contract")


def load_source_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "independent_chart_source_validator", SOURCE_VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError("unable to load independent chart source validator")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def validate_manifest(
    manifest_path: Path,
    *,
    artifact_root: Path | None = None,
    source_manifest_path: Path | None = None,
    source_root: Path | None = None,
) -> list[str]:
  errors: list[str] = []
  manifest = load_json(manifest_path, "tagged-set manifest", errors)
  if manifest is None:
    return errors
  exact_keys(manifest, ROOT_KEYS, "manifest", errors)
  if manifest.get("schema") != SCHEMA:
    errors.append(f"manifest.schema must be {SCHEMA!r}")
  if manifest.get("set_id") != "middlebury-checker24-linear-srgb-tagged-v1":
    errors.append("manifest.set_id is not the pinned set identity")
  if not isinstance(manifest.get("description"), str) or not manifest["description"]:
    errors.append("manifest.description must be a non-empty string")
  if manifest.get("promotion_eligible") is not False:
    errors.append("tagged reference set must not claim promotion eligibility")
  blockers = manifest.get("promotion_blockers")
  if not isinstance(blockers, list) or set(blockers) != REQUIRED_BLOCKERS or len(
      blockers
  ) != len(REQUIRED_BLOCKERS):
    errors.append("manifest.promotion_blockers must record the exact unresolved gates")

  source = exact_keys(manifest.get("source"), SOURCE_KEYS, "source", errors) or {}
  tagger = exact_keys(manifest.get("tagger"), TAGGER_KEYS, "tagger", errors) or {}
  matrix = exact_keys(manifest.get("matrix"), MATRIX_KEYS, "matrix", errors) or {}
  summary = exact_keys(
      manifest.get("artifact_summary"), SUMMARY_KEYS, "artifact_summary", errors
  ) or {}
  for key in ("manifest_sha256", "archive_sha256", "selected_member_set_sha256"):
    require_sha(source.get(key), f"source.{key}", errors)
  if source.get("inventory_id") != "middlebury-checker24-independent-reference-v1":
    errors.append("source.inventory_id is not the pinned source identity")
  if tagger.get("version") != TAGGER_VERSION:
    errors.append("tagger.version does not match the report contract")
  for key in ("binary_sha256", "icc_profile_sha256"):
    require_sha(tagger.get(key), f"tagger.{key}", errors)
  if not isinstance(tagger.get("qt_version"), str) or not tagger.get("qt_version"):
    errors.append("tagger.qt_version must be a non-empty string")

  cameras = matrix.get("cameras")
  illuminants = matrix.get("illuminants")
  exposures = matrix.get("exposures")
  if not isinstance(cameras, list) or len(cameras) != 6:
    errors.append("matrix.cameras must contain exactly six cameras")
    cameras = []
  if not isinstance(illuminants, list) or len(illuminants) != 2:
    errors.append("matrix.illuminants must contain exactly two illuminants")
    illuminants = []
  if not isinstance(exposures, list) or len(exposures) != 4:
    errors.append("matrix.exposures must contain exactly four exposures")
    exposures = []
  camera_map: dict[str, dict[str, Any]] = {}
  manufacturers: set[Any] = set()
  for index, camera in enumerate(cameras):
    value = exact_keys(
        camera, {"directory", "manufacturer", "model"},
        f"matrix.cameras[{index}]", errors,
    ) or {}
    directory = value.get("directory")
    if not isinstance(directory, str) or not directory:
      errors.append(f"matrix.cameras[{index}].directory is invalid")
      continue
    if directory in camera_map:
      errors.append("matrix contains a duplicate camera directory")
    camera_map[directory] = value
    manufacturers.add(value.get("manufacturer"))
  if len(camera_map) != 6 or len(manufacturers) != 6:
    errors.append("matrix must span six unique cameras and manufacturers")
  illuminant_map: dict[str, dict[str, Any]] = {}
  for index, illuminant in enumerate(illuminants):
    value = exact_keys(
        illuminant, {"code", "kelvin", "description"},
        f"matrix.illuminants[{index}]", errors,
    ) or {}
    illuminant_map[value.get("code")] = value
  if {key: value.get("kelvin") for key, value in illuminant_map.items()} != {
      "i1": 3200, "i2": 4800,
  }:
    errors.append("matrix illuminants must be the pinned 3200K and 4800K pair")
  exposure_map: dict[str, dict[str, Any]] = {}
  for index, exposure in enumerate(exposures):
    value = exact_keys(
        exposure, {"code", "stops"}, f"matrix.exposures[{index}]", errors
    ) or {}
    exposure_map[value.get("code")] = value
  if {key: value.get("stops") for key, value in exposure_map.items()} != {
      "e1": -2, "e2": -1, "e3": 0, "e4": 1,
  }:
    errors.append("matrix exposures must cover -2, -1, 0, and +1 stops")

  references = manifest.get("references")
  if not isinstance(references, list) or len(references) != 48:
    errors.append("references must contain exactly 48 matrix cells")
    references = []
  if matrix.get("reference_count") != 48:
    errors.append("matrix.reference_count must be 48")
  cells: set[tuple[Any, Any, Any]] = set()
  artifact_paths: list[str] = []
  source_paths: set[str] = set()
  for index, reference_value in enumerate(references):
    label = f"references[{index}]"
    reference = exact_keys(reference_value, REFERENCE_KEYS, label, errors) or {}
    camera = camera_map.get(reference.get("camera_directory"))
    illuminant = illuminant_map.get(reference.get("illuminant_code"))
    exposure = exposure_map.get(reference.get("exposure_code"))
    cell = (
        reference.get("camera_directory"), reference.get("illuminant_code"),
        reference.get("exposure_code"),
    )
    if cell in cells:
      errors.append(f"{label} duplicates a camera/illuminant/exposure cell")
    cells.add(cell)
    if camera is None or illuminant is None or exposure is None:
      errors.append(f"{label} does not resolve to the declared matrix")
    else:
      expected_meta = {
          "manufacturer": camera.get("manufacturer"), "model": camera.get("model"),
          "kelvin": illuminant.get("kelvin"), "stops": exposure.get("stops"),
      }
      for key, value in expected_meta.items():
        if reference.get(key) != value:
          errors.append(f"{label}.{key} does not match matrix metadata")
    for key in (
        "source_sha256", "output_sha256", "report_sha256",
        "source_pixel_sha256", "output_pixel_sha256",
    ):
      require_sha(reference.get(key), f"{label}.{key}", errors)
    if reference.get("source_pixel_sha256") != reference.get("output_pixel_sha256"):
      errors.append(f"{label} does not prove decoded pixel identity")
    source_id = reference.get("source_id")
    output_id = reference.get("output_id")
    report_id = reference.get("report_id")
    if not safe_logical_path(source_id, "members/checker24/"):
      errors.append(f"{label}.source_id is unsafe")
    if not safe_logical_path(output_id, "references/checker24/"):
      errors.append(f"{label}.output_id is unsafe")
    if not safe_logical_path(report_id, "reports/checker24/"):
      errors.append(f"{label}.report_id is unsafe")
    if isinstance(source_id, str):
      source_paths.add(source_id)
    if isinstance(output_id, str):
      artifact_paths.append(output_id)
    if isinstance(report_id, str):
      artifact_paths.append(report_id)

    if artifact_root is not None and all(
        isinstance(value, str) for value in (output_id, report_id)
    ):
      output_path = artifact_root / output_id
      report_path = artifact_root / report_id
      if not output_path.is_file():
        errors.append(f"{label}: output artifact is missing")
      elif sha256_file(output_path) != reference.get("output_sha256"):
        errors.append(f"{label}: output artifact hash mismatch")
      if not report_path.is_file():
        errors.append(f"{label}: report artifact is missing")
      else:
        report_hash = sha256_file(report_path)
        if report_hash != reference.get("report_sha256"):
          errors.append(f"{label}: report artifact hash mismatch")
        report = load_json(report_path, f"{label} report", errors)
        if report is not None:
          validate_report(report, reference, tagger, f"{label}.report", errors)
    if source_root is not None and isinstance(source_id, str):
      source_path = source_root / source_id
      if not source_path.is_file():
        errors.append(f"{label}: source artifact is missing")
      elif sha256_file(source_path) != reference.get("source_sha256"):
        errors.append(f"{label}: source artifact hash mismatch")

  expected_cells = {
      (camera, illuminant, exposure)
      for camera in camera_map
      for illuminant in illuminant_map
      for exposure in exposure_map
  }
  if cells != expected_cells:
    errors.append("references do not cover the complete declared matrix")
  if len(source_paths) != 48 or len(artifact_paths) != 96 or len(
      set(artifact_paths)
  ) != 96:
    errors.append("reference source/artifact logical identities must be unique")
  if summary.get("artifact_count") != 96:
    errors.append("artifact_summary.artifact_count must be 96")
  if not isinstance(summary.get("byte_size"), int) or isinstance(
      summary.get("byte_size"), bool
  ) or summary.get("byte_size", -1) < 0:
    errors.append("artifact_summary.byte_size must be a non-negative integer")
  require_sha(
      summary.get("artifact_set_sha256"),
      "artifact_summary.artifact_set_sha256", errors,
  )
  if summary.get("hash_algorithm") != HASH_ALGORITHM:
    errors.append("artifact_summary.hash_algorithm does not match the contract")
  if artifact_root is not None and len(artifact_paths) == 96 and all(
      (artifact_root / path).is_file() for path in artifact_paths
  ):
    total, digest = artifact_set_digest(artifact_root, artifact_paths)
    if summary.get("byte_size") != total:
      errors.append("artifact_summary.byte_size does not match the artifacts")
    if summary.get("artifact_set_sha256") != digest:
      errors.append("artifact_summary.artifact_set_sha256 does not match the artifacts")

  if source_manifest_path is not None:
    try:
      source_validator = load_source_validator()
      source_errors = source_validator.validate_manifest(
          source_manifest_path, materialized_root=source_root
      )
      errors.extend(f"source contract: {error}" for error in source_errors)
      source_manifest = load_json(source_manifest_path, "source manifest", errors)
      if source_manifest is not None:
        expected_source = {
            "inventory_id": source_manifest.get("inventory_id"),
            "manifest_sha256": sha256_file(source_manifest_path),
            "archive_sha256": source_manifest.get("dataset", {}).get("archive_sha256"),
            "selected_member_set_sha256": source_manifest.get("selection", {}).get(
                "selected_member_set_sha256"
            ),
        }
        for key, value in expected_source.items():
          if source.get(key) != value:
            errors.append(f"source.{key} does not match the source manifest")
        expected_sources = {
            "members/" + name
            for name in source_validator.selected_member_names(source_manifest)
            if name.endswith("-raw.png")
        }
        if source_paths != expected_sources:
          errors.append("reference source identities do not match the pinned linear members")
    except (OSError, RuntimeError) as exc:
      errors.append(f"unable to validate source contract: {exc}")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(
      args.manifest.resolve(),
      artifact_root=args.artifact_root.resolve() if args.artifact_root else None,
      source_manifest_path=(
          args.source_manifest.resolve() if args.source_manifest else None
      ),
      source_root=args.source_root.resolve() if args.source_root else None,
  )
  if errors:
    for error in errors:
      print(error, file=sys.stderr)
    return 1
  print("status=valid-non-promotion-tagged-reference-set")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
