#!/usr/bin/env python3
"""Gate real 24-edit preview sequences; timings exclude hashing/protected-region checks."""
import json
import math
from pathlib import Path
import re
import statistics
import sys


def read(root):
    receipt = json.loads((root / "output/receipt.json").read_text())
    rows = receipt["samples"]
    if receipt["scope"] != "real-preview-repeated-local-temperature" or not receipt["passed"]:
        raise ValueError("wrong or failed benchmark scope")
    if len(rows) != 24 or [r["frame"] for r in rows] != list(range(24)):
        raise ValueError("all 24 ordered edits are required")
    if len({r["rgbaSha256"] for r in rows}) != 24:
        raise ValueError("every edit must produce distinct pixels")
    for row in rows:
        if not math.isfinite(row["renderMs"]) or row["renderMs"] <= 0 or row["protectedChangedPixels"] != 0:
            raise ValueError("invalid timing or protected-pixel failure")
    log = (root / "render.log").read_text()
    peak = re.search(r"(\d+)\s+maximum resident set size", log)
    if not peak or int(peak[1]) <= 0:
        raise ValueError("host peak memory required")
    warm = sorted(r["renderMs"] for r in rows[1:])
    return receipt, {"coldMs": rows[0]["renderMs"], "warmMedianMs": statistics.median(warm),
                     "secondEditMs": rows[1]["renderMs"], "warmMaxMs": max(warm),
                     "warmP95Ms": warm[math.ceil(len(warm) * .95) - 1], "peakBytes": int(peak[1])}


def main():
    before, before_metrics = read(Path(sys.argv[1]))
    after, after_metrics = read(Path(sys.argv[2]))
    improvement = 1 - after_metrics["warmMedianMs"] / before_metrics["warmMedianMs"]
    gates = {
        "sameContract": all(before[k] == after[k] for k in ("width", "height", "fullResolution", "renderingModel")),
        "sameOrderedPixels": all(a["temperature"] == b["temperature"] and a["rgbaSha256"] == b["rgbaSha256"]
                                 for a, b in zip(before["samples"], after["samples"])),
        "warmMedianAtLeast15PercentFaster": improvement >= .15,
        "warmP95Within5Percent": after_metrics["warmP95Ms"] <= before_metrics["warmP95Ms"] * 1.05,
        "coldWithin20Percent": after_metrics["coldMs"] <= before_metrics["coldMs"] * 1.20,
        "peakMemoryWithin10Percent": after_metrics["peakBytes"] <= before_metrics["peakBytes"] * 1.10,
    }
    print(json.dumps({"accepted": all(gates.values()), "gates": gates, "improvementPercent": improvement * 100,
                      "before": before_metrics, "after": after_metrics}, indent=2))
    return 0 if all(gates.values()) else 1


if __name__ == "__main__":
    try:
        if len(sys.argv) != 3:
            raise ValueError("usage: compare-repeated-local-edit.py BEFORE_ROOT AFTER_ROOT")
        sys.exit(main())
    except (ValueError, OSError, KeyError, TypeError, IndexError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(2)
