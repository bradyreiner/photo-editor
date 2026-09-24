#!/usr/bin/env python3
"""Gate paired legacy_preview_locality_driver runs, including exact PNG bytes."""
import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import sys


def load(root):
    samples = sorted(root.glob("run-*/receipt.json"))
    if len(samples) < 3:
        raise ValueError("at least three completed runs are required")
    result = []
    for path in samples:
        receipt = json.loads(path.read_text())
        if not receipt["passed"] or receipt["localTemperature"]["protectedChangedPixels"] != 0:
            raise ValueError("pixel protection failed")
        if not receipt["missingReferenceRejected"] or not receipt["diskReferenceRecipeIsolationPassed"]:
            raise ValueError("reference isolation failed")
        local_ms = receipt["localTemperature"]["renderMs"]
        compare_ms = receipt["stages"][2]["renderMs"]
        if not all(math.isfinite(value) and value > 0 for value in (local_ms, compare_ms)):
            raise ValueError("invalid render timing")
        match = re.search(r"(\d+)\s+maximum resident set size", path.parent.with_suffix(".log").read_text())
        if not match or int(match[1]) <= 0:
            raise ValueError("missing host peak memory")
        images = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(path.parent.glob("*.png"))}
        if len(images) != 8:
            raise ValueError("missing reference or local-edit PNG")
        result.append({"localMs": local_ms, "compareMs": compare_ms, "peakBytes": int(match[1]),
                       "contract": [receipt[k] for k in ("width", "height", "renderingModel", "fullResolution")],
                       "images": images})
    return result


def main():
    before, after = (load(Path(arg)) for arg in sys.argv[1:])
    def median(rows, key):
        return statistics.median(row[key] for row in rows)
    improvement = 1 - median(after, "localMs") / median(before, "localMs")
    gates = {
        "sameRunsAndPixels": len(before) == len(after) and all(
            a["contract"] == b["contract"] and a["images"] == b["images"] for a, b in zip(before, after)),
        "localMedianAtLeast15PercentFaster": improvement >= 0.15,
        "comparisonMedianWithin5Percent": median(after, "compareMs") <= median(before, "compareMs") * 1.05,
        "peakMemoryWithin10Percent": max(r["peakBytes"] for r in after) <= max(r["peakBytes"] for r in before) * 1.10,
    }
    print(json.dumps({"accepted": all(gates.values()), "gates": gates, "improvementPercent": improvement * 100,
                      "before": before, "after": after}, indent=2))
    return 0 if all(gates.values()) else 1


if __name__ == "__main__":
    try:
        if len(sys.argv) != 3:
            raise ValueError("usage: compare-raw-local-edit.py BEFORE_ROOT AFTER_ROOT")
        sys.exit(main())
    except (ValueError, KeyError, OSError, IndexError, TypeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
