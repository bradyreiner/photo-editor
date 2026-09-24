# Single catalog resolution at startup — September 10, 2026

Startup checked the managed service with `ensureCatalogService`, then checked
readiness using `runCatalogHelper`. The latter prepares its own environment and
already resolves the managed service, so startup repeated the same helper round
trip before Library admission.

The startup worker now calls `probe_catalog_readiness`, which creates the required
runtime directories, resolves the managed environment once through
`runCatalogHelper`, and validates the health response. Explicit database URLs
still require a successful read probe. Job recovery remains after admission.
No catalog authority is cached or changed, and no timeout was increased.

## Baseline and acceptance

This continuation allowed up to three focused attempts, with at least 20%
improvement in the measured bottleneck and no more than 10% peak memory growth.
The first candidate passed. The test injects a 250 ms managed resolver and runs
the same readiness sequence on a Qt worker five times. The baseline used the
previous startup sequence before the production change was applied.

| Measurement | Before | After |
| --- | ---: | ---: |
| Median readiness time | 666 ms | 347 ms |
| Managed resolutions over five probes | 10 | 5 |
| Health queries | 5 | 5 |
| UI admission time | 0 ms | 0 ms |
| UI heartbeat ticks during probes | 358 | 217 |
| Peak process RSS | 20,086,784 B | 19,955,712 B |

The measured phase improves **47.90%**, with **0.65% less peak RSS**. Heartbeats
continue during the shorter operation. This is a controlled readiness benchmark,
not a claim that complete cold application startup improves by 48%. First-use
samples were 1,060 and 979 ms and include thread-pool/process initialization.

The regression also checks explicit authority passthrough, rejection of invalid
JSON and false or mistyped readiness values, a failing health process that prints
a successful-looking response, and managed-resolution failure before the health
query. The test uses temporary fake helpers and never touches the real catalog.

## Investigation and boundaries

A three-second sample of the current idle app showed the main thread waiting for
events in 2,358 of 2,426 samples. Both previously failing UI checks,
`editor_preview_feedback_test` and `job_runner_startup_test`, passed on the fresh
baseline without any assertion or timing-budget change. Their earlier failures
and the observed host contention remain relevant; this does not establish
interaction latency percentiles or claim all editing/thumbnail stalls fixed.

Evidence is local in `/private/tmp/framehouse-ui-next/`: `main-before.sample.txt`,
`ui-baseline.log`, `probe-baseline-host.log`, `probe-candidate.log`,
`probe-regression.log`, and the numerical gate in `probe-gate.json`.

## Reproduction

```sh
cmake --build --preset build-release-macos --target catalog_readiness_probe_test
build/release-macos/tests/catalog_readiness_probe_test
/usr/bin/time -l build/release-macos/tests/catalog_readiness_probe_test --measure
```

The memory measurement requires host access on macOS. The normal test covers
both success and failure cases; `--measure` runs the five successful probes.

## Final confirmation

The Release app built successfully. All four focused tests passed:
`catalog_readiness_probe_test`, `app_bootstrap_test`,
`editor_preview_feedback_test`, and `job_runner_startup_test` (18.57 seconds
total). No Rust code or test thresholds changed in this continuation.

The normal launcher reopened the app with the existing catalog and zero pending
or active saves. On this launch, desktop startup reported 777 ms, the QML route
31 ms, and the initial managed-resolution/health helper calls 195 and 170 ms.
Library thumbnail work began without a readiness failure. These live observations
confirm the path runs; they are not a paired whole-startup speed comparison.
See `final-build.log`, `final-tests.log`, `relaunch.log` and `relaunch.sample.txt`
in the evidence directory. Rendering-thread synchronization and thumbnail waits
remain outside this change's performance claim.
