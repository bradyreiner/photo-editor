# Recipe crash recovery and acknowledgement replay

## Implemented boundary

Completed editor edits now checkpoint their asset-bound save queue locally before
the 350 ms catalog-save debounce. `QSaveFile` atomically replaces a versioned,
checksummed outbox under `recipe-outbox` in the app's data directory. Direct-write
fallback is disabled; checkpoint files are owner-readable/writable only. A failed
write retains the previous file and publishes a separate persistent save error.
An edit is not crash-protected if its checkpoint failed or its gesture/transaction
has not yet reached `persistCurrentState`.

The outbox records pending snapshots, the immutable active save and any exact
migration retry. Catalog acknowledgement checkpoints the updated queue. An
unacknowledged active save is recovered with the same ordering rules as a failed
helper: newer pending ordinary edits win; an exact migration approval stays ahead
of subsequent edits. The catalog remains PostgreSQL; the outbox is not another
catalog backend.

Recovery initializes on the first asset open. Recovered edits populate the
controller's local state cache before the old catalog adjustment payload is
applied, and the queue retries catalog persistence. Original media, linked layers
and referenced profile assets are not copied or modified by the checkpoint.

The runtime root and a hashed configured database authority scope the checkpoint.
The actual connection string is not stored in the file. A live-owner `QLockFile`
prevents a second editor from writing this catalog's outbox; locks do not expire
merely because time passes. Authority/root changes in a live controller stop
saving rather than redirecting pending edits. Invalid schema, checksum, recipe
roundtrip, duplicate identities and oversized data fail closed without rewriting
the original recovery file. The explicit limits are 512 pending assets and a
256 MiB encoded checkpoint, not a bounded peak-memory guarantee.

The Studio inspector now has a save-status chip independent of preview status.
It shows pending saves, and retains an accessible error/detail when checkpoint
protection is unavailable. It does not claim a failed checkpoint is saved locally.

## Migration replay safety

The audit found a second failure: a migration could commit, lose its helper
acknowledgement, then replay and overwrite both later edits and the original
rollback pointer. Acceptance now serializes per asset using a transaction-scoped
PostgreSQL advisory lock. An identical accepted decision returns success without
rewriting recipe or migration records. Conflicting, rolled-back and stale
approvals are rejected. A candidate record can still be accepted normally.
The original rollback record remains authoritative on identical replay.

The helper still makes a backup before invoking the acceptance transaction. A
replayed/rejected request can therefore retain an extra backup; it does not
replace the original rollback pointer. Removing that redundant work is separate
from the correctness fix.

## Verified evidence

- Six focused tests pass in 16.04 seconds:
  `/private/tmp/recipe-journal-focused-tests.log`. Coverage includes real
  killed-writer lock recovery, active/pending ordering, migration evidence,
  cross-catalog file rejection, exclusive ownership, atomic write failure,
  corrupt/checksum/version rejection, acknowledged removal, stdin transport,
  canonical persistence, relink and QML save states.
- Real isolated PostgreSQL, large JPEG recipe (2,812,246 bytes):
  `/private/tmp/recipe-journal-large-jpeg.log`. Port 52991 stopped and independently
  reported Connection refused.
- Real isolated PostgreSQL, DNG document/JPEG document/linked DNG and large raster
  mask (2,812,348-byte recipe): `/private/tmp/recipe-journal-large-raw.log`.
  Port 53230 stopped and independently reported Connection refused.
- Both database runs kill an actual editor process after checkpointing but
  before its catalog debounce. They first verify the old catalog commit is
  unchanged, then reopen using that stale payload, recover the local state,
  save it canonically and reopen again. Both reopens preserve exact recipe state
  and rendered pixels. Existing rapid-switch and injected-save-failure/retry
  checks also pass.
- Both runs exercise migration acknowledgement replay after a subsequent edit,
  retaining that edit and the original complete migration row/rollback pointer.
  Conflicting, rolled-back and newer decisions are protected; candidate promotion
  remains functional. This uses synthetic approval metadata in the disposable
  catalog, not photographer-approved rendering-quality evidence.
- `cargo test --workspace` passes:
  `/private/tmp/recipe-journal-rust-tests.log`. Native app/test builds and helper
  builds pass without compiler warnings/errors in the final logs.
- The save-error chip was rendered and visually inspected at
  `/private/tmp/framehouse-recipe-chip-fO3Jiv/error.png`; the QML test covers pending,
  error, full accessible detail and committed/hidden states. A Qt font-alias
  diagnostic was emitted by the offscreen test; it was not a compiler failure.
- Packaged-app proof passed in 16,399 ms at
  `/private/tmp/framehouse-recipe-error-native-wJuLGg/receipt.json`. A second
  writer holds only the new QA profile's outbox lock. The real editor refuses
  checkpoint writes, retains pending work, places/renders/exports a RAW source
  layer and visibly shows the separate save error with the exact full reason.
  `startup.png` was visually inspected: the chip is readable and unobscured.
  The private model runtime reports stopped/not-owned, and port 53660
  independently reports Connection refused. All window samples were inactive;
  this is UI correctness evidence, not a foreground performance measurement.

## Performance and remaining limits

The bounded performance workflow stopped before optimization: read-only macOS
session inspection still reports the screen locked. No foreground/realtime
acceptance or causal claim about earlier variable relink timings is made.

Three explicit large-RAW checkpoint measurements were 26, 26 and 25 ms for a
2,814,563-byte outbox. They measure synchronous serialization and atomic local
write, not input-to-photon latency, worst-case filesystem stalls or peak memory.
This is additional UI-thread work and remains a realtime optimization target.
The subsequent `recipe-checkpoint-performance-2026-09-07.md` records a v2 format
with v1 recovery compatibility and a macOS digest optimization: 66–72% lower
large-recipe p95, lower benchmark peak memory, and 7 ms actual-DNG checkpoints.
Those newer measurements supersede this initial format's timing, not its broader
recovery or realtime limitations.

Recovery is at-least-once, not exactly-once. The single-editor lock does not
coordinate independent catalog writers, restored database generations, or
external agent/batch mutations; stale-write conflict detection across those
writers remains necessary. Changing the connection string deliberately selects
another namespace, leaving prior checkpoints retained for reconciliation rather
than replaying them into an unverified authority.

Editor recipe close coordination is subsequently implemented and verified in
`editor-close-coordination-2026-09-07.md`, including failed-checkpoint rejection
and stopping the current helper before teardown. Power-loss durability,
universal background-job shutdown, unfinished gesture recovery, a user-facing corrupt-journal repair/reconciliation
flow, large-document incremental/async checkpointing, complete undo-history
recovery, immutable project sources and broad camera/output-space validation are
still open. This slice is crash recovery for the tested completed editor recipes,
not completion of the full application goal or realtime rendering.
