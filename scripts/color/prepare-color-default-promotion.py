#!/usr/bin/env python3
"""Prepare a hash-bound, non-promoting color-default evidence draft."""

from __future__ import annotations

import argparse
import datetime as dt
import importlib.util
import json
import sys
from pathlib import Path
from typing import Any


VALIDATOR_PATH = Path(__file__).with_name(
    "validate-color-default-promotion.py"
)


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--promotion-id", required=True)
  parser.add_argument("--created-on", default=dt.date.today().isoformat())
  parser.add_argument("--repository-root", type=Path, default=Path("."))
  parser.add_argument("--output", type=Path, required=True)
  parser.add_argument("--apple-manifest", type=Path, required=True)
  parser.add_argument("--apple-artifact-root", type=Path, required=True)
  parser.add_argument("--source-inventory", type=Path, required=True)
  parser.add_argument("--tagged-set-manifest", type=Path, required=True)
  parser.add_argument("--raw-renderer", type=Path, required=True)
  parser.add_argument("--metal-test", type=Path, required=True)
  parser.add_argument("--windows-raw-report", type=Path, required=True)
  parser.add_argument("--windows-raw-executable", type=Path, required=True)
  parser.add_argument("--darwin-migration-report", type=Path, required=True)
  parser.add_argument("--darwin-migration-executable", type=Path, required=True)
  parser.add_argument("--windows-migration-report", type=Path, required=True)
  parser.add_argument("--windows-migration-executable", type=Path, required=True)
  parser.add_argument("--corpus-manifest", type=Path, required=True)
  parser.add_argument("--corpus-report-root", type=Path, required=True)
  parser.add_argument("--corpus-aggregate", type=Path, required=True)
  parser.add_argument("--preset-approval", type=Path, required=True)
  parser.add_argument("--display-proof", type=Path, required=True)
  parser.add_argument("--printer-proof", type=Path, required=True)
  return parser.parse_args()


def load_validator() -> Any:
  spec = importlib.util.spec_from_file_location(
      "promotion_draft_validator", VALIDATOR_PATH
  )
  if spec is None or spec.loader is None:
    raise RuntimeError(f"unable to load validator: {VALIDATOR_PATH}")
  module = importlib.util.module_from_spec(spec)
  sys.modules["promotion_draft_validator"] = module
  spec.loader.exec_module(module)
  return module


def bundle_path(
    root: Path, path: Path, label: str, errors: list[str], *, directory: bool = False
) -> tuple[Path, str]:
  resolved = path.expanduser().resolve()
  try:
    logical = resolved.relative_to(root.resolve()).as_posix()
  except ValueError:
    errors.append(f"{label} must already be inside the evidence volume")
    return (resolved, "__invalid__")
  exists = resolved.is_dir() if directory else resolved.is_file()
  if not exists:
    errors.append(f"{label} is missing or has the wrong type")
  return (resolved, logical)


def file_reference(
    root: Path, path: Path, path_key: str, hash_key: str,
    label: str, errors: list[str], validator: Any,
) -> dict[str, Any]:
  resolved, logical = bundle_path(root, path, label, errors)
  return {
      path_key: logical,
      hash_key: validator.sha256_file(resolved) if resolved.is_file() else "",
  }


