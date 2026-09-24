#!/usr/bin/env python3
"""Validate measured physical display-switching or printer-paper color proof."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import math
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "photo-editor-physical-color-output-proof:v2"
MEASUREMENT_SCHEMA = "photo-editor-physical-patch-measurements:v1"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ROOT_KEYS = {
    "schema", "proof_id", "proof_type", "executed_on", "platform",
    "measurement_device", "profiles", "devices", "cases", "switch_events",
    "reviewers", "promotion_eligible", "result",
}
PLATFORM_KEYS = {
    "os", "version", "architecture", "app_build_sha256", "build_mode",
}
MEASUREMENT_DEVICE_KEYS = {
    "manufacturer", "model", "serial_sha256", "calibration_certificate_id",
    "calibrated_on", "calibration_expires_on",
}
PROFILE_KEYS = {
    "profile_id", "role", "path", "sha256", "byte_size",
}
DISPLAY_DEVICE_KEYS = {
    "device_id", "manufacturer", "model", "serial_sha256", "mode",
    "peak_nits", "black_nits", "connection",
}
PRINTER_DEVICE_KEYS = {
    "device_id", "manufacturer", "model", "serial_sha256", "driver_version",
}
DISPLAY_CASE_KEYS = {
    "case_id", "device_id", "profile_id", "mode", "patch_count",
    "delta_e_00_p95", "delta_e_00_max", "delta_e_itp_p95",
    "delta_e_itp_max", "p95_delta_e_00_gate", "max_delta_e_00_gate",
    "p95_delta_e_itp_gate", "max_delta_e_itp_gate", "assignment_verified",
    "readback_verified", "stale_frame_count", "result",
    "measurement_path", "measurement_sha256", "measurement_byte_size",
}
PRINTER_CASE_KEYS = {
    "case_id", "device_id", "profile_id", "media", "ink", "patch_count",
    "delta_e_00_p95", "delta_e_00_max", "delta_e_itp_p95",
    "delta_e_itp_max", "p95_delta_e_00_gate", "max_delta_e_00_gate",
    "p95_delta_e_itp_gate", "max_delta_e_itp_gate", "assignment_verified",
    "readback_verified", "stale_frame_count", "result",
    "measurement_path", "measurement_sha256", "measurement_byte_size",
}
SWITCH_KEYS = {
    "event_id", "from_device_id", "to_device_id", "profile_id",
    "profile_cache_rebuilt", "stale_frame_count", "result",
}
REVIEWER_KEYS = {
    "reviewer_id", "reviewed_on", "result", "independent_viewing", "notes",
}
PATCH_MEASUREMENT_KEYS = {
    "schema", "lab_colorimetry", "ictcp_colorimetry", "patches",
}
PATCH_KEYS = {
    "patch_id", "reference_lab_d50", "measured_lab_d50",
    "reference_ictcp", "measured_ictcp",
}
GATES = {
    ("display-switching", "sdr"): (1.0, 3.0, 3.0, 6.0),
    ("display-switching", "hdr"): (2.0, 5.0, 2.0, 5.0),
    ("printer-paper", "printer"): (2.0, 5.0, 4.0, 8.0),
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("manifest", type=Path)
  return parser.parse_args()


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


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def resolve_path(manifest_path: Path, value: Any) -> Path:
  if not isinstance(value, str) or not value:
    return manifest_path.parent / "__invalid__"
  path = Path(value).expanduser()
  return path if path.is_absolute() else (manifest_path.parent / path).resolve()


def valid_date(value: Any, label: str, errors: list[str]) -> dt.date | None:
  if not isinstance(value, str):
    errors.append(f"{label} must use YYYY-MM-DD")
    return None
  try:
    return dt.date.fromisoformat(value)
  except ValueError:
    errors.append(f"{label} must use YYYY-MM-DD")
    return None


def valid_sha256(value: Any) -> bool:
  return isinstance(value, str) and SHA256_PATTERN.fullmatch(value) is not None


def positive_number(value: Any) -> bool:
  return (
      isinstance(value, (int, float)) and not isinstance(value, bool)
      and float(value) > 0.0
  )


def numeric_triplet(
    value: Any, label: str, errors: list[str]
) -> tuple[float, float, float] | None:
  if (
      not isinstance(value, list) or len(value) != 3
      or any(
          not isinstance(item, (int, float)) or isinstance(item, bool)
          or not math.isfinite(float(item))
          for item in value
      )
  ):
    errors.append(f"{label} must contain three finite numbers")
    return None
  return (float(value[0]), float(value[1]), float(value[2]))


def delta_e_2000(
    left: tuple[float, float, float], right: tuple[float, float, float]
) -> float:
  l1, a1, b1 = left
  l2, a2, b2 = right
  c1 = math.hypot(a1, b1)
  c2 = math.hypot(a2, b2)
  mean_c = (c1 + c2) * 0.5
  mean_c7 = mean_c ** 7
  g = 0.5 * (1.0 - math.sqrt(mean_c7 / (mean_c7 + 25.0 ** 7)))
  a1p = (1.0 + g) * a1
  a2p = (1.0 + g) * a2
  c1p = math.hypot(a1p, b1)
  c2p = math.hypot(a2p, b2)

  def hue(a: float, b: float) -> float:
    if abs(a) < 1.0e-15 and abs(b) < 1.0e-15:
      return 0.0
    value = math.degrees(math.atan2(b, a))
    return value + 360.0 if value < 0.0 else value

  h1p = hue(a1p, b1)
  h2p = hue(a2p, b2)
  delta_l = l2 - l1
  delta_c = c2p - c1p
  if c1p * c2p == 0.0:
    delta_h_degrees = 0.0
  elif abs(h2p - h1p) <= 180.0:
    delta_h_degrees = h2p - h1p
  elif h2p <= h1p:
    delta_h_degrees = h2p - h1p + 360.0
  else:
    delta_h_degrees = h2p - h1p - 360.0
  delta_h = 2.0 * math.sqrt(c1p * c2p) * math.sin(
      math.radians(delta_h_degrees * 0.5)
  )
  mean_l = (l1 + l2) * 0.5
  mean_cp = (c1p + c2p) * 0.5
  if c1p * c2p == 0.0:
    mean_h = h1p + h2p
  elif abs(h1p - h2p) <= 180.0:
    mean_h = (h1p + h2p) * 0.5
  elif h1p + h2p < 360.0:
    mean_h = (h1p + h2p + 360.0) * 0.5
  else:
    mean_h = (h1p + h2p - 360.0) * 0.5
  t = (
      1.0
      - 0.17 * math.cos(math.radians(mean_h - 30.0))
      + 0.24 * math.cos(math.radians(2.0 * mean_h))
      + 0.32 * math.cos(math.radians(3.0 * mean_h + 6.0))
      - 0.20 * math.cos(math.radians(4.0 * mean_h - 63.0))
  )
  sl = 1.0 + 0.015 * (mean_l - 50.0) ** 2 / math.sqrt(
      20.0 + (mean_l - 50.0) ** 2
  )
  sc = 1.0 + 0.045 * mean_cp
  sh = 1.0 + 0.015 * mean_cp * t
  delta_theta = 30.0 * math.exp(-((mean_h - 275.0) / 25.0) ** 2)
  mean_cp7 = mean_cp ** 7
  rc = 2.0 * math.sqrt(mean_cp7 / (mean_cp7 + 25.0 ** 7))
  rt = -math.sin(math.radians(2.0 * delta_theta)) * rc
  l_term = delta_l / sl
  c_term = delta_c / sc
  h_term = delta_h / sh
  return math.sqrt(
      l_term * l_term + c_term * c_term + h_term * h_term
      + rt * c_term * h_term
  )


def delta_e_itp(
    left: tuple[float, float, float], right: tuple[float, float, float]
) -> float:
  delta_i = left[0] - right[0]
  delta_t = left[1] - right[1]
  delta_p = left[2] - right[2]
  return 720.0 * math.sqrt(
      delta_i * delta_i + 0.25 * delta_t * delta_t + delta_p * delta_p
  )


def percentile_upper(values: list[float], percentile: float) -> float:
  ordered = sorted(values)
  return ordered[max(0, math.ceil(percentile * len(ordered)) - 1)]


def validate_patch_measurements(
    case: dict[str, Any], manifest_path: Path, label: str, errors: list[str]
) -> dict[str, float] | None:
  path = resolve_path(manifest_path, case.get("measurement_path"))
  if not path.is_file():
    errors.append(f"{label}.measurement_path does not exist")
    return None
  if case.get("measurement_byte_size") != path.stat().st_size:
    errors.append(f"{label}.measurement_byte_size mismatch")
  if case.get("measurement_sha256") != sha256_file(path):
    errors.append(f"{label}.measurement_sha256 mismatch")
  try:
    with path.open("r", encoding="utf-8") as handle:
      payload = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    errors.append(f"{label} measurement file is unreadable: {exc}")
    return None
  payload = exact_object(
      payload, PATCH_MEASUREMENT_KEYS, f"{label}.measurements", errors
  )
  if (
      payload.get("schema") != MEASUREMENT_SCHEMA
      or payload.get("lab_colorimetry") != "CIE Lab D50 2-degree"
      or payload.get("ictcp_colorimetry") != "BT.2100 PQ ICtCp; Delta E ITP BT.2124"
  ):
    errors.append(f"{label} measurement colorimetry contract is invalid")
  patches = payload.get("patches")
  if not isinstance(patches, list) or len(patches) < 24:
    errors.append(f"{label} measurements must contain at least 24 patches")
    return None
  if case.get("patch_count") != len(patches):
    errors.append(f"{label}.patch_count does not match its measurement file")
  patch_ids: set[str] = set()
  de00_values: list[float] = []
  deitp_values: list[float] = []
  for index, value in enumerate(patches):
    patch_label = f"{label}.measurements.patches[{index}]"
    patch = exact_object(value, PATCH_KEYS, patch_label, errors)
    patch_id = patch.get("patch_id")
    if not isinstance(patch_id, str) or not patch_id or patch_id in patch_ids:
      errors.append(f"{patch_label}.patch_id must be unique and non-empty")
    else:
      patch_ids.add(patch_id)
    reference_lab = numeric_triplet(
        patch.get("reference_lab_d50"), f"{patch_label}.reference_lab_d50", errors
    )
    measured_lab = numeric_triplet(
        patch.get("measured_lab_d50"), f"{patch_label}.measured_lab_d50", errors
    )
    reference_ictcp = numeric_triplet(
        patch.get("reference_ictcp"), f"{patch_label}.reference_ictcp", errors
    )
    measured_ictcp = numeric_triplet(
        patch.get("measured_ictcp"), f"{patch_label}.measured_ictcp", errors
    )
    if None not in (reference_lab, measured_lab, reference_ictcp, measured_ictcp):
      de00_values.append(delta_e_2000(reference_lab, measured_lab))
      deitp_values.append(delta_e_itp(reference_ictcp, measured_ictcp))
  if len(de00_values) != len(patches) or len(deitp_values) != len(patches):
    return None
  return {
      "delta_e_00_p95": percentile_upper(de00_values, 0.95),
      "delta_e_00_max": max(de00_values),
      "delta_e_itp_p95": percentile_upper(deitp_values, 0.95),
      "delta_e_itp_max": max(deitp_values),
  }


def validate_manifest(manifest_path: Path) -> list[str]:
  errors: list[str] = []
  manifest_path = manifest_path.resolve()
  try:
    with manifest_path.open("r", encoding="utf-8") as handle:
      manifest = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    return [f"unable to read physical color proof: {exc}"]
  manifest = exact_object(manifest, ROOT_KEYS, "manifest", errors)
  proof_type = manifest.get("proof_type")
  if proof_type not in {"display-switching", "printer-paper"}:
    errors.append("manifest.proof_type must be display-switching or printer-paper")
  if (
      manifest.get("schema") != SCHEMA
      or manifest.get("result") != "passed"
      or manifest.get("promotion_eligible") is not False
  ):
    errors.append("physical proof must be a passed, non-promoting v2 artifact")
  if not isinstance(manifest.get("proof_id"), str) or not manifest["proof_id"]:
    errors.append("manifest.proof_id must be non-empty")
  executed_on = valid_date(manifest.get("executed_on"), "manifest.executed_on", errors)

  platform = exact_object(manifest.get("platform"), PLATFORM_KEYS, "manifest.platform", errors)
  for key in ("os", "version", "architecture"):
    if not isinstance(platform.get(key), str) or not platform[key]:
      errors.append(f"manifest.platform.{key} must be non-empty")
  if platform.get("build_mode") != "release" or not valid_sha256(platform.get("app_build_sha256")):
    errors.append("physical proof must identify an exact Release application build")

  measurement = exact_object(
      manifest.get("measurement_device"), MEASUREMENT_DEVICE_KEYS,
      "manifest.measurement_device", errors,
  )
  for key in ("manufacturer", "model", "calibration_certificate_id"):
    if not isinstance(measurement.get(key), str) or not measurement[key]:
      errors.append(f"manifest.measurement_device.{key} must be non-empty")
  if not valid_sha256(measurement.get("serial_sha256")):
    errors.append("measurement-device serial must be privacy-preserving SHA-256")
  calibrated_on = valid_date(
      measurement.get("calibrated_on"), "measurement_device.calibrated_on", errors
  )
  expires_on = valid_date(
      measurement.get("calibration_expires_on"),
      "measurement_device.calibration_expires_on", errors,
  )
  if (
      executed_on is not None and calibrated_on is not None and expires_on is not None
      and not calibrated_on <= executed_on <= expires_on
  ):
    errors.append("measurement-device calibration was not valid on the proof date")

  profiles = manifest.get("profiles")
  if not isinstance(profiles, list) or not profiles:
    errors.append("manifest.profiles must be a non-empty array")
    profiles = []
  profile_map: dict[str, dict[str, Any]] = {}
  for index, value in enumerate(profiles):
    label = f"manifest.profiles[{index}]"
    profile = exact_object(value, PROFILE_KEYS, label, errors)
    profile_id = profile.get("profile_id")
    if not isinstance(profile_id, str) or not profile_id or profile_id in profile_map:
      errors.append(f"{label}.profile_id must be unique and non-empty")
      continue
    if profile.get("role") not in {"display-sdr", "display-hdr", "proof-output"}:
      errors.append(f"{label}.role is invalid")
    path = resolve_path(manifest_path, profile.get("path"))
    if not path.is_file():
      errors.append(f"{label}.path does not exist")
    else:
      if profile.get("byte_size") != path.stat().st_size:
        errors.append(f"{label}.byte_size mismatch")
      if profile.get("sha256") != sha256_file(path):
        errors.append(f"{label}.sha256 mismatch")
    profile_map[profile_id] = profile

  devices = manifest.get("devices")
  if not isinstance(devices, list) or not devices:
    errors.append("manifest.devices must be a non-empty array")
    devices = []
  device_map: dict[str, dict[str, Any]] = {}
  device_keys = DISPLAY_DEVICE_KEYS if proof_type == "display-switching" else PRINTER_DEVICE_KEYS
  for index, value in enumerate(devices):
    label = f"manifest.devices[{index}]"
    device = exact_object(value, device_keys, label, errors)
    device_id = device.get("device_id")
    if not isinstance(device_id, str) or not device_id or device_id in device_map:
      errors.append(f"{label}.device_id must be unique and non-empty")
      continue
    for key in ("manufacturer", "model"):
      if not isinstance(device.get(key), str) or not device[key]:
        errors.append(f"{label}.{key} must be non-empty")
    if not valid_sha256(device.get("serial_sha256")):
      errors.append(f"{label}.serial_sha256 must be a SHA-256 digest")
    if proof_type == "display-switching":
      if device.get("mode") not in {"sdr", "hdr"}:
        errors.append(f"{label}.mode must be sdr or hdr")
      if not positive_number(device.get("peak_nits")):
        errors.append(f"{label}.peak_nits must be positive")
      black = device.get("black_nits")
      if not isinstance(black, (int, float)) or isinstance(black, bool) or black < 0:
        errors.append(f"{label}.black_nits must be non-negative")
      if not isinstance(device.get("connection"), str) or not device["connection"]:
        errors.append(f"{label}.connection must be non-empty")
    elif not isinstance(device.get("driver_version"), str) or not device["driver_version"]:
      errors.append(f"{label}.driver_version must be non-empty")
    device_map[device_id] = device
  if proof_type == "display-switching":
    modes = {device.get("mode") for device in device_map.values()}
    if len(device_map) < 2 or modes != {"sdr", "hdr"}:
      errors.append("display proof requires distinct physical SDR and HDR devices")

  cases = manifest.get("cases")
  if not isinstance(cases, list) or not cases:
    errors.append("manifest.cases must be a non-empty array")
    cases = []
  seen_devices: set[str] = set()
  media_pairs: set[tuple[Any, Any]] = set()
  case_ids: set[str] = set()
  case_keys = DISPLAY_CASE_KEYS if proof_type == "display-switching" else PRINTER_CASE_KEYS
  for index, value in enumerate(cases):
    label = f"manifest.cases[{index}]"
    case = exact_object(value, case_keys, label, errors)
    case_id = case.get("case_id")
    if not isinstance(case_id, str) or not case_id or case_id in case_ids:
      errors.append(f"{label}.case_id must be unique and non-empty")
    else:
      case_ids.add(case_id)
    device = device_map.get(case.get("device_id"))
    profile = profile_map.get(case.get("profile_id"))
    if device is None or profile is None:
      errors.append(f"{label} must reference known device and profile identities")
      continue
    seen_devices.add(case["device_id"])
    if proof_type == "display-switching":
      mode = case.get("mode")
      if mode != device.get("mode") or profile.get("role") != f"display-{mode}":
        errors.append(f"{label} display mode/profile/device identity is inconsistent")
      gate_key = (proof_type, mode)
    else:
      if profile.get("role") != "proof-output":
        errors.append(f"{label} printer case requires a proof-output profile")
      for key in ("media", "ink"):
        if not isinstance(case.get(key), str) or not case[key]:
          errors.append(f"{label}.{key} must be non-empty")
      media_pairs.add((case.get("media"), case.get("ink")))
      gate_key = (proof_type, "printer")
    if not isinstance(case.get("patch_count"), int) or case["patch_count"] < 24:
      errors.append(f"{label}.patch_count must be at least 24")
    measured_metrics = validate_patch_measurements(
        case, manifest_path, label, errors
    )
    expected_gates = GATES.get(gate_key)
    gate_fields = (
        "p95_delta_e_00_gate", "max_delta_e_00_gate",
        "p95_delta_e_itp_gate", "max_delta_e_itp_gate",
    )
    metric_fields = (
        "delta_e_00_p95", "delta_e_00_max",
        "delta_e_itp_p95", "delta_e_itp_max",
    )
    if expected_gates is None or tuple(case.get(key) for key in gate_fields) != expected_gates:
      errors.append(f"{label} quality-gate identity is invalid")
    for metric, gate in zip(metric_fields, expected_gates or (0, 0, 0, 0)):
      value_metric = case.get(metric)
      if (
          not isinstance(value_metric, (int, float)) or isinstance(value_metric, bool)
          or value_metric < 0 or value_metric > gate
      ):
        errors.append(f"{label}.{metric} exceeds its physical-output gate")
      if (
          measured_metrics is not None
          and isinstance(value_metric, (int, float))
          and not isinstance(value_metric, bool)
          and not math.isclose(
              float(value_metric), measured_metrics[metric],
              rel_tol=1.0e-9, abs_tol=1.0e-9,
          )
      ):
        errors.append(f"{label}.{metric} does not match recomputed patch measurements")
    if (
        case.get("assignment_verified") is not True
        or case.get("readback_verified") is not True
        or case.get("stale_frame_count") != 0
        or case.get("result") != "passed"
    ):
      errors.append(f"{label} must prove assignment, readback, zero stale frames, and pass")
  if seen_devices != set(device_map):
    errors.append("physical proof cases must cover every declared device")
  if proof_type == "printer-paper" and len(media_pairs) < 2:
    errors.append("printer proof requires at least two distinct paper/ink combinations")

  events = manifest.get("switch_events")
  if not isinstance(events, list):
    errors.append("manifest.switch_events must be an array")
    events = []
  if proof_type == "display-switching" and len(events) < 4:
    errors.append("display proof requires at least four physical switch events")
  if proof_type == "printer-paper" and events:
    errors.append("printer-paper proof must not manufacture display switch events")
  for index, value in enumerate(events):
    label = f"manifest.switch_events[{index}]"
    event = exact_object(value, SWITCH_KEYS, label, errors)
    source = event.get("from_device_id")
    target = event.get("to_device_id")
    profile = profile_map.get(event.get("profile_id"))
    if source not in device_map or target not in device_map or source == target or profile is None:
      errors.append(f"{label} must identify a real transition and target profile")
    elif profile.get("role") != f"display-{device_map[target].get('mode')}":
      errors.append(f"{label}.profile_id does not match the target display mode")
    if (
        event.get("profile_cache_rebuilt") is not True
        or event.get("stale_frame_count") != 0
        or event.get("result") != "passed"
    ):
      errors.append(f"{label} must rebuild the profile cache with zero stale frames")

  reviewers = manifest.get("reviewers")
  if not isinstance(reviewers, list):
    errors.append("manifest.reviewers must be an array")
    reviewers = []
  reviewer_ids: set[str] = set()
  for index, value in enumerate(reviewers):
    label = f"manifest.reviewers[{index}]"
    reviewer = exact_object(value, REVIEWER_KEYS, label, errors)
    reviewer_id = reviewer.get("reviewer_id")
    if not isinstance(reviewer_id, str) or not reviewer_id:
      errors.append(f"{label}.reviewer_id must be non-empty")
    else:
      reviewer_ids.add(reviewer_id)
    valid_date(reviewer.get("reviewed_on"), f"{label}.reviewed_on", errors)
    if (
        reviewer.get("result") != "approved"
        or reviewer.get("independent_viewing") is not True
        or not isinstance(reviewer.get("notes"), str)
        or not reviewer["notes"]
    ):
      errors.append(f"{label} must record an independent approval and notes")
  if len(reviewer_ids) < 2:
    errors.append("physical output proof requires two distinct approving reviewers")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(args.manifest)
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print("status=valid-physical-color-output-proof")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
