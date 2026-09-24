# Editor close coordination and recipe recovery

## Implemented boundary

Closing the main window or choosing Quit now passes through an editor-recipe
close coordinator. It first verifies that completed edits can be checkpointed,
pauses new catalog-save launches, and waits asynchronously for the current save
helper. The helper gets 1.5 seconds to finish, then termination, then a kill after
another 250 ms. Failure to stop within a further second blocks closing. The
controller handles the helper's acknowledgement before the final checkpoint.
Unacknowledged work stays in the local outbox for recovery on a later asset open;
an unavailable catalog does not require discarding edits to exit.

Checkpoint errors block closing and display the full reason, with **Keep editing**
and **Retry close** actions. There is no discard shortcut. Active coalesced edits,
brush commits and the checked selection/inpaint commit paths also block closing
until finished or cancelled. Keep editing resumes normal save scheduling.
Checkpoint errors use the dedicated save chip and close dialog without replacing
preview status. This prevents a successful render being mislabeled Preview failed
solely because its recovery checkpoint could not be written.

The actual window-close and application-Quit boundaries revalidate the checkpoint
after readiness, protecting against a late edit/failure between the queued ready
signal and Quit. A catalog acknowledgement can empty the queue while local outbox
cleanup fails; that error also blocks closing and must be successfully retried.
An untouched editor with no pending saves or recovery error does not create an
outbox simply because the user closes the application.

