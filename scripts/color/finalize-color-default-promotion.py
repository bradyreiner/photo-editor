#!/usr/bin/env python3
"""Attach trusted signed reviews and finalize a color-default promotion."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import tempfile
from pathlib import Path
from typing import Any


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-color-default-promotion.py"
)
UNSIGNED_REVIEWER_KEYS = {
    "reviewer_id", "reviewed_on", "decision", "independent",
    "evidence_set_sha256", "categories", "notes",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--draft", type=Path, required=True)
  parser.add_argument("--approval", nargs=2, action="append", required=True,
                      metavar=("REVIEWER_JSON", "SIGNATURE"))
  parser.add_argument("--allowed-signers", type=Path, required=True)
  parser.add_argument("--repository-root", type=Path, default=Path("."))
  parser.add_argument("--output", type=Path, required=True)
  return parser.parse_args()


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "promotion_finalizer_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load validator: {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  sys.modules["promotion_finalizer_validator"] = module
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


def finalize(
    draft_path: Path, approvals: list[tuple[Path, Path]],
    allowed_signers_path: Path, repository_root: Path, output_path: Path, *,
    require_clean_repository: bool = True,
    require_tracked_allowed_signers: bool = True,
) -> tuple[list[str], dict[str, Any]]:
  errors: list[str] = []
  validator = load_validator()
  draft_path = draft_path.resolve()
  output_path = output_path.resolve()
  bundle_root = draft_path.parent
  if output_path.parent != bundle_root:
    errors.append("final manifest must remain beside its evidence draft")
  if output_path.exists():
    errors.append("final promotion output already exists")
  draft = load_object(draft_path, "promotion draft", errors)
  if (
      draft.get("schema") != validator.SCHEMA
      or draft.get("result") != "pending-approval"
      or draft.get("promotion_eligible") is not False
      or draft.get("final_reviewers") != []
  ):
    errors.append("input must be an untouched pending-approval promotion draft")
  evidence = draft.get("evidence")
  evidence_sha = validator.canonical_sha256(evidence)
  if draft.get("evidence_set_sha256") != evidence_sha:
    errors.append("promotion draft evidence hash is stale")
  if len(approvals) < 2:
    errors.append("at least two signed reviewer approvals are required")

  reviewers: list[dict[str, Any]] = []
  for index, (reviewer_path, signature_path) in enumerate(approvals):
    label = f"approval[{index}]"
    reviewer = load_object(reviewer_path.resolve(), f"{label} reviewer", errors)
    unknown = set(reviewer) - UNSIGNED_REVIEWER_KEYS
    missing = UNSIGNED_REVIEWER_KEYS - set(reviewer)
    if unknown or missing:
      errors.append(f"{label} reviewer fields do not match the unsigned contract")
    signature_path = signature_path.resolve()
    try:
      signature_logical = signature_path.relative_to(bundle_root).as_posix()
    except ValueError:
      errors.append(f"{label} signature must be inside the evidence volume")
      signature_logical = "__invalid__"
    if not signature_path.is_file():
      errors.append(f"{label} signature is missing")
      signature_sha = ""
    else:
      signature_sha = validator.sha256_file(signature_path)
    reviewers.append({
        **reviewer,
        "signature_path": signature_logical,
        "signature_sha256": signature_sha,
    })
  final = {
      **draft,
      "final_reviewers": reviewers,
      "promotion_eligible": True,
      "result": "approved",
  }
  if errors:
    return (errors, final)

  candidate_path: Path | None = None
  try:
    with tempfile.NamedTemporaryFile(
        mode="w", encoding="utf-8", dir=bundle_root,
        prefix=".color-promotion-candidate-", suffix=".json", delete=False,
    ) as handle:
      json.dump(final, handle, indent=2, sort_keys=True, ensure_ascii=True)
      handle.write("\n")
      candidate_path = Path(handle.name)
    errors = validator.validate_manifest(
        candidate_path, repository_root, allowed_signers_path,
        require_clean_repository=require_clean_repository,
        require_tracked_allowed_signers=require_tracked_allowed_signers,
    )
  finally:
    if candidate_path is not None:
      candidate_path.unlink(missing_ok=True)
  if errors:
    return (errors, final)
  try:
    with output_path.open("x", encoding="utf-8") as handle:
      json.dump(final, handle, indent=2, sort_keys=True, ensure_ascii=True)
      handle.write("\n")
  except OSError as exc:
    return ([f"unable to write final promotion manifest: {exc}"], final)
  return ([], final)


def main() -> int:
  args = parse_args()
  approvals = [(Path(pair[0]), Path(pair[1])) for pair in args.approval]
  errors, _ = finalize(
      args.draft, approvals, args.allowed_signers,
      args.repository_root, args.output,
  )
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print(f"status=approved-color-default-promotion:{args.output}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
