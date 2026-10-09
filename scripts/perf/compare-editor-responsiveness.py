#!/usr/bin/env python3
"""Compare native --responsiveness-proof-dir artifacts and their time -l logs."""

import argparse
import json
import re
import statistics
from pathlib import Path


def summarize(directory):
    report = json.loads((directory / "responsiveness.json").read_text())
    metrics = {}
    for action in ("delete_adjustment_layer", "pixel_brush_commit", "append_video"):
        rows = [row for row in report["samples"] if row["action"] == action]
        # The first video add includes cold probing; compare warm adds separately.
        if action == "append_video":
            rows = [row for row in rows if row["iteration"] > 0]
        if not rows:
            raise ValueError(f"Missing samples: {directory}: {action}")
        metrics[action] = {
            key: statistics.median(row[key] for row in rows)
            for key in ("call_ms", "first_frame_ms", "settled_ms", "heartbeat_max_gap_ms")
        }
    log = directory.with_suffix(".log")
    rss = re.search(r"(\d+)\s+maximum resident set size", log.read_text()) if log.exists() else None
    return report, metrics, int(rss[1]) if rss else None


def summarize_runs(directories):
    runs = [summarize(directory) for directory in directories]
    metrics = {
        action: {key: statistics.median(run[1][action][key] for run in runs)
                 for key in runs[0][1][action]}
        for action in runs[0][1]
    }
    rss = statistics.median(run[2] for run in runs) if all(run[2] is not None for run in runs) else None
    return [run[0] for run in runs], metrics, rss


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--before", type=Path, nargs="+", required=True)
    parser.add_argument("--after", type=Path, nargs="+", required=True)
    parser.add_argument("--action", choices=("delete_adjustment_layer", "pixel_brush_commit", "append_video"), required=True)
    parser.add_argument("--minimum-improvement", type=float, default=20.0)
    parser.add_argument("--regression-cap", type=float, default=20.0)
    parser.add_argument("--regression-floor-ms", type=float, default=2.0,
                        help="Ignore timing changes below this absolute timer/noise tolerance.")
    args = parser.parse_args()
    before, bm, brss = summarize_runs(args.before)
    after, am, arss = summarize_runs(args.after)
    reports = before + after
    improvement = 100 * (1 - am[args.action]["call_ms"] / bm[args.action]["call_ms"])
    guards = {}
    guard_failures = []
    for action in bm:
        for key in bm[action]:
            if action == args.action and key == "call_ms":
                continue
            name = f"{action}.{key}"
            guards[name] = 100 * (am[action][key] / bm[action][key] - 1)
            if guards[name] > args.regression_cap and am[action][key] - bm[action][key] > args.regression_floor_ms:
                guard_failures.append(name)
    if brss and arss:
        guards["rss"] = 100 * (arss / brss - 1)
        if guards["rss"] > args.regression_cap:
            guard_failures.append("rss")
    correctness = {
        "native_cocoa_metal": all(r.get("platform") == "cocoa" and r.get("metal_scenegraph") for r in reports),
        "undo_redo_exact": all(r.get("paint_recipe_undo_redo_exact") for r in reports),
        "export_pixels_exact": all(r.get("paint_export_pixel_sha256") for r in reports) and len({r.get("paint_export_pixel_sha256") for r in reports}) == 1,
        "recovery_journals_nonempty": all(r.get("recovery_journal_bytes", 0) > 0 for r in reports),
        "rss_available": brss is not None and arss is not None,
    }
    accepted = all(correctness.values()) and improvement >= args.minimum_improvement and not guard_failures
    print(json.dumps({"before": bm, "after": am, "call_improvement_percent": improvement,
                      "guard_regressions_percent": guards, "correctness": correctness,
                      "guard_failures": guard_failures,
                      "thresholds": {"minimum_improvement_percent": args.minimum_improvement,
                                     "regression_cap_percent": args.regression_cap,
                                     "timing_noise_tolerance_ms": args.regression_floor_ms},
                      "accepted": accepted}, indent=2))
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
