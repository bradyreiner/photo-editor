#!/usr/bin/env python3
"""Gate the bounded native layer-inspector optimization, not overall app readiness."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys


def read_proof(directory):
    receipt = json.loads((directory / "receipt.json").read_text())
    log = (directory / "launch.log").read_text()
    footprint = re.search(r"^\s*(\d+)\s+peak memory footprint\s*$", log, re.MULTILINE)
    if not footprint:
        raise ValueError(f"Missing native peak memory measurement: {directory}")
    layer = receipt["placedLayerProof"]
    samples = receipt["eventLoopTickSamples"]
    result = {
        "ok": receipt["ok"] and layer["ok"],
        "redo_ms": layer["redoElapsedMs"],
        "undo_ms": layer["undoElapsedMs"],
        "elapsed_ms": receipt["elapsedMs"],
        "peak_memory_bytes": int(footprint.group(1)),
        "inspector_undo_redo": layer.get("layerSnapshotUndoRedo", False),
        "foreground_active": bool(samples) and all(
            sample["windowActive"] and sample["windowExposed"] and sample["applicationState"] == 4
            for sample in samples),
        "export_sha256": hashlib.sha256(Path(layer["layerExportPath"]).read_bytes()).hexdigest(),
    }
    for metric in ("elapsed_ms", "peak_memory_bytes"):
        if result[metric] <= 0:
            raise ValueError(f"Invalid {metric}: {directory}")
    if result["redo_ms"] < 0 or result["undo_ms"] < 0:
        raise ValueError(f"Invalid history timing: {directory}")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    args = parser.parse_args()
    before, after = read_proof(args.baseline), read_proof(args.candidate)
    if before["redo_ms"] <= 0:
        raise ValueError("Baseline redo must exceed timer resolution")
    gates = {
        "both_native_proofs_pass": bool(before["ok"] and after["ok"]),
        "matching_foreground_execution": before["foreground_active"] and after["foreground_active"],
        "redo_at_least_80_percent_faster": after["redo_ms"] <= before["redo_ms"] * 0.2,
        "whole_proof_regression_at_most_10_percent": after["elapsed_ms"] <= before["elapsed_ms"] * 1.1,
        "peak_memory_regression_at_most_10_percent": after["peak_memory_bytes"] <= before["peak_memory_bytes"] * 1.1,
        "original_deadline_preserved": after["elapsed_ms"] <= 45000,
        "inspector_follows_undo_redo": bool(after["inspector_undo_redo"]),
        "export_bytes_unchanged": before["export_sha256"] == after["export_sha256"],
    }
    print(json.dumps({"baseline": before, "candidate": after, "gates": gates,
                      "accepted": all(gates.values())}, indent=2))
    return 0 if all(gates.values()) else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"Invalid proof: {error}", file=sys.stderr)
        sys.exit(2)
