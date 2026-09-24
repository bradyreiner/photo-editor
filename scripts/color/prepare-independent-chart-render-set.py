#!/usr/bin/env python3
"""Render all tagged chart cells through shipping and independent ACES 2 paths."""

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
DEFAULT_REFERENCE_GENERATOR = Path(__file__).with_name(
    "render-independent-chart-ocio-reference.py"
)
TAGGED_VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-tagged-set.py"
)
RENDER_SET_VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-render-set.py"
)
SCHEMA = "photo-editor-independent-chart-render-set:v1"
SET_ID = "middlebury-checker24-aces2-ocio-srgb-render-set-v1"
HASH_ALGORITHM = (
    "sha256(sorted(logical_path + NUL + byte_size + NUL + file_sha256 + LF))"
)


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(
      description="Create all 48 shipping/direct-OCIO chart render pairs."
  )
  parser.add_argument("--tagged-set-root", type=Path, required=True)
  parser.add_argument("--candidate-tool", type=Path, required=True)
  parser.add_argument(
      "--reference-generator", type=Path, default=DEFAULT_REFERENCE_GENERATOR
  )
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
    digest.update(logical_path.encode("utf-8"))
    digest.update(b"\0")
    digest.update(str(size).encode("ascii"))
    digest.update(b"\0")
    digest.update(sha256_file(path).encode("ascii"))
    digest.update(b"\n")
    total += size
  return total, digest.hexdigest()


def run_checked(command: list[str], label: str) -> None:
  process = subprocess.run(
      command, check=False, capture_output=True, text=True
  )
  if process.returncode != 0:
    detail = process.stderr.strip() or process.stdout.strip()
    raise ValueError(f"{label} failed: {detail}")


