#!/usr/bin/env python3
"""Measure tone and color bias in migration review pairs without rerendering."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


PACKET_CONTRACT = "photo-editor-rendering-migration-review-packet:v1"
DIAGNOSIS_CONTRACT = "photo-editor-rendering-migration-pair-diagnosis:v3"
MINIMUM_EXPOSURE_BAND_FRACTION = 0.001


def srgb8_to_linear(value: int) -> float:
    encoded = value / 255.0
    if encoded <= 0.04045:
        return encoded / 12.92
    return ((encoded + 0.055) / 1.055) ** 2.4


SRGB8_TO_LINEAR = tuple(srgb8_to_linear(value) for value in range(256))


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
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def value_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def decode_ppm(path: Path, max_width: int, ffmpeg: str) -> tuple[int, int, bytes]:
    command = [
        ffmpeg,
        "-v",
        "error",
        "-i",
        str(path),
        "-vf",
        f"scale=min({max_width}\\,iw):-2",
        "-frames:v",
        "1",
        "-f",
        "image2pipe",
        "-vcodec",
        "ppm",
        "-",
    ]
    result = subprocess.run(command, check=False, capture_output=True)
    if result.returncode != 0:
        raise ValueError(f"ffmpeg could not decode {path}: {result.stderr.decode(errors='replace').strip()}")
    data = result.stdout
    match = re.match(rb"P6\s+(?:#[^\n]*\n\s*)*(\d+)\s+(\d+)\s+255\s", data)
    if match is None:
        raise ValueError(f"ffmpeg returned an invalid PPM for {path}")
    width = int(match.group(1))
    height = int(match.group(2))
    pixels = data[match.end():]
    if len(pixels) != width * height * 3:
        raise ValueError(f"decoded PPM size is inconsistent for {path}")
    return width, height, pixels


def quantile(histogram: list[int], fraction: float, offset: int = 0) -> int:
    total = sum(histogram)
    if total <= 0:
        return 0
    target = max(1, math.ceil(total * fraction))
    running = 0
    for index, count in enumerate(histogram):
        running += count
        if running >= target:
            return index + offset
    return len(histogram) - 1 + offset


def rounded(value: float) -> float:
    return round(value, 6)


def pair_metrics(reference: bytes, candidate: bytes, width: int, height: int) -> dict[str, Any]:
    if len(reference) != len(candidate):
        raise ValueError("candidate/reference decoded sizes differ")
    count = width * height
    reference_hist = [0] * 256
    candidate_hist = [0] * 256
    difference_hist = [0] * 511
    reference_rgb = [0, 0, 0]
    candidate_rgb = [0, 0, 0]
    reference_chroma_sum = 0
    candidate_chroma_sum = 0
    band_count = [0] * 8
    band_reference_linear_luma_sum = [0.0] * 8
    band_candidate_linear_luma_sum = [0.0] * 8
    absolute_luma_difference = 0
    for offset in range(0, len(reference), 3):
        rr, rg, rb = reference[offset], reference[offset + 1], reference[offset + 2]
        cr, cg, cb = candidate[offset], candidate[offset + 1], candidate[offset + 2]
        reference_y = (54 * rr + 183 * rg + 19 * rb + 128) >> 8
        candidate_y = (54 * cr + 183 * cg + 19 * cb + 128) >> 8
        reference_linear_y = (
            0.2126 * SRGB8_TO_LINEAR[rr]
            + 0.7152 * SRGB8_TO_LINEAR[rg]
            + 0.0722 * SRGB8_TO_LINEAR[rb]
        )
        candidate_linear_y = (
            0.2126 * SRGB8_TO_LINEAR[cr]
            + 0.7152 * SRGB8_TO_LINEAR[cg]
            + 0.0722 * SRGB8_TO_LINEAR[cb]
        )
        reference_hist[reference_y] += 1
        candidate_hist[candidate_y] += 1
        difference = candidate_y - reference_y
        difference_hist[difference + 255] += 1
        absolute_luma_difference += abs(difference)
        band = min(7, reference_y // 32)
        band_count[band] += 1
        band_reference_linear_luma_sum[band] += reference_linear_y
        band_candidate_linear_luma_sum[band] += candidate_linear_y
        reference_rgb[0] += rr
        reference_rgb[1] += rg
        reference_rgb[2] += rb
        candidate_rgb[0] += cr
        candidate_rgb[1] += cg
        candidate_rgb[2] += cb
        reference_chroma_sum += max(rr, rg, rb) - min(rr, rg, rb)
        candidate_chroma_sum += max(cr, cg, cb) - min(cr, cg, cb)
    bands: list[dict[str, Any]] = []
    exposure_stops: list[float] = []
    all_exposure_stops: list[float] = []
    minimum_exposure_band_samples = max(
        1, math.ceil(count * MINIMUM_EXPOSURE_BAND_FRACTION)
    )
    for index in range(8):
        if band_count[index] == 0:
            continue
        reference_mean = band_reference_linear_luma_sum[index] / band_count[index]
        candidate_mean = band_candidate_linear_luma_sum[index] / band_count[index]
        # Exposure is a ratio in scene-linear light. The artifacts are
        # display-encoded sRGB, so using their code values directly would
        # materially understate the tonal non-uniformity.
        stops = math.log2(
            (candidate_mean + 1.0e-9) / (reference_mean + 1.0e-9)
        )
        all_exposure_stops.append(stops)
        exposure_gate_eligible = (
            band_count[index] >= minimum_exposure_band_samples
        )
        if exposure_gate_eligible:
            exposure_stops.append(stops)
        bands.append({
            "referenceLumaRange": [index * 32, min(255, index * 32 + 31)],
            "sampleCount": band_count[index],
            "exposureGateEligible": exposure_gate_eligible,
            "referenceMeanLinearLuma": rounded(reference_mean),
            "candidateMeanLinearLuma": rounded(candidate_mean),
            "relativeLinearExposureStops": rounded(stops),
        })
    mean_reference_luma = sum(index * value for index, value in enumerate(reference_hist)) / count
    mean_candidate_luma = sum(index * value for index, value in enumerate(candidate_hist)) / count
    return {
        "sampleWidth": width,
        "sampleHeight": height,
        "samplePixelCount": count,
        "referenceMeanLuma": rounded(mean_reference_luma),
        "candidateMeanLuma": rounded(mean_candidate_luma),
        "candidateMinusReferenceMeanLuma": rounded(mean_candidate_luma - mean_reference_luma),
        "meanAbsoluteLumaDifference": rounded(absolute_luma_difference / count),
        "lumaQuantiles": {
            "reference": {name: quantile(reference_hist, value) for name, value in (("p01", .01), ("p05", .05), ("p50", .5), ("p95", .95), ("p99", .99))},
            "candidate": {name: quantile(candidate_hist, value) for name, value in (("p01", .01), ("p05", .05), ("p50", .5), ("p95", .95), ("p99", .99))},
            "candidateMinusReference": {name: quantile(difference_hist, value, -255) for name, value in (("p05", .05), ("p50", .5), ("p95", .95))},
        },
        "nearBlackPixelCount": {
            "reference": sum(reference_hist[:3]),
            "candidate": sum(candidate_hist[:3]),
            "candidateMinusReference": sum(candidate_hist[:3]) - sum(reference_hist[:3]),
        },
        "nearWhitePixelCount": {
            "reference": sum(reference_hist[253:]),
            "candidate": sum(candidate_hist[253:]),
            "candidateMinusReference": sum(candidate_hist[253:]) - sum(reference_hist[253:]),
        },
        "meanRgb": {
            "reference": [rounded(value / count) for value in reference_rgb],
            "candidate": [rounded(value / count) for value in candidate_rgb],
        },
        "meanChromaRange": {
            "reference": rounded(reference_chroma_sum / count),
            "candidate": rounded(candidate_chroma_sum / count),
        },
        "relativeLinearExposureByReferenceDisplayLumaBand": bands,
        "relativeLinearExposureRangeStops": rounded(max(exposure_stops) - min(exposure_stops)) if exposure_stops else 0.0,
        "allObservedLinearExposureRangeStops": rounded(max(all_exposure_stops) - min(all_exposure_stops)) if all_exposure_stops else 0.0,
        "minimumExposureBandSampleCount": minimum_exposure_band_samples,
        "excludedExposureBandCount": sum(
            not band["exposureGateEligible"] for band in bands
        ),
    }


def build_diagnosis(packet: Path, output: Path, max_width: int, ffmpeg: str) -> int:
    packet = packet.resolve()
    output = output.resolve()
    if output.exists():
        return fail(f"output already exists: {output}")
    if max_width < 16 or max_width > 4096:
        return fail("max width must be between 16 and 4096")
    manifest_path = packet / "manifest.json"
    manifest = load_object(manifest_path)
    if manifest.get("contract") != PACKET_CONTRACT:
        return fail("review packet contract is invalid")
    assets = manifest.get("assets")
    if not isinstance(assets, list) or not assets:
        return fail("review packet contains no assets")
    results: list[dict[str, Any]] = []
    for asset in assets:
        asset_id = asset.get("assetId")
        artifacts = asset.get("artifacts")
        if not isinstance(asset_id, str) or not isinstance(artifacts, dict):
            return fail("review packet asset is invalid")
        decoded: dict[str, tuple[int, int, bytes]] = {}
        for role in ("reference", "candidate"):
            artifact = artifacts.get(role)
            if not isinstance(artifact, dict):
                return fail(f"{asset_id}: {role} artifact is missing")
            path = packet / str(artifact.get("path", ""))
            if not path.is_file() or file_sha256(path) != artifact.get("sha256"):
                return fail(f"{asset_id}: {role} artifact identity is invalid")
            decoded[role] = decode_ppm(path, max_width, ffmpeg)
        if decoded["reference"][:2] != decoded["candidate"][:2]:
            return fail(f"{asset_id}: sampled dimensions differ")
        width, height = decoded["reference"][:2]
        results.append({
            "assetId": asset_id,
            "referenceSha256": artifacts["reference"]["sha256"],
            "candidateSha256": artifacts["candidate"]["sha256"],
            "metrics": pair_metrics(decoded["reference"][2], decoded["candidate"][2], width, height),
        })
    mean_luma_deltas = [item["metrics"]["candidateMinusReferenceMeanLuma"] for item in results]
    exposure_ranges = [item["metrics"]["relativeLinearExposureRangeStops"] for item in results]
    diagnosis: dict[str, Any] = {
        "contract": DIAGNOSIS_CONTRACT,
        "packetDigestSha256": manifest.get("packetDigestSha256"),
        "packetManifestFileSha256": file_sha256(manifest_path),
        "sourceReportSha256": manifest.get("sourceReportSha256"),
        "sourceReportFileSha256": manifest.get("sourceReportFileSha256"),
        "producer": manifest.get("producer"),
        "sampling": {
            "decoder": "ffmpeg-to-ppm",
            "maximumWidth": max_width,
            "exposureMethod": "IEC-61966-2-1-sRGB-decode-then-Rec709-linear-luminance-ratio",
            "bandDomain": "reference-display-encoded-sRGB-luma",
            "minimumExposureBandFraction": MINIMUM_EXPOSURE_BAND_FRACTION,
            "tailDetailGate": "separate-strict-clipping-and-near-white-evidence",
        },
        "summary": {
            "assetCount": len(results),
            "candidateDarkerMeanCount": sum(value < 0 for value in mean_luma_deltas),
            "candidateBrighterMeanCount": sum(value > 0 for value in mean_luma_deltas),
            "meanCandidateMinusReferenceLuma": rounded(sum(mean_luma_deltas) / len(mean_luma_deltas)),
            "maximumRelativeLinearExposureRangeStops": rounded(max(exposure_ranges)),
            "universalExposureConsistentCount": sum(value <= 0.25 for value in exposure_ranges),
        },
        "assets": results,
    }
    diagnosis["diagnosisSha256"] = value_sha256(diagnosis)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(diagnosis, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"diagnosis={output}")
    print(f"assets={len(results)}")
    print(f"candidate_darker={diagnosis['summary']['candidateDarkerMeanCount']}")
    print(f"universal_exposure_consistent={diagnosis['summary']['universalExposureConsistentCount']}")
    print(f"diagnosis_sha256={diagnosis['diagnosisSha256']}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packet", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-width", type=int, default=1024)
    parser.add_argument("--ffmpeg", default="ffmpeg")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        return build_diagnosis(args.packet, args.output, args.max_width, args.ffmpeg)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
