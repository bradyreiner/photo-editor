# Export submission and durable admission

Status: provisional, not accepted as a performance improvement. The focused
responsiveness gate and full functional regression pass, but total runtime misses
the comparison cap and the native nine-copy proof times out. The candidate remains
work in progress within the broader goal, not realtime rendering or completion.

## Contract and bounds

Submitting an export now creates a visible in-memory `Saving` request. Only a
successful selected-catalog write or discovery of the matching existing ID counts
as durable admission. A helper failure retains `Save unconfirmed`, without writing
a legacy fallback. Retry uses the same ID and must not reset completed work.
Queue methods return accepted request counts; callers observe `admissionFinished`
for confirmation. Agent reports distinguish submitted, pending and durably queued.

Admission runs on a serial worker pool, with at most 64 unconfirmed requests and
128 MiB of logical UTF-16 recipe payload per controller. That byte limit excludes
metadata and is not a total memory cap. Payloads outlive a closed controller;
application shutdown drains the pool before Qt application state is destroyed.
Normal window close is guarded while admissions are pending; failed saves require
retry or explicit acknowledgement of the unsaved-request loss risk.

The performance loop allows at most three candidates. The primary gate is
foreground submission below 50 ms with an intentionally slow helper and continued
UI heartbeats. All existing output assertions remain required. The baseline full
regression is 177.60 s and 485,524,416 bytes peak footprint; a conservative +10%
comparison cap is 195.36 s and 534,076,858 bytes. A slower functional pass does not
clear that performance cap. Different timer boundaries must be reported explicitly.

## Evidence so far

- Baseline `/private/tmp/export-admission-baseline.log`: 919 ms foreground
  submission, zero heartbeats, immediate Queued state.
- Candidate `/private/tmp/export-admission-final-focused.log`: 21 ms foreground
  submission, 117 heartbeats while finishing, immediate Saving state. The same
  focused test checks failed reads without fallback, same-ID retry, lost receipt
  preserving Completed, 64-request admission bound and detached payload lifetime.
- First full candidate missed the existing deferred-preview observation deadline.
  Retained PostgreSQL later showed that fixture Completed. No deadline was changed
  in response to that failure.
- Second full run `/private/tmp/export-admission-full-export-retry.log` exited 1
  in 217.52 s. Retained selected-catalog records show 18 verified Completed
  deliveries and one Queued, not a reported format/profile mismatch. Peak footprint
  was 485,295,016 bytes. This does not pass correctness or the total-time guard.
- The full test now distinguishes submission from catalog confirmation: admission
  must finish within 30 seconds; its unchanged 60-second delivery window then
  begins at the same durable-admission boundary as the previous synchronous API.
  Both phase times and total runtime remain recorded. Output assertions were not
  removed or relaxed; failure logging now prints the offending state and summary.
- First native attempt `/private/tmp/export-admission-native-NPxN9f` verified
  visible Saving text and a rendered close guard, then failed because the harness
  focused a photo without adding it to batch selection. Screenshots also exposed
  a misleading zero-job empty state during saving. The harness now selects its
  one fixture and the page presents pending rows separately from saved history.
  Forced exit exposed workers running after QApplication teardown; shutdown drain
  ordering addresses that lifetime defect. These corrections require another run.

## Follow-up results

- Build `/private/tmp/export-admission-ui-build.log` completed successfully. The
  latest focused run `/private/tmp/export-admission-shutdown-focused.log` passed
  at 43 ms foreground submission with 110 heartbeats during completion.
- Native run `/private/tmp/export-admission-native-final-1LkMxD` shows actual
  pending rows and the close dialog; both screenshots were visually inspected.
  The agent's real report transitions to submittedCount=1, queuedCount=1,
  admissionPending=false and durablyQueued=true. It nevertheless exits 1 at
  the 45-second stage-4 limit: eight verified Completed copy jobs and one Queued
  remain in the selected catalog. This is not a passing native end-to-end proof.
  Independent SHA-256 comparison of all eight completed files matches the source
  exactly (3d7c5bc2f6180c0ec496165442299525764e5385a72ab63b4f19c5057dae6904).
  The owned server on port 58503 was stopped and checked after readback.
- Full regression `/private/tmp/export-admission-full-final.log` exits 0, with
  every existing assertion passing at 227,043 ms and total time 227.80 s. Peak
  footprint is 485,671,872 bytes. Memory is within the comparison cap; runtime
  is 28.27% above the 177.60-second baseline and does NOT clear the +10% cap.
  Submission was at 153,425 ms, durable admission at 170,698 ms, verified delivery
  at 218,638 ms and final retry/all assertions at 227,043 ms. No latency claim is
  derived from the passing correctness result.
- A one-second sample of the verified live regression PID 50641 is retained at
  `/private/tmp/export-admission-candidate-sample.txt`. It observes foreground
  `JobRunner::handleAsyncJobFinished -> upsertPersistedJobRecord ->
  QProcess::waitForFinished`, alongside background history readers. This is a
  remaining foreground completion-write wait, not a full-run causal attribution
  of the runtime difference.
- Forced-exit logs still contain Qt application-path warnings from other detached
  history workers. Draining the admission pool does not establish safe lifetime
  ordering for all application workers. That broader shutdown defect remains open.
- An additional caller audit found the existing `agent.edit.export_queued` event
  should be emitted at durable confirmation, not removed or emitted at submission.
  The final callback now emits it only on successful confirmation, with a separate
  unconfirmed event on failure. The app and agent controller test rebuilt cleanly
  in `/private/tmp/export-admission-receipt-event-build.log`; that last log-event
  change has build proof only, not a rerun of the full agent test.

The diagnosed full-test database (port 49386) and first native database (57870)
were explicitly checked and stopped after inspection. All artifacts remain. No
user catalog was migrated, no original was modified and no model was downloaded.
The final full regression's owned database on port 60544 also reports stopped.
All tool sessions started for these builds/tests/inspections are terminal. Scoped
`git diff --check` reports no whitespace errors in the changed tracked sources.

## Remaining boundaries

Cross-process stale writes/CAS, authority changes during deferred work, a general
unsaved-request journal, full-application shutdown coordination and representative
large-batch latency are not established here. Source/recipe validation still runs
on the foreground thread. Native UI proof and final functional/performance results
must be recorded before accepting the change. Broader camera/color calibration,
RAW/JPEG realtime rendering, real 3D/4D quality, embedded inference/search and the
professional editing inventory remain governed by `active-app-completion-goal.md`.
