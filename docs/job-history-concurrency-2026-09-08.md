# Job-history concurrency and preservation

This is progress on safe loading and recovery, not completion of the app goal.
Before moving additional job writes off the foreground thread, source inspection
found that legacy migration uploaded a snapshot and then cleared the entire
local file. Concurrent appends or same-ID updates could disappear. Fallback
upsert/delete also performed unlocked read/modify/write sequences, and an
unreadable or malformed file was treated as empty.

## Reproduction and repair

The new `job_store_concurrency_test` deterministically holds a migration helper,
appends a job and changes the uploaded job's status, then releases the helper.
The unmodified implementation fails with `migration erased a concurrent append
or same-ID update` in `/private/tmp/job-store-race-baseline.log`.

The implementation now:

- Uses a cross-process lock around local-file mutation, including fallback
  upsert/delete and migration cleanup. The bounded lock wait is one second;
  failure reports an error instead of overwriting data. Helper I/O does not hold
  this lock.
- Serializes migration upload passes with a separate nonwaiting lock and takes
  its snapshot after acquiring that lock. A competing migration reports that
  recovery must retry rather than uploading an older snapshot out of order.
- Removes only versions exactly matching acknowledged upload snapshots. New
  jobs and changed versions remain local. Version lookup is indexed by ID before
  taking the mutation lock, avoiding quadratic matching inside that lock.
- Reloads remaining local history and reports incomplete migration instead of
  silently treating it as complete. Startup's existing failure gate retries
  recovery before dispatching queued work.
- Rejects invalid/unreadable legacy history for mutation or migration, retaining
  original bytes. It validates the jobs array, record objects, IDs and duplicate
  IDs. Atomic writes now check both byte count and commit success.

The normal catalog remains PostgreSQL. This neither switches catalog authority
nor makes all job-store writes asynchronous.

## Verification

Five focused tests pass in 23.02 seconds on the final build:
`job_store_concurrency_test`, `job_runner_startup_test`, `job_runner_scan_test`,
`job_runner_wake_test`, and `job_runner_cancellation_test`.
Log: `/private/tmp/job-store-race-accepted-tests.log`.

The new test covers two overlapping migration attempts, concurrent append and
same-ID update preservation, twelve separate writer processes with large job
metadata, selective deletion, pruning unchanged snapshots while retaining late
work, successful retry, and malformed-history upsert/delete rejection. All
processes use an isolated temporary directory and controlled helper, so this
part alone does not claim real database durability.

Real PostgreSQL proof on the final build is retained in
`/private/tmp/job-store-real-final-GMNyPp` with log
`/private/tmp/job-store-real-final-proof.log`. Paused/canceled job states and
nested metadata survive migration and a subsequent helper read. Acknowledged
local snapshots retire only after upload. An intentionally malformed local test
file remains byte-identical and does not erase the two catalog rows. An earlier
equivalent proof remains at `/private/tmp/job-store-real-OaFSEL`.

Reproduce with a fresh empty test directory:

```sh
build/dev-macos-debug/tests/job_store_concurrency_test \
  --real-catalog /private/tmp/job-store-real-NEW_UNIQUE_DIRECTORY
```

The rebuilt native Scene/import/Edit proof at
`/private/tmp/job-store-native-yJC9co` passes neutral pixels/profile, masked
exposure/P3 autosave, completed final outline and three-step undo. All four PNG
outputs are byte-identical to the preceding startup-recovery proof. Active and
exposed window state matches throughout; `comparison.json` retains the fidelity
guards. `scene-edit-mask.png` was visually inspected, not used to claim pointer
or stylus coverage. This pass makes no broad performance claim.

Builds have no warning/error diagnostics and focused diff checks pass. Only
fresh proof catalogs were used. Their test-owned servers were stopped after
exact-directory checks; data and receipts remain. No user catalog was modified.

## Remaining risks and full scope

These locks coordinate cooperating local-history writers and migrations. They
do not provide optimistic concurrency for catalog rows: migration still uses
the helper's ordinary upsert, and stale job start/completion/user-mutation
snapshots can conflict with newer catalog state. That conflict handling needs
repair before more writes become asynchronous. Crash injection, lock-timeout UI
and noncooperating writers are not covered by this proof.

Previously failing history/model assertions are not fixed or counted as passing
here. RAW/color calibration and realtime coverage, real 4D reconstruction,
embedded-model quality and professional editing capabilities remain part of the
unchanged completion goal.
