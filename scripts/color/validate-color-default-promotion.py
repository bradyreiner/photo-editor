#!/usr/bin/env python3
"""Revalidate the complete evidence required to promote ACES 2 as the default."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath
from typing import Any


SCRIPT_DIR = Path(__file__).parent
APPLE_VALIDATOR_PATH = SCRIPT_DIR / "validate-apple-device-color-proof.py"
WINDOWS_VALIDATOR_PATH = SCRIPT_DIR / "validate-windows-raw-runtime-proof.py"
MIGRATION_VALIDATOR_PATH = SCRIPT_DIR / "validate-rendering-migration-proof.py"
CORPUS_VALIDATOR_PATH = SCRIPT_DIR / "validate-color-corpus.py"
AGGREGATOR_PATH = SCRIPT_DIR / "aggregate-color-corpus-report.py"
PRESET_VALIDATOR_PATH = SCRIPT_DIR / "validate-built-in-preset-approval.py"
PHYSICAL_VALIDATOR_PATH = SCRIPT_DIR / "validate-physical-color-output-proof.py"

SCHEMA = "photo-editor-color-default-promotion:v2"
APPROVAL_SCHEMA = "photo-editor-color-default-promotion-approval:v1"
APPROVAL_NAMESPACE = "photo-editor-color-approval"
CANDIDATE_MODEL = "aces2-ocio-v1"
LEGACY_MODELS = ["aces2-hellwig-jmh-v1", "legacy-oklch-v1"]
REQUIRED_FINAL_CATEGORIES = {
    "color-science", "editorial", "release-engineering",
}
RESOLVED_APPLE_BLOCKERS = {
    "license-approved-production-raw-corpus-pending",
    "physical-sdr-hdr-monitor-switching-proof-pending",
    "physical-printer-paper-proof-pending",
    "blinded-editorial-approval-pending",
    "built-in-preset-retuning-pending",
}
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
REVISION_PATTERN = re.compile(r"^[0-9a-f]{40}$")
ROOT_KEYS = {
    "schema", "promotion_id", "created_on", "source_revision",
    "candidate_rendering_model", "legacy_rendering_models", "evidence",
    "evidence_set_sha256", "final_reviewers", "promotion_eligible", "result",
}
EVIDENCE_KEYS = {
    "apple_device", "windows_raw", "rendering_migrations", "base_corpus",
    "preset_approval", "display_proof", "printer_proof",
}
APPLE_KEYS = {
    "manifest_path", "manifest_sha256", "artifact_root",
    "source_inventory_path", "source_inventory_sha256",
    "tagged_set_manifest_path", "tagged_set_manifest_sha256",
    "raw_renderer_path", "raw_renderer_sha256", "metal_test_path",
    "metal_test_sha256",
}
BINARY_PROOF_KEYS = {
    "report_path", "report_sha256", "executable_path", "executable_sha256",
}
CORPUS_KEYS = {
    "manifest_path", "manifest_sha256", "comparison_report_root",
    "aggregate_report_path", "aggregate_report_sha256",
}
FILE_PROOF_KEYS = {"manifest_path", "manifest_sha256"}
REVIEWER_KEYS = {
    "reviewer_id", "reviewed_on", "decision", "independent",
    "evidence_set_sha256", "categories", "notes", "signature_path",
    "signature_sha256",
}


def parse_args() -> argparse.Namespace:
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("manifest", type=Path)
  parser.add_argument("--repository-root", type=Path, default=Path("."))
  parser.add_argument("--allowed-signers", type=Path, required=True)
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
    errors.append(f"{label} root must be an object")
    return None
  return value


def sha256_file(path: Path) -> str:
  digest = hashlib.sha256()
  with path.open("rb") as handle:
    for block in iter(lambda: handle.read(1024 * 1024), b""):
      digest.update(block)
  return digest.hexdigest()


def canonical_sha256(value: Any) -> str:
  payload = json.dumps(
      value, ensure_ascii=True, separators=(",", ":"), sort_keys=True
  ).encode("utf-8")
  return hashlib.sha256(payload).hexdigest()


def approval_statement(
    promotion_id: Any, source_revision: Any, evidence_set_sha256: Any,
    reviewer: dict[str, Any],
) -> bytes:
  statement = {
      "schema": APPROVAL_SCHEMA,
      "promotion_id": promotion_id,
      "source_revision": source_revision,
      "evidence_set_sha256": evidence_set_sha256,
      "reviewer_id": reviewer.get("reviewer_id"),
      "reviewed_on": reviewer.get("reviewed_on"),
      "decision": reviewer.get("decision"),
      "independent": reviewer.get("independent"),
      "categories": reviewer.get("categories"),
      "notes": reviewer.get("notes"),
  }
  return json.dumps(
      statement, ensure_ascii=True, separators=(",", ":"), sort_keys=True
  ).encode("utf-8") + b"\n"


def repository_revision(repository_root: Path) -> str | None:
  try:
    result = subprocess.run(
        ["git", "-C", str(repository_root.resolve()), "rev-parse", "HEAD"],
        check=False, capture_output=True, text=True, timeout=10,
    )
  except (OSError, subprocess.SubprocessError):
    return None
  revision = result.stdout.strip().lower()
  return revision if result.returncode == 0 and REVISION_PATTERN.fullmatch(revision) else None


def repository_is_clean(repository_root: Path) -> bool | None:
  try:
    result = subprocess.run(
        [
            "git", "-C", str(repository_root.resolve()), "status",
            "--porcelain=v1", "--untracked-files=all",
        ],
        check=False, capture_output=True, text=True, timeout=30,
    )
  except (OSError, subprocess.SubprocessError):
    return None
  return not result.stdout.strip() if result.returncode == 0 else None


def allowed_signers_is_tracked(
    repository_root: Path, allowed_signers_path: Path
) -> bool:
  root = repository_root.resolve()
  path = allowed_signers_path.resolve()
  try:
    relative = path.relative_to(root).as_posix()
    result = subprocess.run(
        ["git", "-C", str(root), "show", f"HEAD:{relative}"],
        check=False, capture_output=True, timeout=10,
    )
  except (OSError, ValueError, subprocess.SubprocessError):
    return False
  try:
    current = path.read_bytes()
  except OSError:
    return False
  return result.returncode == 0 and result.stdout == current


def verify_ssh_signature(
    allowed_signers_path: Path, reviewer_id: str,
    signature_path: Path, statement: bytes,
) -> bool:
  try:
    result = subprocess.run(
        [
            "ssh-keygen", "-Y", "verify", "-f", str(allowed_signers_path),
            "-I", reviewer_id, "-n", APPROVAL_NAMESPACE,
            "-s", str(signature_path),
        ],
        input=statement, check=False, capture_output=True, timeout=15,
    )
  except (OSError, subprocess.SubprocessError):
    return False
  return result.returncode == 0


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


def valid_date(value: Any, label: str, errors: list[str]) -> dt.date | None:
  if not isinstance(value, str):
    errors.append(f"{label} must use YYYY-MM-DD")
    return None
  try:
    return dt.date.fromisoformat(value)
  except ValueError:
    errors.append(f"{label} must use YYYY-MM-DD")
    return None


def resolve_bundle_path(
    bundle_root: Path, value: Any, label: str, errors: list[str]
) -> Path:
  invalid = bundle_root / "__invalid__"
  if not isinstance(value, str) or not value or "\\" in value or ":" in value:
    errors.append(f"{label} must be a portable bundle-relative path")
    return invalid
  logical = PurePosixPath(value)
  if logical.is_absolute() or any(part in ("", ".", "..") for part in logical.parts):
    errors.append(f"{label} must remain inside the evidence bundle")
    return invalid
  resolved = (bundle_root / Path(*logical.parts)).resolve()
  try:
    resolved.relative_to(bundle_root.resolve())
  except ValueError:
    errors.append(f"{label} escapes the evidence bundle")
    return invalid
  return resolved


def verify_file_reference(
    record: dict[str, Any], path_key: str, hash_key: str,
    bundle_root: Path, label: str, errors: list[str],
) -> Path:
  path = resolve_bundle_path(bundle_root, record.get(path_key), f"{label}.{path_key}", errors)
  expected = record.get(hash_key)
  if not valid_sha256(expected):
    errors.append(f"{label}.{hash_key} must be a SHA-256 digest")
  if not path.is_file():
    errors.append(f"{label}.{path_key} is missing")
  elif expected != sha256_file(path):
    errors.append(f"{label}.{hash_key} does not match the referenced file")
  return path


def collect_reviewers(value: Any) -> set[str]:
  if not isinstance(value, list):
    return set()
  return {
      item["reviewer_id"] for item in value
      if isinstance(item, dict)
      and isinstance(item.get("reviewer_id"), str)
      and item["reviewer_id"]
  }


def validate_manifest(
    manifest_path: Path, repository_root: Path, allowed_signers_path: Path, *,
    require_clean_repository: bool = True,
    require_tracked_allowed_signers: bool = True,
) -> list[str]:
  errors: list[str] = []
  manifest_path = manifest_path.resolve()
  bundle_root = manifest_path.parent
  allowed_signers_path = allowed_signers_path.resolve()
  manifest = load_json(manifest_path, "default-promotion manifest", errors)
  if manifest is None:
    return errors
  manifest = exact_object(manifest, ROOT_KEYS, "manifest", errors)
  evidence = exact_object(manifest.get("evidence"), EVIDENCE_KEYS, "manifest.evidence", errors)

  if (
      manifest.get("schema") != SCHEMA
      or manifest.get("result") != "approved"
      or manifest.get("promotion_eligible") is not True
  ):
    errors.append("manifest must be an approved, promotion-eligible v2 decision")
  if not isinstance(manifest.get("promotion_id"), str) or not manifest["promotion_id"]:
    errors.append("manifest.promotion_id must be non-empty")
  created_on = valid_date(manifest.get("created_on"), "manifest.created_on", errors)
  if not isinstance(manifest.get("source_revision"), str) or REVISION_PATTERN.fullmatch(
      manifest["source_revision"]
  ) is None:
    errors.append("manifest.source_revision must be a full 40-character Git revision")
  current_revision = repository_revision(repository_root)
  if current_revision is None:
    errors.append("repository source revision could not be resolved")
  elif manifest.get("source_revision") != current_revision:
    errors.append("manifest.source_revision does not match the checked-out repository")
  if require_clean_repository:
    clean = repository_is_clean(repository_root)
    if clean is None:
      errors.append("repository cleanliness could not be verified")
    elif not clean:
      errors.append("default promotion requires a clean repository checkout")
  if not allowed_signers_path.is_file():
    errors.append("reviewer allowed-signers file is missing")
  elif require_tracked_allowed_signers and not allowed_signers_is_tracked(
      repository_root, allowed_signers_path
  ):
    errors.append(
        "reviewer allowed-signers file must match the version tracked by source_revision"
    )
  if manifest.get("candidate_rendering_model") != CANDIDATE_MODEL:
    errors.append(f"candidate rendering model must be {CANDIDATE_MODEL}")
  if manifest.get("legacy_rendering_models") != LEGACY_MODELS:
    errors.append("legacy rendering models must retain every historical renderer")
  expected_evidence_sha = canonical_sha256(evidence)
  if manifest.get("evidence_set_sha256") != expected_evidence_sha:
    errors.append("manifest.evidence_set_sha256 does not bind the complete evidence map")

  apple_validator = load_module(APPLE_VALIDATOR_PATH, "default_promotion_apple_validator")
  windows_validator = load_module(WINDOWS_VALIDATOR_PATH, "default_promotion_windows_validator")
  migration_validator = load_module(MIGRATION_VALIDATOR_PATH, "default_promotion_migration_validator")
  corpus_validator = load_module(CORPUS_VALIDATOR_PATH, "default_promotion_corpus_validator")
  aggregator = load_module(AGGREGATOR_PATH, "default_promotion_corpus_aggregator")
  preset_validator = load_module(PRESET_VALIDATOR_PATH, "default_promotion_preset_validator")
  physical_validator = load_module(PHYSICAL_VALIDATOR_PATH, "default_promotion_physical_validator")
  underlying_reviewers: set[str] = set()

  apple = exact_object(evidence.get("apple_device"), APPLE_KEYS, "evidence.apple_device", errors)
  apple_manifest_path = verify_file_reference(
      apple, "manifest_path", "manifest_sha256", bundle_root,
      "evidence.apple_device", errors,
  )
  source_inventory_path = verify_file_reference(
      apple, "source_inventory_path", "source_inventory_sha256", bundle_root,
      "evidence.apple_device", errors,
  )
  tagged_set_path = verify_file_reference(
      apple, "tagged_set_manifest_path", "tagged_set_manifest_sha256", bundle_root,
      "evidence.apple_device", errors,
  )
  raw_renderer_path = verify_file_reference(
      apple, "raw_renderer_path", "raw_renderer_sha256", bundle_root,
      "evidence.apple_device", errors,
  )
  metal_test_path = verify_file_reference(
      apple, "metal_test_path", "metal_test_sha256", bundle_root,
      "evidence.apple_device", errors,
  )
  apple_root = resolve_bundle_path(
      bundle_root, apple.get("artifact_root"),
      "evidence.apple_device.artifact_root", errors,
  )
  if not apple_root.is_dir():
    errors.append("evidence.apple_device.artifact_root is missing")
  apple_errors = apple_validator.validate_manifest(
      apple_manifest_path, source_inventory_path=source_inventory_path,
      tagged_set_manifest_path=tagged_set_path,
      raw_renderer_path=raw_renderer_path, metal_test_path=metal_test_path,
      artifact_root=apple_root,
  )
  errors.extend(f"apple-device: {error}" for error in apple_errors)
  apple_manifest = load_json(apple_manifest_path, "Apple proof", errors) or {}
  apple_blockers = apple_manifest.get("promotion_blockers")
  if (
      not isinstance(apple_blockers, list)
      or any(not isinstance(value, str) for value in apple_blockers)
      or set(apple_blockers) != RESOLVED_APPLE_BLOCKERS
  ):
    errors.append("Apple proof must enumerate the exact blockers resolved by this promotion gate")

  windows = exact_object(evidence.get("windows_raw"), BINARY_PROOF_KEYS, "evidence.windows_raw", errors)
  windows_report_path = verify_file_reference(
      windows, "report_path", "report_sha256", bundle_root,
      "evidence.windows_raw", errors,
  )
  windows_executable_path = verify_file_reference(
      windows, "executable_path", "executable_sha256", bundle_root,
      "evidence.windows_raw", errors,
  )
  errors.extend(
      f"windows-raw: {error}" for error in windows_validator.validate_report(
          windows_report_path, windows_executable_path
      )
  )

  migrations = evidence.get("rendering_migrations")
  if not isinstance(migrations, list) or len(migrations) != 2:
    errors.append("evidence.rendering_migrations must contain exactly Darwin and Windows proofs")
    migrations = []
  migration_platforms: set[str] = set()
  migration_executable_hashes: set[str] = set()
  for index, value in enumerate(migrations):
    label = f"evidence.rendering_migrations[{index}]"
    migration = exact_object(value, BINARY_PROOF_KEYS, label, errors)
    report_path = verify_file_reference(
        migration, "report_path", "report_sha256", bundle_root, label, errors
    )
    executable_path = verify_file_reference(
        migration, "executable_path", "executable_sha256", bundle_root, label, errors
    )
    errors.extend(
        f"migration[{index}]: {error}"
        for error in migration_validator.validate_report(report_path, executable_path)
    )
    report = load_json(report_path, f"migration proof {index}", errors) or {}
    kernel = (report.get("hardware") or {}).get("kernel_type")
    if isinstance(kernel, str):
      migration_platforms.add(kernel.lower())
    executable_hash = migration.get("executable_sha256")
    if isinstance(executable_hash, str):
      migration_executable_hashes.add(executable_hash)
  if migration_platforms != {"darwin", "winnt"}:
    errors.append("rendering migration proofs must execute on Darwin and Windows")
  if len(migration_executable_hashes) != 2:
    errors.append("Darwin and Windows migration proofs must bind distinct binaries")

  corpus = exact_object(evidence.get("base_corpus"), CORPUS_KEYS, "evidence.base_corpus", errors)
  corpus_manifest_path = verify_file_reference(
      corpus, "manifest_path", "manifest_sha256", bundle_root,
      "evidence.base_corpus", errors,
  )
  aggregate_path = verify_file_reference(
      corpus, "aggregate_report_path", "aggregate_report_sha256", bundle_root,
      "evidence.base_corpus", errors,
  )
  report_root = resolve_bundle_path(
      bundle_root, corpus.get("comparison_report_root"),
      "evidence.base_corpus.comparison_report_root", errors,
  )
  if not report_root.is_dir():
    errors.append("evidence.base_corpus.comparison_report_root is missing")
  errors.extend(
      f"base-corpus: {error}" for error in corpus_validator.validate_manifest(
          corpus_manifest_path, require_promotion_approval=True
      )
  )
  corpus_manifest = load_json(corpus_manifest_path, "base corpus", errors) or {}
  if corpus_manifest.get("candidate_rendering_model") != CANDIDATE_MODEL:
    errors.append("base corpus must evaluate the exact default candidate model")
  underlying_reviewers.update(collect_reviewers(corpus_manifest.get("review_records")))
  aggregate_errors, regenerated_aggregate = aggregator.aggregate(
      corpus_manifest_path, report_root, aggregate_path
  )
  errors.extend(f"base-corpus aggregate: {error}" for error in aggregate_errors)
  recorded_aggregate = load_json(aggregate_path, "base aggregate report", errors)
  if recorded_aggregate is not None and recorded_aggregate != regenerated_aggregate:
    errors.append("base aggregate report does not match regenerated evidence")

  preset = exact_object(evidence.get("preset_approval"), FILE_PROOF_KEYS, "evidence.preset_approval", errors)
  preset_path = verify_file_reference(
      preset, "manifest_path", "manifest_sha256", bundle_root,
      "evidence.preset_approval", errors,
  )
  errors.extend(
      f"preset-approval: {error}" for error in preset_validator.validate_manifest(
          preset_path, repository_root.resolve()
      )
  )
  preset_manifest = load_json(preset_path, "preset approval", errors) or {}
  for evaluation in preset_manifest.get("evaluations", []):
    if isinstance(evaluation, dict):
      reviewer_ids = evaluation.get("reviewer_ids")
      if isinstance(reviewer_ids, list):
        underlying_reviewers.update(
            value for value in reviewer_ids if isinstance(value, str) and value
        )

  physical_manifests: dict[str, dict[str, Any]] = {}
  for evidence_key, expected_type in (
      ("display_proof", "display-switching"),
      ("printer_proof", "printer-paper"),
  ):
    label = f"evidence.{evidence_key}"
    record = exact_object(evidence.get(evidence_key), FILE_PROOF_KEYS, label, errors)
    proof_path = verify_file_reference(
        record, "manifest_path", "manifest_sha256", bundle_root, label, errors
    )
    errors.extend(
        f"{evidence_key}: {error}"
        for error in physical_validator.validate_manifest(proof_path)
    )
    proof = load_json(proof_path, evidence_key, errors) or {}
    physical_manifests[evidence_key] = proof
    if proof.get("proof_type") != expected_type:
      errors.append(f"{label} must contain a {expected_type} proof")
    underlying_reviewers.update(collect_reviewers(proof.get("reviewers")))
  display_platform = physical_manifests.get("display_proof", {}).get("platform")
  display_os = str(
      display_platform.get("os", "") if isinstance(display_platform, dict) else ""
  ).lower()
  if not display_os.startswith("windows"):
    errors.append("display-switching proof must execute the Windows runtime ICC path")

  reviewers = manifest.get("final_reviewers")
  if not isinstance(reviewers, list) or len(reviewers) < 2:
    errors.append("manifest.final_reviewers must contain at least two reviewers")
    reviewers = []
  final_ids: set[str] = set()
  for index, value in enumerate(reviewers):
    label = f"manifest.final_reviewers[{index}]"
    reviewer = exact_object(value, REVIEWER_KEYS, label, errors)
    reviewer_id = reviewer.get("reviewer_id")
    if not isinstance(reviewer_id, str) or not reviewer_id:
      errors.append(f"{label}.reviewer_id must be non-empty")
    elif reviewer_id in final_ids:
      errors.append(f"{label}.reviewer_id must be distinct")
    else:
      final_ids.add(reviewer_id)
    reviewed_on = valid_date(reviewer.get("reviewed_on"), f"{label}.reviewed_on", errors)
    if created_on is not None and reviewed_on is not None and reviewed_on < created_on:
      errors.append(f"{label} approved before the complete evidence manifest existed")
    categories = reviewer.get("categories")
    categories_valid = (
        isinstance(categories, list)
        and all(isinstance(category, str) for category in categories)
        and set(categories) == REQUIRED_FINAL_CATEGORIES
    )
    if (
        reviewer.get("decision") != "approved"
        or reviewer.get("independent") is not True
        or reviewer.get("evidence_set_sha256") != expected_evidence_sha
        or not categories_valid
        or not isinstance(reviewer.get("notes"), str)
        or not reviewer["notes"]
    ):
      errors.append(f"{label} must independently approve every final review category and evidence hash")
    signature_path = verify_file_reference(
        reviewer, "signature_path", "signature_sha256", bundle_root,
        label, errors,
    )
    if (
        isinstance(reviewer_id, str) and reviewer_id
        and allowed_signers_path.is_file()
        and signature_path.is_file()
        and not verify_ssh_signature(
            allowed_signers_path, reviewer_id, signature_path,
            approval_statement(
                manifest.get("promotion_id"), manifest.get("source_revision"),
                expected_evidence_sha, reviewer,
            ),
        )
    ):
      errors.append(f"{label} detached SSH signature is invalid or untrusted")
  overlap = final_ids & underlying_reviewers
  if overlap:
    errors.append(
        "final promotion reviewers must be independent of evidence preparation: "
        + ", ".join(sorted(overlap))
    )
  return errors


def main() -> int:
  args = parse_args()
  errors = validate_manifest(
      args.manifest, args.repository_root, args.allowed_signers
  )
  if errors:
    for error in errors:
      print(f"error={error}", file=sys.stderr)
    return 1
  print("status=approved-color-default-promotion")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
