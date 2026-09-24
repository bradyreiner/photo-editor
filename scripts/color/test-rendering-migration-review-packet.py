#!/usr/bin/env python3
"""Regression tests for rendering-migration-review-packet.py."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("rendering-migration-review-packet.py")


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value), encoding="utf-8")


def write_sealed_report(path: Path, value: dict[str, object]) -> None:
    payload = dict(value)
    payload.pop("reportSha256", None)
    canonical = json.dumps(
        payload, sort_keys=True, separators=(",", ":")
    ).encode("utf-8")
    payload["reportSha256"] = digest(canonical)
    path.write_text(
        json.dumps(payload, sort_keys=True, separators=(",", ":")),
        encoding="utf-8",
    )


def run(*arguments: str, expect: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, "-B", str(SCRIPT), *arguments],
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
        executable.write_bytes(b"release-binary")
        report = root / "report.json"
        artifact_root = Path(str(report) + ".assets")
        artifact_root.mkdir()
        names: list[str] = []
        for prefix in (
            "reference-export",
            "candidate-export",
            "delta-e00-heatmap",
            "reference-target-display-p3-simulation",
            "candidate-target-display-p3-simulation",
        ):
            content = prefix.encode()
            name = f"{prefix}-sha256-{digest(content)}.png"
            (artifact_root / name).write_bytes(content)
            names.append(name)
        payload = {
            "schema": "photo-editor-rendering-migration-batch-report:v2",
            "producer": {
                "contract": "photo-editor-rendering-migration-batch-producer:v1",
                "executableSha256": digest(executable.read_bytes()),
                "executableName": "Looksy",
                "buildMode": "release",
            },
            "assets": [{
                "assetId": "asset-0123456789abcdef",
                "sourcePath": "/photos/example.dng",
                "evidenceValid": True,
                "evidenceSha256": "b" * 64,
                "evidenceArtifactFileNames": names,
                "p95DeltaE00": 1.5,
                "maximumDeltaE00": 4.0,
                "addedShadowClipCount": 0,
                "addedHighlightClipCount": 1,
                "risks": ["added-clipping"],
            }],
        }
        write_sealed_report(report, payload)
        tampered_report = root / "tampered-report.json"
        tampered_payload = json.loads(report.read_text(encoding="utf-8"))
        tampered_payload["assets"][0]["evidenceValid"] = False
        write_json(tampered_report, tampered_payload)
        run(
            "build", "--report", str(tampered_report),
            "--executable", str(executable),
            "--output", str(root / "tampered-packet"), expect=1,
        )
        packet = root / "packet"
        run("build", "--report", str(report), "--executable", str(executable), "--output", str(packet))
        run("validate", "--packet", str(packet), "--executable", str(executable))
        run("validate", "--packet", str(packet), "--executable", str(executable), "--require-complete", expect=1)
        archive_one = root / "review-one.zip"
        archive_two = root / "review-two.zip"
        run(
            "archive", "--packet", str(packet), "--executable", str(executable),
            "--output", str(archive_one),
        )
        run(
            "archive", "--packet", str(packet), "--executable", str(executable),
            "--output", str(archive_two),
        )
        run(
            "archive", "--packet", str(packet), "--executable", str(executable),
            "--output", str(archive_two), expect=1,
        )
        if archive_one.read_bytes() != archive_two.read_bytes():
            raise AssertionError("review packet archives should be deterministic")
        run(
            "validate-archive", "--archive", str(archive_one),
            "--executable", str(executable),
        )
        run(
            "validate-archive", "--archive", str(archive_one),
            "--executable", str(executable), "--require-complete", expect=1,
        )
        with zipfile.ZipFile(archive_one, mode="r") as archive:
            names = archive.namelist()
            if "report.json" in names or any(name.startswith("/") for name in names):
                raise AssertionError("portable archive must not expose source paths or reports")
            unexpected_archive = root / "review-unexpected.zip"
            with zipfile.ZipFile(unexpected_archive, mode="w") as changed:
                for info in archive.infolist():
                    changed.writestr(info, archive.read(info))
                changed.writestr("unexpected.txt", b"must fail")
        run(
            "validate-archive", "--archive", str(unexpected_archive),
            "--executable", str(executable), expect=1,
        )
        malformed_archive = root / "review-malformed.zip"
        malformed_archive.write_bytes(b"not-a-zip")
        malformed_result = run(
            "validate-archive", "--archive", str(malformed_archive),
            "--executable", str(executable), expect=1,
        )
        if "error=" not in malformed_result.stderr or "Traceback" in malformed_result.stderr:
            raise AssertionError("malformed archives should fail cleanly")
        ratings_path = packet / "ratings.json"
        run(
            "record-ratings", "--packet", str(packet), "--executable", str(executable),
            "--ratings", str(ratings_path), "--output", str(root / "incomplete.json"),
            expect=1,
        )
        ratings = json.loads(ratings_path.read_text(encoding="utf-8"))
        ratings.update({
            "reviewer": "Independent photographer",
            "reviewedAt": "2026-09-02T18:30:00-06:00",
            "independentReview": True,
        })
        rating = ratings["cases"]["asset-0123456789abcdef"]
        rating.update({
            "decision": "approve",
            "notes": "Color and detail remain natural.",
            "scores": {
                "colorFidelity": 5,
                "highlightDetail": 4,
                "shadowDetail": 4,
                "stackIntegrity": 5,
                "previewExportMatch": 5,
            },
        })
        returned_ratings = root / "returned-ratings.json"
        write_json(returned_ratings, ratings)
        run(
            "validate", "--packet", str(packet), "--executable", str(executable),
            "--ratings", str(returned_ratings), "--require-complete",
        )
        run(
            "validate", "--packet", str(packet), "--executable", str(executable),
            "--ratings", str(returned_ratings), "--require-approved",
        )
        completed_ratings = root / "completed-ratings.json"
        run(
            "record-ratings", "--packet", str(packet), "--executable", str(executable),
            "--ratings", str(returned_ratings), "--output", str(completed_ratings),
        )
        if completed_ratings.read_bytes() != returned_ratings.read_bytes():
            raise AssertionError("recorded ratings must preserve the returned bytes exactly")
        run(
            "record-ratings", "--packet", str(packet), "--executable", str(executable),
            "--ratings", str(returned_ratings), "--output", str(completed_ratings), expect=1,
        )
        run(
            "validate-archive", "--archive", str(archive_one),
            "--executable", str(executable), "--ratings", str(completed_ratings),
            "--require-approved",
        )
        rejected = json.loads(returned_ratings.read_text(encoding="utf-8"))
        rejected["cases"]["asset-0123456789abcdef"]["decision"] = "reject"
        rejected_path = root / "rejected-ratings.json"
        write_json(rejected_path, rejected)
        run(
            "validate", "--packet", str(packet), "--executable", str(executable),
            "--ratings", str(rejected_path), "--require-complete",
        )
        run(
            "validate", "--packet", str(packet), "--executable", str(executable),
            "--ratings", str(rejected_path), "--require-approved", expect=1,
        )
        unexpected = json.loads(returned_ratings.read_text(encoding="utf-8"))
        unexpected["untrustedField"] = True
        unexpected_path = root / "unexpected-ratings.json"
        write_json(unexpected_path, unexpected)
        run(
            "validate", "--packet", str(packet), "--executable", str(executable),
            "--ratings", str(unexpected_path), expect=1,
        )
        index_path = packet / "index.html"
        original_index = index_path.read_text(encoding="utf-8")
        index_path.write_text(original_index + "tampered", encoding="utf-8")
        run("validate", "--packet", str(packet), "--executable", str(executable), expect=1)
        index_path.write_text(original_index, encoding="utf-8")
        artifact = next((packet / "assets").rglob("candidate.png"))
        artifact.write_bytes(b"tampered")
        run("validate", "--packet", str(packet), "--executable", str(executable), expect=1)
    print("rendering_migration_review_packet_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
