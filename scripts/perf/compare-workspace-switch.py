#!/usr/bin/env python3
"""Fail closed on missing/foreground-ineligible real Main.qml route evidence."""
import argparse
import json
import math
import statistics
from pathlib import Path


PAGES = {"Library", "Edit", "Carousel", "Timeline", "Export", "Scene"}


def percentile(values, fraction):
    values = sorted(values)
    position = (len(values) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    return values[lower] + (values[upper] - values[lower]) * (position - lower)


def compare(before, after, before_manifest, after_manifest):
    failures, metrics = [], {}
    for name, receipt, manifest in (("before", before, before_manifest), ("after", after, after_manifest)):
        if receipt.get("schema") != "framehouse-workspace-switch-proof-v2" or receipt.get("setup") != "isolated-settings-onboarding-complete-auto-preview":
            failures.append(f"{name}: unsupported receipt")
        if receipt.get("eligible") is not True or receipt.get("error") != "":
            failures.append(f"{name}: failed or foreground-ineligible run")
        if receipt.get("cameraPreserved") is not True or receipt.get("scenePoints", 0) <= 0:
            failures.append(f"{name}: scene correctness evidence missing")
        if receipt.get("captures") or receipt.get("rapidNavigationChecked"):
            failures.append(f"{name}: visual/functional run must not be used as timing evidence")
        if manifest.get("target_events_met") is not True or manifest.get("scenario") != "perf-workspace-switch":
            failures.append(f"{name}: incomplete wrapper artifact")
        # The existing wrapper terminates a live app once its target event exists;
        # -15 is not a teardown proof. Receipt correctness is required separately.
        if manifest.get("process_returncode") not in (0, -15):
            failures.append(f"{name}: unexpected process exit")
        for key in ("peak_rss_kb",):
            if not isinstance(manifest.get(key), (int, float)) or manifest[key] <= 0:
                failures.append(f"{name}: missing {key}")
        rows = receipt.get("routes", [])
        keys = {(row.get("page"), row.get("iteration")) for row in rows}
        if len(keys) != len(rows) or ("Scene", -1) not in keys:
            failures.append(f"{name}: duplicate or missing first scene route")
        iterations = {row.get("iteration") for row in rows if row.get("iteration", -1) >= 0}
        if len(iterations) < 6 or iterations != set(range(len(iterations))):
            failures.append(f"{name}: requires initial visit and at least five warm rounds")
        for iteration in iterations:
            if {row.get("page") for row in rows if row.get("iteration") == iteration} != PAGES:
                failures.append(f"{name}: incomplete round {iteration}")
        for row in rows:
            if row.get("foreground") is not True or row.get("frameSwaps", 0) <= 0:
                failures.append(f"{name}: route was not foreground/presented")
            times = [row.get(key) for key in ("visibleMs", "inputReleasedMs", "presentedReadyMs")]
            if any(not isinstance(x, (int, float)) or not math.isfinite(x) or x < 0 for x in times):
                failures.append(f"{name}: invalid timing")
            elif times != sorted(times):
                failures.append(f"{name}: inconsistent readiness order")
    if before.get("scenePoints") != after.get("scenePoints") or before.get("qmlDiskCacheDisabled") != after.get("qmlDiskCacheDisabled"):
        failures.append("Workload or QML cache policy differs")
    def workload(manifest):
        command = manifest.get("command", [])
        selected = []
        for flag in ("--source", "--scene-project", "--iterations", "--timeout-ms"):
            if flag not in command or command.index(flag) + 1 >= len(command):
                return None
            selected.append(command[command.index(flag) + 1])
        return selected
    if workload(before_manifest) is None or workload(before_manifest) != workload(after_manifest):
        failures.append("Explicit source, scene, rounds or deadline differ or are missing")
    if failures:
        return {"passed": False, "failures": failures, "metrics": metrics}

    def metric(name, previous, candidate, ratio):
        passed = candidate <= previous * ratio
        metrics[name] = {"before": previous, "after": candidate,
                         "changePercent": (candidate / previous - 1) * 100 if previous else None,
                         "passed": passed}
        if not passed:
            failures.append(name + ": exceeds declared gate")

    warm_before = [r["presentedReadyMs"] for r in before["routes"] if r["iteration"] > 0]
    warm_after = [r["presentedReadyMs"] for r in after["routes"] if r["iteration"] > 0]
    if len(warm_before) != len(warm_after):
        failures.append("Warm sample counts differ")
    metric("warmMedianMs", statistics.median(warm_before), statistics.median(warm_after), .8)
    for page in sorted(PAGES):
        previous = [r["presentedReadyMs"] for r in before["routes"] if r["page"] == page and r["iteration"] > 0]
        candidate = [r["presentedReadyMs"] for r in after["routes"] if r["page"] == page and r["iteration"] > 0]
        metric(page + "WarmP95Ms", percentile(previous, .95), percentile(candidate, .95), 1.1)
        if page == "Scene":
            metric("sceneWarmMedianMs", statistics.median(previous), statistics.median(candidate), .8)
        first_before = next(r["presentedReadyMs"] for r in before["routes"] if r["page"] == page)
        first_after = next(r["presentedReadyMs"] for r in after["routes"] if r["page"] == page)
        metric(page + "FirstVisitMs", first_before, first_after, 1.1)
    for key in ("totalMs", "maxHeartbeatGapMs", "sceneLoadMs"):
        metric(key, before[key], after[key], 1.1)
    metric("peakRssKb", before_manifest["peak_rss_kb"], after_manifest["peak_rss_kb"], 1.1)
    return {"passed": not failures, "failures": failures, "metrics": metrics}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for argument in ("before", "after", "before-artifact", "after-artifact", "output"):
        parser.add_argument("--" + argument, type=Path, required=True)
    args = parser.parse_args()
    read = lambda path: json.loads(path.read_text())
    result = compare(read(args.before), read(args.after),
                     read(args.before_artifact / "manifest.json"), read(args.after_artifact / "manifest.json"))
    with args.output.open("x") as output:
        json.dump(result, output, indent=2)
        output.write("\n")
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
