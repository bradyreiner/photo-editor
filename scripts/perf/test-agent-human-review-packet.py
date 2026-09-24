#!/usr/bin/env python3
"""Self-test the auditable Agent photographer-review packet workflow."""

from __future__ import annotations

import copy
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
TOOL = REPO_ROOT / "scripts/perf/agent-human-review-packet.py"
PNG_1X1 = bytes.fromhex(
    "89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c489"
    "0000000d49444154789c6360f8cff000000401010018dd8db10000000049454e44ae426082"
)


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


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="agent-human-review-packet-") as raw_root:
        root = Path(raw_root)
        before = root / "before.png"
        after = root / "after.png"
        before.write_bytes(PNG_1X1)
        after.write_bytes(PNG_1X1 + b"reviewed-proof")
        source = root / "source.jpg"
        source.write_bytes(b"licensed-photo")
        recipe = root / "recipe.json"
        write_json(recipe, {"version": 1})
        manifest = {
            "manifest_version": 1,
            "agent_evaluation_version": 1,
            "asset_count": 1,
            "assets": [
                {
                    "asset_id": "portrait-1",
                    "path": str(source),
                    "recipe": str(recipe),
                    "media_type": "photo",
                    "format": "jpeg",
                    "license": "Test fixture",
                    "license_verified": True,
                    "license_evidence": "test-only",
                    "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                    "size_bytes": source.stat().st_size,
                    "tags": [
                        "dark_raw",
                        "clipped_highlights",
                        "smooth_sky",
                        "skin",
                        "foliage",
                        "textured_detail",
                    ],
                    "agent_cases": [
                        {
                            "case_id": "portrait-warmer",
                            "instructions": "Make the portrait warmer but keep the tone unchanged.",
                            "expected_pixel_checks": ["warmer", "preserveTone"],
                            "protected_regions": [
                                {
                                    "label": "shirt",
                                    "x": 0.1,
                                    "y": 0.7,
                                    "width": 0.3,
                                    "height": 0.2,
                                    "max_mean_rgb_delta": 0.02,
                                }
                            ],
                            "human_review": {"status": "pending"},
                        }
                    ],
                }
            ],
        }
        manifest_path = root / "manifest.json"
        write_json(manifest_path, manifest)
        report = {
            "contract": "agent-real-photo-corpus-report-v1",
            "summary": {"caseCount": 1, "machinePassCount": 1},
            "cases": [
                {
                    "caseId": "portrait-warmer",
                    "assetId": "portrait-1",
                    "machinePass": True,
                    "failures": [],
                    "pixelIntentAlignment": {
                        "available": True,
                        "checks": [
                            {
                                "name": "warmer",
                                "delta": 0.018,
                                "threshold": 0.01,
                                "passed": True,
                            },
                            {
                                "name": "preserveTone",
                                "delta": 0.3,
                                "threshold": 3.0,
                                "passed": True,
                            },
                        ],
                    },
                    "artifacts": {
                        "beforeProofPath": str(before),
                        "afterProofPath": str(after),
                    },
                }
            ],
        }
        report_path = root / "report.json"
        write_json(report_path, report)
        packet_dir = root / "packet"
        result = run(
            "build",
            "--manifest",
            str(manifest_path),
            "--report",
            str(report_path),
            "--output",
            str(packet_dir),
        )
        require(result.returncode == 0, f"valid packet build failed: {result.stderr}")
        contract = json.loads((packet_dir / "review-contract.json").read_text())
        page = (packet_dir / "index.html").read_text(encoding="utf-8")
        static_page = page.split("<script>", 1)[0]
        require(
            contract["contract"] == "agent-human-review-packet-v2"
            and contract["manifestCaseCount"] == 1
            and contract["reportCaseCount"] == 1
            and contract["humanDecisionRequired"] is True
            and contract["machineResultCannotSetHumanRating"] is True
            and contract["blindReviewRequired"] is True
            and contract["machineEvidenceEmbeddedInReviewPage"] is False
            and contract["sourcePathsEmbeddedInReviewPage"] is False
            and contract["reviewExperienceContract"]
            == "agent-blind-light-table-v1"
            and contract["localDraftAutosave"] is True
            and contract["localDraftContainsProofPixels"] is False
            and contract["directBeforeAfterComparison"] is True
            and len(contract["packetDigestSha256"]) == 64
            and len(contract["cases"][0]["beforeProofSha256"]) == 64
            and len(contract["cases"][0]["afterProofSha256"]) == 64
            and (packet_dir / contract["cases"][0]["beforeProof"]).is_file()
            and (packet_dir / contract["cases"][0]["afterProof"]).is_file()
            and "Photographer Taste Calibration" in page
            and "Machine evidence passed" not in page
            and "Machine evidence failed" not in page
            and "Machine result hidden" in page,
            "review packet should bind a blind human decision to copied, hashed proof pixels",
        )
        require(
            'data-id="portrait-warmer"' in static_page
            and 'src="proofs/portrait-warmer-before.png"' in static_page
            and 'src="proofs/portrait-warmer-after.png"' in static_page
            and "preserveTone" in static_page
            and "Intent match" in static_page
            and "Protected fidelity" in static_page
            and "Visible artifact" in static_page
            and "BLIND LIGHT TABLE" in static_page
            and 'data-proof-mode="overlay"' in static_page
            and 'data-split type="range"' in static_page
            and "Hold B for before" in static_page
            and "Private draft autosaves locally" in static_page
            and "Next unfinished" in static_page
            and "Copy for Looksy" in static_page
            and "Download JSON" in static_page
            and "framehouse-agent-calibration:" in page
            and "localStorage.setItem" in page
            and "navigator.clipboard.writeText" in page
            and "document.execCommand('copy')" in page
            and "Finish copied ratings" in page
            and "localDraftContainsProofPixels" in page
            and '"machinePass"' not in page
            and '"machineFailures"' not in page
            and str(manifest_path) not in page
            and str(report_path) not in page
            and "+'\\n'" in page,
            "review page should be resumable without embedding the machine verdict or source paths",
        )
        node = shutil.which("node")
        if node is not None:
            script_source = page.split("<script>", 1)[1].split("</script>", 1)[0]
            syntax_result = subprocess.run(
                [node, "--check"],
                input=script_source,
                capture_output=True,
                check=False,
                text=True,
            )
            require(
                syntax_result.returncode == 0,
                f"review packet JavaScript is invalid: {syntax_result.stderr}",
            )

        stale_report = copy.deepcopy(report)
        stale_report["cases"] = []
        stale_report_path = root / "stale-report.json"
        write_json(stale_report_path, stale_report)
        mismatch_result = run(
            "build",
            "--manifest",
            str(manifest_path),
            "--report",
            str(stale_report_path),
            "--output",
            str(root / "stale-packet"),
        )
        require(
            mismatch_result.returncode == 1
            and "manifest/report case mismatch" in mismatch_result.stderr,
            "packet generation should reject stale machine reports that omit a manifest case",
        )

        ratings = json.loads((packet_dir / "ratings.template.json").read_text())
        ratings.update(
            {
                "reviewer": "Test Photographer",
                "reviewedAt": "2026-08-22T06:00:00Z",
                "cases": {
                    "portrait-warmer": {
                        "status": "pass",
                        "rating": 4,
                        "dimensions": {
                            "intentMatch": 5,
                            "naturalness": 4,
                            "artifactFreedom": 5,
                            "protectedFidelity": 5,
                        },
                        "issueTags": [],
                        "notes": "Warmth is believable and the protected shirt remains neutral.",
                    }
                },
            }
        )
        ratings_path = root / "ratings.json"
        write_json(ratings_path, ratings)
        reviewed_manifest = root / "reviewed-manifest.json"
        apply_result = run(
            "apply",
            "--manifest",
            str(manifest_path),
            "--packet",
            str(packet_dir / "review-contract.json"),
            "--ratings",
            str(ratings_path),
            "--output-manifest",
            str(reviewed_manifest),
        )
        require(apply_result.returncode == 0, f"valid ratings failed: {apply_result.stderr}")
        source_manifest = json.loads(manifest_path.read_text())
        reviewed = json.loads(reviewed_manifest.read_text())
        source_review = source_manifest["assets"][0]["agent_cases"][0]["human_review"]
        applied_review = reviewed["assets"][0]["agent_cases"][0]["human_review"]
        require(
            source_review == {"status": "pending"}
            and applied_review["status"] == "pass"
            and applied_review["rating"] == 4
            and applied_review["dimensions"]["naturalness"] == 4
            and applied_review["issue_tags"] == []
            and applied_review["reviewer"] == "Test Photographer"
            and applied_review["review_packet_digest_sha256"]
            == contract["packetDigestSha256"]
            and applied_review["before_proof_sha256"]
            == contract["cases"][0]["beforeProofSha256"],
            "rating application should preserve the source and audit the exact reviewed proof identity",
        )

        profile_path = root / "taste-profile.json"
        profile_result = run(
            "profile",
            "--manifest",
            str(reviewed_manifest),
            "--packet",
            str(packet_dir / "review-contract.json"),
            "--report",
            str(report_path),
            "--output",
            str(profile_path),
        )
        require(
            profile_result.returncode == 0,
            f"valid taste profile failed: {profile_result.stderr}",
        )
        profile = json.loads(profile_path.read_text())
        require(
            profile["contract"] == "agent-photographer-calibration-profile-v1"
            and profile["blindReview"] is True
            and profile["localOnly"] is True
            and profile["professionalReadinessClaimed"] is False
            and profile["automaticThresholdMutationAllowed"] is False
            and profile["outcomes"]["agreementPass"] == 1
            and profile["outcomes"]["falseConfidence"] == 0
            and profile["tasteSummary"]["dimensionMeans"]["naturalness"] == 4.0
            and profile["thresholdSignals"]["warmth"]["suggestedAction"]
            == "hold-until-disagreement"
            and len(profile["profileDigestSha256"]) == 64,
            "profile should preserve taste, agreement, and threshold evidence without auto-tuning",
        )

        altered_review = copy.deepcopy(reviewed)
        altered_review["assets"][0]["agent_cases"][0]["human_review"][
            "after_proof_sha256"
        ] = "0" * 64
        altered_review_path = root / "altered-reviewed-manifest.json"
        write_json(altered_review_path, altered_review)
        altered_profile_result = run(
            "profile",
            "--manifest",
            str(altered_review_path),
            "--packet",
            str(packet_dir / "review-contract.json"),
            "--report",
            str(report_path),
            "--output",
            str(root / "altered-profile.json"),
        )
        require(
            altered_profile_result.returncode == 1
            and "review proof identity does not match the packet"
            in altered_profile_result.stderr,
            "taste profile generation should reject altered reviewed-proof identity",
        )

        incomplete_dimensions = copy.deepcopy(ratings)
        del incomplete_dimensions["cases"]["portrait-warmer"]["dimensions"]["naturalness"]
        incomplete_path = root / "incomplete-dimensions.json"
        write_json(incomplete_path, incomplete_dimensions)
        incomplete_result = run(
            "apply",
            "--manifest",
            str(manifest_path),
            "--packet",
            str(packet_dir / "review-contract.json"),
            "--ratings",
            str(incomplete_path),
            "--output-manifest",
            str(root / "incomplete-output.json"),
        )
        require(
            incomplete_result.returncode == 1
            and "every photographic rating dimension is required"
            in incomplete_result.stderr,
            "incomplete photographic ratings should fail closed",
        )

        failed_without_reason = copy.deepcopy(ratings)
        failed_without_reason["cases"]["portrait-warmer"]["status"] = "fail"
        failed_without_reason_path = root / "failed-without-reason.json"
        write_json(failed_without_reason_path, failed_without_reason)
        failed_without_reason_result = run(
            "apply",
            "--manifest",
            str(manifest_path),
            "--packet",
            str(packet_dir / "review-contract.json"),
            "--ratings",
            str(failed_without_reason_path),
            "--output-manifest",
            str(root / "failed-without-reason-output.json"),
        )
        require(
            failed_without_reason_result.returncode == 1
            and "failed review needs at least one issue tag"
            in failed_without_reason_result.stderr,
            "failed reviews should provide a structured calibration reason",
        )

        rejected_ratings = copy.deepcopy(ratings)
        rejected_ratings["cases"]["portrait-warmer"].update(
            {
                "status": "fail",
                "rating": 2,
                "dimensions": {
                    "intentMatch": 3,
                    "naturalness": 2,
                    "artifactFreedom": 4,
                    "protectedFidelity": 5,
                },
                "issueTags": ["too-warm"],
                "notes": "The measured direction is right, but the warmth is not deliverable.",
            }
        )
        rejected_path = root / "rejected-ratings.json"
        write_json(rejected_path, rejected_ratings)
        rejected_manifest = root / "rejected-manifest.json"
        rejected_apply = run(
            "apply",
            "--manifest",
            str(manifest_path),
            "--packet",
            str(packet_dir / "review-contract.json"),
            "--ratings",
            str(rejected_path),
            "--output-manifest",
            str(rejected_manifest),
        )
        require(rejected_apply.returncode == 0, rejected_apply.stderr)
        rejected_profile_path = root / "rejected-profile.json"
        rejected_profile_result = run(
            "profile",
            "--manifest",
            str(rejected_manifest),
            "--packet",
            str(packet_dir / "review-contract.json"),
            "--report",
            str(report_path),
            "--output",
            str(rejected_profile_path),
        )
        require(rejected_profile_result.returncode == 0, rejected_profile_result.stderr)
        rejected_profile = json.loads(rejected_profile_path.read_text())
        require(
            rejected_profile["outcomes"]["falseConfidence"] == 1
            and rejected_profile["tasteSummary"]["issueTagCounts"]["too-warm"] == 1
            and rejected_profile["thresholdSignals"]["warmth"]["status"]
            == "human-rejected-machine-pass"
            and rejected_profile["thresholdSignals"]["warmth"]["suggestedAction"]
            == "tighten-and-rerun"
            and rejected_profile["thresholdSignals"]["warmth"]
            ["automaticThresholdChangeAllowed"]
            is False,
            "a machine-pass human-fail should become an explicit, non-automatic threshold signal",
        )

        tampered_ratings = copy.deepcopy(ratings)
        tampered_ratings["packetDigestSha256"] = "0" * 64
        tampered_path = root / "tampered-ratings.json"
        write_json(tampered_path, tampered_ratings)
        tampered_result = run(
            "apply",
            "--manifest",
            str(manifest_path),
            "--packet",
            str(packet_dir / "review-contract.json"),
            "--ratings",
            str(tampered_path),
            "--output-manifest",
            str(root / "tampered-output.json"),
        )
        require(
            tampered_result.returncode == 1
            and "different review packet" in tampered_result.stderr,
            "ratings from another proof packet should fail closed",
        )

    print("agent_human_review_packet_self_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
