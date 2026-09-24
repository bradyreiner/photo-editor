# Preserve history while job recovery is unresolved

## Reproduced problem

The separate recovery warning kept Library usable, but existing controller
methods could still change/delete history while the runner was paused. Clear
Done, retry/resume, metadata edits and recovery acknowledgement could therefore
alter evidence needed to reconcile a legacy/catalog conflict.

The extended `job_recovery_status_test` failed before the fix with:
`Unresolved recovery still advertises writable job controls`.

## Implementation

Both `JobQueueModel` and `ExportJobController` now expose
`historyMutationsEnabled`, derived from the unresolved startup-recovery error.
The app publishes the same recovery result to both controllers. Error state
survives history refresh; only explicit recovery success re-enables these
controls.

Twelve general-queue entrypoints and three export-history entrypoints reject
mutation before any helper call. This includes retry, cancellation, pause,
resume, priority/resource policy, reorder, duplicate, remove, bulk retry/clear,
and recovery acknowledgement. Model capability roles and Jobs Center maps
reflect the same state and notify consumers when it changes. Export history
also displays the recovery error instead of a success-only status.

Export, Studio, Timeline, Import and the global Clear Finished/retry affordances
disable their affected actions. Library browsing, history inspection and output
reveal are not blocked. This guards the controller entrypoints as well as the
visible controls, so direct calls from stale UI cannot bypass them.

## Proof

- Desktop and focused native builds pass with no compiler warning/error
  diagnostics in the captured build logs. No whole-app QML-warning claim.
- Six recovery/runner tests pass in 23.64 seconds:
  `/private/tmp/job-recovery-controls-tests.log`.
- The extended regression invokes all 15 guarded entrypoints and checks zero
  helper calls, unchanged model rows, disabled capabilities and status, then
  clears the recovery error and checks Resume is accepted again. The helper is
  controlled; this last check proves control admission, not actual rendering.
- Real PostgreSQL native proof: `/private/tmp/job-store-real-controls-uOsBH3`.
  The app invokes all guarded actions against real history after navigating
  Library to Export. `jobRecoveryIsolationProof` reports blocked mutations,
  disabled Clear Done, one visible Library photo, three readable jobs, no catalog
  error, and pending/deferred onboarding. Inspected Export screenshot visibly
  shows the disabled cleanup control and preserved history.
- Real `jobs-before.json`/`jobs-after.json` are byte-identical. The pre-run local
  history SHA check passes. No history version was discarded.
- First native run failed a harness check that read `QQuickItem::isEnabled()`
  instead of this custom menu's QML `enabled` property. The corrected check
  passes without changing the data-preservation gates. Original log retained as
  `launch-before-control-check.log` in that proof root.
- Native trained Scene/import/Edit regression:
  `/private/tmp/job-controls-scene-oRSQGc`. Four PNG outputs exactly match
  `/private/tmp/job-recovery-scene-w6c64o`. Neutral pixels/profile, saved
  mask/color edit, complete mask outline, three undos and foreground/exposed
  state pass; the Edit screenshot was inspected. Whole-proof maximum timer gap
  is still 1,569 ms, so this is not realtime proof.
- `export_recipe_handoff_test` and `photo_export_recovery_test` pass. The larger
  `export_job_controller_test` exposed a stale fixture startup sequence: it
  constructed a runner but never initialized its recovery barrier. Both sandbox
  and native runs failed at deferred-preview dispatch. The fixture now waits
  for successful runner recovery before creating work, matching the app; the
  production recovery barrier and original dispatch assertion are unchanged.
  The corrected native test progressed into the delivery matrix (a live read
  showed eight completed jobs, one running and ten queued) but hit the explicit
  180-second CTest limit. It is **not passing**. Logs:
  `/private/tmp/job-controls-export-native-test.log` (original assertion) and
  `/private/tmp/job-controls-export-startup-test.log` (corrected startup,
  timeout). Remaining delivery-matrix behavior and a suitable bounded functional
  test runtime still need investigation; no assertions or watchdogs were relaxed.

The conflict and Scene proof servers on ports 59753 and 60665 were stopped after
exact owned-data-directory checks and verified stopped afterward. Artifacts are
retained. User databases and originals were not touched.
The timed-out export test could not run its destructor cleanup. Its live process
and PostgreSQL directory were identified before shutdown; server port 61377 was
then explicitly stopped and verified unhealthy/stopped. The retained diagnostic
root is `/var/folders/qx/ck73lj3j7fl5tk_glls1rrq40000gn/T/qt_temp-NtULoD`.

## Remaining scope

This is a guard for known unresolved recovery in these controllers, not a
database-wide transaction or concurrency guarantee. Direct store/helper callers,
stale worker writes, deletion tombstones, version comparison/reconciliation,
and races before recovery reports an error remain to address. New-job creation
is not blocked by this history guard; the runner still gates dispatch. No model
weights were downloaded and no inference, independent RAW calibration, 4D
reconstruction or Photoshop-class completion is claimed.
