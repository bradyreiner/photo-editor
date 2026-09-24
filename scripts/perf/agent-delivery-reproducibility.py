#!/usr/bin/env python3
"""Compare repeated Agent real-photo deliveries without hiding pixel drift."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path
from typing import Any


REPORT_CONTRACT = "agent-real-photo-corpus-report-v1"
RECEIPT_CONTRACT = "agent-professional-delivery-reproducibility-v1"


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


def load_object(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def required_file(case: dict[str, Any], key: str) -> Path:
    artifacts = case.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ValueError(f"{case.get('caseId', 'case')}: artifacts must be an object")
    value = artifacts.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{case.get('caseId', 'case')}: {key} is missing")
    path = Path(value).expanduser().resolve()
    if not path.is_file():
        raise ValueError(f"{case.get('caseId', 'case')}: artifact does not exist: {path}")
    return path


def stable_runtime_artifacts(case: dict[str, Any]) -> list[dict[str, Any]]:
    materialization = case.get("retouchMaterialization")
    if not isinstance(materialization, dict):
        return []
    artifacts = materialization.get("artifacts")
    if not isinstance(artifacts, list):
        return []
    stable: list[dict[str, Any]] = []
    keys = (
        "retouchId",
        "retouchMode",
        "status",
        "artifactOwner",
        "modelName",
        "runtimeProvider",
        "runtimeLabel",
        "fallback",
        "qualityMode",
        "regionKind",
        "textureResidualMean",
        "textureStrength",
        "textureExemplar",
        "alternateCandidateEvaluated",
        "alternateModelName",
        "candidateSelection",
        "reviewedRegionGeometryApplied",
        "reviewedRegionCoverageExpansion",
        "selectedCandidateIndex",
        "sourceWidth",
        "sourceHeight",
        "sourceBoundsX",
        "sourceBoundsY",
        "sourceBoundsWidth",
        "sourceBoundsHeight",
        "patchPayloadBytes",
        "sourceImageVersion",
        "reviewedPlanHash",
    )
    for artifact in artifacts:
        if isinstance(artifact, dict):
            stable.append({key: artifact[key] for key in keys if key in artifact})
    return stable


def stable_case_evidence(case: dict[str, Any]) -> dict[str, Any]:
    case_id = case.get("caseId")
    if not isinstance(case_id, str) or not case_id:
        raise ValueError("every report case needs a non-empty caseId")
    before = required_file(case, "beforeProofPath")
    after = required_file(case, "afterProofPath")
    recipe_diff = required_file(case, "recipeDiffPath")
    evidence: dict[str, Any] = {
        "caseId": case_id,
        "assetId": case.get("assetId"),
        "caseKind": case.get("caseKind"),
        "instructions": case.get("instructions"),
        "expectedPixelChecks": case.get("expectedPixelChecks", []),
        "machinePass": case.get("machinePass"),
        "gatePass": case.get("gatePass"),
        "failures": case.get("failures", []),
        "beforeProofSha256": file_sha256(before),
        "afterProofSha256": file_sha256(after),
        "recipeDiffSha256": file_sha256(recipe_diff),
        "pixelIntentAlignment": case.get("pixelIntentAlignment", {}),
        "protectedRegions": case.get("protectedRegions", []),
        "retouchQuality": case.get("retouchQuality", {}),
        "runtimeArtifacts": stable_runtime_artifacts(case),
        "proofRejected": case.get("proofRejected", False),
        "productionRetouchRejection": case.get(
            "productionRetouchRejection", False
        ),
    }
    evidence["evidenceDigestSha256"] = value_sha256(evidence)
    return evidence


def human_review_complete(case: dict[str, Any]) -> bool:
    review = case.get("humanReview")
    if not isinstance(review, dict) or review.get("status") != "pass":
        return False
    rating = review.get("rating")
    notes = review.get("notes")
    return (
        isinstance(rating, (int, float))
        and not isinstance(rating, bool)
        and 1 <= float(rating) <= 5
        and isinstance(notes, str)
        and bool(notes.strip())
    )


def run_evidence(path: Path, report: dict[str, Any]) -> dict[str, Any]:
    if report.get("contract") != REPORT_CONTRACT:
        raise ValueError(f"{path}: contract must be {REPORT_CONTRACT}")
    cases = report.get("cases")
    if not isinstance(cases, list) or not cases:
        raise ValueError(f"{path}: cases must be a non-empty array")
    stable_cases: list[dict[str, Any]] = []
    seen: set[str] = set()
    human_complete = True
    for case in cases:
        if not isinstance(case, dict):
            raise ValueError(f"{path}: every case must be an object")
        stable = stable_case_evidence(case)
        case_id = stable["caseId"]
        if case_id in seen:
            raise ValueError(f"{path}: duplicate caseId {case_id}")
        seen.add(case_id)
        stable_cases.append(stable)
        human_complete = human_complete and human_review_complete(case)
    stable_cases.sort(key=lambda value: value["caseId"])
    run_digest = value_sha256(stable_cases)
    summary = report.get("summary")
    if not isinstance(summary, dict):
        summary = {}
    return {
        "reportPath": str(path),
        "reportFileSha256": file_sha256(path),
        "createdAt": report.get("createdAt"),
        "caseCount": len(stable_cases),
        "machinePassCount": summary.get("machinePassCount"),
        "machineFailCount": summary.get("machineFailCount"),
        "humanCalibrationComplete": human_complete,
        "deliveryEvidenceDigestSha256": run_digest,
        "cases": stable_cases,
    }


def compare_runs(runs: list[dict[str, Any]]) -> dict[str, Any]:
    baseline = runs[0]
    baseline_cases = {case["caseId"]: case for case in baseline["cases"]}
    drift: list[dict[str, Any]] = []
    case_results: list[dict[str, Any]] = []
    all_case_ids = sorted(
        set().union(*(set(case["caseId"] for case in run["cases"]) for run in runs))
    )
    for case_id in all_case_ids:
        evidence_by_run: list[dict[str, Any]] = []
        baseline_case = baseline_cases.get(case_id)
        for run_index, run in enumerate(runs):
            current = next(
                (case for case in run["cases"] if case["caseId"] == case_id), None
            )
            if current is None or baseline_case is None:
                drift.append(
                    {
                        "caseId": case_id,
                        "runIndex": run_index + 1,
                        "kind": "case-set-drift",
                        "detail": "case is missing from one repeated delivery",
                    }
                )
                continue
            stable_fields = sorted(
                (set(current) | set(baseline_case))
                - {"caseId", "evidenceDigestSha256"}
            )
            changed_fields = [
                field
                for field in stable_fields
                if current.get(field) != baseline_case.get(field)
            ]
            if changed_fields:
                drift.append(
                    {
                        "caseId": case_id,
                        "runIndex": run_index + 1,
                        "kind": "delivery-evidence-drift",
                        "changedFields": changed_fields,
                    }
                )
            evidence_by_run.append(
                {
                    "runIndex": run_index + 1,
                    "evidenceDigestSha256": current["evidenceDigestSha256"],
                    "afterProofSha256": current["afterProofSha256"],
                    "recipeDiffSha256": current["recipeDiffSha256"],
                }
            )
        case_results.append(
            {
                "caseId": case_id,
                "reproducible": not any(item["caseId"] == case_id for item in drift),
                "runs": evidence_by_run,
            }
        )

    all_machine_pass = all(
        run["machineFailCount"] == 0
        and run["machinePassCount"] == run["caseCount"]
        and all(
            case["machinePass"] is True
            and case["gatePass"] is True
            and case["failures"] == []
            for case in run["cases"]
        )
        for run in runs
    )
    digest_stable = len(
        {run["deliveryEvidenceDigestSha256"] for run in runs}
    ) == 1
    human_calibrated = all(run["humanCalibrationComplete"] for run in runs)
    reproducible = all_machine_pass and digest_stable and not drift
    receipt: dict[str, Any] = {
        "contract": RECEIPT_CONTRACT,
        "version": 1,
        "runCount": len(runs),
        "caseCount": len(all_case_ids),
        "allMachineCasesPassed": all_machine_pass,
        "reproducibilityPassed": reproducible,
        "exactDeliveryEvidenceDigestStable": digest_stable,
        "unexplainedDriftCount": len(drift),
        "unexplainedDrift": drift,
        "caseResults": case_results,
        "runs": [
            {key: value for key, value in run.items() if key != "cases"}
            for run in runs
        ],
        "deliveryEvidenceDigestSha256": (
            baseline["deliveryEvidenceDigestSha256"] if reproducible else ""
        ),
        "excludedVolatileFields": [
            "createdAt",
            "artifactRoot",
            "artifact paths",
            "run UUIDs",
            "wall-clock durations",
            "UI event pump counts",
        ],
        "humanCalibrationComplete": human_calibrated,
        "humanCalibrationRequired": True,
        "professionalReadinessClaimed": False,
        "professionalReadinessBlockers": (
            []
            if human_calibrated and reproducible
            else [
                blocker
                for blocker, active in (
                    ("repeated-delivery-drift", not reproducible),
                    ("photographer-ratings-pending", not human_calibrated),
                )
                if active
            ]
        ),
    }
    receipt["receiptDigestSha256"] = value_sha256(receipt)
    return receipt


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Compare repeated full Agent corpus deliveries by exact proof and recipe evidence."
    )
    parser.add_argument(
        "--report", action="append", required=True, help="Corpus report; provide at least twice."
    )
    parser.add_argument("--output", required=True, help="Output reproducibility receipt JSON.")
    parser.add_argument("--minimum-runs", type=int, default=2)
    parser.add_argument("--require-reproducible", action="store_true")
    parser.add_argument("--require-human-calibration", action="store_true")
    args = parser.parse_args()
    if args.minimum_runs < 2:
        print("minimum-runs must be at least 2", file=sys.stderr)
        return 2
    paths = [Path(value).expanduser().resolve() for value in args.report]
    if len(paths) < args.minimum_runs:
        print(
            f"at least {args.minimum_runs} repeated delivery reports are required",
            file=sys.stderr,
        )
        return 2
    try:
        runs = [run_evidence(path, load_object(path)) for path in paths]
        receipt = compare_runs(runs)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    output = Path(args.output).expanduser().resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(receipt, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"receipt={output}")
    print(f"runs={receipt['runCount']}")
    print(f"cases={receipt['caseCount']}")
    print(f"reproducibility_passed={str(receipt['reproducibilityPassed']).lower()}")
    print(f"unexplained_drift={receipt['unexplainedDriftCount']}")
    print(f"human_calibration_complete={str(receipt['humanCalibrationComplete']).lower()}")
    print(f"receipt_digest_sha256={receipt['receiptDigestSha256']}")
    if args.require_reproducible and not receipt["reproducibilityPassed"]:
        return 1
    if args.require_human_calibration and not receipt["humanCalibrationComplete"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
