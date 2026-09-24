#!/usr/bin/env python3
"""Gate paired editor_warm_asset_loading_test receipts and sibling time -l logs."""
import json
import math
from pathlib import Path
import re
import statistics
import sys


def read(path):
    path = Path(path)
    receipt = json.loads(path.read_text())
    samples = receipt["samples"]
    if len(samples) != 12 or not receipt["pixelsPassed"]:
        raise ValueError("expected twelve validated warm cache selections")
    for sample in samples:
        if not sample["cacheHit"] or not re.fullmatch(r"[0-9a-f]{64}", sample["rgbaSha256"]):
            raise ValueError("invalid cache hit or pixel digest")
        for field in ("admissionMs", "readyMs"):
            if not math.isfinite(sample[field]) or sample[field] <= 0:
                raise ValueError("invalid timing")
    rss = re.search(r"(\d+)\s+maximum resident set size", path.with_suffix(".log").read_text())
    if rss is None or int(rss[1]) <= 0:
        raise ValueError("missing host peak RSS")
    ready = sorted(sample["readyMs"] for sample in samples)
    return receipt, {
        "admissionMedianMs": statistics.median(s["admissionMs"] for s in samples),
        "readyMedianMs": statistics.median(ready),
        "readyP95Ms": ready[math.ceil(len(ready) * .95) - 1],
        "peakRssBytes": int(rss[1]),
    }


def main():
    if len(sys.argv) != 3:
        raise ValueError("usage: compare-warm-asset-loading.py BEFORE.json AFTER.json")
    before, b = read(sys.argv[1])
    after, a = read(sys.argv[2])
    improvement = 100 * (1 - a["admissionMedianMs"] / b["admissionMedianMs"])
    gates = {
        "sameDimensions": all(before[k] == after[k] for k in ("width", "height")),
        "identicalPixels": [s["rgbaSha256"] for s in before["samples"]]
        == [s["rgbaSha256"] for s in after["samples"]],
        "asyncAndLatestSelection": after["asyncPassed"] is True,
        "admissionImprovementAtLeast15Percent": improvement >= 15,
        "readyMedianWithin20Percent": a["readyMedianMs"] <= b["readyMedianMs"] * 1.2,
        "readyP95Within20Percent": a["readyP95Ms"] <= b["readyP95Ms"] * 1.2,
        "peakRssWithin10Percent": a["peakRssBytes"] <= b["peakRssBytes"] * 1.1,
    }
    accepted = all(gates.values())
    print(json.dumps({"before": b, "after": a, "admissionImprovementPercent": improvement,
                      "gates": gates, "accepted": accepted}, indent=2))
    return 0 if accepted else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
