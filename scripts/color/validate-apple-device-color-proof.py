#!/usr/bin/env python3
"""Validate a physical-Apple-device Core Image and Metal color proof."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import platform
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any


REAL_RAW_VALIDATOR_PATH = Path(__file__).with_name(
    "validate-real-raw-render-set.py"
)
TAGGED_VALIDATOR_PATH = Path(__file__).with_name(
    "validate-independent-chart-tagged-set.py"
)
SCHEMA = "photo-editor-apple-device-color-proof:v1"
METAL_SCHEMA = "photo-editor-metal-aces2-device-proof:v2"
METAL_TOOL_VERSION = "photo-editor-metal-aces2-output-parity:v2"
HASH_ALGORITHM = (
    "sha256(sorted(logical_path + NUL + byte_size + NUL + file_sha256 + LF))"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ROOT_KEYS = {
    "schema", "proof_id", "description", "promotion_eligible",
    "promotion_blockers", "host", "inputs", "tools", "core_image",
    "metal", "artifact_summary", "result",
}
HOST_KEYS = {"system", "release", "machine", "python_version"}
INPUT_KEYS = {
    "source_inventory_manifest_sha256", "tagged_set_id",
    "tagged_set_manifest_sha256", "tagged_set_artifact_sha256",
    "real_raw_set_id", "real_raw_manifest_path",
    "real_raw_manifest_sha256", "real_raw_artifact_sha256",
}
TOOLS_KEYS = {
    "raw_renderer_sha256", "metal_test_sha256", "runner_sha256",
    "validator_sha256",
}
CORE_KEYS = {
    "required_backend", "candidate_count", "passed_count", "candidates",
}
CORE_CANDIDATE_KEYS = {
    "candidate_id", "decoder_backend", "report_path", "report_sha256",
    "output_path", "output_sha256",
}
METAL_KEYS = {
    "report_path", "report_sha256", "device_name", "result",
}
SUMMARY_KEYS = {
    "artifact_count", "byte_size", "artifact_set_sha256", "hash_algorithm",
}
METAL_REPORT_KEYS = {
    "schema", "tool_version", "hardware", "luts", "synthetic_parity",
    "tagged_chart_parity", "timing", "telemetry", "result",
}
METAL_HARDWARE_KEYS = {
    "backend", "device_name", "kernel_type", "kernel_version",
    "product_type", "product_version", "cpu_architecture", "qt_version",
    "build_mode",
}
METAL_LUT_KEYS = {
    "final_cube_size", "interactive_cube_size", "final_cache_key_sha256",
    "interactive_cache_key_sha256",
}
METAL_SYNTHETIC_KEYS = {"final_97", "interactive_65"}
METAL_SYNTHETIC_TIER_KEYS = {
    "pixel_count", "p99_delta_e_00", "maximum_delta_e_00",
    "maximum_channel_error", "p99_gate", "maximum_gate", "channel_gate",
}
METAL_TAGGED_KEYS = {
    "required", "set_id", "manifest_sha256", "artifact_set_sha256",
    "reference_count", "pixel_count", "delta_e_00_over_0_25",
    "allowed_exceedances", "maximum_delta_e_00", "maximum_channel_error",
    "maximum_gate", "channel_gate",
}
METAL_TELEMETRY_KEYS = {
    "command_buffer_count", "kernel_dispatch_count", "cpu_to_gpu_bytes",
    "gpu_to_cpu_bytes",
}
METAL_TIMING_KEYS = {
    "sample_count", "pixel_count_per_sample", "p95_wall_ms",
    "maximum_wall_ms", "p95_gate_ms", "maximum_gate_ms",
    "telemetry_final_output_ms",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--artifact-root", type=Path)
  parser.add_argument("--source-inventory", type=Path, required=True)
  parser.add_argument("--tagged-set-manifest", type=Path, required=True)
  parser.add_argument("--raw-renderer", type=Path, required=True)
  parser.add_argument("--metal-test", type=Path, required=True)
  return parser.parse_args()


def load_module(path: Path, name: str) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load module: {path}")
  module = importlib.util.module_from_spec(spec)
  spec.loader.exec_module(module)
  return module


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


def safe_path(value: Any, prefix: str = "") -> bool:
  if not isinstance(value, str) or not value or "\\" in value or ":" in value:
    return False
  path = PurePosixPath(value)
  return (
      not path.is_absolute() and str(path) == value and value.startswith(prefix)
      and all(part not in ("", ".", "..") for part in path.parts)
  )


def all_artifact_paths(root: Path, manifest_path: Path) -> list[str]:
  excluded = manifest_path.resolve()
  return sorted(
      path.relative_to(root).as_posix()
      for path in root.rglob("*")
      if path.is_file() and path.resolve() != excluded
  )


def validate_metal_report(
    report: dict[str, Any], tagged_manifest: dict[str, Any],
    label: str, errors: list[str],
) -> None:
  exact_keys(report, METAL_REPORT_KEYS, label, errors)
  hardware = exact_keys(report.get("hardware"), METAL_HARDWARE_KEYS, f"{label}.hardware", errors) or {}
  luts = exact_keys(report.get("luts"), METAL_LUT_KEYS, f"{label}.luts", errors) or {}
  synthetic = exact_keys(report.get("synthetic_parity"), METAL_SYNTHETIC_KEYS, f"{label}.synthetic_parity", errors) or {}
  tagged = exact_keys(report.get("tagged_chart_parity"), METAL_TAGGED_KEYS, f"{label}.tagged_chart_parity", errors) or {}
  timing = exact_keys(report.get("timing"), METAL_TIMING_KEYS, f"{label}.timing", errors) or {}
  telemetry = exact_keys(report.get("telemetry"), METAL_TELEMETRY_KEYS, f"{label}.telemetry", errors) or {}
  if report.get("schema") != METAL_SCHEMA or report.get("tool_version") != METAL_TOOL_VERSION or report.get("result") != "passed":
    errors.append(f"{label} must be a passed v2 dual-tier Metal device proof")
  for key in (
      "device_name", "kernel_version", "product_version",
      "cpu_architecture", "qt_version",
  ):
    if not isinstance(hardware.get(key), str) or not hardware.get(key):
      errors.append(f"{label}.hardware.{key} must be non-empty")
  if hardware.get("backend") != "Metal" or hardware.get("kernel_type") != "darwin":
    errors.append(f"{label} must identify a physical Darwin Metal backend")
  if hardware.get("build_mode") != "release":
    errors.append(f"{label} must come from a Release executable")
  if luts.get("final_cube_size") != 97 or luts.get("interactive_cube_size") != 65:
    errors.append(f"{label} must exercise the selected 97/65-cube tiers")
  for key in ("final_cache_key_sha256", "interactive_cache_key_sha256"):
    if not valid_sha256(luts.get(key)):
      errors.append(f"{label}.luts.{key} must be a SHA-256 digest")
  for tier_name in ("final_97", "interactive_65"):
    tier = exact_keys(
        synthetic.get(tier_name), METAL_SYNTHETIC_TIER_KEYS,
        f"{label}.synthetic_parity.{tier_name}", errors,
    ) or {}
    if tier.get("pixel_count") != 169 or tier.get("p99_gate") != 0.25 or tier.get("maximum_gate") != 0.75 or tier.get("channel_gate") != 2.0e-4:
      errors.append(f"{label} {tier_name} synthetic gate identity is invalid")
    if any(not isinstance(tier.get(key), (int, float)) for key in (
        "p99_delta_e_00", "maximum_delta_e_00", "maximum_channel_error"
    )):
      errors.append(f"{label} {tier_name} synthetic metrics must be numeric")
    elif tier["p99_delta_e_00"] > 0.25 or tier["maximum_delta_e_00"] > 0.75 or tier["maximum_channel_error"] > 2.0e-4:
      errors.append(f"{label} {tier_name} synthetic parity exceeds its gate")
  tagged_summary = tagged_manifest.get("artifact_summary") or {}
  expected_tagged = {
      "required": True,
      "set_id": tagged_manifest.get("set_id"),
      "manifest_sha256": sha256_file(Path(tagged_manifest["__manifest_path"])),
      "artifact_set_sha256": tagged_summary.get("artifact_set_sha256"),
      "reference_count": 48,
      "pixel_count": 11751552,
      "maximum_gate": 0.75,
      "channel_gate": 2.0e-4,
  }
  for key, expected in expected_tagged.items():
    if tagged.get(key) != expected:
      errors.append(f"{label}.tagged_chart_parity.{key} is invalid")
  for key in ("delta_e_00_over_0_25", "allowed_exceedances"):
    if not isinstance(tagged.get(key), int) or tagged[key] < 0:
      errors.append(f"{label}.tagged_chart_parity.{key} must be non-negative")
  if isinstance(tagged.get("delta_e_00_over_0_25"), int) and isinstance(tagged.get("allowed_exceedances"), int) and tagged["delta_e_00_over_0_25"] > tagged["allowed_exceedances"]:
    errors.append(f"{label} tagged-chart p99 gate failed")
  if any(not isinstance(tagged.get(key), (int, float)) for key in (
      "maximum_delta_e_00", "maximum_channel_error"
  )):
    errors.append(f"{label} tagged-chart metrics must be numeric")
  elif tagged["maximum_delta_e_00"] > 0.75 or tagged["maximum_channel_error"] > 2.0e-4:
    errors.append(f"{label} tagged-chart maximum gate failed")
  expected_timing = {
      "sample_count": 48,
      "pixel_count_per_sample": 606 * 404,
      "p95_gate_ms": 25.0,
      "maximum_gate_ms": 50.0,
  }
  for key, expected in expected_timing.items():
    if timing.get(key) != expected:
      errors.append(f"{label}.timing.{key} is invalid")
  for key in ("p95_wall_ms", "maximum_wall_ms"):
    if not isinstance(timing.get(key), (int, float)) or timing[key] < 0:
      errors.append(f"{label}.timing.{key} must be a non-negative number")
  if (
      isinstance(timing.get("p95_wall_ms"), (int, float))
      and timing["p95_wall_ms"] > 25.0
  ):
    errors.append(f"{label} tagged-chart p95 latency gate failed")
  if (
      isinstance(timing.get("maximum_wall_ms"), (int, float))
      and timing["maximum_wall_ms"] > 50.0
  ):
    errors.append(f"{label} tagged-chart maximum latency gate failed")
  if (
      not isinstance(timing.get("telemetry_final_output_ms"), int)
      or timing["telemetry_final_output_ms"] < 0
  ):
    errors.append(
        f"{label}.timing.telemetry_final_output_ms must be non-negative"
    )
  for key in METAL_TELEMETRY_KEYS:
    if not isinstance(telemetry.get(key), int) or telemetry[key] <= 0:
      errors.append(f"{label}.telemetry.{key} must prove positive GPU work")
  if isinstance(telemetry.get("command_buffer_count"), int) and isinstance(telemetry.get("kernel_dispatch_count"), int) and telemetry["command_buffer_count"] != telemetry["kernel_dispatch_count"]:
    errors.append(f"{label} Metal command and kernel counts must agree")
  if isinstance(telemetry.get("kernel_dispatch_count"), int) and telemetry["kernel_dispatch_count"] < 52:
    errors.append(f"{label} must dispatch four stress and 48 chart kernels")


def validate_manifest(
    manifest_path: Path, *, source_inventory_path: Path,
    tagged_set_manifest_path: Path, raw_renderer_path: Path,
    metal_test_path: Path, artifact_root: Path | None = None,
) -> list[str]:
  errors: list[str] = []
  root = artifact_root or manifest_path.parent
  manifest = load_json(manifest_path, "Apple device proof manifest", errors)
  tagged_manifest = load_json(tagged_set_manifest_path, "tagged set manifest", errors)
  if manifest is None or tagged_manifest is None:
    return errors
  tagged_manifest["__manifest_path"] = str(tagged_set_manifest_path)
  exact_keys(manifest, ROOT_KEYS, "manifest", errors)
  if manifest.get("schema") != SCHEMA or manifest.get("result") != "passed":
    errors.append("manifest must be a passed v1 Apple device proof")
  if manifest.get("promotion_eligible") is not False:
    errors.append("hardware proof alone must not claim default promotion")
  blockers = manifest.get("promotion_blockers")
  if not isinstance(blockers, list) or not blockers or any(
      not isinstance(value, str) or not value for value in blockers
  ):
    errors.append("promotion blockers must remain explicit")
  host = exact_keys(manifest.get("host"), HOST_KEYS, "manifest.host", errors) or {}
  if host.get("system") != "Darwin" or any(
      not isinstance(host.get(key), str) or not host.get(key)
      for key in HOST_KEYS - {"system"}
  ):
    errors.append("host must identify the physical Darwin execution environment")
  inputs = exact_keys(manifest.get("inputs"), INPUT_KEYS, "manifest.inputs", errors) or {}
  tools = exact_keys(manifest.get("tools"), TOOLS_KEYS, "manifest.tools", errors) or {}
  core = exact_keys(manifest.get("core_image"), CORE_KEYS, "manifest.core_image", errors) or {}
  metal = exact_keys(manifest.get("metal"), METAL_KEYS, "manifest.metal", errors) or {}
  if not raw_renderer_path.is_file() or tools.get("raw_renderer_sha256") != sha256_file(raw_renderer_path):
    errors.append("RAW renderer executable hash mismatch")
  if not metal_test_path.is_file() or tools.get("metal_test_sha256") != sha256_file(metal_test_path):
    errors.append("Metal test executable hash mismatch")
  if tools.get("runner_sha256") != sha256_file(Path(__file__).with_name("run-apple-device-color-proof.py")):
    errors.append("Apple proof runner hash mismatch")
  if tools.get("validator_sha256") != sha256_file(Path(__file__)):
    errors.append("Apple proof validator hash mismatch")
  if inputs.get("source_inventory_manifest_sha256") != sha256_file(source_inventory_path):
    errors.append("source inventory hash mismatch")
  if inputs.get("tagged_set_id") != tagged_manifest.get("set_id") or inputs.get("tagged_set_manifest_sha256") != sha256_file(tagged_set_manifest_path) or inputs.get("tagged_set_artifact_sha256") != (tagged_manifest.get("artifact_summary") or {}).get("artifact_set_sha256"):
    errors.append("tagged-set input identity mismatch")

  real_manifest_logical = inputs.get("real_raw_manifest_path")
  if not safe_path(real_manifest_logical, "real-raw-render-set/"):
    errors.append("real-RAW manifest path is unsafe")
    real_manifest_path = root / "__invalid__"
  else:
    real_manifest_path = root / real_manifest_logical
  real_manifest = load_json(real_manifest_path, "real-RAW render set", errors)
  if real_manifest is not None:
    if inputs.get("real_raw_set_id") != real_manifest.get("set_id") or inputs.get("real_raw_manifest_sha256") != sha256_file(real_manifest_path) or inputs.get("real_raw_artifact_sha256") != (real_manifest.get("artifact_summary") or {}).get("artifact_set_sha256"):
      errors.append("real-RAW input identity mismatch")
    real_validator = load_module(
        REAL_RAW_VALIDATOR_PATH, "apple_proof_real_raw_validator"
    )
    errors.extend(
        "real-RAW: " + error for error in real_validator.validate_manifest(
            real_manifest_path, source_inventory_path=source_inventory_path,
            artifact_root=real_manifest_path.parent,
        )
    )
  tagged_validator = load_module(
      TAGGED_VALIDATOR_PATH, "apple_proof_tagged_set_validator"
  )
  errors.extend(
      "tagged-set: " + error for error in tagged_validator.validate_manifest(
          tagged_set_manifest_path, artifact_root=tagged_set_manifest_path.parent,
      )
  )

  candidates = core.get("candidates")
  if core.get("required_backend") != "Core Image CIRAWFilter" or not isinstance(candidates, list):
    errors.append("Core Image proof must require the exact CIRAWFilter backend")
    candidates = []
  if core.get("candidate_count") != len(candidates) or core.get("passed_count") != len(candidates) or not candidates:
    errors.append("every real-RAW candidate must pass Core Image execution")
  real_cells = {
      cell.get("candidate_id"): cell for cell in (real_manifest or {}).get("cells", [])
      if isinstance(cell, dict)
  }
  seen: set[str] = set()
  for index, value in enumerate(candidates):
    label = f"manifest.core_image.candidates[{index}]"
    candidate = exact_keys(value, CORE_CANDIDATE_KEYS, label, errors)
    if candidate is None:
      continue
    candidate_id = candidate.get("candidate_id")
    cell = real_cells.get(candidate_id)
    if cell is None or candidate_id in seen:
      errors.append(f"{label}.candidate_id is unknown or duplicated")
      continue
    seen.add(candidate_id)
    shipping = next(
        (render for render in cell.get("renders", []) if render.get("decode_path") == "shipping-default"),
        {},
    )
    expected = {
        "decoder_backend": "Core Image CIRAWFilter",
        "report_path": "real-raw-render-set/" + str(shipping.get("report_path", "")),
        "report_sha256": shipping.get("report_sha256"),
        "output_path": "real-raw-render-set/" + str(shipping.get("output_path", "")),
        "output_sha256": shipping.get("output_sha256"),
    }
    for key, expected_value in expected.items():
      if candidate.get(key) != expected_value:
        errors.append(f"{label}.{key} does not prove Core Image execution")
  if seen != set(real_cells):
    errors.append("Core Image proof does not cover every real-RAW cell")

  metal_report_logical = metal.get("report_path")
  if not safe_path(metal_report_logical, "metal/"):
    errors.append("Metal report path is unsafe")
    metal_report_path = root / "__invalid__"
  else:
    metal_report_path = root / metal_report_logical
  metal_report = load_json(metal_report_path, "Metal device report", errors)
  if metal_report is not None:
    if metal.get("report_sha256") != sha256_file(metal_report_path) or metal.get("device_name") != (metal_report.get("hardware") or {}).get("device_name") or metal.get("result") != "passed":
      errors.append("Metal report identity mismatch")
    validate_metal_report(metal_report, tagged_manifest, "metal report", errors)

  summary = exact_keys(manifest.get("artifact_summary"), SUMMARY_KEYS, "manifest.artifact_summary", errors) or {}
  paths = all_artifact_paths(root, manifest_path)
  if paths:
    byte_size, digest = artifact_set_digest(root, paths)
    expected_summary = {
        "artifact_count": len(paths), "byte_size": byte_size,
        "artifact_set_sha256": digest, "hash_algorithm": HASH_ALGORITHM,
    }
    if summary != expected_summary:
      errors.append("artifact summary does not match the complete device proof")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(
      args.manifest, source_inventory_path=args.source_inventory,
      tagged_set_manifest_path=args.tagged_set_manifest,
      raw_renderer_path=args.raw_renderer, metal_test_path=args.metal_test,
      artifact_root=args.artifact_root,
  )
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print("status=valid-physical-apple-device-color-proof")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
