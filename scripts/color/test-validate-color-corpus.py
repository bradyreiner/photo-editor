#!/usr/bin/env python3
"""Self-test the licensed color-corpus validator and promotion policy."""

from __future__ import annotations

import copy
import hashlib
import importlib.util
import json
import sys
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = REPO_ROOT / "scripts/color/validate-color-corpus.py"
EXAMPLE_PATH = (
    REPO_ROOT / "perf-corpora/color-validation-required/manifest.example.json"
)


def load_validator() -> Any:
    spec = importlib.util.spec_from_file_location("color_corpus_validator", VALIDATOR_PATH)
    if spec is None or spec.loader is None:
        raise SystemExit(f"unable to import {VALIDATOR_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules["color_corpus_validator"] = module
    spec.loader.exec_module(module)
    return module


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def materialize_fixture(root: Path, validator: Any) -> tuple[Path, dict[str, Any]]:
    payload = json.loads(EXAMPLE_PATH.read_text(encoding="utf-8"))
    for asset in payload["assets"]:
        path = root / asset["path"]
        path.parent.mkdir(parents=True, exist_ok=True)
        content = f"immutable-test-asset:{asset['asset_id']}\n".encode("utf-8")
        path.write_bytes(content)
        asset["byte_size"] = len(content)
        asset["sha256"] = hashlib.sha256(content).hexdigest()

    artifact_path = root / "reviews/candidate-comparison-manifest.json"
    artifact_payload = {
        "schema": "photo-editor-blinded-comparison:v1",
        "labels": ["A", "B"],
    }
    write_json(artifact_path, artifact_payload)
    artifact_bytes = artifact_path.read_bytes()
    captures = [
        asset["asset_id"] for asset in payload["assets"] if asset["kind"] == "capture"
    ]
    recipe = next(asset for asset in payload["assets"] if asset["kind"] == "recipe")
    output_profile = next(
        asset
        for asset in payload["assets"]
        if asset["kind"] == "profile" and asset["profile_role"] == "output"
    )
    categories = sorted(validator.REQUIRED_REVIEW_CATEGORIES)
    payload["comparison_sets"] = [
        {
            "comparison_set_id": "candidate-default-ab-v1",
            "candidate_rendering_model": payload["candidate_rendering_model"],
            "baseline_rendering_model": "legacy-oklch-v1",
            "blinded": True,
            "artifact_manifest_path": str(artifact_path.relative_to(root)),
            "artifact_manifest_sha256": hashlib.sha256(artifact_bytes).hexdigest(),
            "artifact_manifest_byte_size": len(artifact_bytes),
            "capture_ids": captures,
            "categories": categories,
            "recipe_sha256": recipe["sha256"],
            "output_profile_sha256": output_profile["sha256"],
        }
    ]
    payload["review_records"] = [
        {
            "review_id": f"candidate-default-review-{index}",
            "reviewer_id": f"editorial-reviewer-{index}",
            "reviewed_on": "2026-07-14",
            "comparison_set_id": "candidate-default-ab-v1",
            "result": "approved",
            "blinded_identity_revealed_after_decision": False,
            "categories": categories,
            "notes": "Fixture approval used only to exercise validator policy.",
        }
        for index in (1, 2)
    ]
    manifest_path = root / "manifest.json"
    write_json(manifest_path, payload)
    return manifest_path, payload


def error_contains(errors: list[str], fragment: str) -> bool:
    return any(fragment in error for error in errors)


def main() -> int:
    validator = load_validator()
    example_errors = validator.validate_manifest(EXAMPLE_PATH, schema_only=True)
    require(not example_errors, f"checked-in schema example was rejected: {example_errors}")
    example_promotion_errors = validator.validate_manifest(
        EXAMPLE_PATH,
        schema_only=True,
        require_promotion_approval=True,
    )
    require(
        error_contains(example_promotion_errors, "at least two distinct reviewers"),
        "schema example must not satisfy the promotion gate",
    )
    with tempfile.TemporaryDirectory(prefix="photo-editor-color-corpus-") as temp:
        root = Path(temp)
        manifest_path, valid_payload = materialize_fixture(root, validator)

        errors = validator.validate_manifest(
            manifest_path, require_promotion_approval=True
        )
        require(not errors, f"valid populated corpus was rejected: {errors}")

        first_asset_path = root / valid_payload["assets"][0]["path"]
        original_bytes = first_asset_path.read_bytes()
        first_asset_path.write_bytes(b"X" + original_bytes[1:])
        errors = validator.validate_manifest(manifest_path)
        require(
            error_contains(errors, ".sha256 mismatch"),
            f"changed asset bytes were not rejected: {errors}",
        )
        schema_errors = validator.validate_manifest(manifest_path, schema_only=True)
        require(
            not schema_errors,
            f"schema-only validation unexpectedly read changed bytes: {schema_errors}",
        )
        first_asset_path.write_bytes(original_bytes)

        duplicate_payload = copy.deepcopy(valid_payload)
        duplicate_payload["assets"][1]["asset_id"] = duplicate_payload["assets"][0]["asset_id"]
        duplicate_path = root / "duplicate.json"
        write_json(duplicate_path, duplicate_payload)
        errors = validator.validate_manifest(duplicate_path, schema_only=True)
        require(
            error_contains(errors, "duplicate asset_id"),
            f"duplicate asset IDs were not rejected: {errors}",
        )

        coverage_payload = copy.deepcopy(valid_payload)
        for asset in coverage_payload["assets"]:
            if asset["kind"] == "capture":
                asset["scene_tags"] = [
                    tag for tag in asset["scene_tags"] if tag != "neon-led"
                ]
        coverage_path = root / "missing-coverage.json"
        write_json(coverage_path, coverage_payload)
        errors = validator.validate_manifest(coverage_path, schema_only=True)
        require(
            error_contains(errors, "missing required scene coverage: neon-led"),
            f"missing scene coverage was not rejected: {errors}",
        )

        typo_payload = copy.deepcopy(valid_payload)
        typo_payload["assets"][0]["sha265"] = typo_payload["assets"][0]["sha256"]
        typo_path = root / "unknown-field.json"
        write_json(typo_path, typo_payload)
        errors = validator.validate_manifest(typo_path, schema_only=True)
        require(
            error_contains(errors, "unknown field 'sha265'"),
            f"unknown fields were not rejected: {errors}",
        )

        count_payload = copy.deepcopy(valid_payload)
        count_payload["asset_count"] += 1
        count_path = root / "wrong-count.json"
        write_json(count_path, count_payload)
        errors = validator.validate_manifest(count_path, schema_only=True)
        require(
            error_contains(errors, "manifest.asset_count mismatch"),
            f"incorrect asset_count was not rejected: {errors}",
        )

        reviewer_payload = copy.deepcopy(valid_payload)
        reviewer_payload["review_records"][1]["reviewer_id"] = reviewer_payload[
            "review_records"
        ][0]["reviewer_id"]
        reviewer_path = root / "same-reviewer.json"
        write_json(reviewer_path, reviewer_payload)
        errors = validator.validate_manifest(
            reviewer_path,
            schema_only=True,
            require_promotion_approval=True,
        )
        require(
            error_contains(errors, "at least two distinct reviewers"),
            f"same-person double approval was not rejected: {errors}",
        )

        incomplete_set_payload = copy.deepcopy(valid_payload)
        incomplete_set_payload["comparison_sets"][0]["capture_ids"].pop()
        incomplete_set_path = root / "incomplete-comparison-set.json"
        write_json(incomplete_set_path, incomplete_set_payload)
        errors = validator.validate_manifest(
            incomplete_set_path,
            schema_only=True,
            require_promotion_approval=True,
        )
        require(
            error_contains(errors, "covering every capture"),
            f"incomplete comparison set was not rejected: {errors}",
        )

        unrelated_profile_payload = copy.deepcopy(valid_payload)
        unrelated_profile_payload["comparison_sets"][0]["output_profile_sha256"] = "f" * 64
        unrelated_profile_path = root / "unrelated-output-profile.json"
        write_json(unrelated_profile_path, unrelated_profile_payload)
        errors = validator.validate_manifest(unrelated_profile_path, schema_only=True)
        require(
            error_contains(errors, "does not identify a profile asset"),
            f"unrelated output profile digest was not rejected: {errors}",
        )

        loose_gate_payload = copy.deepcopy(valid_payload)
        loose_gate_payload["quality_gates"]["preview"]["max_delta_e_00"] = 0.1
        loose_gate_path = root / "invalid-quality-gate.json"
        write_json(loose_gate_path, loose_gate_payload)
        errors = validator.validate_manifest(loose_gate_path, schema_only=True)
        require(
            error_contains(errors, "max_delta_e_00 must be >= p95_delta_e_00"),
            f"internally inconsistent quality gate was not rejected: {errors}",
        )

    print("color_corpus_validator_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