The guard uses Qt's interruptible Quit/window-close mechanism, not an override of
forced termination. `QCoreApplication::exit` bypasses that interruption; see the
[official Qt quit contract](https://doc.qt.io/qt-6/qcoreapplication.html#quit).
The controller destructor disconnects recipe-process callbacks before member
teardown and uses a bounded kill/wait fallback for abnormal/programmatic exits.
Normal user close stops the helper asynchronously before that destructor runs.
The app marks a clean exit only after coordinator readiness.

## Verification

Large-recipe JPEG and real DNG database runs pass:

- `/private/tmp/editor-close-jpeg-database-final.log`: temporary PostgreSQL port
  60489 stopped and independently returned Connection refused.
- `/private/tmp/editor-close-raw-database-final.log`: temporary PostgreSQL port
  60723 stopped and independently returned Connection refused. The RAW fixture
  is the 4208 x 3120 Nexus 6 DNG used in the prior recovery proof, with a linked
  RAW source and a 2.8 MB recipe including raster-mask data.

Both complete the close/failure/retry/acknowledgement-cleanup cases below as well
as the prior rapid-switch, killed-editor, migration-replay and pixel-parity cases.
The first new JPEG attempt (`/private/tmp/editor-close-jpeg-database.log`) is not
accepted evidence: out-of-range exposure inputs clamped to the same value, so
later steps did not create new edits. The corrected cases assert distinct actual
state and pending work before injecting failures. Its temporary port 60043 was
also stopped and independently verified closed.

The focused coordinator test covers checkpoint failure, retry, cancellation,
repeated requests, a real helper that ignores termination, event-loop heartbeats,
late checkpoint failures, and an actual application Quit event in a subprocess.
The QML test calls the window's real close method, checks the visible blocking
popup and exact error text, and invokes its Keep editing button.

Seven final focused tests pass in 14.15 seconds at
`/private/tmp/editor-close-focused-final-tests.log`: coordinator, dialog, save
status chip, queue, journal/current digest, journal/portable digest and source
relink. The blocked-dialog screenshot at
`/private/tmp/framehouse-close-dialog-final-qRARgB/blocked.png` was visually
inspected; its full error text and both actions fit the popup. Final app and
database-test build output is `/private/tmp/editor-close-ack-build.log`; focused
test relinking is `/private/tmp/editor-close-focused-final-build.log`. Neither
contains compiler warnings or errors.

The private PostgreSQL integration test uses changed in-range exposures and large
raster-mask recipes. It waits for a real delayed database transaction before
closing its helper, checks that transaction did not commit, then recovers the
outbox and compares canonical recipes and rendered pixels on fresh reopens.
It also checks an unfinished edit, a failed atomic checkpoint, successful retry,
a healthy helper finishing within grace, and failed outbox cleanup after a
successful catalog acknowledgement. All database/file failures affect only the
test-owned temporary cluster and runtime directory.

### Packaged app

- Normal close: `/private/tmp/framehouse-editor-close-native-Qf6obr/receipt.json`
  passes. The RAW-layer render/export/undo workflow completed before an actual
  window-close request; the close receipt confirms ready, stopped helper, no
  error and pending recovery. The exited profile retains one v2 pending recipe
  object, no active helper entry, and `session.json` records `cleanExit: true`.
  There is no `QProcess: Destroyed while process ... is still running` warning.
  The private runtime reports stopped/not-owned and port 61112 independently
  returned Connection refused. The pre-close screenshot was visually inspected.
- Blocked close: `/private/tmp/framehouse-editor-close-blocked-Q2L7YV/receipt.json`
  passes with the actual window still open and a locked-outbox error. Its visible
  centered modal was inspected: full reason, Keep editing and Retry close fit;
  the dedicated save-error chip remains visible behind it. The test harness uses
  `exit` only after capturing the rejected user close to end this failure test;
  the profile correctly retains `cleanExit: false`. Private runtime port 61215
  stopped and independently returned Connection refused. This screenshot also
  exposed the save-to-preview-status classification bug corrected afterward;
  the subsequent status-isolation proof is recorded below.

Both runs use new isolated app data, preferences, cache and model directories and
an intentionally unavailable private catalog endpoint. The catalog recovery
banner is expected; no user catalog or model weights are changed. Their receipt
elapsed times (19,586 ms and 20,176 ms) cover proof work before close, not close
latency. Every sampled window-active flag is false, so these establish runtime
and UI correctness, not foreground interaction performance.

### Final status-isolation verification

After removing checkpoint failures from preview status, the real large DNG test
passes again at `/private/tmp/editor-close-status-raw-database.log`, including an
explicit unchanged-preview-status assertion during failed close. Its private
PostgreSQL port 61455 stopped and independently returned Connection refused.
Final app/test build output is `/private/tmp/editor-close-status-final-build.log`
with no compiler warnings or errors; the seven focused tests pass again at
`/private/tmp/editor-close-status-focused-tests.log`. Scoped `git diff --check`
passes.

The final blocked-close packaged proof at
`/private/tmp/framehouse-editor-close-status-JLF7ZF/receipt.json` passes. Its
visually inspected `startup.png` shows the complete blocking dialog and save-error
chip without the misleading Preview failed badge. Structured events contain zero
preview failures classified from the checkpoint-error message. The app exits
with no running-QProcess destruction warning. The private runtime is stopped and
not owned; port 61761 independently returned Connection refused. The 18,135 ms
receipt time is proof duration, not a foreground realtime or close-latency claim.

## Remaining limits

This coordinates editor recipes, not every timeline, export, agent, training or
background-job lifecycle. It does not recover unfinished gestures or serialize
complete undo history. Forced process termination still depends on previously
successful checkpoints. Checkpoint serialization and file writes are still
synchronous; the helper wait is asynchronous, not a claim that close has no
UI-thread I/O. Large-document storage, independent multiwriter conflict handling,
power-loss guarantees and corrupt-journal reconciliation remain separate work.
Retry repairs the tested write/cleanup failures on an already opened journal;
this change does not add a reopen/reconciliation path for a journal whose initial
ownership or schema check failed. Keep editing retains the live document in that
case, but initial-ownership recovery still needs a separate safe workflow.
RAW/working-space accuracy across cameras, foreground realtime rendering, 4D
training, embedded model weights and the complete professional editing toolset
remain subject to the full gates in `active-app-completion-goal.md`.
