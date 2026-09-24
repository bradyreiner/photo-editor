#!/usr/bin/env python3
"""Adversarial tests for the real-RAW render-set evidence contract."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any, Callable


VALIDATOR_PATH = Path(__file__).with_name("validate-real-raw-render-set.py")


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "real_raw_render_set_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError("unable to load real-RAW render-set validator")
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


def load_json(path: Path) -> dict[str, Any]:
  with path.open("r", encoding="utf-8") as handle:
    value = json.load(handle)
  if not isinstance(value, dict):
    raise ValueError(f"JSON root must be an object: {path}")
  return value


def statistics() -> dict[str, Any]:
  return {
      "pixel_count": 4, "nonfinite_sample_count": 0,
      "red_minimum": 0.01, "red_maximum": 0.8,
      "green_minimum": 0.02, "green_maximum": 0.7,
      "blue_minimum": 0.03, "blue_maximum": 0.6,
      "alpha_minimum": 1.0, "alpha_maximum": 1.0,
  }


def make_report(
    source_id: str, source_sha256: str, output_id: str, output_sha256: str,
    decode_path: str, output_pixel_sha256: str,
) -> dict[str, Any]:
  return {
      "schema": "photo-editor-real-raw-candidate-report:v2",
      "tool_version": "photo-editor-real-raw-candidate-render:v3",
      "source_id": source_id, "output_id": output_id,
      "source_file_sha256": source_sha256, "source_byte_size": 8,
      "decoded_pixel_sha256": sha256_bytes(b"decoded"),
      "decoded_icc_sha256": sha256_bytes(b"linear-srgb"),
      "decoded_width": 2, "decoded_height": 2,
      "decoded_storage": "rgba32-float",
      "decoded_statistics": statistics(),
      "decoded_metadata": {
          "valid": True, "is_raw": True,
          "pixel_encoding": (
              "scene-linear-rgb"
              if decode_path == "portable-libraw"
              else "camera-linear-rgb"
          ),
          "decoder_backend": (
              "LibRaw characterized linear-sRGB test"
              if decode_path == "portable-libraw"
              else "LibRaw camera-linear test"
          ),
          "decoded_color_space_name": "Linear sRGB",
          "camera_profile_identity": "fixture-profile",
          "black_subtraction_applied": True,
          "white_normalization_applied": True,
          "demosaic_applied": True,
          "white_balance_applied": decode_path == "portable-libraw",
          "highlight_reconstruction_applied": False,
          "lens_correction_applied": False,
          "baseline_exposure_applied": False,
          "camera_transform_applied": decode_path == "portable-libraw",
          "decoder_rendering_applied": decode_path == "portable-libraw",
          "input_profile_name": "Embedded", "camera_make": "Fixture Make",
          "camera_model": "Fixture Model", "as_shot_temperature": 5200,
          "as_shot_tint": 0, "baseline_exposure": 0,
          "highlight_headroom": 1,
      },
      "decoder_capability": {
          "available": True, "strict_version": True, "backend": "LibRaw",
          "compiled_version": "test", "runtime_version": "test",
          "validated_version": "test", "pixel_contract": "fixture",
          "applied_controls": "fixture", "deferred_controls": "fixture",
          "maximum_raw_memory_bytes": 1024,
      },
      "output_file_sha256": output_sha256,
      "output_pixel_sha256": output_pixel_sha256,
      "output_statistics": statistics(),
      "output_icc_sha256": sha256_bytes(b"srgb"),
      "output_color_space": "sRGB", "output_storage": "rgba16-png",
      "recipe": {
          "schema": "photo-editor-real-raw-recipe:v1",
          "decode_path": decode_path, "input_color_profile": "Embedded",
          "working_color_profile": "Linear sRGB",
          "raw_demosaic_quality": "Full", "raw_highlight_recovery": True,
          "raw_gamut_mapping": False, "raw_lens_correction": True,
          "output_color_profile": "sRGB",
          "photographic_rendering_model": "aces2-ocio-v1",
          "render_intent": "Perceptual",
          "output_gamut_mapping_policy": "Perceptual",
          "black_point_compensation": True, "backend": "cpu-only",
          "output_storage": "rgba16-png",
      },
      "recipe_sha256": sha256_bytes(decode_path.encode("ascii")),
      "backend_requested": "cpu-only", "backend_used": "cpu",
      "fallback_reason": "cpu_requested", "qt_version": "6.test",
  }


def make_fixture(
    root: Path, validator: Any
) -> tuple[Path, Path, dict[str, Any], dict[str, Path]]:
  candidate_id = "fixture-real-raw"
  source_id = f"sources/{candidate_id}/capture"
  source_sha256 = sha256_bytes(b"raw-data")
  inventory = {
      "schema": "photo-editor-color-source-inventory:v1",
      "inventory_id": "fixture-inventory", "promotion_eligible": False,
      "promotion_blockers": ["pending-license-review"],
      "candidates": [{
          "candidate_id": candidate_id,
          "capture": {"sha256": source_sha256},
          "camera": {"make": "Fixture Make", "model": "Fixture Model"},
          "illuminant": "daylight", "scene_tags": ["sky"],
      }],
  }
  inventory_path = root / "source-inventory.json"
  write_json(inventory_path, inventory)
  artifacts: list[str] = []
  renders: list[dict[str, Any]] = []
  report_paths: dict[str, Path] = {}
  output_pixel_sha256 = sha256_bytes(b"same-pixels")
  for decode_path in validator.RENDER_PATHS:
    output_id = f"renders/{candidate_id}/{decode_path}/candidate.png"
    report_id = f"reports/{candidate_id}/{decode_path}/candidate.json"
    output_path = root / output_id
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(b"png:" + decode_path.encode("ascii"))
    output_sha256 = validator.sha256_file(output_path)
    report = make_report(
        source_id, source_sha256, output_id, output_sha256, decode_path,
        output_pixel_sha256,
    )
    report_path = root / report_id
    write_json(report_path, report)
    report_paths[decode_path] = report_path
    renders.append({
        "decode_path": decode_path, "output_id": output_id,
        "output_path": output_id, "output_sha256": output_sha256,
        "report_path": report_id,
        "report_sha256": validator.sha256_file(report_path),
        "decoder_backend": report["decoded_metadata"]["decoder_backend"],
        "output_pixel_sha256": output_pixel_sha256,
    })
    artifacts.extend((output_id, report_id))
  byte_size, artifact_digest = validator.artifact_set_digest(root, artifacts)
  manifest = {
      "schema": validator.SCHEMA, "set_id": "fixture-set",
      "description": "validator fixture", "promotion_eligible": False,
      "promotion_blockers": ["pending-license-review"],
      "source_inventory": {
          "inventory_id": inventory["inventory_id"],
          "manifest_sha256": validator.sha256_file(inventory_path),
      },
      "tools": {
          "candidate_renderer_sha256": sha256_bytes(b"renderer"),
          "candidate_tool_version": validator.TOOL_VERSION,
          "qt_version": "6.test",
      },
      "render_paths": list(validator.RENDER_PATHS),
      "matrix": {
          "candidate_count": 1, "camera_count": 1,
          "illuminants": ["daylight"], "render_count": 2,
      },
      "artifact_summary": {
          "artifact_count": 4, "byte_size": byte_size,
          "artifact_set_sha256": artifact_digest,
          "hash_algorithm": validator.HASH_ALGORITHM,
      },
      "cells": [{
          "candidate_id": candidate_id, "source_id": source_id,
          "source_sha256": source_sha256, "camera_make": "Fixture Make",
          "camera_model": "Fixture Model", "illuminant": "daylight",
          "scene_tags": ["sky"], "same_decoder_backend": False,
          "pixel_identical": True, "renders": renders,
      }],
  }
  manifest_path = root / "real-raw-render-set.json"
  write_json(manifest_path, manifest)
  return manifest_path, inventory_path, manifest, report_paths


def main() -> int:
  validator = load_validator()
  with tempfile.TemporaryDirectory(prefix="real-raw-render-set-test-") as temporary:
    root = Path(temporary)
    manifest_path, inventory_path, manifest, report_paths = make_fixture(
        root, validator
    )

    def validate(value: dict[str, Any]) -> list[str]:
      write_json(manifest_path, value)
      return validator.validate_manifest(
          manifest_path, source_inventory_path=inventory_path,
          artifact_root=root,
      )

    baseline_errors = validate(manifest)
    if baseline_errors:
      raise AssertionError(f"valid real-RAW fixture rejected: {baseline_errors}")

    cases: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
        ("promotion", lambda value: value.__setitem__("promotion_eligible", True), "non-promotable"),
        ("unknown-field", lambda value: value.__setitem__("surprise", True), "unknown field"),
        ("missing-path", lambda value: value["cells"][0]["renders"].pop(), "both required paths"),
        ("source-substitution", lambda value: value["cells"][0].__setitem__("source_sha256", sha256_bytes(b"other")), "source inventory"),
        ("traversal", lambda value: value["cells"][0]["renders"][0].__setitem__("output_path", "renders/../escape.png"), "canonical"),
        ("false-backend-claim", lambda value: value["cells"][0].__setitem__("same_decoder_backend", True), "inaccurate"),
    ]
    for name, mutate, expected in cases:
      value = copy.deepcopy(manifest)
      mutate(value)
      errors = validate(value)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} mutation not rejected as expected: {errors}")

    portable_report_path = report_paths[validator.RENDER_PATHS[0]]
    original_report = load_json(portable_report_path)
    broken_report = copy.deepcopy(original_report)
    broken_report["output_statistics"]["alpha_minimum"] = 0
    write_json(portable_report_path, broken_report)
    errors = validate(manifest)
    if not any("fully opaque" in error for error in errors):
      raise AssertionError(f"transparent output report was accepted: {errors}")
    write_json(portable_report_path, original_report)

    output_path = root / manifest["cells"][0]["renders"][0]["output_path"]
    original_output = output_path.read_bytes()
    output_path.write_bytes(original_output + b"tamper")
    errors = validate(manifest)
    if not any("artifact hash mismatch" in error for error in errors):
      raise AssertionError(f"output byte tampering was accepted: {errors}")

  print("real RAW render-set contract tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
