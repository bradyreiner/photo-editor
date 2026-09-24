#!/usr/bin/env python3
"""Import canonical Lab/ICtCp patch CSV as physical color-proof evidence."""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import sys
from pathlib import Path
from typing import Any


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-physical-color-output-proof.py"
)
CSV_FIELDS = [
    "patch_id",
    "reference_l", "reference_a", "reference_b",
    "measured_l", "measured_a", "measured_b",
    "reference_i", "reference_ct", "reference_cp",
    "measured_i", "measured_ct", "measured_cp",
]


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("input", type=Path)
  parser.add_argument("--output", type=Path, required=True)
  return parser.parse_args()


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "physical_measurement_import_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load validator: {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  sys.modules["physical_measurement_import_validator"] = module
  spec.loader.exec_module(module)
  return module


def parse_number(value: Any, label: str, errors: list[str]) -> float | None:
  try:
    parsed = float(value)
  except (TypeError, ValueError):
    errors.append(f"{label} must be numeric")
    return None
  if not math.isfinite(parsed):
    errors.append(f"{label} must be finite")
    return None
  return parsed


def import_measurements(
    input_path: Path, output_path: Path
) -> tuple[list[str], dict[str, Any]]:
  errors: list[str] = []
  if output_path.exists():
    return (["measurement evidence output already exists"], {})
  try:
    with input_path.open("r", encoding="utf-8-sig", newline="") as handle:
      reader = csv.DictReader(handle)
      if reader.fieldnames != CSV_FIELDS:
        return (["measurement CSV header does not match the canonical field order"], {})
      rows = list(reader)
  except (OSError, csv.Error) as exc:
    return ([f"unable to read measurement CSV: {exc}"], {})
  if len(rows) < 24:
    errors.append("measurement CSV must contain at least 24 patch rows")

  patches: list[dict[str, Any]] = []
  patch_ids: set[str] = set()
  for index, row in enumerate(rows, start=2):
    patch_id = row.get("patch_id", "").strip()
    if not patch_id or patch_id in patch_ids:
      errors.append(f"row {index} patch_id must be unique and non-empty")
    else:
      patch_ids.add(patch_id)
    values = {
        key: parse_number(row.get(key), f"row {index} {key}", errors)
        for key in CSV_FIELDS[1:]
    }
    if any(value is None for value in values.values()):
      continue
    patches.append({
        "patch_id": patch_id,
        "reference_lab_d50": [
            values["reference_l"], values["reference_a"],
            values["reference_b"],
        ],
        "measured_lab_d50": [
            values["measured_l"], values["measured_a"],
            values["measured_b"],
        ],
        "reference_ictcp": [
            values["reference_i"], values["reference_ct"],
            values["reference_cp"],
        ],
        "measured_ictcp": [
            values["measured_i"], values["measured_ct"],
            values["measured_cp"],
        ],
    })
  if errors:
    return (errors, {})

  validator = load_validator()
  de00 = [
      validator.delta_e_2000(
          tuple(patch["reference_lab_d50"]),
          tuple(patch["measured_lab_d50"]),
      )
      for patch in patches
  ]
  deitp = [
      validator.delta_e_itp(
          tuple(patch["reference_ictcp"]), tuple(patch["measured_ictcp"])
      )
      for patch in patches
  ]
  payload = {
      "schema": validator.MEASUREMENT_SCHEMA,
      "lab_colorimetry": "CIE Lab D50 2-degree",
      "ictcp_colorimetry": "BT.2100 PQ ICtCp; Delta E ITP BT.2124",
      "patches": patches,
  }
  encoded = (
      json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
  ).encode("utf-8")
  try:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("xb") as handle:
      handle.write(encoded)
  except OSError as exc:
    return ([f"unable to write measurement evidence: {exc}"], {})
  return ([], {
      "measurement_path": str(output_path.resolve()),
      "measurement_sha256": hashlib.sha256(encoded).hexdigest(),
      "measurement_byte_size": len(encoded),
      "patch_count": len(patches),
      "delta_e_00_p95": validator.percentile_upper(de00, 0.95),
      "delta_e_00_max": max(de00),
      "delta_e_itp_p95": validator.percentile_upper(deitp, 0.95),
      "delta_e_itp_max": max(deitp),
  })


def main() -> int:
  args = parse_args()
  errors, summary = import_measurements(args.input, args.output)
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print(json.dumps(summary, indent=2, sort_keys=True))
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
