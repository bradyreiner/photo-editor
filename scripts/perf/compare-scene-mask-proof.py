#!/usr/bin/env python3
"""Gate Scene mask foreground admission without claiming whole-app realtime."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


def read_proof(directory):
    receipt = json.loads((directory / "receipt.json").read_text())
    log = (directory / "launch.log").read_text()
    memory = re.search(r"^\s*(\d+)\s+peak memory footprint\s*$", log, re.MULTILINE)
    if not memory:
        raise ValueError(f"Missing native memory measurement: {directory}")
    events = [json.loads(line) for path in (directory / "runtime/logs/runs").glob("*/desktop.jsonl")
              for line in path.read_text().splitlines()]
    timings = {}
    for operation in ("mask_stack", "active_mask_exposure"):
        samples = [event["fields"]["elapsed_ms"] for event in events
                   if event.get("event") == "preview.mask_stack_change.completed"
                   and event["fields"].get("operation") == operation]
        if len(samples) != 1 or samples[0] <= 0:
            raise ValueError(f"Expected one measured {operation} action: {directory}")
        timings[operation] = samples[0]
    samples = receipt["eventLoopTickSamples"]
    proof = receipt["scenePhotoEditProof"]
    result = {
        "ok": receipt["ok"] and all(proof[key] for key in (
            "ok", "normalCatalogEditor", "neutralPixelsAndProfileExact",
            "maskAndColorEditSaved", "undoPixelsAndProfileExact")) and proof["undoCount"] == 3,
        "foreground": bool(samples) and all(sample["windowActive"] and sample["windowExposed"]
            and sample["applicationState"] == 4 for sample in samples),
        "final_outline_ready": proof.get("maskOutlineReady", False),
        "admission_ms": timings,
        "elapsed_ms": receipt["elapsedMs"],
        "maximum_tick_gap_ms": receipt["maxEventLoopTickGapMs"],
        "peak_memory_bytes": int(memory.group(1)),
        "image_hashes": {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in (
            "scene-to-edit.png", "scene-edit-neutral.png", "scene-edit-masked.png", "scene-edit-undo.png")},
    }
    if result["elapsed_ms"] <= 0 or result["peak_memory_bytes"] <= 0:
        raise ValueError(f"Invalid elapsed/memory measurement: {directory}")
    return result


def compare(before, after):
    gates = {
        "native_fidelity_and_undo_pass": bool(before["ok"] and after["ok"]),
        "matching_foreground": before["foreground"] and after["foreground"],
        "candidate_final_outline_ready": after["final_outline_ready"],
        "each_mask_action_at_least_30_percent_faster": all(
            after["admission_ms"][key] <= value * 0.7 for key, value in before["admission_ms"].items()),
        "whole_proof_regression_at_most_10_percent": after["elapsed_ms"] <= before["elapsed_ms"] * 1.1,
        "peak_memory_regression_at_most_10_percent": after["peak_memory_bytes"] <= before["peak_memory_bytes"] * 1.1,
        "all_four_output_files_exact": before["image_hashes"] == after["image_hashes"],
        "original_deadline_preserved": after["elapsed_ms"] <= 120000,
    }
    return {"baseline": before, "candidate": after, "gates": gates, "accepted": all(gates.values())}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    args = parser.parse_args()
    result = compare(read_proof(args.baseline), read_proof(args.candidate))
    print(json.dumps(result, indent=2))
    return 0 if result["accepted"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Invalid proof: {error}", file=sys.stderr)
        sys.exit(2)
