#!/usr/bin/env python3
"""Contract tests for repeated professional Agent delivery evidence."""

from __future__ import annotations

import copy
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPO_ROOT / "scripts/perf/agent-delivery-reproducibility.py"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def run(*arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-B", str(TOOL), *arguments],
        capture_output=True,
        check=False,
        text=True,
    )


def make_report(root: Path, run_name: str, human_status: str) -> Path:
    artifact_root = root / run_name / "artifacts"
    artifact_root.mkdir(parents=True)
    before = artifact_root / "before.png"
    after = artifact_root / "after.png"
    recipe = artifact_root / "recipe-diff.json"
    before.write_bytes(b"stable-before-proof")
    after.write_bytes(b"stable-after-proof")
    write_json(recipe, {"exposure": {"before": 0.0, "after": 0.35}})
    human_review: dict[str, Any] = {"status": human_status}
    if human_status == "pass":
        human_review.update(
            {
                "rating": 5,
                "notes": "The repeated delivery is visually consistent and ready.",
            }
        )
    report = {
        "contract": "agent-real-photo-corpus-report-v1",
        "createdAt": "2026-08-22T01:00:00Z" if run_name == "run-1" else "2026-08-22T02:00:00Z",
        "artifactRoot": str(artifact_root),
        "summary": {
            "caseCount": 1,
            "machinePassCount": 1,
            "machineFailCount": 0,
        },
        "cases": [
            {
                "caseId": "portrait-natural-lift",
                "assetId": "portrait-1",
                "caseKind": "global",
                "instructions": "Lift the portrait naturally without changing skin tone.",
                "expectedPixelChecks": ["brighter", "preserveSkinTone"],
                "machinePass": True,
                "gatePass": True,
                "failures": [],
                "runDurationMs": 120 if run_name == "run-1" else 875,
                "artifacts": {
                    "beforeProofPath": str(before),
                    "afterProofPath": str(after),
                    "recipeDiffPath": str(recipe),
                },
                "pixelIntentAlignment": {"passed": True, "meanDelta": 0.12},
                "protectedRegions": [{"label": "skin", "passed": True}],
                "retouchQuality": {},
                "retouchMaterialization": {
                    "durationMs": 10 if run_name == "run-1" else 400,
                    "uiEventPumpCount": 1 if run_name == "run-1" else 18,
                    "artifacts": [],
                },
                "humanReview": human_review,
            }
        ],
    }
    report_path = root / run_name / "report.json"
    write_json(report_path, report)
    return report_path


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="agent-delivery-reproducibility-") as raw_root:
        root = Path(raw_root)
        first = make_report(root, "run-1", "pending")
        second = make_report(root, "run-2", "pending")
        receipt_path = root / "receipt.json"
        result = run(
            "--report",
            str(first),
            "--report",
            str(second),
            "--output",
            str(receipt_path),
            "--require-reproducible",
        )
        require(result.returncode == 0, f"stable deliveries failed: {result.stderr}")
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
        require(
            receipt["contract"] == "agent-professional-delivery-reproducibility-v1"
            and receipt["reproducibilityPassed"] is True
            and receipt["exactDeliveryEvidenceDigestStable"] is True
            and receipt["unexplainedDrift"] == []
            and len(receipt["deliveryEvidenceDigestSha256"]) == 64
            and receipt["humanCalibrationComplete"] is False
            and receipt["humanCalibrationRequired"] is True
            and receipt["professionalReadinessClaimed"] is False,
            "stable output bytes must pass while pending photographer review remains explicit",
        )

        human_gate = run(
            "--report",
            str(first),
            "--report",
            str(second),
            "--output",
            str(root / "pending-human-receipt.json"),
            "--require-human-calibration",
        )
        require(
            human_gate.returncode == 1,
            "pending photographer ratings must fail the human-calibration gate",
        )

        second_value = json.loads(second.read_text(encoding="utf-8"))
        second_after = Path(second_value["cases"][0]["artifacts"]["afterProofPath"])
        second_after.write_bytes(b"drifted-after-proof")
        drift_receipt_path = root / "drift-receipt.json"
        drift_result = run(
            "--report",
            str(first),
            "--report",
            str(second),
            "--output",
            str(drift_receipt_path),
            "--require-reproducible",
        )
        require(drift_result.returncode == 1, "changed proof pixels must fail closed")
        drift_receipt = json.loads(drift_receipt_path.read_text(encoding="utf-8"))
        require(
            drift_receipt["reproducibilityPassed"] is False
            and drift_receipt["unexplainedDriftCount"] == 1
            and drift_receipt["unexplainedDrift"][0]["changedFields"]
            == ["afterProofSha256"],
            "the receipt must name the exact changed stable-evidence field",
        )

        false_first = make_report(root, "false-run-1", "pending")
        false_second = make_report(root, "false-run-2", "pending")
        for false_report in (false_first, false_second):
            false_value = json.loads(false_report.read_text(encoding="utf-8"))
            false_value["cases"][0]["machinePass"] = False
            write_json(false_report, false_value)
        false_result = run(
            "--report",
            str(false_first),
            "--report",
            str(false_second),
            "--output",
            str(root / "false-summary-receipt.json"),
            "--require-reproducible",
        )
        require(
            false_result.returncode == 1,
            "a misleading pass summary must not override failed case evidence",
        )

        reviewed_first = make_report(root, "reviewed-run-1", "pass")
        reviewed_second = make_report(root, "reviewed-run-2", "pass")
        reviewed_result = run(
            "--report",
            str(reviewed_first),
            "--report",
            str(reviewed_second),
            "--output",
            str(root / "reviewed-receipt.json"),
            "--require-reproducible",
            "--require-human-calibration",
        )
        require(
            reviewed_result.returncode == 0,
            f"complete photographer ratings should satisfy both gates: {reviewed_result.stderr}",
        )

    print("agent_delivery_reproducibility_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
