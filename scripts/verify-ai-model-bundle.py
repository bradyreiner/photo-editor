#!/usr/bin/env python3
"""Verify that a packaged local image-model bundle is complete and pinned."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path
from typing import Any


SCHEMA = "framehouse-ai-model-bundle-v1"
ALLOWED_ROLES = {"inpaint", "selection"}
REQUIRED_INPAINT_PATH = "inpaint/lama.onnx"
REQUIRED_INPAINT_CONTRACT = "opencv-lama-image-mask-nchw-float32-512-v1"


class BundleError(ValueError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def required_string(value: Any, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise BundleError(f"{label} must be a non-empty string")
    return value.strip()


def verified_relative_path(root: Path, value: Any, label: str) -> tuple[str, Path]:
    relative = required_string(value, label)
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts:
        raise BundleError(f"{label} must stay inside the model bundle")
    resolved = (root / path).resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise BundleError(f"{label} escapes the model bundle") from error
    return path.as_posix(), resolved


def validate_bundle(root: Path, require_inpaint: bool = False) -> dict[str, Any]:
    root = root.expanduser().resolve()
    manifest_path = root / "manifest.json"
    if not manifest_path.is_file():
        raise BundleError(f"model manifest is missing: {manifest_path}")
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise BundleError(f"unable to read model manifest: {error}") from error
    if not isinstance(manifest, dict):
        raise BundleError("model manifest root must be an object")
    if manifest.get("schema") != SCHEMA:
        raise BundleError(f"model manifest schema must be {SCHEMA}")
    if manifest.get("version") != 1:
        raise BundleError("model manifest version must be 1")
    artifacts = manifest.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise BundleError("model manifest artifacts must be a non-empty array")

    seen_ids: set[str] = set()
    seen_paths: set[str] = set()
    roles: set[str] = set()
    verified: list[dict[str, Any]] = []
    for index, artifact in enumerate(artifacts):
        label = f"artifact {index}"
        if not isinstance(artifact, dict):
            raise BundleError(f"{label} must be an object")
        artifact_id = required_string(artifact.get("id"), f"{label} id")
        if artifact_id in seen_ids:
            raise BundleError(f"duplicate model artifact id: {artifact_id}")
        seen_ids.add(artifact_id)
        role = required_string(artifact.get("role"), f"{label} role")
        if role not in ALLOWED_ROLES:
            raise BundleError(f"{label} role must be one of {sorted(ALLOWED_ROLES)}")
        roles.add(role)
        contract = required_string(artifact.get("contract"), f"{label} contract")
        if role == "inpaint" and contract != REQUIRED_INPAINT_CONTRACT:
            raise BundleError(
                f"{label} contract must be {REQUIRED_INPAINT_CONTRACT} for inpaint"
            )
        relative, artifact_path = verified_relative_path(
            root, artifact.get("path"), f"{label} path"
        )
        if relative in seen_paths:
            raise BundleError(f"duplicate model artifact path: {relative}")
        seen_paths.add(relative)
        if not artifact_path.is_file():
            raise BundleError(f"model artifact is missing: {artifact_path}")
        expected_bytes = artifact.get("bytes")
        if not isinstance(expected_bytes, int) or isinstance(expected_bytes, bool) or expected_bytes <= 0:
            raise BundleError(f"{label} bytes must be a positive integer")
        actual_bytes = artifact_path.stat().st_size
        if actual_bytes != expected_bytes:
            raise BundleError(
                f"{label} byte size mismatch: expected {expected_bytes}, got {actual_bytes}"
            )
        expected_sha256 = required_string(artifact.get("sha256"), f"{label} sha256").lower()
        if len(expected_sha256) != 64 or any(
            character not in "0123456789abcdef" for character in expected_sha256
        ):
            raise BundleError(f"{label} sha256 must be 64 lowercase hexadecimal characters")
        actual_sha256 = sha256_file(artifact_path)
        if actual_sha256 != expected_sha256:
            raise BundleError(
                f"{label} SHA-256 mismatch: expected {expected_sha256}, got {actual_sha256}"
            )
        license_info = artifact.get("license")
        if not isinstance(license_info, dict):
            raise BundleError(f"{label} license must be an object")
        license_name = required_string(license_info.get("name"), f"{label} license name")
        license_url = required_string(license_info.get("url"), f"{label} license url")
        source_url = required_string(artifact.get("sourceUrl"), f"{label} sourceUrl")
        verified.append(
            {
                "id": artifact_id,
                "role": role,
                "contract": contract,
                "path": relative,
                "bytes": actual_bytes,
                "sha256": actual_sha256,
                "license": {"name": license_name, "url": license_url},
                "sourceUrl": source_url,
            }
        )

    if require_inpaint:
        matching = [
            artifact
            for artifact in verified
            if artifact["role"] == "inpaint" and artifact["path"] == REQUIRED_INPAINT_PATH
        ]
        if len(matching) != 1:
            raise BundleError(
                f"the bundle must contain exactly one inpaint artifact at {REQUIRED_INPAINT_PATH}"
            )
    return {
        "ok": True,
        "schema": SCHEMA,
        "root": str(root),
        "artifactCount": len(verified),
        "roles": sorted(roles),
        "artifacts": verified,
    }


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="framehouse-ai-model-bundle-") as temporary:
        root = Path(temporary)
        model_path = root / REQUIRED_INPAINT_PATH
        model_path.parent.mkdir(parents=True)
        model_bytes = b"deterministic-lama-fixture"
        model_path.write_bytes(model_bytes)
        manifest = {
            "schema": SCHEMA,
            "version": 1,
            "artifacts": [
                {
                    "id": "lama-inpaint",
                    "role": "inpaint",
                    "contract": REQUIRED_INPAINT_CONTRACT,
                    "path": REQUIRED_INPAINT_PATH,
                    "bytes": len(model_bytes),
                    "sha256": hashlib.sha256(model_bytes).hexdigest(),
                    "sourceUrl": "https://example.invalid/lama.onnx",
                    "license": {
                        "name": "fixture-license",
                        "url": "https://example.invalid/license",
                    },
                }
            ],
        }
        manifest_path = root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        report = validate_bundle(root, require_inpaint=True)
        if report["artifactCount"] != 1 or report["roles"] != ["inpaint"]:
            raise BundleError("valid self-test bundle did not produce the expected report")
        model_path.write_bytes(b"corrupted")
        try:
            validate_bundle(root, require_inpaint=True)
        except BundleError as error:
            if "byte size mismatch" not in str(error) and "SHA-256 mismatch" not in str(error):
                raise
        else:
            raise BundleError("corrupted self-test bundle was accepted")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", nargs="?", type=Path)
    parser.add_argument("--require-inpaint", action="store_true")
    parser.add_argument("--json", action="store_true", dest="json_output")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.self_test:
            run_self_test()
            print("AI model bundle verifier self-test passed")
            return 0
        if args.bundle is None:
            raise BundleError("bundle path is required")
        report = validate_bundle(args.bundle, require_inpaint=args.require_inpaint)
    except BundleError as error:
        print(f"AI model bundle invalid: {error}", file=sys.stderr)
        return 1
    if args.json_output:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            "AI model bundle verified: "
            f"{report['artifactCount']} artifact(s), roles={','.join(report['roles'])}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
