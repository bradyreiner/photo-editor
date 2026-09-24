#!/usr/bin/env python3
"""Adversarial tests for the complete color-default promotion gate."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace
from typing import Any, Callable


SCRIPT_DIR = Path(__file__).parent
REPOSITORY_ROOT = SCRIPT_DIR.parents[1]
VALIDATOR_PATH = SCRIPT_DIR / "validate-color-default-promotion.py"


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


def relative(root: Path, path: Path) -> str:
  return path.resolve().relative_to(root.resolve()).as_posix()


def binary_record(root: Path, report: Path, executable: Path) -> dict[str, Any]:
  return {
      "report_path": relative(root, report),
      "report_sha256": sha256_file(report),
      "executable_path": relative(root, executable),
      "executable_sha256": sha256_file(executable),
  }


def windows_raw_fixture(root: Path, validator: Any) -> tuple[Path, Path]:
  executable = root / "raw_camera_linear_decoder_test.exe"
  executable.parent.mkdir(parents=True, exist_ok=True)
  executable.write_bytes(b"fixture-windows-release-raw-executable")
  report = {
      "schema": validator.SCHEMA,
      "tool_version": validator.TOOL_VERSION,
      "hardware": {
          "kernel_type": "winnt", "kernel_version": "10.0",
          "product_type": "windows", "product_version": "11",
          "cpu_architecture": "x86_64", "qt_version": "6.7.3",
          "build_mode": "release",
      },
      "executable_sha256": sha256_file(executable),
      "capability": {
          "backend": "LibRaw", "compiled_version": "0.21.5-Release",
          "runtime_version": "0.21.5-Release",
          "validated_version": "0.21.5",
          "pixel_contract": "camera-rgb-device-values-no-camera-matrix",
          "maximum_raw_memory_bytes": 1536 * 1024 * 1024,
      },
      "synthetic_dng": {
          "sha256": "1" * 64, "width": 128, "height": 128,
          "camera_linear_backend": "LibRaw camera-linear 0.21.5-Release",
          "shipping_backend": "LibRaw characterized linear-sRGB 0.21.5-Release",
          "camera_linear_pixel_sha256": "2" * 64,
          "shipping_rgb_maximum": 0.75, "concurrent_decode_count": 4,
      },
      "contracts": {key: True for key in validator.CONTRACT_KEYS},
      "result": "passed",
  }
  report_path = root / "report.json"
  write_json(report_path, report)
  return report_path, executable


def migration_fixture(
    root: Path, validator: Any, helper: Any, kernel: str, executable_bytes: bytes,
) -> tuple[Path, Path]:
  executable = root / (
      "rendering_migration_proof_test.exe"
      if kernel == "winnt" else "rendering_migration_proof_test"
  )
  executable.parent.mkdir(parents=True, exist_ok=True)
  executable.write_bytes(executable_bytes)
  report = helper.fixture(validator, executable)
  report["hardware"]["kernel_type"] = kernel
  report["hardware"]["product_type"] = "windows" if kernel == "winnt" else "macos"
  report["hardware"]["cpu_architecture"] = "x86_64" if kernel == "winnt" else "arm64"
  report_path = root / "report.json"
  write_json(report_path, report)
  return report_path, executable


def build_fixture(
    root: Path, validator: Any
) -> tuple[Path, dict[str, Any], Path]:
  apple_validator = load_module(
      validator.APPLE_VALIDATOR_PATH, "promotion_test_apple_validator"
  )
  apple_helper = load_module(
      SCRIPT_DIR / "test-apple-device-color-proof.py",
      "promotion_test_apple_helper",
  )
  apple_root = root / "apple"
  apple_root.mkdir()
  (
      apple_manifest_path, source_inventory_path, tagged_set_path,
      raw_renderer_path, metal_test_path, apple_manifest, _,
  ) = apple_helper.build_fixture(apple_root, apple_validator)
  apple_manifest["promotion_blockers"] = sorted(
      validator.RESOLVED_APPLE_BLOCKERS
  )
  write_json(apple_manifest_path, apple_manifest)

  windows_validator = load_module(
      validator.WINDOWS_VALIDATOR_PATH, "promotion_test_windows_validator"
  )
  windows_report, windows_executable = windows_raw_fixture(
      root / "windows-raw", windows_validator
  )

  migration_validator = load_module(
      validator.MIGRATION_VALIDATOR_PATH, "promotion_test_migration_validator"
  )
  migration_helper = load_module(
      SCRIPT_DIR / "test-rendering-migration-proof.py",
      "promotion_test_migration_helper",
  )
  darwin_report, darwin_executable = migration_fixture(
      root / "migration-darwin", migration_validator, migration_helper,
      "darwin", b"fixture-darwin-release-migration-executable",
  )
  windows_migration_report, windows_migration_executable = migration_fixture(
      root / "migration-windows", migration_validator, migration_helper,
      "winnt", b"fixture-windows-release-migration-executable",
  )

  corpus_validator = load_module(
      validator.CORPUS_VALIDATOR_PATH, "promotion_test_corpus_validator"
  )
  corpus_helper = load_module(
      SCRIPT_DIR / "test-validate-color-corpus.py",
      "promotion_test_corpus_helper",
  )
  aggregate_helper = load_module(
      SCRIPT_DIR / "test-aggregate-color-corpus-report.py",
      "promotion_test_aggregate_helper",
  )
  aggregator = load_module(
      validator.AGGREGATOR_PATH, "promotion_test_aggregator"
  )
  corpus_root = root / "base-corpus"
  corpus_root.mkdir()
  corpus_manifest_path, corpus_manifest = corpus_helper.materialize_fixture(
      corpus_root, corpus_validator
  )
  report_root = aggregate_helper.materialize_reports(
      corpus_root, corpus_manifest_path, corpus_manifest
  )
  aggregate_path = corpus_root / "aggregate.json"
  aggregate_errors, aggregate = aggregator.aggregate(
      corpus_manifest_path, report_root, aggregate_path
  )
  if aggregate_errors:
    raise AssertionError(f"base corpus fixture failed: {aggregate_errors}")
  write_json(aggregate_path, aggregate)

  preset_validator = load_module(
      validator.PRESET_VALIDATOR_PATH, "promotion_test_preset_validator"
  )
  preset_helper = load_module(
      SCRIPT_DIR / "test-built-in-preset-approval.py",
      "promotion_test_preset_helper",
  )
  preset_root = root / "preset-approval"
  preset_root.mkdir()
  preset_path, _ = preset_helper.build_fixture(preset_root, preset_validator)

  physical_validator = load_module(
      validator.PHYSICAL_VALIDATOR_PATH, "promotion_test_physical_validator"
  )
  physical_helper = load_module(
      SCRIPT_DIR / "test-physical-color-output-proof.py",
      "promotion_test_physical_helper",
  )
  display_root = root / "display-proof"
  printer_root = root / "printer-proof"
  display_root.mkdir()
  printer_root.mkdir()
  display = physical_helper.display_fixture(display_root, physical_validator)
  display["platform"]["os"] = "Windows 11"
  display_path = display_root / "manifest.json"
  write_json(display_path, display)
  printer = physical_helper.printer_fixture(printer_root, physical_validator)
  printer_path = printer_root / "manifest.json"
  write_json(printer_path, printer)

  evidence = {
      "apple_device": {
          "manifest_path": relative(root, apple_manifest_path),
          "manifest_sha256": sha256_file(apple_manifest_path),
          "artifact_root": relative(root, apple_root),
          "source_inventory_path": relative(root, source_inventory_path),
          "source_inventory_sha256": sha256_file(source_inventory_path),
          "tagged_set_manifest_path": relative(root, tagged_set_path),
          "tagged_set_manifest_sha256": sha256_file(tagged_set_path),
          "raw_renderer_path": relative(root, raw_renderer_path),
          "raw_renderer_sha256": sha256_file(raw_renderer_path),
          "metal_test_path": relative(root, metal_test_path),
          "metal_test_sha256": sha256_file(metal_test_path),
      },
      "windows_raw": binary_record(
          root, windows_report, windows_executable
      ),
      "rendering_migrations": [
          binary_record(root, darwin_report, darwin_executable),
          binary_record(
              root, windows_migration_report, windows_migration_executable
          ),
      ],
      "base_corpus": {
          "manifest_path": relative(root, corpus_manifest_path),
          "manifest_sha256": sha256_file(corpus_manifest_path),
          "comparison_report_root": relative(root, report_root),
          "aggregate_report_path": relative(root, aggregate_path),
          "aggregate_report_sha256": sha256_file(aggregate_path),
      },
      "preset_approval": {
          "manifest_path": relative(root, preset_path),
          "manifest_sha256": sha256_file(preset_path),
      },
      "display_proof": {
          "manifest_path": relative(root, display_path),
          "manifest_sha256": sha256_file(display_path),
      },
      "printer_proof": {
          "manifest_path": relative(root, printer_path),
          "manifest_sha256": sha256_file(printer_path),
      },
  }
  evidence_sha = validator.canonical_sha256(evidence)
  key_root = root / "reviewer-keys"
  approval_root = root / "approvals"
  key_root.mkdir()
  approval_root.mkdir()
  signing_keys: dict[str, Path] = {}
  allowed_lines: list[str] = []
  for index in (1, 2):
    reviewer_id = f"final-approver-{index}"
    key_path = key_root / reviewer_id
    subprocess.run(
        [
            "ssh-keygen", "-q", "-t", "ed25519", "-N", "",
            "-C", reviewer_id, "-f", str(key_path),
        ],
        check=True, capture_output=True,
    )
    signing_keys[reviewer_id] = key_path
    allowed_lines.append(
        f"{reviewer_id} {key_path.with_suffix('.pub').read_text(encoding='utf-8').strip()}"
    )
  allowed_signers_path = root / "allowed-signers"
  allowed_signers_path.write_text("\n".join(allowed_lines) + "\n", encoding="utf-8")

  final_reviewers = []
  for index in (1, 2):
    reviewer = {
          "reviewer_id": f"final-approver-{index}",
          "reviewed_on": "2026-07-16", "decision": "approved",
          "independent": True, "evidence_set_sha256": evidence_sha,
          "categories": sorted(validator.REQUIRED_FINAL_CATEGORIES),
          "notes": "Fixture approval exercises the complete promotion decision.",
    }
    statement_path = approval_root / f"final-approver-{index}.statement.json"
    statement_path.write_bytes(validator.approval_statement(
        "fixture-aces2-default-promotion-v1",
        validator.repository_revision(REPOSITORY_ROOT), evidence_sha, reviewer,
    ))
    subprocess.run(
        [
            "ssh-keygen", "-Y", "sign", "-f",
            str(signing_keys[reviewer["reviewer_id"]]),
            "-n", validator.APPROVAL_NAMESPACE, str(statement_path),
        ],
        check=True, capture_output=True,
    )
    signature_path = Path(str(statement_path) + ".sig")
    reviewer["signature_path"] = relative(root, signature_path)
    reviewer["signature_sha256"] = sha256_file(signature_path)
    final_reviewers.append(reviewer)
  manifest = {
      "schema": validator.SCHEMA,
      "promotion_id": "fixture-aces2-default-promotion-v1",
      "created_on": "2026-07-16",
      "source_revision": validator.repository_revision(REPOSITORY_ROOT),
      "candidate_rendering_model": validator.CANDIDATE_MODEL,
      "legacy_rendering_models": validator.LEGACY_MODELS,
      "evidence": evidence,
      "evidence_set_sha256": evidence_sha,
      "final_reviewers": final_reviewers,
      "promotion_eligible": True,
      "result": "approved",
  }
  manifest_path = root / "color-default-promotion.json"
  write_json(manifest_path, manifest)
  return manifest_path, manifest, allowed_signers_path


def main() -> int:
  validator = load_module(VALIDATOR_PATH, "color_default_promotion_validator")
  with tempfile.TemporaryDirectory(prefix="color-default-promotion-") as temp:
    root = Path(temp)
    manifest_path, manifest, allowed_signers_path = build_fixture(root, validator)

    preparer = load_module(
        SCRIPT_DIR / "prepare-color-default-promotion.py",
        "promotion_test_preparer",
    )
    evidence = manifest["evidence"]
    apple = evidence["apple_device"]
    windows_raw = evidence["windows_raw"]
    darwin_migration, windows_migration = evidence["rendering_migrations"]
    corpus = evidence["base_corpus"]
    resolve = lambda value: root / value
    prepare_args = SimpleNamespace(
        promotion_id=manifest["promotion_id"],
        created_on=manifest["created_on"],
        repository_root=REPOSITORY_ROOT,
        output=root / "prepared-color-default-promotion.json",
        apple_manifest=resolve(apple["manifest_path"]),
        apple_artifact_root=resolve(apple["artifact_root"]),
        source_inventory=resolve(apple["source_inventory_path"]),
        tagged_set_manifest=resolve(apple["tagged_set_manifest_path"]),
        raw_renderer=resolve(apple["raw_renderer_path"]),
        metal_test=resolve(apple["metal_test_path"]),
        windows_raw_report=resolve(windows_raw["report_path"]),
        windows_raw_executable=resolve(windows_raw["executable_path"]),
        darwin_migration_report=resolve(darwin_migration["report_path"]),
        darwin_migration_executable=resolve(darwin_migration["executable_path"]),
        windows_migration_report=resolve(windows_migration["report_path"]),
        windows_migration_executable=resolve(windows_migration["executable_path"]),
        corpus_manifest=resolve(corpus["manifest_path"]),
        corpus_report_root=resolve(corpus["comparison_report_root"]),
        corpus_aggregate=resolve(corpus["aggregate_report_path"]),
        preset_approval=resolve(evidence["preset_approval"]["manifest_path"]),
        display_proof=resolve(evidence["display_proof"]["manifest_path"]),
        printer_proof=resolve(evidence["printer_proof"]["manifest_path"]),
    )
    prepare_errors, prepared = preparer.create_draft(
        prepare_args, require_clean_repository=False
    )
    if (
        prepare_errors or prepared["evidence"] != manifest["evidence"]
        or prepared["result"] != "pending-approval"
        or prepared["promotion_eligible"] is not False
        or prepared["final_reviewers"]
    ):
      raise AssertionError(
          f"promotion preparation did not reproduce the evidence map: {prepare_errors}"
      )
    outside_args = copy.copy(prepare_args)
    outside_args.output = root / "outside-path-draft.json"
    outside_args.raw_renderer = VALIDATOR_PATH
    outside_errors, _ = preparer.create_draft(
        outside_args, require_clean_repository=False
    )
    if not any("inside the evidence volume" in error for error in outside_errors):
      raise AssertionError(f"outside evidence path was accepted: {outside_errors}")

    def validate(value: dict[str, Any]) -> list[str]:
      write_json(manifest_path, value)
      return validator.validate_manifest(
          manifest_path, REPOSITORY_ROOT, allowed_signers_path,
          require_clean_repository=False,
          require_tracked_allowed_signers=False,
      )

    errors = validate(manifest)
    if errors:
      raise AssertionError(f"valid complete promotion evidence rejected: {errors}")
    tracked_errors = validator.validate_manifest(
        manifest_path, REPOSITORY_ROOT, allowed_signers_path,
        require_clean_repository=False,
        require_tracked_allowed_signers=True,
    )
    if not any("must match the version tracked" in error for error in tracked_errors):
      raise AssertionError(
          f"untracked reviewer trust policy was accepted: {tracked_errors}"
      )
    restricted_signers = root / "restricted-allowed-signers"
    restricted_signers.write_text(
        allowed_signers_path.read_text(encoding="utf-8").splitlines()[0] + "\n",
        encoding="utf-8",
    )
    restricted_errors = validator.validate_manifest(
        manifest_path, REPOSITORY_ROOT, restricted_signers,
        require_clean_repository=False,
        require_tracked_allowed_signers=False,
    )
    if not any("invalid or untrusted" in error for error in restricted_errors):
      raise AssertionError(
          f"reviewer outside the trust policy was accepted: {restricted_errors}"
      )

    statement_creator = load_module(
        SCRIPT_DIR / "create-color-promotion-approval-statement.py",
        "promotion_test_statement_creator",
    )
    reviewer_draft = {
        key: value for key, value in manifest["final_reviewers"][0].items()
        if key not in {"signature_path", "signature_sha256"}
    }
    reviewer_draft_path = root / "reviewer-draft.json"
    generated_statement_path = root / "generated-approval-statement.json"
    write_json(reviewer_draft_path, reviewer_draft)
    statement_errors = statement_creator.create_statement(
        manifest_path, reviewer_draft_path, generated_statement_path
    )
    expected_statement_path = root / "approvals/final-approver-1.statement.json"
    if statement_errors or generated_statement_path.read_bytes() != expected_statement_path.read_bytes():
      raise AssertionError(
          f"canonical approval statement generation drifted: {statement_errors}"
      )
    stale_reviewer = copy.deepcopy(reviewer_draft)
    stale_reviewer["evidence_set_sha256"] = "0" * 64
    write_json(reviewer_draft_path, stale_reviewer)
    statement_errors = statement_creator.create_statement(
        manifest_path, reviewer_draft_path, root / "stale-approval-statement.json"
    )
    if not any("exact evidence" in error for error in statement_errors):
      raise AssertionError(
          f"stale reviewer statement was accepted: {statement_errors}"
      )

    reviewer_paths: list[Path] = []
    approval_pairs: list[tuple[Path, Path]] = []
    for index, reviewer in enumerate(manifest["final_reviewers"], start=1):
      unsigned = {
          key: value for key, value in reviewer.items()
          if key not in {"signature_path", "signature_sha256"}
      }
      reviewer_path = root / f"final-reviewer-{index}.json"
      write_json(reviewer_path, unsigned)
      reviewer_paths.append(reviewer_path)
      approval_pairs.append((reviewer_path, resolve(reviewer["signature_path"])))
    finalizer = load_module(
        SCRIPT_DIR / "finalize-color-default-promotion.py",
        "promotion_test_finalizer",
    )
    finalized_path = root / "finalized-color-default-promotion.json"
    finalize_errors, finalized = finalizer.finalize(
        prepare_args.output, approval_pairs, allowed_signers_path,
        REPOSITORY_ROOT, finalized_path,
        require_clean_repository=False,
        require_tracked_allowed_signers=False,
    )
    if finalize_errors or finalized != manifest or not finalized_path.is_file():
      raise AssertionError(
          f"signed promotion finalization failed: {finalize_errors}"
      )
    overwrite_errors, _ = finalizer.finalize(
        prepare_args.output, approval_pairs, allowed_signers_path,
        REPOSITORY_ROOT, finalized_path,
        require_clean_repository=False,
        require_tracked_allowed_signers=False,
    )
    if not any("already exists" in error for error in overwrite_errors):
      raise AssertionError(f"final promotion overwrite was accepted: {overwrite_errors}")

    cases: list[tuple[str, Callable[[dict[str, Any]], None], str]] = [
        ("wrong-candidate", lambda value: value.__setitem__("candidate_rendering_model", "other"), "candidate rendering model"),
        ("wrong-revision", lambda value: value.__setitem__("source_revision", "0" * 40), "does not match the checked-out repository"),
        ("missing-legacy", lambda value: value["legacy_rendering_models"].pop(), "legacy rendering models"),
        ("stale-evidence-hash", lambda value: value.__setitem__("evidence_set_sha256", "0" * 64), "does not bind"),
        ("one-migration", lambda value: value["evidence"]["rendering_migrations"].pop(), "exactly Darwin and Windows"),
        ("early-review", lambda value: value["final_reviewers"][0].__setitem__("reviewed_on", "2026-07-15"), "before the complete evidence"),
        ("partial-review", lambda value: value["final_reviewers"][0]["categories"].pop(), "every final review category"),
        ("signed-note-tamper", lambda value: value["final_reviewers"][0].__setitem__("notes", "Changed after signing"), "detached SSH signature"),
        ("preparer-as-approver", lambda value: value["final_reviewers"][0].__setitem__("reviewer_id", "physical-reviewer-1"), "independent of evidence preparation"),
        ("self-promotion", lambda value: value.__setitem__("promotion_eligible", False), "promotion-eligible"),
        ("unknown-field", lambda value: value.__setitem__("checkbox", True), "unknown field"),
    ]
    for name, mutate, expected in cases:
      value = copy.deepcopy(manifest)
      mutate(value)
      errors = validate(value)
      if not any(expected in error for error in errors):
        raise AssertionError(f"{name} mutation was accepted: {errors}")

    substituted = root / manifest["evidence"]["windows_raw"]["executable_path"]
    original = substituted.read_bytes()
    substituted.write_bytes(b"substituted")
    errors = validate(manifest)
    if not any("does not match the referenced file" in error for error in errors):
      raise AssertionError(f"nested binary substitution was accepted: {errors}")
    substituted.write_bytes(original)

  print("Color default-promotion adversarial tests passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
