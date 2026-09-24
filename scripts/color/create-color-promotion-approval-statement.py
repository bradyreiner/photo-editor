#!/usr/bin/env python3
"""Create the canonical statement a final color-promotion reviewer signs."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from pathlib import Path
from typing import Any


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-color-default-promotion.py"
)
REVIEWER_KEYS = {
    "reviewer_id", "reviewed_on", "decision", "independent",
    "evidence_set_sha256", "categories", "notes",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--promotion-manifest", type=Path, required=True)
  parser.add_argument("--reviewer-record", type=Path, required=True)
  parser.add_argument("--output", type=Path, required=True)
  return parser.parse_args()


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "approval_statement_promotion_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load validator: {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  sys.modules["approval_statement_promotion_validator"] = module
  spec.loader.exec_module(module)
  return module


def load_object(path: Path, label: str, errors: list[str]) -> dict[str, Any]:
  try:
    with path.open("r", encoding="utf-8") as handle:
      value = json.load(handle)
  except (OSError, json.JSONDecodeError) as exc:
    errors.append(f"unable to read {label}: {exc}")
    return {}
  if not isinstance(value, dict):
    errors.append(f"{label} must be an object")
    return {}
  return value


def create_statement(
    manifest_path: Path, reviewer_path: Path, output_path: Path
) -> list[str]:
  errors: list[str] = []
  validator = load_validator()
  manifest = load_object(manifest_path, "promotion manifest", errors)
  reviewer = load_object(reviewer_path, "reviewer record", errors)
  if manifest.get("schema") != validator.SCHEMA:
    errors.append(f"promotion manifest schema must be {validator.SCHEMA}")
  evidence = manifest.get("evidence")
  expected_evidence_sha = validator.canonical_sha256(evidence)
  if manifest.get("evidence_set_sha256") != expected_evidence_sha:
    errors.append("promotion manifest evidence hash is stale")
  unknown = set(reviewer) - REVIEWER_KEYS
  missing = REVIEWER_KEYS - set(reviewer)
  for key in sorted(unknown):
    errors.append(f"reviewer record has unknown field {key!r}")
  for key in sorted(missing):
    errors.append(f"reviewer record is missing field {key!r}")
  categories = reviewer.get("categories")
  if (
      reviewer.get("decision") != "approved"
      or reviewer.get("independent") is not True
      or reviewer.get("evidence_set_sha256") != expected_evidence_sha
      or not isinstance(categories, list)
      or any(not isinstance(category, str) for category in categories)
      or set(categories) != validator.REQUIRED_FINAL_CATEGORIES
      or not isinstance(reviewer.get("reviewer_id"), str)
      or not reviewer["reviewer_id"]
      or not isinstance(reviewer.get("reviewed_on"), str)
      or validator.valid_date(
          reviewer.get("reviewed_on"), "reviewer.reviewed_on", errors
      ) is None
      or not isinstance(reviewer.get("notes"), str)
      or not reviewer["notes"]
  ):
    errors.append(
        "reviewer record must approve the exact evidence and every final category"
    )
  if output_path.exists():
    errors.append("approval statement output already exists")
  if errors:
    return errors
  statement = validator.approval_statement(
      manifest.get("promotion_id"), manifest.get("source_revision"),
      expected_evidence_sha, reviewer,
  )
  try:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("xb") as handle:
      handle.write(statement)
  except OSError as exc:
    return [f"unable to write approval statement: {exc}"]
  return []


def main() -> int:
  args = parse_args()
  errors = create_statement(
      args.promotion_manifest, args.reviewer_record, args.output
  )
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print(f"status=created-color-promotion-approval-statement:{args.output}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
