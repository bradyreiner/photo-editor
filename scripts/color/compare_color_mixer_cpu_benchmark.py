#!/usr/bin/env python3
"""Compare the same compiled Look-stage benchmark against two mixer builds."""

import argparse
import hashlib
import json
import re
import statistics
import subprocess
from pathlib import Path

RGBA_HASH_ALGORITHM = "fnv1a64-rgba-f32-le-v1"
RGBA_HASH_PATTERN = re.compile(r"^[0-9a-f]{16}$")


def percentile(values, fraction):
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int((len(ordered) * fraction) + 0.999999) - 1))
    return ordered[index]


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(binary, sample, count):
    observations = []
    for _ in range(count):
        output = subprocess.check_output([binary, sample], text=True)
        observation = json.loads(output)
        if observation.get("schema") != "color-mixer-cpu-benchmark:v1":
            raise RuntimeError(f"unexpected benchmark schema from {binary}")
        if observation.get("sample") != sample:
            raise RuntimeError(f"benchmark sample mismatch from {binary}: {sample}")
        if observation.get("rgba_hash_algorithm") != RGBA_HASH_ALGORITHM:
            raise RuntimeError(f"missing or incompatible RGBA hash algorithm from {binary}")
        output_hash = observation.get("bitwise_rgba_hash")
        if not isinstance(output_hash, str) or not RGBA_HASH_PATTERN.fullmatch(output_hash):
            raise RuntimeError(f"missing or malformed bitwise RGBA hash from {binary}")
        observations.append(observation)
    return {
        "cold_ms_p50": statistics.median(item["cold_ms"] for item in observations),
        "cold_ms_p95": percentile([item["cold_ms"] for item in observations], 0.95),
        "warm_median_ms_p50": statistics.median(
            item["warm_median_ms"] for item in observations),
        "warm_median_ms_p95": percentile(
            [item["warm_median_ms"] for item in observations], 0.95),
        "warm_p95_ms_p50": statistics.median(
            item["warm_p95_ms"] for item in observations),
        "changed_pixels": sorted({item["changed_pixels"] for item in observations}),
        # Preserve the v1 field for existing report readers. The exact output
        # comparison is carried by bitwise_rgba_hashes below.
        "checksum": sorted({round(item["checksum"], 8) for item in observations}),
        "rgba_hash_algorithm": RGBA_HASH_ALGORITHM,
        "bitwise_rgba_hashes": sorted(
            {item["bitwise_rgba_hash"] for item in observations}),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--process-runs", type=int, default=9)
    args = parser.parse_args()
    if args.process_runs < 5:
        parser.error("--process-runs must be at least 5 to report a cold p95")

    report = {
        "schema": "color-mixer-cpu-comparison:v1",
        "pixels_per_run": 131072,
        "process_runs_per_case": args.process_runs,
        "scope": "compiled CPU Look stage; no masks, layers, UI, or GPU dispatch",
        "builds": {
            "baseline": {"path": str(args.baseline), "sha256": sha256(args.baseline)},
            "candidate": {"path": str(args.candidate), "sha256": sha256(args.candidate)},
        },
        "samples": {},
    }
    for sample in ("neutral", "colored", "dark", "hdr"):
        baseline = run(str(args.baseline), sample, args.process_runs)
        candidate = run(str(args.candidate), sample, args.process_runs)
        report["samples"][sample] = {
            "baseline": baseline,
            "candidate": candidate,
            "rgba_hash_algorithm": RGBA_HASH_ALGORITHM,
            "bitwise_rgba_hash_sets_match": (
                baseline["bitwise_rgba_hashes"] == candidate["bitwise_rgba_hashes"]),
        }
    print(json.dumps(report, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
