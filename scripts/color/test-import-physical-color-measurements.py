#!/usr/bin/env python3
"""Contract tests for physical patch-measurement CSV ingestion."""

from __future__ import annotations

import csv
import importlib.util
import json
import sys
import tempfile
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).parent
IMPORTER_PATH = SCRIPT_DIR / "import-physical-color-measurements.py"


def load_module(path: Path, name: str) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load module: {path}")
  module = importlib.util.module_from_spec(spec)
  sys.modules[name] = module
  spec.loader.exec_module(module)
  return module


def write_csv(path: Path, importer: Any, rows: list[dict[str, Any]]) -> None:
  with path.open("w", encoding="utf-8", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=importer.CSV_FIELDS)
    writer.writeheader()
    writer.writerows(rows)


def fixture_rows(count: int = 24) -> list[dict[str, Any]]:
  rows = []
  for index in range(count):
    rows.append({
        "patch_id": f"patch-{index:03d}",
        "reference_l": 40.0 + index, "reference_a": 2.0,
        "reference_b": -3.0, "measured_l": 40.0 + index,
        "measured_a": 2.0, "measured_b": -3.0,
        "reference_i": 0.2 + index / 100.0, "reference_ct": 0.0,
        "reference_cp": 0.0, "measured_i": 0.2 + index / 100.0,
        "measured_ct": 0.0, "measured_cp": 0.0,
    })
  return rows


def main() -> int:
  importer = load_module(IMPORTER_PATH, "physical_measurement_importer_test")
  validator = load_module(
      SCRIPT_DIR / "validate-physical-color-output-proof.py",
      "physical_measurement_validator_test",
  )
  with tempfile.TemporaryDirectory(prefix="physical-measurement-import-") as temp:
    root = Path(temp)
    csv_path = root / "measurements.csv"
    output_path = root / "measurements.json"
    rows = fixture_rows()
    write_csv(csv_path, importer, rows)
    errors, summary = importer.import_measurements(csv_path, output_path)
    if errors or summary.get("patch_count") != 24 or any(
        summary.get(key) != 0.0
        for key in (
            "delta_e_00_p95", "delta_e_00_max",
            "delta_e_itp_p95", "delta_e_itp_max",
        )
    ):
      raise AssertionError(f"valid measurement import failed: {errors} {summary}")
    payload = json.loads(output_path.read_text(encoding="utf-8"))
    if (
        payload.get("schema") != validator.MEASUREMENT_SCHEMA
        or len(payload.get("patches", [])) != 24
    ):
      raise AssertionError("measurement importer emitted the wrong evidence schema")
    errors, _ = importer.import_measurements(csv_path, output_path)
    if not any("already exists" in error for error in errors):
      raise AssertionError(f"evidence overwrite was accepted: {errors}")

    duplicate_rows = fixture_rows()
    duplicate_rows[1]["patch_id"] = duplicate_rows[0]["patch_id"]
    duplicate_path = root / "duplicate.csv"
    write_csv(duplicate_path, importer, duplicate_rows)
    errors, _ = importer.import_measurements(
        duplicate_path, root / "duplicate.json"
    )
    if not any("unique" in error for error in errors):
      raise AssertionError(f"duplicate patch identity was accepted: {errors}")

    nonfinite_rows = fixture_rows()
    nonfinite_rows[0]["measured_l"] = "nan"
    nonfinite_path = root / "nonfinite.csv"
    write_csv(nonfinite_path, importer, nonfinite_rows)
    errors, _ = importer.import_measurements(
        nonfinite_path, root / "nonfinite.json"
    )
    if not any("finite" in error for error in errors):
      raise AssertionError(f"non-finite measurement was accepted: {errors}")

    short_path = root / "short.csv"
    write_csv(short_path, importer, fixture_rows(23))
    errors, _ = importer.import_measurements(short_path, root / "short.json")
    if not any("at least 24" in error for error in errors):
      raise AssertionError(f"undersized measurement set was accepted: {errors}")

  print("Physical color-measurement importer tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
