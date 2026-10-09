#!/usr/bin/env python3
"""Compare two frozen effect benchmarks; run without concurrent builds or tests."""

import argparse
import hashlib
import json
import platform
import statistics
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, required=True)
    parser.add_argument("--after", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executables = {"before": args.before.resolve(), "after": args.after.resolve()}
    report = {
        "platform": platform.platform(),
        "executable_sha256": {
            name: hashlib.sha256(path.read_bytes()).hexdigest()
            for name, path in executables.items()
        },
        "gates": {
            "tiled_diffusion_2048_ratio_max": 0.8,
            "per_case_ratio_max": 1.15,
            "per_case_allowance_ms": 1,
            "rss_ratio_max": 1.1,
            "rss_allowance_bytes": 8 * 1024 * 1024,
            "exact_hashes": True,
        },
        "cases": [], "memory": [], "failures": [],
    }
    for width in (2048, 1024, 127):
        runs = {"before": [], "after": []}
        for pair in range(3):
            order = ("before", "after") if pair % 2 == 0 else ("after", "before")
            for name in order:
                path = args.output / f"{width}-{pair}-{name}.json"
                with path.with_suffix(".log").open("w") as log:
                    subprocess.run([str(executables[name]), str(path.resolve()), str(width), "3"],
                                   stdout=log, stderr=subprocess.STDOUT, check=True)
                data = json.loads(path.read_text())
                if data["width"] != width or data["height"] != width * 2 // 3 + 1:
                    raise ValueError("Benchmark dimensions do not match request")
                runs[name].append(data)
                print(width, pair, name, "complete", flush=True)
        keys = [(c["effect"], c["edge"], c["tiled"]) for c in runs["before"][0]["cases"]]
        for run in runs["before"] + runs["after"]:
            if keys != [(c["effect"], c["edge"], c["tiled"]) for c in run["cases"]]:
                raise ValueError("Benchmark case inventories differ")
        for index, key in enumerate(keys):
            cases = {name: [run["cases"][index] for run in values] for name, values in runs.items()}
            for values in cases.values():
                if any(len(case["ms"]) != 3 for case in values):
                    raise ValueError("Expected three calls per case and process")
            medians = {name: statistics.median(statistics.median(c["ms"]) for c in values)
                       for name, values in cases.items()}
            row = {"width": width, "effect": key[0], "edge": key[1], "tiled": key[2],
                   "before_ms": medians["before"], "after_ms": medians["after"],
                   "hash_exact": len({c["hash"] for values in cases.values() for c in values}) == 1}
            row["latency_pass"] = medians["after"] <= medians["before"] * 1.15 + 1
            if not row["hash_exact"] or not row["latency_pass"]:
                report["failures"].append(row)
            if width == 2048 and key == ("optical_diffusion", 1, True):
                report["primary_pass"] = medians["after"] <= medians["before"] * 0.8
            report["cases"].append(row)
        peaks = {name: max(run["peakRssBytes"] for run in values) for name, values in runs.items()}
        memory = {"width": width, **peaks,
                  "passed": peaks["before"] > 0 and peaks["after"] > 0 and
                  peaks["after"] <= peaks["before"] * 1.1 + 8 * 1024 * 1024}
        report["memory"].append(memory)
        if not memory["passed"]:
            report["failures"].append(memory)
    report["accepted"] = report.get("primary_pass", False) and not report["failures"]
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print("accepted:", report["accepted"])
    return 0 if report["accepted"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
