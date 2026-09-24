#!/usr/bin/env python3
"""End-to-end adversarial test for all-built-in-preset approval."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable


SCRIPT_DIR = Path(__file__).parent
REPOSITORY_ROOT = SCRIPT_DIR.parent.parent
VALIDATOR_PATH = SCRIPT_DIR / "validate-built-in-preset-approval.py"
CORPUS_VALIDATOR_PATH = SCRIPT_DIR / "validate-color-corpus.py"
CORPUS_FIXTURE_PATH = SCRIPT_DIR / "test-validate-color-corpus.py"
AGGREGATOR_PATH = SCRIPT_DIR / "aggregate-color-corpus-report.py"
AGGREGATOR_FIXTURE_PATH = SCRIPT_DIR / "test-aggregate-color-corpus-report.py"
INVENTORY_PATH = (
    REPOSITORY_ROOT
    / "perf-corpora/color-validation-required/built-in-preset-inventory.json"
)


def load_module(path: Path, name: str) -> Any:
  spec = importlib.util.spec_from_file_location(name, path)
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load module: {path}")
  module = importlib.util.module_from_spec(spec)
  sys.modules[name] = module
  spec.loader.exec_module(module)
  return module


def write_json(path: Path, value: dict[str, Any]) -> None:
  path.parent.mkdir(parents=True, exist_ok=True)
  path.write_text(
      json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
  )


def sha256_file(path: Path) -> str:
  return hashlib.sha256(path.read_bytes()).hexdigest()


def build_fixture(root: Path, validator: Any) -> tuple[Path, dict[str, Any]]:
  corpus_validator = load_module(
      CORPUS_VALIDATOR_PATH, "preset_approval_test_corpus_validator"
  )
  corpus_fixture = load_module(
      CORPUS_FIXTURE_PATH, "preset_approval_test_corpus_fixture"
  )
  aggregator = load_module(
      AGGREGATOR_PATH, "preset_approval_test_aggregator"
  )
  aggregate_fixture = load_module(
      AGGREGATOR_FIXTURE_PATH, "preset_approval_test_aggregate_fixture"
  )
  inventory = json.loads(INVENTORY_PATH.read_text(encoding="utf-8"))
  evaluations: list[dict[str, Any]] = []
  for index, preset in enumerate(inventory["presets"]):
    evaluation_root = root / f"preset-{index:02d}"
    manifest_path, corpus = corpus_fixture.materialize_fixture(
        evaluation_root, corpus_validator
    )
    preset_name = preset["name"]
    candidate_model = f"{validator.BASE_MODEL}|preset:{preset_name}"
    comparison_set_id = f"preset-{index:02d}-blinded-v1"
    corpus["corpus_id"] = f"preset-{index:02d}-fixture-corpus"
    corpus["candidate_rendering_model"] = candidate_model
    recipe = next(asset for asset in corpus["assets"] if asset["kind"] == "recipe")
    recipe["rendering_model"] = candidate_model
    recipe_path = evaluation_root / recipe["path"]
    recipe_path.write_bytes(
        f"fixture-preset-recipe:{preset_name}:{candidate_model}\n".encode("utf-8")
    )
    recipe["byte_size"] = recipe_path.stat().st_size
    recipe["sha256"] = sha256_file(recipe_path)
    corpus["comparison_sets"][0]["comparison_set_id"] = comparison_set_id
    corpus["comparison_sets"][0]["candidate_rendering_model"] = candidate_model
    corpus["comparison_sets"][0]["recipe_sha256"] = recipe["sha256"]
    for review_index, review in enumerate(corpus["review_records"], start=1):
      review["review_id"] = f"preset-{index:02d}-review-{review_index}"
      review["comparison_set_id"] = comparison_set_id
    write_json(manifest_path, corpus)
    report_root = aggregate_fixture.materialize_reports(
        evaluation_root, manifest_path, corpus
    )
    aggregate_path = evaluation_root / "aggregate.json"
    aggregate_errors, aggregate = aggregator.aggregate(
        manifest_path, report_root, aggregate_path
    )
    if aggregate_errors:
      raise AssertionError(
          f"fixture aggregate for {preset_name} failed: {aggregate_errors}"
      )
    write_json(aggregate_path, aggregate)
    evaluations.append({
        "preset_name": preset_name,
        "corpus_manifest_path": str(manifest_path.resolve()),
        "corpus_manifest_sha256": sha256_file(manifest_path),
        "corpus_id": corpus["corpus_id"],
        "corpus_version": corpus["corpus_version"],
        "candidate_rendering_model": candidate_model,
        "comparison_set_id": comparison_set_id,
        "comparison_report_root": str(report_root.resolve()),
        "aggregate_report_path": str(aggregate_path.resolve()),
        "aggregate_report_sha256": sha256_file(aggregate_path),
        "aggregate_report_count": aggregate["expected_report_count"],
        "recipe_sha256": corpus["comparison_sets"][0]["recipe_sha256"],
        "reviewer_ids": sorted(
            review["reviewer_id"] for review in corpus["review_records"]
        ),
        "result": "passed",
    })
  manifest = {
      "schema": validator.SCHEMA,
      "approval_id": "fixture-all-built-in-presets-v1",
      "inventory_manifest_path": str(INVENTORY_PATH.resolve()),
      "inventory_manifest_sha256": sha256_file(INVENTORY_PATH),
      "inventory_source_region_sha256": inventory["source_region_sha256"],
      "candidate_base_model": validator.BASE_MODEL,
      "required_review_categories": sorted(
          corpus_validator.REQUIRED_REVIEW_CATEGORIES
      ),
      "evaluations": evaluations,
      "promotion_eligible": True,
      "result": "passed",
  }
  manifest_path = root / "preset-approval.json"
  write_json(manifest_path, manifest)
  return manifest_path, manifest


def main() -> int:
  validator = load_module(VALIDATOR_PATH, "built_in_preset_approval_validator")
  with tempfile.TemporaryDirectory(prefix="preset-approval-") as temp:
    root = Path(temp)
    manifest_path, manifest = build_fixture(root, validator)

    def validate(value: dict[str, Any]) -> list[str]:
      write_json(manifest_path, value)
      return validator.validate_manifest(manifest_path, REPOSITORY_ROOT)

    errors = validate(manifest)
    if errors:
      raise AssertionError(f"complete preset approval fixture rejected: {errors}")
    cases: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
        ("promotion", lambda value: value.__setitem__("promotion_eligible", False), "promotion-eligible"),
        ("inventory", lambda value: value.__setitem__("inventory_manifest_sha256", "0" * 64), "inventory SHA-256 mismatch"),
        ("missing-preset", lambda value: value["evaluations"].pop(), "every inventory preset"),
        ("reordered", lambda value: value["evaluations"].reverse(), "every inventory preset"),
        ("model", lambda value: value["evaluations"][0].__setitem__("candidate_rendering_model", "legacy-oklch-v1"), "candidate_rendering_model is invalid"),
        ("reviewers", lambda value: value["evaluations"][0].__setitem__("reviewer_ids", ["editorial-reviewer-1"]), "every distinct approving reviewer"),
        ("unknown", lambda value: value.__setitem__("checkbox", True), "unknown field"),
    ]
    for name, mutate, expected in cases:
      value = copy.deepcopy(manifest)
      mutate(value)
      errors = validate(value)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} mutation was accepted: {errors}")

    first_aggregate = Path(manifest["evaluations"][0]["aggregate_report_path"])
    original = first_aggregate.read_bytes()
    first_aggregate.write_bytes(original + b"tamper")
    errors = validate(manifest)
    if not any("aggregate_report_sha256 mismatch" in error for error in errors):
      raise AssertionError(f"aggregate tampering was accepted: {errors}")

  print("built-in preset approval adversarial tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
