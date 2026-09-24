#!/usr/bin/env python3
"""Regression test for rendering migration pair diagnosis."""

from __future__ import annotations

import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


SCRIPT = Path(__file__).with_name("diagnose-rendering-migration-pairs.py")


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def ppm(values: list[int]) -> bytes:
    pixels = bytearray()
    for value in values:
        pixels.extend((value, value, value))
    return b"P6\n16 1\n255\n" + bytes(pixels)


def run(*arguments: str, expect: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [sys.executable, "-B", str(SCRIPT), *arguments],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != expect:
        raise AssertionError(f"expected {expect}, got {result.returncode}\n{result.stdout}\n{result.stderr}")
    return result


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        packet = root / "packet"
        asset_root = packet / "assets" / "asset-0123456789abcdef"
        asset_root.mkdir(parents=True)
        reference = ppm(list(range(8, 249, 16)))
        candidate = ppm([max(0, value // 2) for value in range(8, 249, 16)])
        (asset_root / "reference.ppm").write_bytes(reference)
        (asset_root / "candidate.ppm").write_bytes(candidate)
        manifest = {
            "contract": "photo-editor-rendering-migration-review-packet:v1",
            "packetDigestSha256": "a" * 64,
            "sourceReportSha256": "b" * 64,
            "sourceReportFileSha256": "c" * 64,
            "producer": {
                "contract": "photo-editor-rendering-migration-batch-producer:v1",
                "executableSha256": "d" * 64,
                "executableName": "Looksy",
                "buildMode": "release",
            },
            "assets": [{
                "assetId": "asset-0123456789abcdef",
                "artifacts": {
                    "reference": {"path": "assets/asset-0123456789abcdef/reference.ppm", "sha256": sha(reference)},
                    "candidate": {"path": "assets/asset-0123456789abcdef/candidate.ppm", "sha256": sha(candidate)},
                },
            }],
        }
        (packet / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        output = root / "diagnosis.json"
        common = ("--packet", str(packet), "--output", str(output), "--max-width", "16")
        run(*common)
        diagnosis = json.loads(output.read_text(encoding="utf-8"))
        metrics = diagnosis["assets"][0]["metrics"]
        if (
            diagnosis["summary"]["candidateDarkerMeanCount"] != 1
            or metrics["candidateMinusReferenceMeanLuma"] >= 0
            or metrics["lumaQuantiles"]["candidate"]["p50"] >= metrics["lumaQuantiles"]["reference"]["p50"]
            or diagnosis["contract"] != "photo-editor-rendering-migration-pair-diagnosis:v3"
            or diagnosis["sampling"]["exposureMethod"]
            != "IEC-61966-2-1-sRGB-decode-then-Rec709-linear-luminance-ratio"
            or diagnosis["sourceReportSha256"] != "b" * 64
            or diagnosis["producer"] != manifest["producer"]
            or metrics["relativeLinearExposureRangeStops"] <= 0
            or metrics["allObservedLinearExposureRangeStops"]
            < metrics["relativeLinearExposureRangeStops"]
            or metrics["minimumExposureBandSampleCount"] != 1
            or metrics["excludedExposureBandCount"] != 0
        ):
            raise AssertionError("diagnosis did not detect the synthetic nonlinear darker candidate")
        run(*common, expect=1)
        output.unlink()
        (asset_root / "candidate.ppm").write_bytes(b"tampered")
        run(*common, expect=1)
    print("diagnose_rendering_migration_pairs_test=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
