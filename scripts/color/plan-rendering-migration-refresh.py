#!/usr/bin/env python3
"""Plan small exact-binary refresh chunks for a migration progress report."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import sys
from pathlib import Path
from typing import Any

from rendering_migration_report_identity import validate_report_identity


PLAN_CONTRACT = "photo-editor-rendering-migration-refresh-plan:v2"
DIAGNOSIS_CONTRACT = "photo-editor-rendering-migration-pair-diagnosis:v3"
REVIEW_TOOL_PATH = Path(__file__).resolve().with_name(
    "rendering-migration-review-packet.py"
)
MAXIMUM_TRANSITION_EXPOSURE_RANGE_STOPS = 0.25
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


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


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def publish_new_file(path: Path, payload: bytes) -> None:
    """Publish evidence without ever replacing an existing path."""
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


def string_list(value: Any) -> list[str]:
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        return []
    return list(dict.fromkeys(item for item in value if item))


def dimensions(asset: dict[str, Any]) -> tuple[int, int] | None:
    shipping = asset.get("shippingFormatEvidence")
    if not isinstance(shipping, dict):
        return None
    for key in ("pq-avif-10", "hlg-avif-10"):
        evidence = shipping.get(key)
        if not isinstance(evidence, dict):
            continue
        for inspection_key in ("candidateInspection", "referenceInspection"):
            inspection = evidence.get(inspection_key)
            if not isinstance(inspection, dict):
                continue
            width = inspection.get("width")
            height = inspection.get("height")
            if (
                isinstance(width, int)
                and not isinstance(width, bool)
                and isinstance(height, int)
                and not isinstance(height, bool)
                and width > 0
                and height > 0
            ):
                return width, height
    return None


def validate_quality_diagnosis(
    diagnosis_path: Path,
    progress: dict[str, Any],
    producer: dict[str, Any],
    refreshed_asset_ids: list[str],
) -> dict[str, Any]:
    diagnosis = load_object(diagnosis_path)
    diagnosis_payload = dict(diagnosis)
    diagnosis_digest = diagnosis_payload.pop("diagnosisSha256", None)
    if (
        not isinstance(diagnosis_digest, str)
        or SHA256_PATTERN.fullmatch(diagnosis_digest) is None
        or value_sha256(diagnosis_payload) != diagnosis_digest
    ):
        raise ValueError("quality diagnosis identity is invalid")
    assets = diagnosis.get("assets")
    summary = diagnosis.get("summary")
    if diagnosis.get("contract") != DIAGNOSIS_CONTRACT:
        raise ValueError("quality diagnosis must use the supported-band linear-light v3 contract")
    if diagnosis.get("sourceReportSha256") != progress.get("reportSha256"):
        raise ValueError("quality diagnosis does not belong to the progress report")
    if diagnosis.get("producer") != producer:
        raise ValueError("quality diagnosis producer does not match the progress report")
    if not isinstance(assets, list) or not isinstance(summary, dict):
        raise ValueError("quality diagnosis is incomplete")
    diagnosed_ids = []
    for asset in assets:
        if not isinstance(asset, dict) or not isinstance(asset.get("assetId"), str):
            raise ValueError("quality diagnosis contains an invalid asset")
        metrics = asset.get("metrics")
        exposure_range = (
            metrics.get("relativeLinearExposureRangeStops")
            if isinstance(metrics, dict)
            else None
        )
        if (
            not isinstance(exposure_range, (int, float))
            or isinstance(exposure_range, bool)
            or exposure_range > MAXIMUM_TRANSITION_EXPOSURE_RANGE_STOPS
        ):
            raise ValueError(
                f"{asset['assetId']}: renderer-transition exposure consistency gate failed"
            )
        diagnosed_ids.append(asset["assetId"])
    if (
        len(set(diagnosed_ids)) != len(diagnosed_ids)
        or sorted(diagnosed_ids) != sorted(refreshed_asset_ids)
    ):
        raise ValueError("quality diagnosis does not cover every refreshed asset exactly")
    if (
        summary.get("assetCount") != len(refreshed_asset_ids)
        or summary.get("universalExposureConsistentCount") != len(refreshed_asset_ids)
    ):
        raise ValueError("quality diagnosis summary does not pass the transition gate")
    return diagnosis


def validate_human_approval(
    packet_path: Path,
    ratings_path: Path,
    executable_path: Path,
    progress_path: Path,
    progress: dict[str, Any],
    producer: dict[str, Any],
    refreshed_asset_ids: list[str],
) -> dict[str, Any]:
    specification = importlib.util.spec_from_file_location(
        "framehouse_rendering_migration_review_packet", REVIEW_TOOL_PATH
    )
    if specification is None or specification.loader is None:
        raise ValueError("rendering-migration review validator is unavailable")
    review_tool = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(review_tool)
    if review_tool.validate_packet(
        packet_path,
        executable_path,
        False,
        require_approved=True,
        ratings_path=ratings_path,
        emit_status=False,
    ) != 0:
        raise ValueError("unanimously approved independent review is required")
    manifest_path = packet_path / "manifest.json"
    manifest = load_object(manifest_path)
    ratings = load_object(ratings_path)
    assets = manifest.get("assets")
    if not isinstance(assets, list):
        raise ValueError("review packet assets are missing")
    reviewed_ids = [
        asset.get("assetId") for asset in assets if isinstance(asset, dict)
    ]
    if (
        len(reviewed_ids) != len(assets)
        or len(set(reviewed_ids)) != len(reviewed_ids)
        or sorted(reviewed_ids) != sorted(refreshed_asset_ids)
    ):
        raise ValueError("approved review does not cover every refreshed asset exactly")
    if (
        manifest.get("sourceReportSha256") != progress.get("reportSha256")
        or manifest.get("sourceReportFileSha256") != file_sha256(progress_path)
    ):
        raise ValueError("approved review does not belong to the progress report")
    if manifest.get("producer") != producer:
        raise ValueError("approved review producer does not match the progress report")
    return {
        "contract": manifest.get("contract"),
        "reviewUiContract": manifest.get("reviewUiContract"),
        "packetDigestSha256": manifest.get("packetDigestSha256"),
        "manifestFileSha256": file_sha256(manifest_path),
        "ratingsContract": ratings.get("contract"),
        "ratingsFileSha256": file_sha256(ratings_path),
        "validatorScriptSha256": file_sha256(REVIEW_TOOL_PATH),
        "reviewer": ratings.get("reviewer"),
        "reviewedAt": ratings.get("reviewedAt"),
        "assetCount": len(reviewed_ids),
        "status": "unanimously-approved",
    }


def build_plan(
    progress_path: Path,
    dimensions_path: Path,
    executable_path: Path,
    output_path: Path,
    max_assets: int,
    max_megapixels: float,
    quality_diagnosis_path: Path | None,
    review_packet_path: Path | None,
    approved_ratings_path: Path | None,
) -> int:
    if max_assets <= 0 or max_megapixels <= 0.0:
        return fail("chunk budgets must be positive")
    progress_path = progress_path.resolve()
    dimensions_path = dimensions_path.resolve()
    executable_path = executable_path.resolve()
    output_path = output_path.resolve()
    if output_path.exists():
        return fail(f"output already exists: {output_path}")
    progress, producer = validate_report_identity(progress_path, executable_path)
    dimension_report, _ = validate_report_identity(
        dimensions_path, allow_historical_v1=True
    )
    assets = progress.get("assets")
    dimension_assets = dimension_report.get("assets")
    if not isinstance(assets, list) or not isinstance(dimension_assets, list):
        return fail("both reports must contain asset arrays")
    dimensions_by_id: dict[str, tuple[int, int]] = {}
    for asset in dimension_assets:
        if not isinstance(asset, dict) or not isinstance(asset.get("assetId"), str):
            continue
        size = dimensions(asset)
        if size is not None:
            dimensions_by_id[asset["assetId"]] = size

    pending: list[dict[str, Any]] = []
    skipped_missing: list[str] = []
    refreshed: list[str] = []
    seen: set[str] = set()
    for asset in assets:
        if not isinstance(asset, dict):
            return fail("progress report contains an invalid asset")
        asset_id = asset.get("assetId")
        if not isinstance(asset_id, str) or not asset_id or asset_id in seen:
            return fail("progress report contains an invalid or duplicate asset id")
        seen.add(asset_id)
        if asset.get("evidenceValid") is True:
            refreshed.append(asset_id)
            continue
        blockers = string_list(asset.get("blockers"))
        if "missing-original" in blockers:
            skipped_missing.append(asset_id)
            continue
        size = dimensions_by_id.get(asset_id)
        if size is None:
            return fail(f"{asset_id}: no trusted historical dimensions are available")
        width, height = size
        pending.append({
            "assetId": asset_id,
            "width": width,
            "height": height,
            "pixels": width * height,
            "megapixels": round((width * height) / 1_000_000.0, 6),
        })
    pending.sort(key=lambda item: (item["pixels"], item["assetId"]))

    quality_gate: dict[str, Any] | None = None
    human_approval_gate: dict[str, Any] | None = None
    if refreshed and pending:
        if quality_diagnosis_path is None:
            return fail(
                "a passing linear-light quality diagnosis is required before planning more refresh chunks"
            )
        quality_diagnosis_path = quality_diagnosis_path.resolve()
        quality_gate = validate_quality_diagnosis(
            quality_diagnosis_path, progress, producer, refreshed
        )
        if quality_gate.get("sourceReportFileSha256") != file_sha256(progress_path):
            return fail("quality diagnosis source report file identity is invalid")
        if review_packet_path is None or approved_ratings_path is None:
            return fail(
                "unanimously approved independent review is required before planning more refresh chunks"
            )
        review_packet_path = review_packet_path.resolve()
        approved_ratings_path = approved_ratings_path.resolve()
        human_approval_gate = validate_human_approval(
            review_packet_path,
            approved_ratings_path,
            executable_path,
            progress_path,
            progress,
            producer,
            refreshed,
        )

    pixel_budget = int(max_megapixels * 1_000_000)
    chunks: list[dict[str, Any]] = []
    current: list[dict[str, Any]] = []
    current_pixels = 0
    for asset in pending:
        would_overflow = current and (
            len(current) >= max_assets or current_pixels + asset["pixels"] > pixel_budget
        )
        if would_overflow:
            chunks.append({
                "chunkIndex": len(chunks) + 1,
                "assetCount": len(current),
                "totalPixels": current_pixels,
                "totalMegapixels": round(current_pixels / 1_000_000.0, 6),
                "overBudgetSingleAsset": False,
                "assets": current,
            })
            current = []
            current_pixels = 0
        if asset["pixels"] > pixel_budget:
            if current:
                raise AssertionError("planner must flush before an oversized asset")
            chunks.append({
                "chunkIndex": len(chunks) + 1,
                "assetCount": 1,
                "totalPixels": asset["pixels"],
                "totalMegapixels": asset["megapixels"],
                "overBudgetSingleAsset": True,
                "assets": [asset],
            })
            continue
        current.append(asset)
        current_pixels += asset["pixels"]
    if current:
        chunks.append({
            "chunkIndex": len(chunks) + 1,
            "assetCount": len(current),
            "totalPixels": current_pixels,
            "totalMegapixels": round(current_pixels / 1_000_000.0, 6),
            "overBudgetSingleAsset": False,
            "assets": current,
        })

    plan: dict[str, Any] = {
        "contract": PLAN_CONTRACT,
        "producer": producer,
        "progressReportSha256": progress.get("reportSha256"),
        "progressReportFileSha256": file_sha256(progress_path),
        "dimensionReportSha256": dimension_report.get("reportSha256"),
        "dimensionReportFileSha256": file_sha256(dimensions_path),
        "qualityGate": (
            {
                "contract": quality_gate["contract"],
                "diagnosisSha256": quality_gate.get("diagnosisSha256"),
                "diagnosisFileSha256": file_sha256(quality_diagnosis_path),
                "maximumRelativeLinearExposureRangeStops":
                    MAXIMUM_TRANSITION_EXPOSURE_RANGE_STOPS,
            }
            if quality_gate is not None and quality_diagnosis_path is not None
            else {"status": "not-required-before-first-calibration-chunk"}
        ),
        "humanApprovalGate": (
            human_approval_gate
            if human_approval_gate is not None
            else {"status": "not-required-before-first-calibration-chunk"}
        ),
        "budgets": {
            "maximumAssetsPerChunk": max_assets,
            "maximumMegapixelsPerChunk": max_megapixels,
            "oversizedAssetPolicy": "single-asset-explicit-overage",
        },
        "summary": {
            "catalogAssetCount": len(assets),
            "alreadyRefreshedCount": len(refreshed),
            "pendingAvailableCount": len(pending),
            "missingOriginalCount": len(skipped_missing),
            "chunkCount": len(chunks),
        },
        "alreadyRefreshedAssetIds": sorted(refreshed),
        "missingOriginalAssetIds": sorted(skipped_missing),
        "chunks": chunks,
    }
    plan["planSha256"] = value_sha256(plan)
    publish_new_file(
        output_path,
        (json.dumps(plan, indent=2, sort_keys=True) + "\n").encode("utf-8"),
    )
    print(f"plan={output_path}")
    print(f"chunks={len(chunks)}")
    print(f"pending_available={len(pending)}")
    print(f"missing_originals={len(skipped_missing)}")
    print(f"plan_sha256={plan['planSha256']}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--progress-report", type=Path, required=True)
    parser.add_argument("--dimensions-report", type=Path, required=True)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-assets", type=int, default=4)
    parser.add_argument("--max-megapixels", type=float, default=30.0)
    parser.add_argument("--quality-diagnosis", type=Path)
    parser.add_argument("--review-packet", type=Path)
    parser.add_argument("--approved-ratings", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return build_plan(
            args.progress_report,
            args.dimensions_report,
            args.executable,
            args.output,
            args.max_assets,
            args.max_megapixels,
            args.quality_diagnosis,
            args.review_packet,
            args.approved_ratings,
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