def build_set(
    tagged_set_root: Path,
    candidate_tool: Path,
    reference_generator: Path,
    output: Path,
    *,
    force: bool,
) -> dict[str, Any]:
  tagged_manifest_path = tagged_set_root / "tagged-reference-set.json"
  tagged_validator = load_module(
      TAGGED_VALIDATOR_PATH, "independent_chart_tagged_set_validator"
  )
  tagged_errors = tagged_validator.validate_manifest(
      tagged_manifest_path, artifact_root=tagged_set_root
  )
  if tagged_errors:
    raise ValueError("tagged-set validation failed: " + "; ".join(tagged_errors))
  if not candidate_tool.is_file():
    raise ValueError(f"candidate renderer is missing: {candidate_tool}")
  if not reference_generator.is_file():
    raise ValueError(f"reference generator is missing: {reference_generator}")
  if output.exists() and not force:
    raise ValueError(f"output already exists: {output}; pass --force to replace it")

  tagged = load_json(tagged_manifest_path)
  staging = output.with_name(f".{output.name}.partial")
  shutil.rmtree(staging, ignore_errors=True)
  staging.mkdir(parents=True)
  cells: list[dict[str, Any]] = []
  artifact_paths: list[str] = []
  first_candidate: dict[str, Any] | None = None
  first_reference: dict[str, Any] | None = None
  try:
    for source in tagged["references"]:
      camera = source["camera_directory"]
      stem = (
          f"wb1{source['illuminant_code']}{source['exposure_code']}"
      )
      source_id = source["output_id"]
      source_path = tagged_set_root / source_id
      candidate_id = f"candidates/checker24/{camera}/{stem}-aces2-srgb.png"
      candidate_report_id = (
          f"candidate-reports/checker24/{camera}/{stem}-aces2-srgb.json"
      )
      reference_id = f"references/checker24/{camera}/{stem}-ocio-aces2-srgb.png"
      reference_report_id = (
          f"reference-reports/checker24/{camera}/{stem}-ocio-aces2-srgb.json"
      )
      candidate_path = staging / candidate_id
      candidate_report_path = staging / candidate_report_id
      reference_path = staging / reference_id
      reference_report_path = staging / reference_report_id
      for path in (
          candidate_path, candidate_report_path, reference_path,
          reference_report_path,
      ):
        path.parent.mkdir(parents=True, exist_ok=True)

      run_checked(
          [
              str(candidate_tool), "--input", str(source_path),
              "--output", str(candidate_path), "--report",
              str(candidate_report_path), "--source-id", source_id,
              "--output-id", candidate_id, "--render-path", "desktop-export",
          ],
          f"candidate render for {source_id}",
      )
      run_checked(
          [
              sys.executable, str(reference_generator), "--input",
              str(source_path), "--output", str(reference_path), "--report",
              str(reference_report_path), "--source-id", source_id,
              "--output-id", reference_id,
          ],
          f"independent reference render for {source_id}",
      )
      candidate = load_json(candidate_report_path)
      reference = load_json(reference_report_path)
      if candidate.get("source_file_sha256") != source["output_sha256"]:
        raise ValueError(f"candidate source identity mismatch for {source_id}")
      if reference.get("source_file_sha256") != source["output_sha256"]:
        raise ValueError(f"reference source identity mismatch for {source_id}")
      if candidate.get("output_file_sha256") != sha256_file(candidate_path):
        raise ValueError(f"candidate output hash mismatch for {source_id}")
      if reference.get("output_file_sha256") != sha256_file(reference_path):
        raise ValueError(f"reference output hash mismatch for {source_id}")
      if first_candidate is None:
        first_candidate = candidate
        first_reference = reference
      else:
        stable_candidate = (
            "tool_version", "qt_version", "recipe_sha256", "output_icc_sha256",
        )
        for key in stable_candidate:
          if candidate.get(key) != first_candidate.get(key):
            raise ValueError(f"candidate {key} changed within the render set")
        stable_reference = (
            "tool_version", "ocio_version", "openimageio_version",
            "generator_sha256", "config_cache_id", "processor_cache_id",
        )
        for key in stable_reference:
          if reference.get(key) != first_reference.get(key):
            raise ValueError(f"reference {key} changed within the render set")

      cells.append({
          "camera_directory": camera,
          "manufacturer": source["manufacturer"],
          "model": source["model"],
          "illuminant_code": source["illuminant_code"],
          "kelvin": source["kelvin"],
          "exposure_code": source["exposure_code"],
          "stops": source["stops"],
          "source_id": source_id,
          "source_sha256": source["output_sha256"],
          "candidate_id": candidate_id,
          "candidate_sha256": candidate["output_file_sha256"],
          "candidate_report_id": candidate_report_id,
          "candidate_report_sha256": sha256_file(candidate_report_path),
          "reference_id": reference_id,
          "reference_sha256": reference["output_file_sha256"],
          "reference_report_id": reference_report_id,
          "reference_report_sha256": sha256_file(reference_report_path),
      })
      artifact_paths.extend((
          candidate_id, candidate_report_id, reference_id, reference_report_id,
      ))

    if first_candidate is None or first_reference is None:
      raise ValueError("tagged source set did not contain renderable cells")
    byte_size, digest = artifact_set_digest(staging, artifact_paths)
    manifest = {
        "schema": SCHEMA,
        "set_id": SET_ID,
        "description": (
            "Shipping desktop ACES 2 renders paired with direct float-intermediate "
            "OpenColorIO references for every tagged Middlebury chart cell."
        ),
        "promotion_eligible": False,
        "promotion_blockers": tagged["promotion_blockers"],
        "source": {
            "tagged_set_id": tagged["set_id"],
            "manifest_sha256": sha256_file(tagged_manifest_path),
            "artifact_set_sha256": tagged["artifact_summary"][
                "artifact_set_sha256"
            ],
        },
        "tools": {
            "candidate_renderer_sha256": sha256_file(candidate_tool),
            "reference_generator_sha256": sha256_file(reference_generator),
            "candidate_tool_version": first_candidate["tool_version"],
            "reference_tool_version": first_reference["tool_version"],
            "qt_version": first_candidate["qt_version"],
            "ocio_version": first_reference["ocio_version"],
            "openimageio_version": first_reference["openimageio_version"],
        },
        "recipe": {
            "sha256": first_candidate["recipe_sha256"],
            "render_path": "desktop-export",
            "photographic_rendering_model": "aces2-ocio-v1",
        },
        "output_profile": {
            "name": "sRGB",
            "icc_sha256": first_candidate["output_icc_sha256"],
            "storage": "rgba16-png",
        },
        "matrix": {
            "cameras": tagged["matrix"]["cameras"],
            "illuminants": tagged["matrix"]["illuminants"],
            "exposures": tagged["matrix"]["exposures"],
            "cell_count": len(cells),
        },
        "artifact_summary": {
            "artifact_count": len(artifact_paths),
            "byte_size": byte_size,
            "artifact_set_sha256": digest,
            "hash_algorithm": HASH_ALGORITHM,
        },
        "cells": cells,
    }
    manifest_path = staging / "render-set.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    validator = load_module(
        RENDER_SET_VALIDATOR_PATH, "independent_chart_render_set_validator"
    )
    errors = validator.validate_manifest(
        manifest_path,
        tagged_set_manifest_path=tagged_manifest_path,
        artifact_root=staging,
    )
    if errors:
      raise ValueError("render-set validation failed: " + "; ".join(errors))
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
        args.tagged_set_root.resolve(), args.candidate_tool.resolve(),
        args.reference_generator.resolve(), args.output.resolve(),
        force=args.force,
    )
  except Exception as exc:  # noqa: BLE001 - preserve exact provenance failure.
    print(f"unable to prepare independent chart render set: {exc}", file=sys.stderr)
    return 1
  print(f"output={args.output.resolve()}")
  print(f"cells={manifest['matrix']['cell_count']}")
  print(f"artifacts={manifest['artifact_summary']['artifact_count']}")
  print(f"artifact_set_sha256={manifest['artifact_summary']['artifact_set_sha256']}")
  print("promotion_eligible=false")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
