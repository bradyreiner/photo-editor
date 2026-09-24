#!/usr/bin/env python3
"""Validate a pass-only Windows Release LibRaw execution report."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "photo-editor-windows-raw-runtime-proof:v1"
TOOL_VERSION = "photo-editor-raw-camera-linear-decoder-test:v1"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ROOT_KEYS = {
    "schema", "tool_version", "hardware", "executable_sha256",
    "capability", "synthetic_dng", "contracts", "result",
}
HARDWARE_KEYS = {
    "kernel_type", "kernel_version", "product_type", "product_version",
    "cpu_architecture", "qt_version", "build_mode",
}
CAPABILITY_KEYS = {
    "backend", "compiled_version", "runtime_version", "validated_version",
    "pixel_contract", "maximum_raw_memory_bytes",
}
SYNTHETIC_KEYS = {
    "sha256", "width", "height", "camera_linear_backend",
    "shipping_backend", "camera_linear_pixel_sha256",
    "shipping_rgb_maximum", "concurrent_decode_count",
}
CONTRACT_KEYS = {
    "atomic_failure", "bounded_preview", "camera_profile_routing",
    "camera_rgb_preservation", "concurrent_determinism",
    "explicit_transform_ownership", "opaque_finite_shipping_pixels",
    "white_balance_override",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("report", type=Path)
  parser.add_argument("--executable", type=Path, required=True)
  return parser.parse_args()


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def exact_object(
    value: Any, keys: set[str], label: str, errors: list[str]
) -> dict[str, Any]:
  if not isinstance(value, dict):
    errors.append(f"{label} must be an object")
    return {}
  for key in sorted(set(value) - keys):
    errors.append(f"{label}: unknown field {key!r}")
  for key in sorted(keys - set(value)):
    errors.append(f"{label}: missing required field {key!r}")
  return value


def nonempty_string(value: Any) -> bool:
  return isinstance(value, str) and bool(value)


def validate_report(report_path: Path, executable_path: Path) -> list[str]:
  errors: list[str] = []
  try:
    with report_path.open("r", encoding="utf-8") as handle:
      report = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    return [f"unable to read Windows RAW runtime report: {exc}"]
  report = exact_object(report, ROOT_KEYS, "report", errors)
  hardware = exact_object(
      report.get("hardware"), HARDWARE_KEYS, "report.hardware", errors
  )
  capability = exact_object(
      report.get("capability"), CAPABILITY_KEYS, "report.capability", errors
  )
  synthetic = exact_object(
      report.get("synthetic_dng"), SYNTHETIC_KEYS,
      "report.synthetic_dng", errors,
  )
  contracts = exact_object(
      report.get("contracts"), CONTRACT_KEYS, "report.contracts", errors
  )
  if (
      report.get("schema") != SCHEMA
      or report.get("tool_version") != TOOL_VERSION
      or report.get("result") != "passed"
  ):
    errors.append("report must be a passed v1 Windows RAW runtime proof")
  if (
      hardware.get("kernel_type") != "winnt"
      or hardware.get("build_mode") != "release"
  ):
    errors.append("report must come from a Windows Release executable")
  for key in HARDWARE_KEYS - {"kernel_type", "build_mode"}:
    if not nonempty_string(hardware.get(key)):
      errors.append(f"report.hardware.{key} must be non-empty")
  if not executable_path.is_file():
    errors.append(f"proof executable is missing: {executable_path}")
  elif report.get("executable_sha256") != sha256_file(executable_path):
    errors.append("proof executable SHA-256 mismatch")
  if capability.get("backend") != "LibRaw":
    errors.append("runtime capability must use LibRaw")
  for key in ("compiled_version", "runtime_version", "validated_version"):
    value = capability.get(key)
    if not nonempty_string(value) or not value.startswith("0.21.5"):
      errors.append(f"report.capability.{key} must identify LibRaw 0.21.5")
  if (
      not nonempty_string(capability.get("pixel_contract"))
      or "no-camera-matrix" not in capability["pixel_contract"]
  ):
    errors.append("runtime capability must retain the no-camera-matrix contract")
  if capability.get("maximum_raw_memory_bytes") != 1536 * 1024 * 1024:
    errors.append("runtime capability must retain the 1.5 GiB RAW memory bound")
  for key in ("sha256", "camera_linear_pixel_sha256"):
    if (
        not isinstance(synthetic.get(key), str)
        or SHA256_PATTERN.fullmatch(synthetic[key]) is None
    ):
      errors.append(f"report.synthetic_dng.{key} must be a SHA-256 digest")
  if synthetic.get("width") != 128 or synthetic.get("height") != 128:
    errors.append("synthetic DNG must execute at its exact 128-by-128 dimensions")
  if not str(synthetic.get("camera_linear_backend", "")).startswith(
      "LibRaw camera-linear"
  ):
    errors.append("explicit camera-profile route did not execute camera-linear LibRaw")
  if not str(synthetic.get("shipping_backend", "")).startswith(
      "LibRaw characterized linear-sRGB"
  ):
    errors.append("Windows shipping route did not execute characterized LibRaw")
  maximum = synthetic.get("shipping_rgb_maximum")
  if not isinstance(maximum, (int, float)) or isinstance(maximum, bool) or maximum <= 0:
    errors.append("Windows shipping route must publish nonzero RGB")
  if synthetic.get("concurrent_decode_count") != 4:
    errors.append("runtime proof must execute four concurrent decoder instances")
  for key in CONTRACT_KEYS:
    if contracts.get(key) is not True:
      errors.append(f"report.contracts.{key} must pass")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_report(args.report, args.executable)
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print("status=valid-windows-release-raw-runtime-proof")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
