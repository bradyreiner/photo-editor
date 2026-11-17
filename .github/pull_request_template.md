## Summary

-

## Validation

-

## Performance Contract

Complete this section for any change that can affect launch, import, library scroll, preview render, masking, timeline playback, export throughput, memory use, or background-job pressure.
For optimization claims, use `docs/performance-optimization-pr-evidence.md`.

- Metric:
- Before:
- After:
- Workload:
- Risk:
- Artifact or log path:

Use `scripts/perf/run-benchmark.py` and `scripts/perf/summarize-jsonl.py` when a reproducible benchmark is available. If the change should not affect performance, write `N/A` and explain why.

## Regression protection

- Regression contract/scenarios exercised:
- Save/reopen/export/recovery compatibility:
- Performance or memory budget changes and reason (no automatic baseline reset):
- Native proof run, or explicitly unqualified areas:

Changes to `tests/fixtures/editor-regression/contract.json`, regression runners,
or CI/release enforcement require explicit review of removed coverage, relaxed
limits, changed fixture hashes, and skip behavior. A passing offscreen run does
not establish visible responsiveness. See `docs/editor-regression-protection.md`.
