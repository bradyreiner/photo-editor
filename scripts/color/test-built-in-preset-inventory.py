#!/usr/bin/env python3
"""Adversarial tests for the immutable built-in preset inventory."""

from __future__ import annotations

import copy
import importlib.util
import json
import shutil
import tempfile
from pathlib import Path
from typing import Any, Callable


SCRIPT_DIR = Path(__file__).parent
REPOSITORY_ROOT = SCRIPT_DIR.parent.parent
VALIDATOR_PATH = SCRIPT_DIR / "validate-built-in-preset-inventory.py"
MANIFEST_PATH = (
    REPOSITORY_ROOT
    / "perf-corpora/color-validation-required/built-in-preset-inventory.json"
)


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "built_in_preset_inventory_validator", VALIDATOR_PATH
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
  baseline_errors = validator.validate_manifest(MANIFEST_PATH, REPOSITORY_ROOT)
  if baseline_errors:
    raise AssertionError(f"checked-in preset inventory is invalid: {baseline_errors}")
  manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
  with tempfile.TemporaryDirectory(prefix="preset-inventory-") as temp:
    root = Path(temp)
    source = root / validator.SOURCE_PATH
    source.parent.mkdir(parents=True)
    shutil.copyfile(REPOSITORY_ROOT / validator.SOURCE_PATH, source)
    inventory = root / "inventory.json"

    def validate(value: dict[str, Any]) -> list[str]:
      write_json(inventory, value)
      return validator.validate_manifest(inventory, root)

    cases: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
        ("hash", lambda value: value.__setitem__("source_region_sha256", "0" * 64), "SHA-256 mismatch"),
        ("count", lambda value: value.__setitem__("preset_count", 13), "preset_count"),
        ("missing", lambda value: value["presets"].pop(), "does not match"),
        ("reordered", lambda value: value["presets"].reverse(), "does not match"),
        ("metadata", lambda value: value["presets"][0].__setitem__("pack", "Other"), "does not match"),
        ("unknown", lambda value: value.__setitem__("approved", True), "unknown field"),
    ]
    for name, mutate, expected in cases:
      value = copy.deepcopy(manifest)
      mutate(value)
      errors = validate(value)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} mutation was accepted: {errors}")

    source_text = source.read_text(encoding="utf-8")
    source.write_text(
        source_text.replace("clean_portrait.exposure = 0.18;", "clean_portrait.exposure = 0.19;"),
        encoding="utf-8",
    )
    errors = validate(manifest)
    if not any("SHA-256 mismatch" in error for error in errors):
      raise AssertionError(f"preset value change did not invalidate inventory: {errors}")

  print("built-in preset inventory adversarial tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
