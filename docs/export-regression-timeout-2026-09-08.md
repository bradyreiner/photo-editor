# Full export regression: corrected lifecycle and verified completion

## Outcome

The full `export_job_controller_test` binary passes with real managed PostgreSQL
after correcting its simulated restart lifecycle. The final run completed every
existing assertion in 176,944 ms and exited successfully in 177.60 seconds.
This is functional evidence, not realtime performance or a measured speedup.

Previous evidence was an outer CTest timeout at 180 seconds. That limit alone
did not identify a stalled export. The retained prior fixture had all 19 batch
delivery outputs, so the failure required more specific observations.

## Test corrections and diagnostics

The earlier startup correction explicitly initialized the original runner and
waited for durable recovery before creating work. This follow-up found that the
later simulated restart kept that original runner alive alongside the new one.
The test now destroys the idle original runner before creating interrupted
history, starts one replacement runner, waits for its recovery result, and only
then reads recovered history. This matches the app's lifecycle rather than
testing two active dispatchers unintentionally.

The test also prints its temporary root and phase times. Optional
`--keep-artifacts` retains the isolated fixture after normal shutdown. All
existing rendering, color/profile, cancellation, mask, recovery, collision,
delivery-integrity, and retry assertions remain. Per-operation deadlines were
not increased. The diagnostic run was allowed up to six minutes overall, but
finished below the earlier 180-second outer limit. One run near that limit is
not evidence of a stable 180-second CI performance budget.

## Evidence

- Build: `/private/tmp/export-timeout-diagnostic-build.log`, successful and no
  compiler warning/error diagnostics found.
- Full run: `/private/tmp/export-timeout-diagnostic-run.log`.
- Retained fixture:
  `/var/folders/qx/ck73lj3j7fl5tk_glls1rrq40000gn/T/export-controller-proof-gSSVGc`.
- Read-only one-second stack sample of the verified live test PID 4438:
  `/private/tmp/export-timeout-sample.txt`.

| Phase | Elapsed from test start |
| --- | ---: |
| Startup recovery ready | 2,420 ms |
| Enter simulated restart | 99,283 ms |
| Recovery and explicit resume verified | 103,804 ms |
| Delivery preflight begins | 111,942 ms |
| 19 ready delivery jobs queued | 129,414 ms |
| Delivery outputs and collision preservation verified | 171,351 ms |
| Failed-destination retry and all remaining assertions passed | 176,944 ms |

The delivery fixture contains 20 candidates: 19 existing 32x24 source images and
one missing original. Assertions check preflight blocks the missing original,
preserves a colliding file, completes the 19 ready deliveries with verified JPEG
format/sRGB profile/dimensions and SHA-256 sidecars, and retries a separate
unavailable-destination job without re-running successful jobs. Earlier sections
exercise the supported output-format/profile and masking paths; these are not
independent camera-calibration or real-RAW corpus tests.

The test's owned PostgreSQL server used port 54253. Its normal cleanup stopped
the server; post-run status reports unhealthy/stopped and both the recorded test
and PostgreSQL PIDs are gone. Artifacts remain. No user catalog or originals were
changed, and no model weights were downloaded.

## Confirmed next target and remaining boundaries

In the sampled interval, the main thread was in
`queueExportWithCropConstraint -> queuePlannedExportWithCropConstraint`.
267 of its 421 sampled stacks were in the synchronous `loadPersistedJobs`
history refresh, and another 68 were in `persistJob`. Both paths reach catalog
helpers and `QProcess::waitForFinished`; environment preparation also launches
the database URL resolver. This identifies a real foreground wait in export
admission. A one-second sample does not establish the full-run bottleneck
distribution or the size of a future improvement.

Next: asynchronous, durable export admission with bounded/coalesced history
updates, explicit completion/error state, and preserved caller/queue semantics.
Retain this full functional regression alongside a focused responsiveness proof.
Database-wide stale writes, concurrent-dispatch claims, explicit conflict
resolution, RAW fidelity/realtime rendering, full Gaussian 3D/4D quality,
embedded-model inference and professional editing completeness remain open.
