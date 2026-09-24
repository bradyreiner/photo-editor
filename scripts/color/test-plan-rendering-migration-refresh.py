#!/usr/bin/env python3
"""Regression tests for the bounded rendering migration refresh planner."""

from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("plan-rendering-migration-refresh.py")
REVIEW_SCRIPT = Path(__file__).with_name("rendering-migration-review-packet.py")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write(path: Path, value: object) -> None:
    path.write_text(json.dumps(value), encoding="utf-8")


def value_sha(value: object) -> str:
    return sha(json.dumps(value, sort_keys=True, separators=(",", ":")).encode())


def write_sealed_report(path: Path, value: dict[str, object]) -> str:
    payload = dict(value)
    payload.pop("reportSha256", None)
    report_sha = value_sha(payload)
    payload["reportSha256"] = report_sha
    path.write_text(
        json.dumps(payload, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )
    return report_sha


def run(*args: str, expect: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, "-B", str(SCRIPT), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != expect:
        raise AssertionError(f"expected {expect}, got {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


def asset(asset_id: str, *, valid: bool = False, missing: bool = False) -> dict[str, object]:
    return {
        "assetId": asset_id,
        "evidenceValid": valid,
        "blockers": ["missing-original"] if missing else ([] if valid else ["candidate-not-compiled"]),
    }


def dimension_asset(asset_id: str, width: int, height: int) -> dict[str, object]:
    return {
        "assetId": asset_id,
        "shippingFormatEvidence": {
            "pq-avif-10": {
                "candidateInspection": {"width": width, "height": height}
            }
        },
    }


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        executable = root / "Looksy"
        executable.write_bytes(b"binary")
        producer = {
            "contract": "photo-editor-rendering-migration-batch-producer:v1",
            "executableSha256": sha(executable.read_bytes()),
            "executableName": "Looksy",
            "buildMode": "release",
        }
        progress = root / "progress.json"
        dimensions = root / "dimensions.json"
        diagnosis = root / "diagnosis.json"
        output = root / "plan.json"
        progress_sha = write_sealed_report(progress, {
            "schema": "photo-editor-rendering-migration-batch-report:v2",
            "producer": producer,
            "assets": [
                asset("asset-0000000000000001", valid=True),
                asset("asset-0000000000000002"),
                asset("asset-0000000000000003"),
                asset("asset-0000000000000004"),
                asset("asset-0000000000000005", missing=True),
            ],
        })
        dimension_sha = write_sealed_report(dimensions, {
            "schema": "photo-editor-rendering-migration-batch-report:v1",
            "assets": [
                dimension_asset("asset-0000000000000002", 1000, 1000),
                dimension_asset("asset-0000000000000003", 2000, 2000),
                dimension_asset("asset-0000000000000004", 4000, 4000),
            ],
        })
        diagnosis_payload = {
            "contract": "photo-editor-rendering-migration-pair-diagnosis:v3",
            "sourceReportSha256": progress_sha,
            "sourceReportFileSha256": sha(progress.read_bytes()),
            "producer": producer,
            "summary": {
                "assetCount": 1,
                "universalExposureConsistentCount": 1,
            },
            "assets": [{
                "assetId": "asset-0000000000000001",
                "metrics": {"relativeLinearExposureRangeStops": 0.12},
            }],
        }
        diagnosis_payload["diagnosisSha256"] = value_sha(diagnosis_payload)
        write(diagnosis, diagnosis_payload)
        review_spec = importlib.util.spec_from_file_location(
            "planner_test_review_packet", REVIEW_SCRIPT
        )
        if review_spec is None or review_spec.loader is None:
            raise AssertionError("review packet module should load")
        review_tool = importlib.util.module_from_spec(review_spec)
        review_spec.loader.exec_module(review_tool)
        packet = root / "review-packet"
        artifact_root = packet / "assets" / "asset-0000000000000001"
        artifact_root.mkdir(parents=True)
        artifacts: dict[str, dict[str, str]] = {}
        for role in sorted(review_tool.REVIEW_ARTIFACT_ROLES):
            payload = role.encode("utf-8")
            relative = Path("assets") / "asset-0000000000000001" / f"{role}.png"
            (packet / relative).write_bytes(payload)
            artifacts[role] = {"path": relative.as_posix(), "sha256": sha(payload)}
        manifest = {
            "contract": review_tool.PACKET_CONTRACT,
            "sourceReportSha256": progress_sha,
            "sourceReportFileSha256": sha(progress.read_bytes()),
            "producer": producer,
            "reviewUiContract": review_tool.REVIEW_UI_CONTRACT,
            "assetCount": 1,
            "assets": [{
                "assetId": "asset-0000000000000001",
                "sourceFileName": "example.dng",
                "evidenceSha256": "c" * 64,
                "risks": [],
                "metrics": {
                    "p95DeltaE00": 1.0,
                    "maximumDeltaE00": 2.0,
                    "addedShadowClipCount": 0,
                    "addedHighlightClipCount": 0,
                },
                "artifacts": artifacts,
            }],
        }
        manifest["packetDigestSha256"] = review_tool.value_sha256(manifest)
        write(packet / "manifest.json", manifest)
        (packet / "index.html").write_text(
            review_tool.packet_html(manifest), encoding="utf-8"
        )
        approved_ratings = root / "approved-ratings.json"
        ratings = {
            "contract": review_tool.RATINGS_CONTRACT,
            "packetDigestSha256": manifest["packetDigestSha256"],
            "sourceReportFileSha256": manifest["sourceReportFileSha256"],
            "reviewer": "Independent photographer",
            "reviewedAt": "2026-09-02T18:30:00-06:00",
            "independentReview": True,
            "cases": {
                "asset-0000000000000001": {
                    "decision": "approve",
                    "notes": "Color and tonal detail remain natural.",
                    "scores": {field: 5 for field in review_tool.SCORE_FIELDS},
                }
            },
        }
        write(approved_ratings, ratings)
        common = (
            "--progress-report", str(progress),
            "--dimensions-report", str(dimensions),
            "--executable", str(executable),
            "--quality-diagnosis", str(diagnosis),
            "--review-packet", str(packet),
            "--approved-ratings", str(approved_ratings),
            "--output", str(output),
            "--max-assets", "2",
            "--max-megapixels", "6",
        )
        common_without_review = tuple(
            value
            for index, value in enumerate(common)
            if index not in {
                common.index("--review-packet"),
                common.index("--review-packet") + 1,
                common.index("--approved-ratings"),
                common.index("--approved-ratings") + 1,
            }
        )
        run(*common_without_review, expect=1)
        run(*common)
        plan = json.loads(output.read_text(encoding="utf-8"))
        if (
            plan["contract"] != "photo-editor-rendering-migration-refresh-plan:v2"
            or plan["dimensionReportSha256"] != dimension_sha
            or plan["summary"] != {
                "catalogAssetCount": 5,
                "alreadyRefreshedCount": 1,
                "pendingAvailableCount": 3,
                "missingOriginalCount": 1,
                "chunkCount": 2,
            }
            or [chunk["assetCount"] for chunk in plan["chunks"]] != [2, 1]
            or plan["chunks"][1]["overBudgetSingleAsset"] is not True
            or plan["humanApprovalGate"]["status"] != "unanimously-approved"
            or plan["humanApprovalGate"]["ratingsFileSha256"] != sha(
                approved_ratings.read_bytes()
            )
            or output.stat().st_mode & 0o777 != 0o600
        ):
            raise AssertionError(
                "planner did not enforce budgets, approval, or private publication"
            )
        run(*common, expect=1)
        output.unlink()
        ratings["cases"]["asset-0000000000000001"]["decision"] = "reject"
        write(approved_ratings, ratings)
        run(*common, expect=1)
        ratings["cases"]["asset-0000000000000001"]["decision"] = "approve"
        write(approved_ratings, ratings)
        diagnosis_payload = json.loads(diagnosis.read_text(encoding="utf-8"))
        diagnosis_payload["assets"][0]["metrics"][
            "relativeLinearExposureRangeStops"
        ] = 0.251
        diagnosis_payload.pop("diagnosisSha256")
        diagnosis_payload["diagnosisSha256"] = value_sha(diagnosis_payload)
        write(diagnosis, diagnosis_payload)
        run(*common, expect=1)
        diagnosis_payload["assets"][0]["metrics"][
            "relativeLinearExposureRangeStops"
        ] = 0.12
        diagnosis_payload.pop("diagnosisSha256")
        diagnosis_payload["diagnosisSha256"] = value_sha(diagnosis_payload)
        write(diagnosis, diagnosis_payload)
        executable.write_bytes(b"different")
        run(*common, expect=1)
        executable.write_bytes(b"binary")
        dimension_payload = json.loads(dimensions.read_text(encoding="utf-8"))
        dimension_payload["assets"] = dimension_payload["assets"][:-1]
        write(dimensions, dimension_payload)
        run(*common, expect=1)
    print("plan_rendering_migration_refresh_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
