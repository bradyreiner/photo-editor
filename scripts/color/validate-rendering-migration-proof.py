#!/usr/bin/env python3
"""Validate a pass-only Release rendering-model migration proof."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


SCHEMA = "photo-editor-rendering-migration-proof:v1"
TOOL_VERSION = "photo-editor-rendering-migration-proof-test:v1"
LEGACY_MODEL = "legacy-oklch-v1"
CANDIDATE_MODEL = "aces2-ocio-v1"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ROOT_KEYS = {
    "schema", "tool_version", "hardware", "executable_sha256",
    "source_fixture_sha256", "rendering_models", "recipe_sha256",
    "contracts", "promotion_eligible", "result",
}
HARDWARE_KEYS = {
    "kernel_type", "kernel_version", "product_type", "product_version",
    "cpu_architecture", "qt_version", "build_mode",
}
MODEL_KEYS = {"legacy", "candidate", "official_processor_available"}
RECIPE_KEYS = {"legacy", "applied", "undo", "redo", "reload"}
CONTRACT_KEYS = {
    "legacy_load_preserved", "preview_non_mutating",
    "preview_compares_models", "cancel_non_mutating",
    "apply_single_undo_step", "apply_only_model_change", "undo_exact",
    "redo_exact", "reload_exact",
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


def valid_sha256(value: Any) -> bool:
  return isinstance(value, str) and SHA256_PATTERN.fullmatch(value) is not None


def validate_report(report_path: Path, executable_path: Path) -> list[str]:
  errors: list[str] = []
  try:
    with report_path.open("r", encoding="utf-8") as handle:
      report = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    return [f"unable to read rendering migration proof: {exc}"]

  report = exact_object(report, ROOT_KEYS, "report", errors)
  hardware = exact_object(
      report.get("hardware"), HARDWARE_KEYS, "report.hardware", errors
  )
  models = exact_object(
      report.get("rendering_models"), MODEL_KEYS,
      "report.rendering_models", errors,
  )
  recipes = exact_object(
      report.get("recipe_sha256"), RECIPE_KEYS,
      "report.recipe_sha256", errors,
  )
  contracts = exact_object(
      report.get("contracts"), CONTRACT_KEYS, "report.contracts", errors
  )

  if (
      report.get("schema") != SCHEMA
      or report.get("tool_version") != TOOL_VERSION
      or report.get("result") != "passed"
  ):
    errors.append("report must be a passed v1 rendering migration proof")
  if report.get("promotion_eligible") is not False:
    errors.append("migration proof alone must not claim default promotion")
  if hardware.get("build_mode") != "release":
    errors.append("rendering migration proof must come from a Release executable")
  for key in HARDWARE_KEYS - {"build_mode"}:
    if not isinstance(hardware.get(key), str) or not hardware[key]:
      errors.append(f"report.hardware.{key} must be non-empty")
  if not executable_path.is_file():
    errors.append(f"proof executable is missing: {executable_path}")
  elif report.get("executable_sha256") != sha256_file(executable_path):
    errors.append("proof executable SHA-256 mismatch")
  if not valid_sha256(report.get("source_fixture_sha256")):
    errors.append("report.source_fixture_sha256 must be a SHA-256 digest")

  if models.get("legacy") != LEGACY_MODEL:
    errors.append(f"legacy rendering model must be {LEGACY_MODEL}")
  if models.get("candidate") != CANDIDATE_MODEL:
    errors.append(f"candidate rendering model must be {CANDIDATE_MODEL}")
  if models.get("official_processor_available") is not True:
    errors.append("pinned official ACES 2 processor must execute")
  for key in RECIPE_KEYS:
    if not valid_sha256(recipes.get(key)):
      errors.append(f"report.recipe_sha256.{key} must be a SHA-256 digest")
  if recipes.get("legacy") != recipes.get("undo"):
    errors.append("undo recipe hash must exactly recover the legacy recipe")
  if not (
      recipes.get("applied") == recipes.get("redo") == recipes.get("reload")
  ):
    errors.append("apply, redo, and reload recipe hashes must match exactly")
  if valid_sha256(recipes.get("legacy")) and (
      recipes.get("legacy") == recipes.get("applied")
  ):
    errors.append("candidate recipe must differ from the legacy recipe")
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
  print("status=valid-release-rendering-migration-proof")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
