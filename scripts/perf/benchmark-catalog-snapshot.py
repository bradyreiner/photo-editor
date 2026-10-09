#!/usr/bin/env python3
"""Compare frozen catalog snapshot benchmarks without concurrent builds/tests."""

import argparse
import hashlib
import json
import statistics
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    binaries = {"before": args.before.resolve(), "after": args.after.resolve()}
    report = {
        "executable_sha256": {k: hashlib.sha256(v.read_bytes()).hexdigest() for k, v in binaries.items()},
        "gates": {
            "10000_readiness_ratio_max": 0.7,
            "1000_readiness_ratio_max": 1.15, "1000_readiness_allowance_ms": 10,
            "heartbeat_allowance_ms": 15,
            "rss_ratio_max": 1.1, "rss_allowance_bytes": 16 * 1024 * 1024,
            "exact_ids_and_recipes": True,
        },
        "cases": [], "failures": [],
    }
    for count in (1000, 10000):
        data = {"before": [], "after": []}
        for pair in range(3):
            order = ("before", "after") if pair % 2 == 0 else ("after", "before")
            for name in order:
                path = root / f"{count}-{pair}-{name}.json"
                with path.with_suffix(".log").open("w") as log:
                    subprocess.run([str(binaries[name]), str(path), str(count)],
                                   stdout=log, stderr=subprocess.STDOUT, check=True)
                run = json.loads(path.read_text())
                if run["count"] != count or len(run["samples"]) != 3:
                    raise ValueError("Unexpected fixture size or sample count")
                data[name].append(run)
                print(count, pair, name, "complete", flush=True)
        if len({run["payload_bytes"] for runs in data.values() for run in runs}) != 1:
            raise ValueError("Snapshot payloads differ in size")
        summary = {}
        for name, runs in data.items():
            summary[name] = {
                "ready_ms": statistics.median(statistics.median(s["ready_ms"] for s in r["samples"]) for r in runs),
                "max_gap_ms": max(s["max_heartbeat_gap_ms"] for r in runs for s in r["samples"]),
                "peak_rss_bytes": max(r["peak_rss_bytes"] for r in runs),
                "exact": all(r["exact_ids_and_recipes"] for r in runs),
            }
        before, after = summary["before"], summary["after"]
        gates = {
            "readiness": after["ready_ms"] <= before["ready_ms"] * (0.7 if count == 10000 else 1.15) + (0 if count == 10000 else 10),
            "heartbeat": after["max_gap_ms"] <= before["max_gap_ms"] + 15,
            "rss": after["peak_rss_bytes"] <= before["peak_rss_bytes"] * 1.1 + 16 * 1024 * 1024,
            "exact": before["exact"] and after["exact"],
        }
        row = {"count": count, **summary, "gates": gates}
        report["cases"].append(row)
        if not all(gates.values()):
            report["failures"].append(row)
    report["accepted"] = not report["failures"]
    (root / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print("accepted:", report["accepted"])
    return 0 if report["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
