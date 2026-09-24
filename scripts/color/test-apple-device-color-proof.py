#!/usr/bin/env python3
"""Adversarial tests for the physical Apple color-proof contract."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any, Callable


SCRIPT_DIR = Path(__file__).parent
VALIDATOR_PATH = SCRIPT_DIR / "validate-apple-device-color-proof.py"
REAL_TEST_PATH = SCRIPT_DIR / "test-real-raw-render-set.py"
REAL_VALIDATOR_PATH = SCRIPT_DIR / "validate-real-raw-render-set.py"
TAGGED_TEST_PATH = SCRIPT_DIR / "test-independent-chart-tagged-set.py"
TAGGED_VALIDATOR_PATH = SCRIPT_DIR / "validate-independent-chart-tagged-set.py"


def load_module(path: Path, name: str) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load module: {path}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


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


def sha256_bytes(value: bytes) -> str:
  return hashlib.sha256(value).hexdigest()


def build_fixture(
    root: Path, validator: Any,
) -> tuple[Path, Path, Path, Path, Path, dict[str, Any], Path]:
  real_test = load_module(REAL_TEST_PATH, "apple_real_test_fixture")
  real_validator = load_module(REAL_VALIDATOR_PATH, "apple_real_validator")
  real_root = root / "real-raw-render-set"
  real_root.mkdir()
  real_manifest_path, inventory_path, real_manifest, _ = real_test.make_fixture(
      real_root, real_validator
  )
  cell = real_manifest["cells"][0]
  shipping = next(
      render for render in cell["renders"]
      if render["decode_path"] == "shipping-default"
  )
  shipping_report_path = real_root / shipping["report_path"]
  shipping_report = load_json(shipping_report_path)
  shipping_report["decoded_metadata"]["decoder_backend"] = (
      "Core Image CIRAWFilter"
  )
  write_json(shipping_report_path, shipping_report)
  shipping["decoder_backend"] = "Core Image CIRAWFilter"
  shipping["report_sha256"] = real_validator.sha256_file(shipping_report_path)
  cell["same_decoder_backend"] = False
  artifact_paths = [
      value
      for render in cell["renders"]
      for value in (render["output_path"], render["report_path"])
  ]
  byte_size, artifact_digest = real_validator.artifact_set_digest(
      real_root, artifact_paths
  )
  real_manifest["artifact_summary"] = {
      "artifact_count": len(artifact_paths), "byte_size": byte_size,
      "artifact_set_sha256": artifact_digest,
      "hash_algorithm": real_validator.HASH_ALGORITHM,
  }
  write_json(real_manifest_path, real_manifest)
  real_errors = real_validator.validate_manifest(
      real_manifest_path, source_inventory_path=inventory_path,
      artifact_root=real_root,
  )
  if real_errors:
    raise AssertionError(f"Core Image real-RAW fixture is invalid: {real_errors}")

  tagged_test = load_module(TAGGED_TEST_PATH, "apple_tagged_test_fixture")
  tagged_validator = load_module(
      TAGGED_VALIDATOR_PATH, "apple_tagged_validator"
  )
  tagged_root = root / "tagged-set"
  tagged_root.mkdir()
  tagged_manifest_path, tagged_manifest = tagged_test.create_set(
      tagged_root, tagged_validator
  )
  tagged_errors = tagged_validator.validate_manifest(
      tagged_manifest_path, artifact_root=tagged_root
  )
  if tagged_errors:
    raise AssertionError(f"tagged fixture is invalid: {tagged_errors}")

  raw_renderer = root / "raw-renderer"
  metal_test = root / "metal-test"
  raw_renderer.write_bytes(b"raw-renderer-binary")
  metal_test.write_bytes(b"metal-test-binary")
  metal_root = root / "metal"
  metal_root.mkdir()
  metal_report_path = metal_root / "metal-aces2-device-proof.json"
  metal_report = {
      "schema": validator.METAL_SCHEMA,
      "tool_version": validator.METAL_TOOL_VERSION,
      "hardware": {
          "backend": "Metal", "device_name": "Fixture GPU",
          "kernel_type": "darwin", "kernel_version": "fixture-kernel",
          "product_type": "macOS", "product_version": "fixture-os",
          "cpu_architecture": "arm64", "qt_version": "6.test",
          "build_mode": "release",
      },
      "luts": {
          "final_cube_size": 97, "interactive_cube_size": 65,
          "final_cache_key_sha256": sha256_bytes(b"final-lut"),
          "interactive_cache_key_sha256": sha256_bytes(b"interactive-lut"),
      },
      "synthetic_parity": {
          "final_97": {
              "pixel_count": 169, "p99_delta_e_00": 0.01,
              "maximum_delta_e_00": 0.02, "maximum_channel_error": 1.0e-5,
              "p99_gate": 0.25, "maximum_gate": 0.75,
              "channel_gate": 2.0e-4,
          },
          "interactive_65": {
              "pixel_count": 169, "p99_delta_e_00": 0.01,
              "maximum_delta_e_00": 0.02, "maximum_channel_error": 1.0e-5,
              "p99_gate": 0.25, "maximum_gate": 0.75,
              "channel_gate": 2.0e-4,
          },
      },
      "tagged_chart_parity": {
          "required": True, "set_id": tagged_manifest["set_id"],
          "manifest_sha256": validator.sha256_file(tagged_manifest_path),
          "artifact_set_sha256": tagged_manifest["artifact_summary"]["artifact_set_sha256"],
          "reference_count": 48, "pixel_count": 11751552,
          "delta_e_00_over_0_25": 10, "allowed_exceedances": 117515,
          "maximum_delta_e_00": 0.1, "maximum_channel_error": 1.0e-5,
          "maximum_gate": 0.75, "channel_gate": 2.0e-4,
      },
      "timing": {
          "sample_count": 48, "pixel_count_per_sample": 606 * 404,
          "p95_wall_ms": 4.0, "maximum_wall_ms": 6.0,
          "p95_gate_ms": 25.0, "maximum_gate_ms": 50.0,
          "telemetry_final_output_ms": 200,
      },
      "telemetry": {
          "command_buffer_count": 52, "kernel_dispatch_count": 52,
          "cpu_to_gpu_bytes": 1024, "gpu_to_cpu_bytes": 512,
      },
      "result": "passed",
  }
  write_json(metal_report_path, metal_report)
  paths = validator.all_artifact_paths(
      root, root / "apple-device-color-proof.json"
  )
  byte_size, digest = validator.artifact_set_digest(root, paths)
  manifest = {
      "schema": validator.SCHEMA, "proof_id": "fixture-apple-proof",
      "description": "contract-only physical Apple proof fixture",
      "promotion_eligible": False,
      "promotion_blockers": ["editorial-approval-pending"],
      "host": {
          "system": "Darwin", "release": "fixture-release",
          "machine": "arm64", "python_version": "fixture-python",
      },
      "inputs": {
          "source_inventory_manifest_sha256": validator.sha256_file(inventory_path),
          "tagged_set_id": tagged_manifest["set_id"],
          "tagged_set_manifest_sha256": validator.sha256_file(tagged_manifest_path),
          "tagged_set_artifact_sha256": tagged_manifest["artifact_summary"]["artifact_set_sha256"],
          "real_raw_set_id": real_manifest["set_id"],
          "real_raw_manifest_path": "real-raw-render-set/real-raw-render-set.json",
          "real_raw_manifest_sha256": validator.sha256_file(real_manifest_path),
          "real_raw_artifact_sha256": real_manifest["artifact_summary"]["artifact_set_sha256"],
      },
      "tools": {
          "raw_renderer_sha256": validator.sha256_file(raw_renderer),
          "metal_test_sha256": validator.sha256_file(metal_test),
          "runner_sha256": validator.sha256_file(
              SCRIPT_DIR / "run-apple-device-color-proof.py"
          ),
          "validator_sha256": validator.sha256_file(VALIDATOR_PATH),
      },
      "core_image": {
          "required_backend": "Core Image CIRAWFilter",
          "candidate_count": 1, "passed_count": 1,
          "candidates": [{
              "candidate_id": cell["candidate_id"],
              "decoder_backend": "Core Image CIRAWFilter",
              "report_path": "real-raw-render-set/" + shipping["report_path"],
              "report_sha256": shipping["report_sha256"],
              "output_path": "real-raw-render-set/" + shipping["output_path"],
              "output_sha256": shipping["output_sha256"],
          }],
      },
      "metal": {
          "report_path": "metal/metal-aces2-device-proof.json",
          "report_sha256": validator.sha256_file(metal_report_path),
          "device_name": "Fixture GPU", "result": "passed",
      },
      "artifact_summary": {
          "artifact_count": len(paths), "byte_size": byte_size,
          "artifact_set_sha256": digest,
          "hash_algorithm": validator.HASH_ALGORITHM,
      },
      "result": "passed",
  }
  manifest_path = root / "apple-device-color-proof.json"
  write_json(manifest_path, manifest)
  return (
      manifest_path, inventory_path, tagged_manifest_path, raw_renderer,
      metal_test, manifest, metal_report_path,
  )


def main() -> int:
  validator = load_module(VALIDATOR_PATH, "apple_device_proof_validator")
  with tempfile.TemporaryDirectory(prefix="apple-device-proof-test-") as temporary:
    root = Path(temporary)
    (
        manifest_path, inventory_path, tagged_manifest_path, raw_renderer,
        metal_test, manifest, metal_report_path,
    ) = build_fixture(root, validator)

    def validate(value: dict[str, Any]) -> list[str]:
      write_json(manifest_path, value)
      return validator.validate_manifest(
          manifest_path, source_inventory_path=inventory_path,
          tagged_set_manifest_path=tagged_manifest_path,
          raw_renderer_path=raw_renderer, metal_test_path=metal_test,
          artifact_root=root,
      )

    baseline_errors = validate(manifest)
    if baseline_errors:
      raise AssertionError(f"valid Apple proof fixture rejected: {baseline_errors}")
    cases: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
        ("promotion", lambda value: value.__setitem__("promotion_eligible", True), "must not claim"),
        ("unknown-field", lambda value: value.__setitem__("surprise", True), "unknown field"),
        ("fallback", lambda value: value["core_image"]["candidates"][0].__setitem__("decoder_backend", "LibRaw"), "does not prove"),
        ("partial-core-image", lambda value: value["core_image"].__setitem__("passed_count", 0), "every real-RAW"),
        ("tool-substitution", lambda value: value["tools"].__setitem__("metal_test_sha256", sha256_bytes(b"other")), "executable hash mismatch"),
        ("metal-path-traversal", lambda value: value["metal"].__setitem__("report_path", "metal/../escape.json"), "unsafe"),
    ]
    for name, mutate, expected in cases:
      value = copy.deepcopy(manifest)
      mutate(value)
      errors = validate(value)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} mutation not rejected: {errors}")

    metal_report = load_json(metal_report_path)
    broken_report = copy.deepcopy(metal_report)
    broken_report["synthetic_parity"]["interactive_65"]["p99_delta_e_00"] = 0.3
    write_json(metal_report_path, broken_report)
    errors = validate(manifest)
    if not any("synthetic parity exceeds" in error for error in errors):
      raise AssertionError(f"failing Metal metric was accepted: {errors}")
    write_json(metal_report_path, metal_report)

    broken_report = copy.deepcopy(metal_report)
    broken_report["telemetry"]["kernel_dispatch_count"] = 0
    write_json(metal_report_path, broken_report)
    errors = validate(manifest)
    if not any("positive GPU work" in error for error in errors):
      raise AssertionError(f"zero-work Metal report was accepted: {errors}")
    write_json(metal_report_path, metal_report)

    broken_report = copy.deepcopy(metal_report)
    broken_report["timing"]["p95_wall_ms"] = 25.1
    write_json(metal_report_path, broken_report)
    errors = validate(manifest)
    if not any("p95 latency gate failed" in error for error in errors):
      raise AssertionError(f"failing Metal latency was accepted: {errors}")
    write_json(metal_report_path, metal_report)

    broken_report = copy.deepcopy(metal_report)
    broken_report["hardware"]["build_mode"] = "debug"
    write_json(metal_report_path, broken_report)
    errors = validate(manifest)
    if not any("Release executable" in error for error in errors):
      raise AssertionError(f"Debug Metal proof was accepted: {errors}")
    write_json(metal_report_path, metal_report)

    raw_output_path = root / manifest["core_image"]["candidates"][0]["output_path"]
    original_output = raw_output_path.read_bytes()
    raw_output_path.write_bytes(original_output + b"tamper")
    errors = validate(manifest)
    if not any("artifact" in error and "hash" in error for error in errors):
      raise AssertionError(f"device-proof byte tampering was accepted: {errors}")

  print("Apple device color-proof contract tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
