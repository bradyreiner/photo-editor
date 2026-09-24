#!/usr/bin/env python3
"""Regression tests for refresh-chunk evidence authorization."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("validate-rendering-migration-refresh-chunk.py")
AUTHORIZATION_SCRIPT = Path(__file__).with_name(
    "authorize-rendering-migration-refresh-chunk.py"
)


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(value: object) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def write_plan(path: Path, value: dict[str, object]) -> None:
    payload = dict(value)
    payload.pop("planSha256", None)
    payload["planSha256"] = sha(canonical(payload))
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")


def write_report(path: Path, value: dict[str, object]) -> None:
    payload = dict(value)
    payload.pop("reportSha256", None)
    payload["reportSha256"] = sha(canonical(payload))
    path.write_bytes(canonical(payload))


def run(*args: str, expect: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, "-B", str(SCRIPT), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != expect:
        raise AssertionError(
            f"expected {expect}, got {result.returncode}\n{result.stdout}\n{result.stderr}"
        )
    return result


def run_authorize(*args: str, expect: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, "-B", str(AUTHORIZATION_SCRIPT), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != expect:
        raise AssertionError(
            f"expected {expect}, got {result.returncode}\n{result.stdout}\n{result.stderr}"
        )
    return result


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        executable = root / "Looksy"
        executable.write_bytes(b"release")
        producer = {
            "contract": "photo-editor-rendering-migration-batch-producer:v1",
            "executableSha256": sha(executable.read_bytes()),
            "executableName": "Looksy",
            "buildMode": "release",
        }
        asset_id = "asset-0123456789abcdef"
        plan = root / "plan.json"
        plan_value = {
            "contract": "photo-editor-rendering-migration-refresh-plan:v2",
            "producer": producer,
            "qualityGate": {
                "contract": "photo-editor-rendering-migration-pair-diagnosis:v3",
                "diagnosisSha256": "1" * 64,
                "diagnosisFileSha256": "2" * 64,
            },
            "humanApprovalGate": {
                "status": "unanimously-approved",
                "contract": "photo-editor-rendering-migration-review-packet:v1",
                "reviewUiContract": "photo-editor-rendering-migration-review-ui:v2",
                "ratingsContract": "photo-editor-rendering-migration-review-ratings:v1",
                "reviewer": "Independent photographer",
                "reviewedAt": "2026-09-02T18:30:00-06:00",
                "assetCount": 1,
                "packetDigestSha256": "3" * 64,
                "manifestFileSha256": "4" * 64,
                "ratingsFileSha256": "5" * 64,
                "validatorScriptSha256": "6" * 64,
            },
            "budgets": {
                "maximumAssetsPerChunk": 1,
                "maximumMegapixelsPerChunk": 15.0,
                "oversizedAssetPolicy": "single-asset-explicit-overage",
            },
            "summary": {"chunkCount": 1, "pendingAvailableCount": 1},
            "alreadyRefreshedAssetIds": ["asset-calibration0001"],
            "missingOriginalAssetIds": [],
            "chunks": [{
                "chunkIndex": 1,
                "assetCount": 1,
                "totalPixels": 20_000_000,
                "totalMegapixels": 20.0,
                "overBudgetSingleAsset": True,
                "assets": [{
                    "assetId": asset_id,
                    "width": 5000,
                    "height": 4000,
                    "pixels": 20_000_000,
                    "megapixels": 20.0,
                }],
            }],
        }
        write_plan(plan, plan_value)
        authorization = root / "authorization.json"
        authorization_args = (
            "--plan", str(plan),
            "--chunk-index", "1",
            "--executable", str(executable),
            "--output", str(authorization),
        )
        run_authorize(*authorization_args)
        authorized = json.loads(authorization.read_text())
        if (
            authorized["contract"]
            != "photo-editor-rendering-migration-refresh-chunk-authorization:v1"
            or authorized["assetIds"] != [asset_id]
            or authorized["totalPixels"] != 20_000_000
            or authorization.stat().st_mode & 0o777 != 0o600
        ):
            raise AssertionError("native chunk authorization is incomplete")
        run_authorize(*authorization_args, expect=1)
        report = root / "component.json"
        artifacts = Path(str(report) + ".assets")
        artifacts.mkdir()
        artifact_payloads = {
            "candidate-export": b"candidate",
            "reference-export": b"reference",
            "delta-e00-heatmap": b"heatmap",
        }
        names = []
        for role, payload in artifact_payloads.items():
            name = f"{role}-sha256-{sha(payload)}.png"
            (artifacts / name).write_bytes(payload)
            names.append(name)
        report_value = {
            "schema": "photo-editor-rendering-migration-batch-report:v2",
            "producer": producer,
            "summary": {
                "assetCount": 1,
                "editedAssetCount": 1,
                "candidateCompiledCount": 1,
                "evidenceReadyCount": 1,
                "allEditedAssetsCandidateCompiled": True,
                "allEditedAssetsHaveEvidence": True,
            },
            "assets": [{
                "assetId": asset_id,
                "evidenceValid": True,
                "blockers": [],
                "evidenceArtifactFileNames": names,
            }],
        }
        write_report(report, report_value)
        receipt = root / "receipt.json"
        common = (
            "--plan", str(plan),
            "--chunk-index", "1",
            "--executable", str(executable),
            "--component-report", str(report),
            "--output", str(receipt),
        )
        run(*common)
        output = json.loads(receipt.read_text())
        if (
            output["contract"]
            != "photo-editor-rendering-migration-refresh-chunk-receipt:v1"
            or output["status"] != "validated-evidence"
            or output["assetIds"] != [asset_id]
            or output["artifactCount"] != 3
            or receipt.stat().st_mode & 0o777 != 0o600
        ):
            raise AssertionError("valid chunk receipt is incomplete")
        run(*common, expect=1)

        receipt.unlink()
        changed = dict(plan_value)
        changed["contract"] = "photo-editor-rendering-migration-refresh-plan:v1"
        write_plan(plan, changed)
        run(*common, expect=1)
        authorization.unlink()
        run_authorize(*authorization_args, expect=1)
        write_plan(plan, plan_value)

        report_payload = json.loads(report.read_text())
        report_payload["assets"][0]["assetId"] = "asset-wrong"
        write_report(report, report_payload)
        run(*common, expect=1)
        report_payload["assets"][0]["assetId"] = asset_id
        write_report(report, report_payload)

        first_artifact = artifacts / names[0]
        first_artifact.write_bytes(b"tampered")
        run(*common, expect=1)

    print("validate_rendering_migration_refresh_chunk_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