def create_draft(
    args: argparse.Namespace, *, require_clean_repository: bool = True
) -> tuple[list[str], dict[str, Any]]:
  errors: list[str] = []
  validator = load_validator()
  output_path = args.output.expanduser().resolve()
  root = output_path.parent
  if output_path.exists():
    errors.append("promotion draft output already exists")
  if not isinstance(args.promotion_id, str) or not args.promotion_id.strip():
    errors.append("promotion_id must be non-empty")
  validator.valid_date(args.created_on, "created_on", errors)
  revision = validator.repository_revision(args.repository_root)
  if revision is None:
    errors.append("repository source revision could not be resolved")
    revision = ""
  if require_clean_repository:
    clean = validator.repository_is_clean(args.repository_root)
    if clean is None:
      errors.append("repository cleanliness could not be verified")
    elif not clean:
      errors.append("promotion preparation requires a clean repository checkout")

  apple_root, apple_root_logical = bundle_path(
      root, args.apple_artifact_root, "apple_artifact_root", errors,
      directory=True,
  )
  del apple_root
  apple: dict[str, Any] = {
      **file_reference(
          root, args.apple_manifest, "manifest_path", "manifest_sha256",
          "apple_manifest", errors, validator,
      ),
      "artifact_root": apple_root_logical,
      **file_reference(
          root, args.source_inventory, "source_inventory_path",
          "source_inventory_sha256", "source_inventory", errors, validator,
      ),
      **file_reference(
          root, args.tagged_set_manifest, "tagged_set_manifest_path",
          "tagged_set_manifest_sha256", "tagged_set_manifest", errors,
          validator,
      ),
      **file_reference(
          root, args.raw_renderer, "raw_renderer_path", "raw_renderer_sha256",
          "raw_renderer", errors, validator,
      ),
      **file_reference(
          root, args.metal_test, "metal_test_path", "metal_test_sha256",
          "metal_test", errors, validator,
      ),
  }

  def binary(report: Path, executable: Path, label: str) -> dict[str, Any]:
    return {
        **file_reference(
            root, report, "report_path", "report_sha256",
            f"{label}_report", errors, validator,
        ),
        **file_reference(
            root, executable, "executable_path", "executable_sha256",
            f"{label}_executable", errors, validator,
        ),
    }

  report_root, report_root_logical = bundle_path(
      root, args.corpus_report_root, "corpus_report_root", errors,
      directory=True,
  )
  del report_root
  evidence = {
      "apple_device": apple,
      "windows_raw": binary(
          args.windows_raw_report, args.windows_raw_executable, "windows_raw"
      ),
      "rendering_migrations": [
          binary(
              args.darwin_migration_report,
              args.darwin_migration_executable,
              "darwin_migration",
          ),
          binary(
              args.windows_migration_report,
              args.windows_migration_executable,
              "windows_migration",
          ),
      ],
      "base_corpus": {
          **file_reference(
              root, args.corpus_manifest, "manifest_path", "manifest_sha256",
              "corpus_manifest", errors, validator,
          ),
          "comparison_report_root": report_root_logical,
          **file_reference(
              root, args.corpus_aggregate, "aggregate_report_path",
              "aggregate_report_sha256", "corpus_aggregate", errors,
              validator,
          ),
      },
      "preset_approval": file_reference(
          root, args.preset_approval, "manifest_path", "manifest_sha256",
          "preset_approval", errors, validator,
      ),
      "display_proof": file_reference(
          root, args.display_proof, "manifest_path", "manifest_sha256",
          "display_proof", errors, validator,
      ),
      "printer_proof": file_reference(
          root, args.printer_proof, "manifest_path", "manifest_sha256",
          "printer_proof", errors, validator,
      ),
  }
  draft = {
      "schema": validator.SCHEMA,
      "promotion_id": args.promotion_id.strip(),
      "created_on": args.created_on,
      "source_revision": revision,
      "candidate_rendering_model": validator.CANDIDATE_MODEL,
      "legacy_rendering_models": validator.LEGACY_MODELS,
      "evidence": evidence,
      "evidence_set_sha256": validator.canonical_sha256(evidence),
      "final_reviewers": [],
      "promotion_eligible": False,
      "result": "pending-approval",
  }
  if errors:
    return (errors, draft)
  encoded = (
      json.dumps(draft, indent=2, sort_keys=True, ensure_ascii=True) + "\n"
  ).encode("utf-8")
  try:
    root.mkdir(parents=True, exist_ok=True)
    with output_path.open("xb") as handle:
      handle.write(encoded)
  except OSError as exc:
    return ([f"unable to write promotion draft: {exc}"], draft)
  return ([], draft)


def main() -> int:
  args = parse_args()
  errors, draft = create_draft(args)
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print(f"status=created-color-promotion-draft:{args.output}")
  print(f"evidence_set_sha256={draft['evidence_set_sha256']}")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
