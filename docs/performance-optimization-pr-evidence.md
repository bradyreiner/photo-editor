# Performance Optimization PR Evidence

Use this block in any PR that claims a performance, memory, worker, rendering,
masking, tool-latency, or UI responsiveness improvement. Fill every field; use
`n/a` only when the metric is unrelated to the change.

```md
## Performance Evidence

Metric:

Workload:

Before:

After:

Delta:

Current artifact:

Baseline artifact:

Command:

Risk:

Fallback/rollback:
```

Required evidence:

- `Metric` names the exact row from `operability-report.json` or
  `metrics.json`, such as `preview_render_p95_ms`,
  `mask_brush_feedback_p95_ms`, `worker_queue_wait_p95_ms`,
  `memory_growth_delta_kb`, or `qml_jank_frames_max`.
- `Workload` names the suite or benchmark scenario, such as
  `perf-edit-global-sliders`, `perf-mask-brush`, `perf-library-scroll`, or
  `local-dev`.
- `Before` and `After` include numeric values with units.
- `Delta` includes the absolute change and direction.
- `Current artifact` and `Baseline artifact` are local or CI artifact paths.
- `Command` is the exact command used to produce the current artifact.
- `Risk` states what could regress, such as image parity, stale render
  cancellation, cache growth, worker starvation, or QML jank.
- `Fallback/rollback` states what will be reverted or disabled if the metric
  regresses.

Recommended command shapes:

```sh
python3 -B scripts/perf/run-app-performance-suite.py --profile smoke
python3 -B scripts/perf/run-app-performance-suite.py --profile local-dev --baseline <artifact-dir>
python3 -B scripts/perf/run-app-performance-suite.py --profile release-proof --baseline <artifact-dir>
python3 -B scripts/perf/run-benchmark.py --scenario <perf-scenario> --app <driver-or-app> --event <event>
```
