#!/usr/bin/env python3
"""Adversarial tests for measured display and printer physical proofs."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any, Callable


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-physical-color-output-proof.py"
)


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "physical_color_output_proof_validator", VALIDATOR_PATH
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


def profile(root: Path, profile_id: str, role: str) -> dict[str, Any]:
  path = root / f"{profile_id}.icc"
  path.write_bytes(f"fixture-icc:{profile_id}:{role}\n".encode("utf-8"))
  return {
      "profile_id": profile_id, "role": role, "path": str(path),
      "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
      "byte_size": path.stat().st_size,
  }


def measurement_record(
    root: Path, validator: Any, case_id: str, patch_count: int
) -> dict[str, Any]:
  path = root / f"{case_id}-measurements.json"
  patches = []
  for index in range(patch_count):
    lab = [float(10 + index % 80), float(index % 9 - 4), float(index % 7 - 3)]
    ictcp = [0.1 + float(index % 80) / 100.0, 0.0, 0.0]
    patches.append({
        "patch_id": f"patch-{index:03d}",
        "reference_lab_d50": lab, "measured_lab_d50": list(lab),
        "reference_ictcp": ictcp, "measured_ictcp": list(ictcp),
    })
  write_json(path, {
      "schema": validator.MEASUREMENT_SCHEMA,
      "lab_colorimetry": "CIE Lab D50 2-degree",
      "ictcp_colorimetry": "BT.2100 PQ ICtCp; Delta E ITP BT.2124",
      "patches": patches,
  })
  return {
      "measurement_path": str(path),
      "measurement_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
      "measurement_byte_size": path.stat().st_size,
  }


def platform() -> dict[str, Any]:
  return {
      "os": "fixture-os", "version": "1", "architecture": "arm64",
      "app_build_sha256": "a" * 64, "build_mode": "release",
  }


def measurement() -> dict[str, Any]:
  return {
      "manufacturer": "Fixture Instruments", "model": "Spectro 1",
      "serial_sha256": "b" * 64,
      "calibration_certificate_id": "calibration-fixture-2026",
      "calibrated_on": "2026-01-01",
      "calibration_expires_on": "2026-12-31",
  }


def reviewers() -> list[dict[str, Any]]:
  return [
      {
          "reviewer_id": f"physical-reviewer-{index}",
          "reviewed_on": "2026-07-15", "result": "approved",
          "independent_viewing": True,
          "notes": "Fixture review exercises the physical proof contract.",
      }
      for index in (1, 2)
  ]


def display_fixture(root: Path, validator: Any) -> dict[str, Any]:
  profiles = [
      profile(root, "display-sdr", "display-sdr"),
      profile(root, "display-hdr", "display-hdr"),
  ]
  devices = [
      {
          "device_id": "display-a", "manufacturer": "Fixture",
          "model": "SDR", "serial_sha256": "1" * 64, "mode": "sdr",
          "peak_nits": 120.0, "black_nits": 0.1, "connection": "internal",
      },
      {
          "device_id": "display-b", "manufacturer": "Fixture",
          "model": "HDR", "serial_sha256": "2" * 64, "mode": "hdr",
          "peak_nits": 1000.0, "black_nits": 0.005, "connection": "displayport",
      },
  ]
  cases = []
  for device, profile_value in zip(devices, profiles):
    gates = validator.GATES[("display-switching", device["mode"])]
    case_id = f"case-{device['mode']}"
    cases.append({
        "case_id": case_id, "device_id": device["device_id"],
        "profile_id": profile_value["profile_id"], "mode": device["mode"],
        "patch_count": 96, "delta_e_00_p95": 0.0,
        "delta_e_00_max": 0.0, "delta_e_itp_p95": 0.0,
        "delta_e_itp_max": 0.0,
        "p95_delta_e_00_gate": gates[0], "max_delta_e_00_gate": gates[1],
        "p95_delta_e_itp_gate": gates[2], "max_delta_e_itp_gate": gates[3],
        "assignment_verified": True, "readback_verified": True,
        "stale_frame_count": 0, "result": "passed",
        **measurement_record(root, validator, case_id, 96),
    })
  events = []
  for index in range(4):
    source_index = index % 2
    target_index = 1 - source_index
    events.append({
        "event_id": f"switch-{index}",
        "from_device_id": devices[source_index]["device_id"],
        "to_device_id": devices[target_index]["device_id"],
        "profile_id": profiles[target_index]["profile_id"],
        "profile_cache_rebuilt": True, "stale_frame_count": 0,
        "result": "passed",
    })
  return {
      "schema": validator.SCHEMA, "proof_id": "fixture-display-proof",
      "proof_type": "display-switching", "executed_on": "2026-07-15",
      "platform": platform(), "measurement_device": measurement(),
      "profiles": profiles, "devices": devices, "cases": cases,
      "switch_events": events, "reviewers": reviewers(),
      "promotion_eligible": False, "result": "passed",
  }


def printer_fixture(root: Path, validator: Any) -> dict[str, Any]:
  output_profile = profile(root, "printer-output", "proof-output")
  device = {
      "device_id": "printer-a", "manufacturer": "Fixture",
      "model": "Printer", "serial_sha256": "3" * 64,
      "driver_version": "1.0",
  }
  gates = validator.GATES[("printer-paper", "printer")]
  cases = []
  for index, media in enumerate(("matte", "gloss"), start=1):
    case_id = f"paper-{index}"
    cases.append({
          "case_id": case_id, "device_id": "printer-a",
          "profile_id": "printer-output", "media": media,
          "ink": "fixture-ink", "patch_count": 96,
          "delta_e_00_p95": 0.0, "delta_e_00_max": 0.0,
          "delta_e_itp_p95": 0.0, "delta_e_itp_max": 0.0,
          "p95_delta_e_00_gate": gates[0], "max_delta_e_00_gate": gates[1],
          "p95_delta_e_itp_gate": gates[2], "max_delta_e_itp_gate": gates[3],
          "assignment_verified": True, "readback_verified": True,
          "stale_frame_count": 0, "result": "passed",
          **measurement_record(root, validator, case_id, 96),
      })
  return {
      "schema": validator.SCHEMA, "proof_id": "fixture-printer-proof",
      "proof_type": "printer-paper", "executed_on": "2026-07-15",
      "platform": platform(), "measurement_device": measurement(),
      "profiles": [output_profile], "devices": [device], "cases": cases,
      "switch_events": [], "reviewers": reviewers(),
      "promotion_eligible": False, "result": "passed",
  }


def main() -> int:
  validator = load_validator()
  reference_de00 = validator.delta_e_2000(
      (50.0, 2.6772, -79.7751), (50.0, 0.0, -82.7485)
  )
  if abs(reference_de00 - 2.0425) > 5.0e-5:
    raise AssertionError(
        f"CIEDE2000 published reference pair mismatch: {reference_de00}"
    )
  if (
      abs(validator.delta_e_itp((0.0, 0.0, 0.0), (1.0 / 720.0, 0.0, 0.0)) - 1.0)
      > 1.0e-12
      or abs(
          validator.delta_e_itp(
              (0.0, 0.0, 0.0), (0.0, 1.0 / 360.0, 0.0)
          ) - 1.0
      ) > 1.0e-12
  ):
    raise AssertionError("Delta E ITP BT.2124 reference weights drifted")
  with tempfile.TemporaryDirectory(prefix="physical-color-proof-") as temp:
    root = Path(temp)
    display = display_fixture(root, validator)
    printer = printer_fixture(root, validator)
    manifest_path = root / "proof.json"

    def validate(value: dict[str, Any]) -> list[str]:
      write_json(manifest_path, value)
      return validator.validate_manifest(manifest_path)

    for name, value in (("display", display), ("printer", printer)):
      errors = validate(value)
      if errors:
        raise AssertionError(f"valid {name} proof rejected: {errors}")
    cases: list[tuple[str, dict[str, Any], Callable[[dict[str, Any]], None], str]] = [
        ("debug", display, lambda value: value["platform"].__setitem__("build_mode", "debug"), "Release application"),
        ("missing-hdr", display, lambda value: value["devices"].pop(), "SDR and HDR"),
        ("stale-frame", display, lambda value: value["switch_events"][0].__setitem__("stale_frame_count", 1), "zero stale frames"),
        ("loose-gate", display, lambda value: value["cases"][0].__setitem__("p95_delta_e_00_gate", 9.0), "gate identity"),
        ("metric", printer, lambda value: value["cases"][0].__setitem__("delta_e_00_max", 5.1), "exceeds"),
        ("one-paper", printer, lambda value: value["cases"].pop(), "two distinct paper"),
        ("one-reviewer", printer, lambda value: value["reviewers"].pop(), "two distinct"),
        ("unknown", printer, lambda value: value.__setitem__("checkbox", True), "unknown field"),
    ]
    for name, baseline, mutate, expected in cases:
      value = copy.deepcopy(baseline)
      mutate(value)
      errors = validate(value)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} mutation was accepted: {errors}")

    profile_path = Path(display["profiles"][0]["path"])
    original = profile_path.read_bytes()
    profile_path.write_bytes(original + b"tamper")
    errors = validate(display)
    if not any("byte_size mismatch" in error or "sha256 mismatch" in error for error in errors):
      raise AssertionError(f"ICC profile tampering was accepted: {errors}")

    profile_path.write_bytes(original)
    measurement_path = Path(display["cases"][0]["measurement_path"])
    original_measurement = measurement_path.read_bytes()
    measurement = json.loads(measurement_path.read_text(encoding="utf-8"))
    measurement["patches"][0]["measured_lab_d50"][0] += 10.0
    write_json(measurement_path, measurement)
    errors = validate(display)
    if not any(
        "measurement_sha256 mismatch" in error
        or "does not match recomputed patch measurements" in error
        for error in errors
    ):
      raise AssertionError(f"patch-measurement tampering was accepted: {errors}")

    measurement_path.write_bytes(original_measurement)
    forged_measurement = json.loads(original_measurement)
    for patch in forged_measurement["patches"]:
      patch["measured_lab_d50"][0] += 1.0
    write_json(measurement_path, forged_measurement)
    forged_display = copy.deepcopy(display)
    forged_case = forged_display["cases"][0]
    forged_case["measurement_sha256"] = hashlib.sha256(
        measurement_path.read_bytes()
    ).hexdigest()
    forged_case["measurement_byte_size"] = measurement_path.stat().st_size
    errors = validate(forged_display)
    if not any("does not match recomputed patch measurements" in error for error in errors):
      raise AssertionError(
          f"rehash with favorable self-reported metrics was accepted: {errors}"
      )

  print("physical color-output proof adversarial tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
