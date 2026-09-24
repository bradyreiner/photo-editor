# Responsive, failure-safe active-job cancellation

## Reproduced defect and gates

Tracing the remaining synchronous job paths found that cancellation observation
treated a failed catalog read as an empty history. That could cancel an active
export even though nobody requested cancellation. The isolated helper regression
returned after **445 ms** and set the cancellation flag on a failed read:
`/private/tmp/job-cancellation-before.log`.

This bounded performance/correctness loop allows at most three attempts. Its
acceptance gates are request return below 100 ms, no cancellation on failed or
malformed reads, continued event-loop heartbeats, preserved genuine pause/cancel/
removal behavior, stale-attempt rejection, and at most one reader plus one pending
follow-up per runner. There is no RSS or whole-app realtime claim; bounded reader
count does not prove bounded history size. Export/loading/recovery regressions and
the isolated native error-state proof protect existing behavior.

## Implementation

Active-job cancellation observation uses a worker with a copied job ID and shared
attempt token. It captures no runner pointer. The owning thread publishes only
results whose ID **and token** still match: restarting the same job ID cannot let
an older cancellation read affect the replacement attempt. Cancellation stays
sticky within an attempt. A notification burst requests one follow-up read.

Read failures do not set the cancellation flag. They schedule a one-second retry;
a new job notification can expedite that retry. Already running helpers retain
their normal timeout. This changes observation latency, not renderer cancellation
check frequency or its cleanup/commit contract.

Observation passes the read-only legacy option. Legacy fallback records cannot
authorize cancellation when the catalog read failed. The shared history parser
now rejects non-object rows and records without IDs, rather than silently dropping
them and presenting malformed history as authoritative emptiness. A successful,
valid history that omits the active job still requests cancellation as before.

## Verification

Accepted on the first implementation attempt. The same controlled failed-helper
scenario returned in **0 ms**, did not cancel, and recorded **41** ten-millisecond
heartbeat ticks (37 on the final CTest rerun). The baseline was 445 ms with a false
cancellation. This is request-return improvement, not faster catalog access.
Controller destruction during a pending read returned in 0 ms without publication.
Evidence: `/private/tmp/job-cancellation-after.log`.

`job_runner_cancellation_test` additionally exercises automatic retry without a
new notification; invalid JSON, wrong top-level shape, non-object rows and missing
IDs; healthy Running, Canceling, Pausing, removal and metadata flags; unchanged
legacy bytes with no upsert; same-ID attempt replacement; sticky cancellation;
and 101 requests producing two reads. These tests run real controlled helper
processes against temporary paths, not a substituted production database.

All seven regressions passed across two runs:

- Full `export_job_controller_test`: 191.65 seconds. This includes a running
  2048-square edited export canceled cooperatively, no published output/integrity
  sidecar, and no hidden partial export left behind, plus ordinary exports and
  recovery/resume. Log: `/private/tmp/job-cancellation-export-regression.log`.
- Six focused cancellation, wake, export-history, queue-history, crash-recovery
  and Timeline scheduling-contract tests: 25.25 seconds. Log:
  `/private/tmp/job-cancellation-final-regressions.log`.

The scheduling source contract now recognizes the coalesced deferred wake timer;
actual wake/cancellation behavior is covered by runtime tests, not that string
check. Native app and test builds passed with no compiler warnings/errors found.
Logs: `/private/tmp/job-cancellation-build.log`,
`/private/tmp/job-cancellation-final-build.log`, and
`/private/tmp/job-cancellation-proof-timing-build.log`. `git diff --check` passes.

## Native proof and timing boundary

Final proof: `/private/tmp/framehouse-cancellation-final-9vBkVZ/`. The receipt
reports `ok: true`, both history readers finished with the expected timeout, and
the screenshot was visually inspected: unavailable history and preservation
guidance are visible with unobscured recovery controls. The private runtime
stopped, and port 56307 was independently confirmed closed. No user catalog,
originals or model weights were changed. Existing ExportView modelData warnings
remain in the final native log; this is not a warning-free UI claim.

The native timing receipt now records per-tick window activity/exposure and
separates observation from screenshot, model-plan/setup and runtime-stop work.
In the final 7,510 ms run, screenshot capture took **1,101 ms**, maximum observation
gap was **1,096 ms**, and the overall maximum was 1,105 ms. All 16 sampled ticks
had an exposed but **inactive** window (application state 2). Model setup checks
took 1 ms and the other measured capture stages rounded to 0 ms.

This clarifies, but does not solve, the previously reported roughly 1.1-second
"startup/presentation" gap: this timer starts after QML setup, includes proof
capture, and does not establish foreground input-to-photon latency or identify
the cause of the pre-capture gaps. Background scheduling is a hypothesis, not a
verified diagnosis. An earlier instrumented run during the export regression
lasted 15,548 ms and also displayed the first-run guide; it is retained at
`/private/tmp/framehouse-cancellation-native-J8rIef/`, not substituted for the
final screenshot. Foreground startup/interaction profiling remains necessary.

## Remaining scope

Runner startup recovery, idle queue scans, per-job dispatch reads, store mutations
and post-command history reloads still have synchronous paths. This change does
not resolve the separately measured startup/presentation stall, whole-app shutdown
latency, sustained full-catalog memory costs, or authoritative multi-process job
claiming. The full RAW, Gaussian 3D/4D, embedded-model and editing-tool requirements
remain open in `active-app-completion-goal.md`.
