#!/usr/bin/env python3
"""Adversarial tests for the complete independent chart render-set contract."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any, Callable


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-render-set.py"
)
BLOCKERS = [
    "no-sensor-raw-files",
    "eight-bit-registered-chart-crops",
    "no-measured-spectral-patch-values",
    "project-license-review-pending",
    "colorchecker-target-rights-review-pending",
    "no-real-dcp-profile",
    "no-rights-cleared-skin-capture",
    "tagged-reference-set-not-yet-corpus-bound",
]


def load_validator():
  spec = importlib.util.spec_from_file_location(
      "independent_chart_render_set_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError("unable to load independent chart render-set validator")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def sha256_bytes(value: bytes) -> str:
  return hashlib.sha256(value).hexdigest()


def write_json(path: Path, value: dict[str, Any]) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(
      json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
  )


def make_fixture(root: Path, validator: Any) -> tuple[Path, Path, dict[str, Any]]:
  cameras = [
      {"directory": f"Camera{index}", "manufacturer": f"Maker{index}",
       "model": f"Model{index}"}
      for index in range(6)
  ]
  illuminants = [
      {"code": "i1", "description": "tungsten photo flood", "kelvin": 3200},
      {"code": "i2", "description": "daylight photo flood", "kelvin": 4800},
  ]
  exposures = [
      {"code": "e1", "stops": -2}, {"code": "e2", "stops": -1},
      {"code": "e3", "stops": 0}, {"code": "e4", "stops": 1},
  ]
  tagged_references: list[dict[str, Any]] = []
  for camera in cameras:
    for illuminant in illuminants:
      for exposure in exposures:
        stem = f"wb1{illuminant['code']}{exposure['code']}"
        payload = f"source:{camera['directory']}:{stem}".encode("ascii")
        tagged_references.append({
            "camera_directory": camera["directory"],
            "manufacturer": camera["manufacturer"],
            "model": camera["model"],
            "illuminant_code": illuminant["code"],
            "kelvin": illuminant["kelvin"],
            "exposure_code": exposure["code"],
            "stops": exposure["stops"],
            "output_id": (
                f"references/checker24/{camera['directory']}/"
                f"{stem}-linear-srgb.png"
            ),
            "output_sha256": sha256_bytes(payload),
        })
  tagged = {
      "schema": "photo-editor-independent-chart-tagged-set:v1",
      "set_id": "middlebury-checker24-linear-srgb-tagged-v1",
      "promotion_blockers": BLOCKERS,
      "matrix": {
          "cameras": cameras, "illuminants": illuminants,
          "exposures": exposures, "reference_count": 48,
      },
      "artifact_summary": {"artifact_set_sha256": sha256_bytes(b"tagged-set")},
      "references": tagged_references,
  }
  tagged_path = root / "tagged-reference-set.json"
  write_json(tagged_path, tagged)

  recipe_sha256 = sha256_bytes(b"desktop-export-recipe")
  output_icc_sha256 = sha256_bytes(b"srgb-icc")
  candidate_renderer_sha256 = sha256_bytes(b"candidate-renderer")
  generator_sha256 = sha256_bytes(b"reference-generator")
  cells: list[dict[str, Any]] = []
  artifact_paths: list[str] = []
  for source in tagged_references:
    camera = source["camera_directory"]
    stem = f"wb1{source['illuminant_code']}{source['exposure_code']}"
    candidate_id = f"candidates/checker24/{camera}/{stem}-aces2-srgb.png"
    candidate_report_id = (
        f"candidate-reports/checker24/{camera}/{stem}-aces2-srgb.json"
    )
    reference_id = f"references/checker24/{camera}/{stem}-ocio-aces2-srgb.png"
    reference_report_id = (
        f"reference-reports/checker24/{camera}/{stem}-ocio-aces2-srgb.json"
    )
    candidate_payload = f"candidate:{camera}:{stem}".encode("ascii")
    reference_payload = f"reference:{camera}:{stem}".encode("ascii")
    candidate_path = root / candidate_id
    reference_path = root / reference_id
    candidate_path.parent.mkdir(parents=True, exist_ok=True)
    reference_path.parent.mkdir(parents=True, exist_ok=True)
    candidate_path.write_bytes(candidate_payload)
    reference_path.write_bytes(reference_payload)
    candidate_sha256 = sha256_bytes(candidate_payload)
    reference_sha256 = sha256_bytes(reference_payload)
    candidate_report = {
        "schema": "photo-editor-independent-chart-candidate-report:v1",
        "tool_version": "photo-editor-independent-chart-candidate-render:v1",
        "renderer": "shipping-desktop-render-output-image-with-backend",
        "qt_version": "6.test", "source_id": source["output_id"],
        "output_id": candidate_id,
        "source_file_sha256": source["output_sha256"],
        "source_pixel_sha256": sha256_bytes(b"source-pixels"),
        "source_icc_sha256": sha256_bytes(b"linear-srgb-icc"),
        "output_file_sha256": candidate_sha256,
        "output_pixel_sha256": sha256_bytes(candidate_payload + b":pixels"),
        "output_icc_sha256": output_icc_sha256,
        "output_color_space": "sRGB", "output_storage": "rgba16-png",
        "width": 606, "height": 404,
        "recipe": {
            "schema": "photo-editor-chart-recipe:v1",
            "input_color_profile": "Embedded",
            "working_color_profile": "Linear sRGB",
            "output_color_profile": "sRGB",
            "photographic_rendering_model": "aces2-ocio-v1",
            "render_intent": "Perceptual",
            "output_gamut_mapping_policy": "Perceptual",
            "black_point_compensation": True,
            "source_black_point": 0, "output_black_point": 0,
            "output_headroom": 1, "backend": "cpu-only",
            "render_path": "desktop-export", "output_storage": "rgba16-png",
        },
        "recipe_sha256": recipe_sha256, "backend_requested": "cpu-only",
        "backend_used": "cpu", "fallback_reason": "cpu_requested",
    }
    reference_report = {
        "schema": "photo-editor-independent-chart-ocio-reference-report:v2",
        "tool_version": "photo-editor-independent-chart-ocio-reference:v2",
        "renderer": "OpenImageIO ImageBufAlgo.ociodisplay",
        "independent_of_photo_editor": True, "source_id": source["output_id"],
        "output_id": reference_id,
        "source_file_sha256": source["output_sha256"],
        "output_file_sha256": reference_sha256,
        "output_pixel_sha1": hashlib.sha1(reference_payload).hexdigest().upper(),
        "output_color_space": "sRGB", "processing_storage": "rgba32-float",
        "output_storage": "rgba16-png", "width": 606, "height": 404,
        "ocio_version": "2.5.1", "openimageio_version": "3.1.9.0",
        "built_in_config": "cg-config-v4.0.0_aces-v2.0_ocio-v2.5",
        "config_cache_id": "config-test", "processor_cache_id": "processor-test",
        "source_color_space": "Linear Rec.709 (sRGB)",
        "display": "sRGB - Display",
        "view": "ACES 2.0 - SDR 100 nits (Rec.709)",
        "generator_sha256": generator_sha256,
        "png_color_metadata": ["sRGB", "gAMA", "cHRM"],
    }
    candidate_report_path = root / candidate_report_id
    reference_report_path = root / reference_report_id
    write_json(candidate_report_path, candidate_report)
    write_json(reference_report_path, reference_report)
    cells.append({
        "camera_directory": camera, "manufacturer": source["manufacturer"],
        "model": source["model"], "illuminant_code": source["illuminant_code"],
        "kelvin": source["kelvin"], "exposure_code": source["exposure_code"],
        "stops": source["stops"], "source_id": source["output_id"],
        "source_sha256": source["output_sha256"], "candidate_id": candidate_id,
        "candidate_sha256": candidate_sha256,
        "candidate_report_id": candidate_report_id,
        "candidate_report_sha256": validator.sha256_file(candidate_report_path),
        "reference_id": reference_id, "reference_sha256": reference_sha256,
        "reference_report_id": reference_report_id,
        "reference_report_sha256": validator.sha256_file(reference_report_path),
    })
    artifact_paths.extend((
        candidate_id, candidate_report_id, reference_id, reference_report_id,
    ))
  byte_size, artifact_digest = validator.artifact_set_digest(root, artifact_paths)
  manifest = {
      "schema": validator.SCHEMA, "set_id": validator.SET_ID,
      "description": "synthetic validator fixture", "promotion_eligible": False,
      "promotion_blockers": BLOCKERS,
      "source": {
          "tagged_set_id": tagged["set_id"],
          "manifest_sha256": validator.sha256_file(tagged_path),
          "artifact_set_sha256": tagged["artifact_summary"]["artifact_set_sha256"],
      },
      "tools": {
          "candidate_renderer_sha256": candidate_renderer_sha256,
          "reference_generator_sha256": generator_sha256,
          "candidate_tool_version": (
              "photo-editor-independent-chart-candidate-render:v1"
          ),
          "reference_tool_version": (
              "photo-editor-independent-chart-ocio-reference:v2"
          ),
          "qt_version": "6.test", "ocio_version": "2.5.1",
          "openimageio_version": "3.1.9.0",
      },
      "recipe": {
          "sha256": recipe_sha256, "render_path": "desktop-export",
          "photographic_rendering_model": "aces2-ocio-v1",
      },
      "output_profile": {
          "name": "sRGB", "icc_sha256": output_icc_sha256,
          "storage": "rgba16-png",
      },
      "matrix": {
          "cameras": cameras, "illuminants": illuminants,
          "exposures": exposures, "cell_count": 48,
      },
      "artifact_summary": {
          "artifact_count": 192, "byte_size": byte_size,
          "artifact_set_sha256": artifact_digest,
          "hash_algorithm": validator.HASH_ALGORITHM,
      },
      "cells": cells,
  }
  manifest_path = root / "render-set.json"
  write_json(manifest_path, manifest)
  return manifest_path, tagged_path, manifest


def main() -> int:
  validator = load_validator()
  with tempfile.TemporaryDirectory(prefix="chart-render-set-test-") as temporary:
    root = Path(temporary)
    manifest_path, tagged_path, manifest = make_fixture(root, validator)

    def validate(value: dict[str, Any]) -> list[str]:
      write_json(manifest_path, value)
      return validator.validate_manifest(
          manifest_path, tagged_set_manifest_path=tagged_path,
          artifact_root=root,
      )

    baseline_errors = validate(manifest)
    if baseline_errors:
      raise AssertionError(f"valid render-set fixture rejected: {baseline_errors}")

    cases: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
        ("unknown field", lambda value: value.__setitem__("surprise", True),
         "unknown field"),
        ("missing cell", lambda value: value["cells"].pop(), "exactly 48"),
        ("false promotion", lambda value: value.__setitem__(
            "promotion_eligible", True), "must not claim promotion"),
        ("path traversal", lambda value: value["cells"][0].__setitem__(
            "candidate_id", "candidates/checker24/../escape.png"), "unsafe"),
    ]
    for name, mutate, expected in cases:
      value = copy.deepcopy(manifest)
      mutate(value)
      errors = validate(value)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} was not rejected as expected: {errors}")

    candidate_path = root / manifest["cells"][0]["candidate_id"]
    original_candidate = candidate_path.read_bytes()
    candidate_path.write_bytes(original_candidate + b"tampered")
    errors = validate(copy.deepcopy(manifest))
    candidate_path.write_bytes(original_candidate)
    if not any("artifact hash mismatch" in error for error in errors):
      raise AssertionError(f"candidate byte tampering was not rejected: {errors}")

    low_precision = copy.deepcopy(manifest)
    report_id = low_precision["cells"][0]["reference_report_id"]
    report_path = root / report_id
    original_report = report_path.read_bytes()
    report = json.loads(original_report)
    report["processing_storage"] = "rgba8-unorm"
    write_json(report_path, report)
    low_precision["cells"][0]["reference_report_sha256"] = (
        validator.sha256_file(report_path)
    )
    artifact_paths = []
    for cell in low_precision["cells"]:
      artifact_paths.extend((
          cell["candidate_id"], cell["candidate_report_id"],
          cell["reference_id"], cell["reference_report_id"],
      ))
    byte_size, digest = validator.artifact_set_digest(root, artifact_paths)
    low_precision["artifact_summary"]["byte_size"] = byte_size
    low_precision["artifact_summary"]["artifact_set_sha256"] = digest
    errors = validate(low_precision)
    report_path.write_bytes(original_report)
    if not any("processing_storage" in error for error in errors):
      raise AssertionError(f"rehashed 8-bit oracle was not rejected: {errors}")

  print("independent chart render-set adversarial tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
