#!/usr/bin/env python3
"""Measure repeated release-mode F1 CPU render/reopen/export runs on a real photo."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import time
from pathlib import Path
from typing import Any


ASSET = "night-chicago-building.jpg"
PARAMETERS = {"radius": 24, "strength": 80, "warmth": 20, "protection": 0}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_output(command: list[str]) -> str:
    completed = subprocess.run(
        command,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=True,
    )
    return completed.stdout.strip()


def parse_time_report(output: str) -> dict[str, float | int]:
    result: dict[str, float | int] = {}
    for key in ("real", "user", "sys"):
        match = re.search(rf"(?m)^{key}\s+([0-9.]+)$", output)
        if not match:
            raise RuntimeError(f"/usr/bin/time did not report {key} seconds")
        result[f"{key}Seconds"] = float(match.group(1))
    rss = re.search(r"(?m)^\s*(\d+)\s+maximum resident set size$", output)
    if not rss:
        raise RuntimeError("/usr/bin/time did not report maximum resident set size")
    result["maximumResidentSetBytes"] = int(rss.group(1))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True, help="Built photo_effect_workflow_test executable")
    parser.add_argument("--corpus-root", type=Path, required=True, help="Verified creative-tools corpus")
    parser.add_argument("--output-dir", type=Path, required=True, help="Directory for small diagnostic receipt/logs")
    parser.add_argument("--scratch-dir", type=Path, required=True, help="Temporary full-resolution render artifacts")
    parser.add_argument("--runs", type=int, default=3, help="Independent full workflow processes (minimum 3)")
    args = parser.parse_args()
    if args.runs < 3:
        parser.error("at least three runs are required for this diagnostic")

    binary = args.binary.expanduser().resolve()
    corpus_root = args.corpus_root.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    scratch_dir = args.scratch_dir.expanduser().resolve()
    if not binary.is_file():
        parser.error(f"workflow test binary does not exist: {binary}")
    manifest_path = corpus_root / "manifest.json"
    if not manifest_path.is_file():
        parser.error(f"verified corpus manifest is missing: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    asset = next(
        (item for item in manifest.get("assets", []) if Path(str(item.get("path", ""))).name == ASSET),
        None,
    )
    if asset is None:
        parser.error(f"verified corpus does not contain {ASSET}")
    source = corpus_root / str(asset["path"])
    if not source.is_file() or sha256(source) != asset["sha256"]:
        parser.error("source image bytes do not match the pinned corpus manifest")

    dimensions = command_output(["sips", "-g", "pixelWidth", "-g", "pixelHeight", str(source)])
    width_match = re.search(r"pixelWidth:\s*(\d+)", dimensions)
    height_match = re.search(r"pixelHeight:\s*(\d+)", dimensions)
    if not width_match or not height_match:
        raise RuntimeError("could not read source dimensions")
    width, height = int(width_match.group(1)), int(height_match.group(1))
    output_dir.mkdir(parents=True, exist_ok=True)
    scratch_dir.mkdir(parents=True, exist_ok=True)

    versions = {
        "os": command_output(["sw_vers", "-productVersion"]),
        "build": command_output(["sw_vers", "-buildVersion"]),
        "cpu": command_output(["sysctl", "-n", "machdep.cpu.brand_string"]),
        "memoryBytes": int(command_output(["sysctl", "-n", "hw.memsize"])),
        "qt": command_output(["/opt/homebrew/opt/qt/bin/qmake", "-query", "QT_VERSION"]),
    }
    environment = os.environ.copy()
    environment.update(
        {
            "QT_QPA_PLATFORM": "offscreen",
            "PHOTO_EFFECT_TEST_PRESERVE_RESOLUTION": "1",
            "PHOTO_EFFECT_TEST_NO_MASK": "1",
            "PHOTO_EFFECT_REVIEW_ONLY": "1",
            "PHOTO_EFFECT_TEST_SOURCE": str(source),
            "PHOTO_EFFECT_REVIEW_RADIUS": str(PARAMETERS["radius"]),
            "PHOTO_EFFECT_REVIEW_STRENGTH": str(PARAMETERS["strength"]),
            "PHOTO_EFFECT_REVIEW_WARMTH": str(PARAMETERS["warmth"]),
            "PHOTO_EFFECT_REVIEW_PROTECTION": str(PARAMETERS["protection"]),
        }
    )

    trials: list[dict[str, Any]] = []
    for index in range(args.runs):
        run_dir = scratch_dir / f"run-{index + 1:02d}"
        run_dir.mkdir(parents=True, exist_ok=True)
        run_environment = environment | {"PHOTO_EFFECT_EVIDENCE_DIR": str(run_dir)}
        started = time.perf_counter()
        completed = subprocess.run(
            ["/usr/bin/time", "-lp", str(binary)],
            cwd=Path(__file__).resolve().parents[2],
            env=run_environment,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        elapsed_seconds = time.perf_counter() - started
        log_path = output_dir / f"run-{index + 1:02d}.log"
        log_path.write_text(completed.stdout, encoding="utf-8")
        if completed.returncode:
            raise RuntimeError(f"F1 workflow run {index + 1} failed; inspect {log_path}")
        if "exact PNG export passed" not in completed.stdout:
            raise RuntimeError(f"F1 workflow run {index + 1} did not prove exact recipe-reopened export")
        metrics_path = run_dir / "diffusion-only-metrics.json"
        output_path = run_dir / "diffusion-only-export.png"
        if not metrics_path.is_file() or not output_path.is_file():
            raise RuntimeError(f"F1 workflow run {index + 1} did not write effect evidence")
        metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
        if (int(metrics["width"]), int(metrics["height"])) != (width, height):
            raise RuntimeError(f"F1 workflow run {index + 1} resized its source")
        trial = {
            "run": index + 1,
            "wallSeconds": elapsed_seconds,
            **parse_time_report(completed.stdout),
            "outputSha256": sha256(output_path),
            "changedPixelFraction": metrics["changedPixelFraction"],
            "meanAbsoluteChannelDelta8Bit": metrics["meanAbsoluteChannelDelta8Bit"],
            "maximumChannelDelta8Bit": metrics["maximumChannelDelta8Bit"],
            "workflowPassed": True,
        }
        trials.append(trial)
        print(
            f"run={index + 1} wall={elapsed_seconds:.3f}s "
            f"cpu={trial['userSeconds'] + trial['sysSeconds']:.3f}s "
            f"maxrss={trial['maximumResidentSetBytes']}B"
        )

    hashes = {trial["outputSha256"] for trial in trials}
    if len(hashes) != 1:
        raise RuntimeError("Repeated F1 CPU workflow outputs are not pixel-identical")
    wall_times = [float(trial["wallSeconds"]) for trial in trials]
    receipt = {
        "schema": "looksy-optical-diffusion-cpu-fallback-diagnostic-v1",
        "date": "2026-09-26",
        "diagnosticOnly": True,
        "qualifiesPlan": False,
        "scope": "Offscreen Qt Quick, full-resolution single photo layer, CPU output, recipe reopen and exact PNG export; no physical input, layer-rich reference scene, GPU execution, or p95 claim.",
        "model": "optical_diffusion:1",
        "parameters": PARAMETERS,
        "backend": "CPU; host Metal availability is not required or inferred by this test",
        "hardware": versions,
        "source": {
            "filename": ASSET,
            "sha256": asset["sha256"],
            "width": width,
            "height": height,
            "megapixels": width * height / 1_000_000,
        },
        "binary": {"path": str(binary), "sha256": sha256(binary)},
        "runs": trials,
        "wallSecondsSummary": {
            "minimum": min(wall_times),
            "median": statistics.median(wall_times),
            "maximum": max(wall_times),
            "sampleCount": len(wall_times),
        },
        "outputPixelsIdenticalAcrossRuns": True,
    }
    receipt_path = output_dir / "cpu-fallback-diagnostic.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"receipt={receipt_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"Optical Diffusion CPU diagnostic failed: {error}", file=sys.stderr)
        raise SystemExit(1)
