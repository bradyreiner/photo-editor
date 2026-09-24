# Legacy job migration cannot overwrite catalog state

The preceding local-file concurrency repair left a separate confirmed defect:
migration used the catalog helper's ordinary unconditional upsert. A legacy
snapshot could therefore overwrite a newer catalog pause/cancel/completion or
metadata change even when local-file locking was correct.

## Reproduction and behavior

The real PostgreSQL test first migrates two jobs, then changes one catalog row
and recreates the older local snapshot. Before the fix, migration overwrites the
newer row and clears the local file. The regression fails with `Migration
silently overwrote a newer catalog job`. Evidence:
`/private/tmp/job-import-conflict-baseline.log` and the isolated catalog at
`/private/tmp/job-store-real-conflict-baseline-z7BYAS`.

Migration now calls the helper's new `import-legacy` command instead of `upsert`:

- An absent job ID can be inserted. The SQL conflict branch cannot replace an
  existing row for this command.
- An identical existing payload can be acknowledged after a previously
  interrupted migration. Every serialized job field must match except the two
  timestamps assigned by PostgreSQL. Comparison reads only the conflicting ID,
  not the entire job history.
- A different existing payload produces an explicit negative acknowledgment.
  The catalog row and local history are preserved, and recovery reports the
  conflict instead of enabling dispatch.
- Local cleanup requires a valid positive helper receipt. Helper failure or an
  invalid receipt cannot silently retire local history.

Ordinary `upsert` retains its current behavior. There is no schema migration or
catalog-authority switch. Desktop and helper updates must ship together; an
older helper rejects the new command and leaves local history intact.

## Concurrency follow-up

The twelve-process fallback-write regression exposed an intermittent acquisition
timeout during this pass. Diagnostics captured `writer-7: Legacy job history is
busy or unavailable`; no file overwrite was reported. The original failure is
retained in `/private/tmp/job-import-conflict-native-tests.log` and the diagnosed
repeat in `/private/tmp/job-import-conflict-lock-check.log`.

Local-file acquisition now makes short retries within the existing one-second
limit. It does not increase that limit or turn synchronous writes into async
ones. The expanded test deliberately holds the lock, requires bounded failure
with unchanged history, then verifies successful writing after release. Five
consecutive complete concurrency runs pass in
`/private/tmp/job-import-conflict-lock-final.log`.

## Final proof

- `cargo test --workspace`: 42 tests pass. The new payload-equivalence test
  permits timestamp differences but rejects changed status, metadata, recipe,
  output path and progress. Log:
  `/private/tmp/job-import-conflict-rust-final-tests.log`.
- Five native tests pass in 24.04 seconds: job-store concurrency and runner
  startup/scan/wake/cancellation. Log:
  `/private/tmp/job-import-conflict-final-native-tests.log`.
- Final real PostgreSQL proof:
  `/private/tmp/job-store-real-conflict-indexed-sS2eSo`, with log
  `/private/tmp/job-import-conflict-indexed-proof.log`. It validates identical
  replay, preserved newer catalog state, retained conflicting local bytes and
  exactly one accepted winner from two simultaneous helper imports of the same
  absent ID with different states. The stored state matches the winner.
  Malformed local history and all three resulting catalog rows remain intact.
  An earlier equivalent conflict proof is retained at
  `/private/tmp/job-store-real-conflict-final-dh2aE7`.
- Native Scene/import/Edit proof:
  `/private/tmp/job-import-native-qNC9Zv`. Neutral pixels/profile, masked
  exposure/P3 autosave, completed final outline and three-step undo pass. All
  four output PNG files are byte-identical to the preceding accepted proof;
  active/exposed state matches. `comparison.json` retains the fidelity checks.
  `scene-edit-mask.png` was visually inspected, not used to claim pointer/stylus
  coverage. This normal-workflow proof does not exercise conflict-review UI.

Reproduce the real conflict test with a fresh empty directory:

```sh
build/dev-macos-debug/tests/job_store_concurrency_test \
  --real-catalog /private/tmp/job-store-real-NEW_UNIQUE_DIRECTORY
```

The Rust helper and desktop targets build without warning/error diagnostics.
Focused diff checks pass. Helper-file rustfmt passes; full catalog-library
rustfmt still reports unrelated existing formatting differences, left intact.
All proof databases are isolated; their owned servers were stopped after
exact-directory checks and their data retained. No user catalog was modified.

## Remaining completion requirements

Ordinary worker/job-state writes still need revisions or equivalent atomic
compare-and-set semantics. This pass does not make them safe against stale
snapshots. Deletion/recreation protection also needs explicit treatment.

Conflict resolution is not yet a finished user workflow. Recovery remains closed
and currently uses the app's startup-failure reporting path; separating job
recovery errors from Library availability and providing a conflict review path
are required next. A conflict must not be silently resolved by discarding either
version. Existing history/model test failures remain unresolved, not counted as
passing here.

The larger RAW/color/realtime, loading, real 3D/4D reconstruction, embedded-model
and professional editing/agent requirements remain the unchanged active goal.
