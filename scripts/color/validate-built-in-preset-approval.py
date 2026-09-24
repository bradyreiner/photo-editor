#!/usr/bin/env python3
"""Validate complete licensed-corpus and editorial approval for every built-in preset."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).parent
INVENTORY_VALIDATOR_PATH = SCRIPT_DIR / "validate-built-in-preset-inventory.py"
CORPUS_VALIDATOR_PATH = SCRIPT_DIR / "validate-color-corpus.py"
AGGREGATOR_PATH = SCRIPT_DIR / "aggregate-color-corpus-report.py"
SCHEMA = "photo-editor-built-in-preset-approval:v1"
BASE_MODEL = "aces2-ocio-v1"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ROOT_KEYS = {
    "schema", "approval_id", "inventory_manifest_path",
    "inventory_manifest_sha256", "inventory_source_region_sha256",
    "candidate_base_model", "required_review_categories", "evaluations",
    "promotion_eligible", "result",
}
EVALUATION_KEYS = {
    "preset_name", "corpus_manifest_path", "corpus_manifest_sha256",
    "corpus_id", "corpus_version", "candidate_rendering_model",
    "comparison_set_id", "comparison_report_root", "aggregate_report_path",
    "aggregate_report_sha256", "aggregate_report_count", "recipe_sha256",
    "reviewer_ids", "result",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--repository-root", type=Path, default=Path("."))
  return parser.parse_args()


def load_module(path: Path, name: str) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load module: {path}")
  module = importlib.util.module_from_spec(spec)
  sys.modules[name] = module
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
    errors.append(f"{label} must be an object")
    return None
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


def validate_manifest(manifest_path: Path, repository_root: Path) -> list[str]:
  errors: list[str] = []
  manifest_path = manifest_path.resolve()
  manifest = load_json(manifest_path, "preset approval manifest", errors)
  if manifest is None:
    return errors
  manifest = exact_object(manifest, ROOT_KEYS, "manifest", errors)
  if (
      manifest.get("schema") != SCHEMA
      or manifest.get("result") != "passed"
      or manifest.get("promotion_eligible") is not True
  ):
    errors.append("manifest must be a passed, promotion-eligible v1 preset approval")
  if not isinstance(manifest.get("approval_id"), str) or not manifest["approval_id"]:
    errors.append("manifest.approval_id must be non-empty")
  if manifest.get("candidate_base_model") != BASE_MODEL:
    errors.append(f"manifest.candidate_base_model must be {BASE_MODEL}")

  inventory_validator = load_module(
      INVENTORY_VALIDATOR_PATH, "preset_approval_inventory_validator"
  )
  corpus_validator = load_module(
      CORPUS_VALIDATOR_PATH, "preset_approval_corpus_validator"
  )
  aggregator = load_module(AGGREGATOR_PATH, "preset_approval_aggregator")
  required_categories = sorted(corpus_validator.REQUIRED_REVIEW_CATEGORIES)
  if manifest.get("required_review_categories") != required_categories:
    errors.append("manifest.required_review_categories must cover every editorial category")

  inventory_path = resolve_path(manifest_path, manifest.get("inventory_manifest_path"))
  if not inventory_path.is_file():
    errors.append("built-in preset inventory is missing")
    return errors
  if manifest.get("inventory_manifest_sha256") != sha256_file(inventory_path):
    errors.append("built-in preset inventory SHA-256 mismatch")
  errors.extend(
      "inventory: " + error
      for error in inventory_validator.validate_manifest(
          inventory_path, repository_root.resolve()
      )
  )
  inventory = load_json(inventory_path, "built-in preset inventory", errors)
  if inventory is None:
    return errors
  if (
      manifest.get("inventory_source_region_sha256")
      != inventory.get("source_region_sha256")
  ):
    errors.append("preset approval is not bound to the current source-region hash")
  expected_names = [
      preset.get("name") for preset in inventory.get("presets", [])
      if isinstance(preset, dict)
  ]

  evaluations = manifest.get("evaluations")
  if not isinstance(evaluations, list):
    errors.append("manifest.evaluations must be an array")
    evaluations = []
  observed_names: list[Any] = []
  for index, value in enumerate(evaluations):
    label = f"manifest.evaluations[{index}]"
    evaluation = exact_object(value, EVALUATION_KEYS, label, errors)
    preset_name = evaluation.get("preset_name")
    observed_names.append(preset_name)
    if preset_name not in expected_names:
      errors.append(f"{label}.preset_name is unknown")
      continue
    expected_model = f"{BASE_MODEL}|preset:{preset_name}"
    if evaluation.get("candidate_rendering_model") != expected_model:
      errors.append(f"{label}.candidate_rendering_model is invalid")
    if evaluation.get("result") != "passed":
      errors.append(f"{label}.result must be passed")

    corpus_path = resolve_path(manifest_path, evaluation.get("corpus_manifest_path"))
    aggregate_path = resolve_path(manifest_path, evaluation.get("aggregate_report_path"))
    report_root = resolve_path(manifest_path, evaluation.get("comparison_report_root"))
    if not corpus_path.is_file() or not aggregate_path.is_file() or not report_root.is_dir():
      errors.append(f"{label} evidence paths must all exist")
      continue
    if evaluation.get("corpus_manifest_sha256") != sha256_file(corpus_path):
      errors.append(f"{label}.corpus_manifest_sha256 mismatch")
    if evaluation.get("aggregate_report_sha256") != sha256_file(aggregate_path):
      errors.append(f"{label}.aggregate_report_sha256 mismatch")
    corpus_errors = corpus_validator.validate_manifest(
        corpus_path, require_promotion_approval=True
    )
    errors.extend(f"{label}.corpus: {error}" for error in corpus_errors)
    corpus = load_json(corpus_path, f"{label} corpus", errors) or {}
    if (
        corpus.get("corpus_id") != evaluation.get("corpus_id")
        or corpus.get("corpus_version") != evaluation.get("corpus_version")
        or corpus.get("candidate_rendering_model") != expected_model
    ):
      errors.append(f"{label} corpus identity does not match the preset evaluation")
    comparison_set = next(
        (
            item for item in corpus.get("comparison_sets", [])
            if isinstance(item, dict)
            and item.get("comparison_set_id") == evaluation.get("comparison_set_id")
        ),
        None,
    )
    if comparison_set is None:
      errors.append(f"{label}.comparison_set_id is not present in its corpus")
    elif comparison_set.get("recipe_sha256") != evaluation.get("recipe_sha256"):
      errors.append(f"{label}.recipe_sha256 does not match its blinded comparison")
    approved_reviewers = sorted({
        record.get("reviewer_id")
        for record in corpus.get("review_records", [])
        if isinstance(record, dict)
        and isinstance(record.get("reviewer_id"), str)
        and record.get("comparison_set_id") == evaluation.get("comparison_set_id")
        and record.get("result") == "approved"
    })
    if len(approved_reviewers) < 2 or evaluation.get("reviewer_ids") != approved_reviewers:
      errors.append(f"{label}.reviewer_ids must identify every distinct approving reviewer")

    aggregate_errors, regenerated = aggregator.aggregate(
        corpus_path, report_root, aggregate_path
    )
    errors.extend(f"{label}.aggregate: {error}" for error in aggregate_errors)
    recorded = load_json(aggregate_path, f"{label} aggregate", errors)
    if recorded is not None and recorded != regenerated:
      errors.append(f"{label} aggregate report does not match fresh revalidation")
    expected_count = regenerated.get("expected_report_count")
    if (
        regenerated.get("schema") != aggregator.SUMMARY_SCHEMA
        or regenerated.get("passed") is not True
        or evaluation.get("aggregate_report_count") != expected_count
        or expected_count != regenerated.get("observed_report_count")
    ):
      errors.append(f"{label} aggregate matrix is incomplete or failed")

  if observed_names != expected_names or len(observed_names) != len(set(observed_names)):
    errors.append("preset approval must cover every inventory preset exactly once and in order")
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(args.manifest, args.repository_root)
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print("status=valid-built-in-preset-approval")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
