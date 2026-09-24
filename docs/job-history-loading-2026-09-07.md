# Non-blocking job-history refresh

Status: implementation, focused recovery tests and packaged visual verification
pass. This is not
completion of all loading or job-store responsiveness work.

## Verified defect

`JobQueueModel::refresh()` synchronously called the catalog job-list helper.
That helper has a five-second timeout, so refreshing job history during an
outage blocked the UI thread. The new `job_queue_loading_test` first reproduced
the original behavior with an isolated app-data directory and a deliberately
unavailable database at loopback port 1: **5,006 ms** before `refresh()` returned,
followed by the expected failed responsiveness assertion.
Evidence: `/private/tmp/job-history-loading-before.log`.

The test never starts or resets the user's catalog and does not read user
originals. A database URL selects authority; this change does not migrate or
switch that authority. Existing legacy-history fallback behavior in the store
remains in place.

## Changes

- Ordinary `refresh()` and its job-change notification path now read on a
  worker. The initial standalone model recovery/read also runs on that worker.
- One reader is active per model. A burst of refresh requests schedules at most
  one follow-up read, and a superseded snapshot is discarded before publication.
- The worker owns a copied reader function and no model pointer. Qt model/reset
  and signal work remains on the model's thread; model destruction does not wait
  for a blocked read or leave a worker referring to the deleted model.
- `loading` and `historyError` expose progress and failure. Failed reads preserve
  the displayed history; a later valid empty response can legitimately clear it.
  If an initial failed catalog read supplies existing legacy history, that
  history can still be displayed with the error rather than silently lost.
- The existing store API optionally returns read/parse errors, without changing
  the default return contract for its other callers. Invalid non-array history
  is distinguished from a valid empty list.
- Export's empty state now distinguishes loading, unavailable history and a
  genuinely empty completed read. Its existing subtitle shows detailed status.
- Startup uses the runner's recovery pass, then asks the queue model to refresh.
  It no longer launches a second recovery pass that could race runner dispatch.

An early broad async version also changed the immediate post-mutation model
contract. The existing recovery test caught that: it reads progress immediately
after a confirmed resume. The final change preserves synchronous private reloads
for current job-control APIs and invalidates any older background snapshot before
those reloads publish. Making mutations themselves asynchronous requires explicit
completion contracts and caller changes, not silently weakening their semantics.

## Regression scope

The new loading test exercises the real unavailable catalog, requires `refresh()`
to return within 100 ms, and requires continued 10 ms UI timer ticks while the
backend is unavailable. Injected readers additionally verify:

- 100 overlapping requests coalesce into two serial reads;
- the obsolete intermediate record is never published;
- failed refresh preserves an existing row;
- successful retry clears the error, including a legitimate empty result;
- deleting a model during a blocked recovery read returns within 100 ms, and
  the owned worker finishes safely afterward.

The existing queue and forced-crash/relaunch tests now wait for asynchronous
history refresh/initialization before checking rows. Immediate job-control
assertions remain meaningful because their synchronous contract is preserved.
The tests use isolated catalog/runtime roots, not the user's catalog connection.

## Packaged proof entry point

```sh
build/release-macos/apps/desktop/Framehouse.app/Contents/MacOS/Framehouse \
  --managed-runtime-proof-dir NEW_EMPTY_TEMP_DIRECTORY \
  --job-history-workspace-proof
```

This reuses the isolated packaged-runtime proof: separate catalog/preferences/
cache/model-store paths, deliberately unavailable catalog, no downloaded model
weights, and an app-owned runtime that is stopped before completion. The new
flag opens Export, explicitly refreshes history, waits for the read to finish,
and requires the actual QML title to say `Job history unavailable`. Its receipt
contains the history error/loading/title fields and existing event-loop metrics.

## Final evidence

- Final `job_queue_loading_test`: request returned in **0 ms**, with **500**
  10 ms heartbeat ticks while the real helper reached its outage timeout. The
  coalescing, stale-result, preservation, retry and destruction checks all pass.
  The timing is the request's main-thread return time, not completed I/O latency.
- All three focused tests pass in 55.13 seconds: `job_queue_model_test`,
  `job_queue_loading_test`, and `crash_recovery_relaunch_test`.
  `/private/tmp/job-history-final-regressions.log` records the final run.
- The rebuilt packaged app exits successfully with proof `ok: true`:
  `/private/tmp/framehouse-job-history-native-5RO1fY/receipt.json`.
  `jobHistoryLoading` is false, the error reports the helper timeout, and the
  actual QML title is `Job history unavailable`.
- `startup.png` in that directory was visually inspected: Export shows the
  unavailable-history message and preserved-history guidance, with catalog
  recovery controls unobscured above it. The empty list is not labeled as a
  successful no-jobs result. This run uses an empty isolated catalog; retaining
  populated history across failure is covered by the controller regression.
- Native app receipt: 16 event-loop timer ticks, maximum gap **1,031 ms**.
  That remaining startup/presentation gap is not fixed by this history-read
  change and is not a realtime pass. Existing unrelated QML undefined-property
  warnings also remain in `/private/tmp/job-history-native-proof.log`.
- Selected final build log `/private/tmp/job-history-complete-build.log` has no
  compiler warnings/errors, and scoped `git diff --check` passes. This is not a
  full-suite or clean-runtime-warning claim.

## Remaining work

- `ExportJobController` history reads, runner selection/recovery/control paths,
  and queue mutations/private reloads still call synchronous store APIs.
- The bootstrap outage guard still needs broader healthy/slow-catalog startup
  coverage; this focused refresh test cannot prove the app's startup budget.
- Model destruction is non-blocking, but an already running helper remains
  bounded by its normal timeout; no claim of immediate helper cancellation or
  immediate whole-app shutdown is made.
- Large-catalog paging, bounded history snapshot memory, import/asset switching,
  cold/warm loading and foreground input-to-photon proof remain open.
- RAW/realtime, embedded inference, Gaussian 4D/film and full professional editing
  requirements remain in `active-app-completion-goal.md`.
