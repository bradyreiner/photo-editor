# Non-blocking export-controller history

The export controller's startup and job-notification reads now run off the GUI
thread. This removes another measured five-second catalog-outage stall; it does
not complete loading, job dispatch/mutation responsiveness or realtime rendering.

## Reproduction and change

The original `ExportJobController::initializeFromStore()` returned after
**5,004 ms** against an isolated unavailable loopback catalog. The new focused
test failed its 100 ms request-return gate before the patch. Native baseline:
`/private/tmp/export-history-before-native.log`. A sandbox-only attempt returned
in 7 ms because it did not reproduce the native helper timeout; that is not used
as the baseline.

Initialization and `jobsChanged` notifications now schedule one copied reader
on a worker. Repeated requests coalesce into one follow-up read; an intermediate
snapshot superseded by a notification or synchronous mutation is not published.
Publication, busy-state updates and signals stay on the controller's thread.
The worker captures no controller pointer, so destroying a controller does not
wait for a blocked read or expose it to later worker access.

`loading` and `historyError` expose the asynchronous completion/error contract.
Failures and reader exceptions preserve existing jobs and their busy state.
A successful empty response can legitimately clear the history. Legacy fallback
records can still populate an initially empty controller. Status distinguishes
loading and unavailable history from a successful empty queue.

Private post-command reloads remain synchronous so confirmed queue/retry/remove
actions still expose their immediate snapshot. They supersede any older worker
result. This patch does not silently turn those command APIs into fire-and-forget
operations, nor claim they are now non-blocking.

The store's existing load API can also migrate legacy records. The new optional
`migrate_legacy` argument defaults to the previous behavior for existing callers;
the export reader passes false. Background observation therefore does not upsert
legacy jobs or clear their file while recovery is working. Startup recovery still
owns migration. No catalog authority, schema or user-data migration is changed.

## Verification

The larger regression exposed a second issue before final validation: completed-job
removal posts many job-change notifications, and each notification previously
scheduled a runner scan plus a separate active-cancellation history read. A native
sample captured blocking helper calls in `clearFinishedJobs()`; recovery history
remained unpublished while the GUI's nominal ten-second observation loop took
15,874 ms. The error was empty and `loading` remained true, rather than a failed
render or corrupt history. Evidence: `/private/tmp/export-history-test-sample.txt`
and `/private/tmp/export-history-recovery-diagnostic.log`.

The runner now uses a single-shot wake timer: bursts coalesce, cancellation is
observed in the same scheduled pass, and a fresh notification expedites a deferred
120 ms scan without multiplying callbacks. Individual scans and store mutations
remain synchronous. `job_runner_wake_test` requires three 100-notification bursts
to produce exactly three helper list calls, including active cancellation and
deferred-scan expediting.

- Focused native loading test: initialization returned in **0 ms**, with **500**
  10 ms heartbeat ticks during the five-second helper outage. It verifies 100
  notifications coalesce to two reads, obsolete records never publish, signals
  stay on the GUI thread, failures/exceptions preserve history and active state,
  retry clears the error/empty result, immediate mutation snapshots win, and
  controller destruction returns within 100 ms while its owned reader completes.
- A controlled temporary helper records the actual commands. Background
  initialization issued only `list` and left the legacy JSON byte-identical.
  This is an API side-effect test, not a substituted outage benchmark.
- Logs: `/private/tmp/export-history-after.log` and
  `/private/tmp/export-history-regressions.log`; the detailed focused output is
  also in CTest's `Testing/Temporary/LastTest.log`.
- The larger export test now waits explicitly for asynchronous recovered history.
  Its existing immediate mutation and rendered export assertions remain. It uses
  explicit temporary app/cache roots, drops an inherited catalog URL, and stops
  its own temporary managed catalog before removing its test directory.
- Its old three-second render/history watchdog failed both the async run and an
  explicit synchronous-notification replay: the latter still had the photo job
  Running in the catalog. This functional watchdog is now 15 seconds; it is not
  realtime evidence. The dedicated 100 ms UI-return gate is unchanged. Replay
  log: `/private/tmp/export-history-sync-notification-baseline.log`.
- Final combined app/runtime/test build:
  `/private/tmp/export-history-coalesced-build.log`.
  No compiler warnings/errors were found; `git diff --check` passes.
- Final regression run: **5/5 passed** in 219.10 seconds: export history loading,
  runner wake coalescing, job queue loading, crash recovery/relaunch and the full
  export controller test. The runner test observed 300 wakeups producing exactly
  three scans; the export controller test passed in 198.89 seconds. Log:
  `/private/tmp/export-history-final-regressions.log`. Existing invalid-primaries
  `QColorSpace` warnings remain in the export test; this is not clean color-space
  validation or a realtime rendering result.

## Packaged native proof

`--job-history-workspace-proof` now initializes both history readers and requires
both to finish with an error against the isolated unavailable catalog. The
receipt captures export-history loading/error/status alongside the actual QML
job-history title. Proof root:
`/private/tmp/framehouse-history-final-native-OK65gB/`.

The receipt has `ok: true`, `jobHistoryProof: true`,
`exportHistoryLoading: false`, and the expected helper-timeout error. The
`startup.png` screenshot was visually inspected: Export says **Job history
unavailable**, explains preservation/reconnection, and leaves the catalog-recovery
controls visible. The private model runtime stopped and port 55744 was
independently confirmed closed. No model weights or user originals were touched.
Unrelated `StudioInspectorGroup.qml` undefined-QString warnings remain in the
native log; the run is not claimed to be warning-free.

The native run lasted 6,934 ms and had 12 event-loop samples; the maximum gap
was **1,145 ms**. That remaining startup/presentation gap is explicitly unresolved.
The focused request-return measurement does not establish whole-app startup or
foreground input-to-photon performance, especially during a concurrent test build.

## Remaining boundaries

Follow-up: `job-cancellation-loading-2026-09-07.md` moves active cancellation
observation off-thread, fixes false cancellation on failed reads, and qualifies
the native timing measurement with capture costs and inactive-window evidence.
The list below records the boundaries at this original export-history change.

Runner recovery, queue selection, cancellation observation, store mutations and
private post-command reloads still have synchronous paths. Already running
helpers retain their normal timeout; controller destruction is tested separately
from whole-app shutdown. Sustained notification traffic, large-history paging,
snapshot memory and slow/healthy production-catalog distributions need further
proof. The full RAW, loading, Gaussian 4D/film, embedded inference and editing-tool
requirements remain in `active-app-completion-goal.md`.
