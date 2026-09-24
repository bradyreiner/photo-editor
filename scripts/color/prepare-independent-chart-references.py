#!/usr/bin/env python3
"""Build the complete, non-promotable tagged Middlebury reference set."""

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


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE_MANIFEST = (
    REPO_ROOT / "perf-corpora" / "color-validation-required" /
    "independent-chart-source.json"
)
SOURCE_VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-source.py"
)
TAGGED_SET_VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-tagged-set.py"
)
TAG_REPORT_SCHEMA = "photo-editor-independent-chart-tag-report:v1"
TAGGER_VERSION = "photo-editor-independent-chart-profile-tag:v1"
SET_SCHEMA = "photo-editor-independent-chart-tagged-set:v1"
SET_BLOCKERS = [
    "no-sensor-raw-files",
    "eight-bit-registered-chart-crops",
    "no-measured-spectral-patch-values",
    "project-license-review-pending",
    "colorchecker-target-rights-review-pending",
    "no-real-dcp-profile",
    "no-rights-cleared-skin-capture",
    "tagged-reference-set-not-yet-corpus-bound",
]
REPORT_KEYS = {
    "schema", "tagger_version", "qt_version", "source_id", "output_id",
    "source_file_sha256", "output_file_sha256", "source_pixel_sha256",
    "output_pixel_sha256", "icc_profile_sha256", "declared_color_space",
    "profile_primaries", "profile_transfer", "width", "height",
    "pixel_samples_unchanged", "tag_only",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Create all 48 deterministically tagged chart references."
  )
  parser.add_argument("--source-manifest", type=Path, default=DEFAULT_SOURCE_MANIFEST)
  parser.add_argument("--materialized-root", type=Path, required=True)
  parser.add_argument("--tag-tool", type=Path, required=True)
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
    report: dict[str, Any], source_id: str, output_id: str
) -> None:
  if set(report) != REPORT_KEYS:
    raise ValueError("tag report field set does not match the versioned contract")
  expected = {
      "schema": TAG_REPORT_SCHEMA,
      "tagger_version": TAGGER_VERSION,
      "source_id": source_id,
      "output_id": output_id,
      "declared_color_space": "Linear sRGB",
      "profile_primaries": "sRGB",
      "profile_transfer": "linear",
      "width": 606,
      "height": 404,
      "pixel_samples_unchanged": True,
      "tag_only": True,
  }
  for key, value in expected.items():
    if report.get(key) != value:
      raise ValueError(f"tag report {key} does not match its required value")
  for key in (
      "source_file_sha256", "output_file_sha256", "source_pixel_sha256",
      "output_pixel_sha256", "icc_profile_sha256",
  ):
    value = report.get(key)
    if not isinstance(value, str) or len(value) != 64 or any(
        character not in "0123456789abcdef" for character in value
    ):
      raise ValueError(f"tag report {key} is not a lowercase SHA-256 digest")
  if report["source_pixel_sha256"] != report["output_pixel_sha256"]:
    raise ValueError("tag report pixel identity proof failed")
  if not isinstance(report.get("qt_version"), str) or not report["qt_version"]:
    raise ValueError("tag report does not identify the Qt implementation")


