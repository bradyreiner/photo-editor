#!/usr/bin/env python3
"""Adversarial tests for the Windows RAW runtime proof validator."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any, Callable


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-windows-raw-runtime-proof.py"
)


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "windows_raw_runtime_proof_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load validator: {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


def write_json(path: Path, value: dict[str, Any]) -> None:
  path.write_text(
      json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
  )


def main() -> int:
  validator = load_validator()
  with tempfile.TemporaryDirectory(prefix="windows-raw-proof-") as temp:
    root = Path(temp)
    executable = root / "raw_camera_linear_decoder_test.exe"
    executable.write_bytes(b"fixture-release-executable")
    report_path = root / "windows-raw-runtime-proof.json"
    digest = hashlib.sha256(executable.read_bytes()).hexdigest()
    report = {
        "schema": validator.SCHEMA,
        "tool_version": validator.TOOL_VERSION,
        "hardware": {
            "kernel_type": "winnt", "kernel_version": "10.0",
            "product_type": "windows", "product_version": "11",
            "cpu_architecture": "x86_64", "qt_version": "6.7.3",
            "build_mode": "release",
        },
        "executable_sha256": digest,
        "capability": {
            "backend": "LibRaw", "compiled_version": "0.21.5-Release",
            "runtime_version": "0.21.5-Release",
            "validated_version": "0.21.5",
            "pixel_contract": "camera-rgb-device-values-no-camera-matrix",
            "maximum_raw_memory_bytes": 1536 * 1024 * 1024,
        },
        "synthetic_dng": {
            "sha256": "1" * 64, "width": 128, "height": 128,
            "camera_linear_backend": "LibRaw camera-linear 0.21.5-Release",
            "shipping_backend": "LibRaw characterized linear-sRGB 0.21.5-Release",
            "camera_linear_pixel_sha256": "2" * 64,
            "shipping_rgb_maximum": 0.75, "concurrent_decode_count": 4,
        },
        "contracts": {key: True for key in validator.CONTRACT_KEYS},
        "result": "passed",
    }
    write_json(report_path, report)
    errors = validator.validate_report(report_path, executable)
    if errors:
      raise AssertionError(f"valid Windows RAW proof rejected: {errors}")
    cases: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
        ("debug", lambda value: value["hardware"].__setitem__("build_mode", "debug"), "Windows Release"),
        ("wrong-platform", lambda value: value["hardware"].__setitem__("kernel_type", "darwin"), "Windows Release"),
        ("wrong-version", lambda value: value["capability"].__setitem__("runtime_version", "0.21.4"), "LibRaw 0.21.5"),
        ("camera-matrix", lambda value: value["capability"].__setitem__("pixel_contract", "camera-rgb"), "no-camera-matrix"),
        ("shipping-fallback", lambda value: value["synthetic_dng"].__setitem__("shipping_backend", "Qt"), "characterized LibRaw"),
        ("zero-output", lambda value: value["synthetic_dng"].__setitem__("shipping_rgb_maximum", 0.0), "nonzero RGB"),
        ("false-contract", lambda value: value["contracts"].__setitem__("atomic_failure", False), "must pass"),
        ("unknown-field", lambda value: value.__setitem__("surprise", True), "unknown field"),
    ]
    for name, mutate, expected in cases:
      value = copy.deepcopy(report)
      mutate(value)
      write_json(report_path, value)
      errors = validator.validate_report(report_path, executable)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} mutation was accepted: {errors}")
    executable.write_bytes(b"substituted")
    write_json(report_path, report)
    errors = validator.validate_report(report_path, executable)
    if not any("SHA-256 mismatch" in error for error in errors):
      raise AssertionError(f"executable substitution was accepted: {errors}")

  print("Windows RAW runtime-proof adversarial tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
