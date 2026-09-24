#!/usr/bin/env python3
"""Bind one rendered migration component to an approved refresh-plan chunk."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path
from typing import Any

from rendering_migration_report_identity import (
    file_sha256,
    validate_report_identity,
)


PLAN_CONTRACT = "photo-editor-rendering-migration-refresh-plan:v2"
RECEIPT_CONTRACT = "photo-editor-rendering-migration-refresh-chunk-receipt:v1"
DIAGNOSIS_CONTRACT = "photo-editor-rendering-migration-pair-diagnosis:v3"
PACKET_CONTRACT = "photo-editor-rendering-migration-review-packet:v1"
REVIEW_UI_CONTRACT = "photo-editor-rendering-migration-review-ui:v2"
RATINGS_CONTRACT = "photo-editor-rendering-migration-review-ratings:v1"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
ARTIFACT_PATTERN = re.compile(r"^[A-Za-z0-9-]+-sha256-([0-9a-f]{64})\.[A-Za-z0-9]+$")


def fail(message: str) -> int:
    print(f"error={message}", file=sys.stderr)
    return 1


def load_object(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def value_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def valid_sha256(value: Any) -> bool:
    return isinstance(value, str) and SHA256_PATTERN.fullmatch(value) is not None


def publish_new_file(path: Path, payload: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as handle:
            handle.write(payload)
            handle.flush()
            os.fsync(handle.fileno())
    except BaseException:
        path.unlink(missing_ok=True)
        raise


def validate_plan(plan_path: Path, executable: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    plan = load_object(plan_path)
    claimed = plan.get("planSha256")
    unsigned = dict(plan)
    unsigned.pop("planSha256", None)
    if (
        plan.get("contract") != PLAN_CONTRACT
        or not valid_sha256(claimed)
        or value_sha256(unsigned) != claimed
    ):
        raise ValueError("refresh plan identity is invalid")
    producer = plan.get("producer")
    if (
        not isinstance(producer, dict)
        or producer.get("executableSha256") != file_sha256(executable)
    ):
        raise ValueError("refresh plan does not belong to the executable")
    quality = plan.get("qualityGate")
    if (
        not isinstance(quality, dict)
        or quality.get("contract") != DIAGNOSIS_CONTRACT
        or not valid_sha256(quality.get("diagnosisSha256"))
        or not valid_sha256(quality.get("diagnosisFileSha256"))
    ):
        raise ValueError("refresh plan quality gate is invalid")
    approval = plan.get("humanApprovalGate")
    refreshed = plan.get("alreadyRefreshedAssetIds")
    if (
        not isinstance(approval, dict)
        or approval.get("status") != "unanimously-approved"
        or approval.get("contract") != PACKET_CONTRACT
        or approval.get("reviewUiContract") != REVIEW_UI_CONTRACT
        or approval.get("ratingsContract") != RATINGS_CONTRACT
        or not isinstance(approval.get("reviewer"), str)
        or not approval["reviewer"].strip()
        or not isinstance(approval.get("reviewedAt"), str)
        or not approval["reviewedAt"].strip()
        or not isinstance(refreshed, list)
        or any(not isinstance(asset_id, str) or not asset_id for asset_id in refreshed)
        or len(set(refreshed)) != len(refreshed)
        or approval.get("assetCount") != len(refreshed)
    ):
        raise ValueError("refresh plan lacks unanimous independent approval")
    for key in (
        "packetDigestSha256",
        "manifestFileSha256",
        "ratingsFileSha256",
        "validatorScriptSha256",
    ):
        if not valid_sha256(approval.get(key)):
            raise ValueError(f"refresh plan approval {key} is invalid")
    chunks = plan.get("chunks")
    summary = plan.get("summary")
    budgets = plan.get("budgets")
    if not isinstance(chunks, list) or not isinstance(summary, dict) or not isinstance(budgets, dict):
        raise ValueError("refresh plan structure is incomplete")
    maximum_assets = budgets.get("maximumAssetsPerChunk")
    maximum_megapixels = budgets.get("maximumMegapixelsPerChunk")
    if (
        not isinstance(maximum_assets, int)
        or isinstance(maximum_assets, bool)
        or maximum_assets <= 0
        or not isinstance(maximum_megapixels, (int, float))
        or isinstance(maximum_megapixels, bool)
        or maximum_megapixels <= 0
        or budgets.get("oversizedAssetPolicy") != "single-asset-explicit-overage"
    ):
        raise ValueError("refresh plan budgets are invalid")
    planned_ids: list[str] = []
    for expected_index, chunk in enumerate(chunks, start=1):
        if not isinstance(chunk, dict) or chunk.get("chunkIndex") != expected_index:
            raise ValueError("refresh plan chunk order is invalid")
        assets = chunk.get("assets")
        if not isinstance(assets, list) or not assets or chunk.get("assetCount") != len(assets):
            raise ValueError(f"refresh plan chunk {expected_index} asset count is invalid")
        if len(assets) > maximum_assets and chunk.get("overBudgetSingleAsset") is not True:
            raise ValueError(f"refresh plan chunk {expected_index} exceeds its asset budget")
        pixels = 0
        for asset in assets:
            if not isinstance(asset, dict):
                raise ValueError(f"refresh plan chunk {expected_index} contains an invalid asset")
            asset_id = asset.get("assetId")
            width = asset.get("width")
            height = asset.get("height")
            asset_pixels = asset.get("pixels")
            if (
                not isinstance(asset_id, str)
                or not asset_id
                or asset_id in planned_ids
                or not isinstance(width, int)
                or isinstance(width, bool)
                or width <= 0
                or not isinstance(height, int)
                or isinstance(height, bool)
                or height <= 0
                or not isinstance(asset_pixels, int)
                or isinstance(asset_pixels, bool)
                or asset_pixels <= 0
                or asset_pixels != width * height
                or asset.get("megapixels") != round(asset_pixels / 1_000_000.0, 6)
            ):
                raise ValueError("refresh plan contains an invalid or duplicate asset")
            planned_ids.append(asset_id)
            pixels += asset_pixels
        if chunk.get("totalPixels") != pixels:
            raise ValueError(f"refresh plan chunk {expected_index} pixel total is invalid")
        if chunk.get("totalMegapixels") != round(pixels / 1_000_000.0, 6):
            raise ValueError(f"refresh plan chunk {expected_index} megapixel total is invalid")
        over_budget = pixels > int(float(maximum_megapixels) * 1_000_000)
        if chunk.get("overBudgetSingleAsset") is not (over_budget and len(assets) == 1):
            raise ValueError(f"refresh plan chunk {expected_index} overage classification is invalid")
    missing = plan.get("missingOriginalAssetIds")
    if (
        summary.get("chunkCount") != len(chunks)
        or summary.get("pendingAvailableCount") != len(planned_ids)
        or not isinstance(missing, list)
        or any(not isinstance(asset_id, str) or not asset_id for asset_id in missing)
        or len(set(missing)) != len(missing)
        or set(planned_ids) & set(refreshed)
        or set(planned_ids) & set(missing)
        or set(refreshed) & set(missing)
    ):
        raise ValueError("refresh plan summary or asset partition is invalid")
    return plan, producer


def validate_artifacts(report_path: Path, assets: list[dict[str, Any]]) -> None:
    root = Path(str(report_path) + ".assets")
    referenced: set[str] = set()
    for asset in assets:
        names = asset.get("evidenceArtifactFileNames")
        if not isinstance(names, list) or not names:
            raise ValueError(f"{asset.get('assetId')}: evidence artifacts are missing")
        for name in names:
            match = ARTIFACT_PATTERN.fullmatch(name) if isinstance(name, str) else None
            if match is None or name in referenced:
                raise ValueError("component report contains an invalid or duplicate artifact")
            path = root / name
            if not path.is_file() or file_sha256(path) != match.group(1):
                raise ValueError(f"component artifact identity is invalid: {name}")
            referenced.add(name)
    actual = {path.name for path in root.iterdir()} if root.is_dir() else set()
    if actual != referenced:
        raise ValueError("component artifact directory contains missing or unexpected files")


def build_receipt(
    plan_path: Path,
    chunk_index: int,
    executable: Path,
    report_path: Path,
    output: Path,
) -> int:
    plan_path = plan_path.resolve()
    executable = executable.resolve()
    report_path = report_path.resolve()
    output = output.resolve()
    if output.exists():
        return fail(f"output already exists: {output}")
    if chunk_index <= 0:
        return fail("chunk index must be positive")
    plan, producer = validate_plan(plan_path, executable)
    chunks = plan["chunks"]
    if chunk_index > len(chunks):
        return fail("chunk index is absent from the refresh plan")
    chunk = chunks[chunk_index - 1]
    planned_ids = [asset["assetId"] for asset in chunk["assets"]]
    report, report_producer = validate_report_identity(report_path, executable)
    if report_producer != producer:
        return fail("component report producer differs from the refresh plan")
    assets = report.get("assets")
    if not isinstance(assets, list) or any(not isinstance(asset, dict) for asset in assets):
        return fail("component report assets are invalid")
    reported_ids = [asset.get("assetId") for asset in assets]
    if (
        len(reported_ids) != len(set(reported_ids))
        or set(reported_ids) != set(planned_ids)
    ):
        return fail("component report does not contain exactly the planned chunk assets")
    if any(asset.get("evidenceValid") is not True or asset.get("blockers") for asset in assets):
        return fail("component report contains incomplete or blocked evidence")
    summary = report.get("summary")
    count = len(assets)
    if (
        not isinstance(summary, dict)
        or summary.get("assetCount") != count
        or summary.get("editedAssetCount") != count
        or summary.get("candidateCompiledCount") != count
        or summary.get("evidenceReadyCount") != count
        or summary.get("allEditedAssetsCandidateCompiled") is not True
        or summary.get("allEditedAssetsHaveEvidence") is not True
    ):
        return fail("component report summary is not evidence-complete")
    validate_artifacts(report_path, assets)
    receipt: dict[str, Any] = {
        "contract": RECEIPT_CONTRACT,
        "status": "validated-evidence",
        "producer": producer,
        "planSha256": plan["planSha256"],
        "planFileSha256": file_sha256(plan_path),
        "chunkIndex": chunk_index,
        "assetIds": planned_ids,
        "componentReportSha256": report["reportSha256"],
        "componentReportFileSha256": file_sha256(report_path),
        "artifactCount": sum(len(asset["evidenceArtifactFileNames"]) for asset in assets),
    }
    receipt["receiptSha256"] = value_sha256(receipt)
    publish_new_file(
        output,
        (json.dumps(receipt, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(f"receipt={output}")
    print(f"chunk_index={chunk_index}")
    print(f"assets={len(planned_ids)}")
    print(f"receipt_sha256={receipt['receiptSha256']}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--chunk-index", type=int, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--component-report", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return build_receipt(
            args.plan,
            args.chunk_index,
            args.executable,
            args.component_report,
            args.output,
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
