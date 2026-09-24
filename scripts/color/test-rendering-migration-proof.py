#!/usr/bin/env python3
"""Adversarial tests for the rendering migration proof validator."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import tempfile
from pathlib import Path
from typing import Any, Callable


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-rendering-migration-proof.py"
)


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "rendering_migration_proof_validator", VALIDATOR_PATH
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


def fixture(validator: Any, executable: Path) -> dict[str, Any]:
  digest = hashlib.sha256(executable.read_bytes()).hexdigest()
  return {
      "schema": validator.SCHEMA,
      "tool_version": validator.TOOL_VERSION,
      "hardware": {
          "kernel_type": "darwin", "kernel_version": "25.0",
          "product_type": "macos", "product_version": "15.6",
          "cpu_architecture": "arm64", "qt_version": "6.7.3",
          "build_mode": "release",
      },
      "executable_sha256": digest,
      "source_fixture_sha256": "1" * 64,
      "rendering_models": {
          "legacy": validator.LEGACY_MODEL,
          "candidate": validator.CANDIDATE_MODEL,
          "official_processor_available": True,
      },
      "recipe_sha256": {
          "legacy": "2" * 64, "applied": "3" * 64,
          "undo": "2" * 64, "redo": "3" * 64, "reload": "3" * 64,
      },
      "contracts": {key: True for key in validator.CONTRACT_KEYS},
      "promotion_eligible": False,
      "result": "passed",
  }


def main() -> int:
  validator = load_validator()
  with tempfile.TemporaryDirectory(prefix="rendering-migration-proof-") as temp:
    root = Path(temp)
    executable = root / "rendering_migration_proof_test"
    executable.write_bytes(b"fixture-release-executable")
    report_path = root / "report.json"
    report = fixture(validator, executable)
    write_json(report_path, report)
    errors = validator.validate_report(report_path, executable)
    if errors:
      raise AssertionError(f"valid rendering migration proof rejected: {errors}")

    cases: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
        ("debug", lambda value: value["hardware"].__setitem__("build_mode", "debug"), "Release"),
        ("legacy-model", lambda value: value["rendering_models"].__setitem__("legacy", "other"), "legacy rendering model"),
        ("candidate-model", lambda value: value["rendering_models"].__setitem__("candidate", "other"), "candidate rendering model"),
        ("no-processor", lambda value: value["rendering_models"].__setitem__("official_processor_available", False), "official ACES 2"),
        ("undo-drift", lambda value: value["recipe_sha256"].__setitem__("undo", "4" * 64), "undo recipe hash"),
        ("redo-drift", lambda value: value["recipe_sha256"].__setitem__("redo", "4" * 64), "apply, redo, and reload"),
        ("no-change", lambda value: value["recipe_sha256"].__setitem__("applied", "2" * 64), "apply, redo, and reload"),
        ("false-contract", lambda value: value["contracts"].__setitem__("reload_exact", False), "must pass"),
        ("promoting", lambda value: value.__setitem__("promotion_eligible", True), "alone must not claim"),
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

  print("Rendering migration-proof adversarial tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