def build_set(
    source_manifest_path: Path,
    materialized_root: Path,
    tag_tool: Path,
    output: Path,
    *,
    force: bool,
) -> dict[str, Any]:
  source_validator = load_module(
      SOURCE_VALIDATOR_PATH, "independent_chart_source_validator"
  )
  source_errors = source_validator.validate_manifest(
      source_manifest_path, materialized_root=materialized_root
  )
  if source_errors:
    raise ValueError("source validation failed: " + "; ".join(source_errors))
  if not tag_tool.is_file():
    raise ValueError(f"tag tool is missing: {tag_tool}")
  if output.exists() and not force:
    raise ValueError(f"output already exists: {output}; pass --force to replace it")

  source_manifest = load_json(source_manifest_path)
  selection = source_manifest["selection"]
  staging = output.with_name(f".{output.name}.partial")
  shutil.rmtree(staging, ignore_errors=True)
  staging.mkdir(parents=True)
  references: list[dict[str, Any]] = []
  artifact_paths: list[str] = []
  profile_sha256: str | None = None
  qt_version: str | None = None
  try:
    for camera in selection["cameras"]:
      for illuminant in selection["illuminants"]:
        for exposure in selection["exposures"]:
          stem = (
              f"{selection['white_balance_code']}"
              f"{illuminant['code']}{exposure['code']}"
          )
          source_id = f"members/checker24/{camera['directory']}/{stem}-raw.png"
          output_id = (
              f"references/checker24/{camera['directory']}/"
              f"{stem}-linear-srgb.png"
          )
          report_id = (
              f"reports/checker24/{camera['directory']}/"
              f"{stem}-linear-srgb.json"
          )
          source_path = materialized_root / source_id
          output_path = staging / output_id
          report_path = staging / report_id
          output_path.parent.mkdir(parents=True, exist_ok=True)
          report_path.parent.mkdir(parents=True, exist_ok=True)
          process = subprocess.run(
              [
                  str(tag_tool), "--input", str(source_path),
                  "--output", str(output_path), "--report", str(report_path),
                  "--source-id", source_id, "--output-id", output_id,
              ],
              check=False,
              capture_output=True,
              text=True,
          )
          if process.returncode != 0:
            detail = process.stderr.strip() or process.stdout.strip()
            raise ValueError(f"tag tool failed for {source_id}: {detail}")
          report = load_json(report_path)
          validate_report(report, source_id, output_id)
          if report["source_file_sha256"] != sha256_file(source_path):
            raise ValueError(f"source hash changed while tagging {source_id}")
          if report["output_file_sha256"] != sha256_file(output_path):
            raise ValueError(f"output hash mismatch for {output_id}")
          if profile_sha256 is None:
            profile_sha256 = report["icc_profile_sha256"]
            qt_version = report["qt_version"]
          elif report["icc_profile_sha256"] != profile_sha256:
            raise ValueError("tag tool emitted more than one ICC profile payload")
          elif report["qt_version"] != qt_version:
            raise ValueError("tag set spans more than one Qt implementation")
          references.append({
              "camera_directory": camera["directory"],
              "manufacturer": camera["manufacturer"],
              "model": camera["model"],
              "illuminant_code": illuminant["code"],
              "kelvin": illuminant["kelvin"],
              "exposure_code": exposure["code"],
              "stops": exposure["stops"],
              "source_id": source_id,
              "source_sha256": report["source_file_sha256"],
              "output_id": output_id,
              "output_sha256": report["output_file_sha256"],
              "report_id": report_id,
              "report_sha256": sha256_file(report_path),
              "source_pixel_sha256": report["source_pixel_sha256"],
              "output_pixel_sha256": report["output_pixel_sha256"],
          })
          artifact_paths.extend((output_id, report_id))

    artifact_bytes, artifact_digest = artifact_set_digest(staging, artifact_paths)
    manifest = {
        "schema": SET_SCHEMA,
        "set_id": "middlebury-checker24-linear-srgb-tagged-v1",
        "description": (
            "Deterministically ICC-tagged, pixel-identical derivatives of the pinned "
            "independent Middlebury linear chart renders."
        ),
        "promotion_eligible": False,
        "promotion_blockers": SET_BLOCKERS,
        "source": {
            "inventory_id": source_manifest["inventory_id"],
            "manifest_sha256": sha256_file(source_manifest_path),
            "archive_sha256": source_manifest["dataset"]["archive_sha256"],
            "selected_member_set_sha256": selection["selected_member_set_sha256"],
        },
        "tagger": {
            "version": TAGGER_VERSION,
            "binary_sha256": sha256_file(tag_tool),
            "qt_version": qt_version,
            "icc_profile_sha256": profile_sha256,
        },
        "matrix": {
            "cameras": selection["cameras"],
            "illuminants": selection["illuminants"],
            "exposures": selection["exposures"],
            "reference_count": len(references),
        },
        "artifact_summary": {
            "artifact_count": len(artifact_paths),
            "byte_size": artifact_bytes,
            "artifact_set_sha256": artifact_digest,
            "hash_algorithm": (
                "sha256(sorted(logical_path + NUL + byte_size + NUL + "
                "file_sha256 + LF))"
            ),
        },
        "references": references,
    }
    manifest_path = staging / "tagged-reference-set.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    tagged_validator = load_module(
        TAGGED_SET_VALIDATOR_PATH, "independent_chart_tagged_set_validator"
    )
    errors = tagged_validator.validate_manifest(
        manifest_path, artifact_root=staging,
        source_manifest_path=source_manifest_path,
        source_root=materialized_root,
    )
    if errors:
      raise ValueError("tagged-set validation failed: " + "; ".join(errors))
    if output.exists():
      shutil.rmtree(output)
    staging.rename(output)
    return manifest
  except Exception:
    shutil.rmtree(staging, ignore_errors=True)
    raise


def main() -> int:
  args = parse_args()
  try:
    manifest = build_set(
        args.source_manifest.resolve(), args.materialized_root.resolve(),
        args.tag_tool.resolve(), args.output.resolve(), force=args.force,
    )
  except Exception as exc:  # noqa: BLE001 - report exact provenance failure.
    print(f"unable to prepare independent chart references: {exc}", file=sys.stderr)
    return 1
  print(f"output={args.output.resolve()}")
  print(f"references={manifest['matrix']['reference_count']}")
  print(f"artifacts={manifest['artifact_summary']['artifact_count']}")
  print(f"artifact_set_sha256={manifest['artifact_summary']['artifact_set_sha256']}")
  print("promotion_eligible=false")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
