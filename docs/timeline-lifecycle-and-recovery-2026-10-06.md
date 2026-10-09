# Timeline lifecycle, recovery, and performance

The reported workflow is opening a timeline, continuing in Edit, and later finding
recoverable work in Recent Timelines. Document lifetime and active preview lifetime
need separate ownership.

## Workflow decisions

- Opening or creating a timeline activates Program preview when Timeline is the
  destination. Source playback remains an explicit action.
- Leaving Timeline pauses Program and Source. The sequence, selection, playhead,
  layout, and unsaved work remain available. Returning restores a paused preview;
  it never resumes audio automatically.
- Hidden Program does not synchronize document state, prefetch cuts, or grade
  decoded frames. Current grading receives cancellation without a UI-thread wait.
  Additional audio/background preview layers release their sources when hidden.
- User-requested exports and durable recovery checkpoints retain their existing
  lifecycle. A navigation action does not save over the last saved project.
- Recent Timelines lists saved projects and recoverable states. Recovery rows say
  “Recoverable work” and open the verified recovery as a dirty working document.
  Opening never overwrites the last saved file. Restore Copy registers a separate
  saved project immediately. Discard refreshes Recents after archiving evidence.
- Recovery discovery and verification for Recents execute on the storage pool.
  Only the newest recovery per timeline appears there; the recovery chooser
  retains its detailed candidates. Recovery entries do not become Pancake sources.
- Ordinary journal commits do not trigger full-chain discovery while the recovery
  chooser is closed.

## First-pass acceptance evidence

The same isolated native shell fixture exercises decoded Program and Source media,
Edit navigation, return, trim/move, pause/seek, save/reopen, and Recent recovery.
Artifacts live under `docs/evidence/timeline-lifecycle-2026-10-06/`.

The baseline retained Program playback in Edit and advanced the hidden playhead
by 0.256 seconds in a 250 ms observation window. The warm Edit switch was 58 ms.
Frame sample counts reset on source changes, so the baseline negative count delta
is not a valid frame-throughput measurement.

Acceptance requires hidden playback off, no hidden playhead movement or frame
publication, Source paused, a decoded picture on return, preserved edits, and an
activatable recovery row. The warm Edit fixture has a 100 ms navigation goal.
Focused controller tests cover cancellation, pause/resume identity, recovery
across controller sessions, saved-file byte preservation, copied registration,
and discard removal. This fixture does not reproduce the user's large project.

All three focused controller tests passed (19.50 seconds combined), including the
existing large-document recovery/controller scenario. The native workflow passed
on an isolated retry and recorded hidden playhead and frame-count deltas of zero,
Source paused, a decoded paused return, and recovery activation from Recents. The
first native run failed startup readiness; it supplies no performance result.

The successful native switch sample was 127 ms, above the 100 ms goal. That run
also started Source playback before leaving, unlike the baseline. The route
latency comparison is therefore not controlled and does not establish an
improvement or regression. Accept the suspension and recovery correctness fixes;
keep route latency and the reported large-project freeze unqualified. Native
visual review also found that the existing 52-pixel list-height estimate clipped
60-pixel rows; Recents now sizes complete rows and exposes a scrollbar.

A later native pass caught saved ISO timestamps without milliseconds sorting
ahead of newer recovery timestamps within the same second. Recents now compares
parsed dates and prefers recovery on a timestamp tie. The focused recovery test
forces this same-second case in its isolated index.

The incremental catalog/timeline source contract passed. The broader timeline UI
source contract still fails its delivery-queue assertion because `job_runner.cpp`
lacks its expected `QStringLiteral("queueOrder")` expression. This is the same
unrelated queue-contract condition documented in the October 4 workflow report;
this patch does not change `job_runner.cpp` or claim a green full suite.

Final desktop build and all three focused tests passed after the timestamp fix
(21.24 seconds combined). The final isolated native workflow passed, including
opening the correctly ordered Recent recovery. Its warm Edit route sample was
132 ms. Hidden playhead and frame-count deltas remained zero. All
20 final capture dimensions matched their manifest and display scale;
the corrected recovery list was visually reviewed. See `verification.json` for
final source/binary hashes and the complete receipt. These are fixture results,
not a controlled speedup or qualification of the reported large-project freeze.

## Remaining qualification and next work

1. Record a representative timeline's clip count, media formats, grades, audio
   layers, journal size, and running build identity. Measure Timeline→Edit input
   dispatch, route presentation, event-loop gaps, and hidden CPU over repeated
   switches. The small native fixture establishes the lifecycle contract only.
2. Measure active-timeline playback/seek costs separately: document projections,
   QML delegate work, decoder readiness, grading, and additional media players.
   Require decoded pictures for throughput claims. Optimize the measured dominant
   path while preserving photo-engine color and export parity.
3. Measure remaining checkpoint snapshot construction with large documents.
   The fix pass below moves history validation, hashing, append/fsync, and periodic
   autosave serialization off the GUI thread. Building the editorial snapshot
   still runs there and requires representative large-project profiling.
4. Qualify 100-track/10,000-clip projects through real QML navigation, long-GOP
   camera media, multilayer audio, and repeated recovery/relaunch. The existing
   large-controller fixture does not establish native workspace smoothness.
5. If retained paused decoder memory is material, add delayed decoder eviction
   with explicit paused-frame and source restoration tests. Avoid tearing down
   the entire QML workspace on each tab switch, which loses interaction state
   and incurs cold construction costs.

Do not mark the reported freeze fully resolved from a build or a short generated
video. Preserve this distinction in release notes and performance acceptance.

## Follow-up audit before the fix pass

An isolated probe linked against the current controller objects measured the real
`flushEditJournal()` path with 128 synthetic still-clip records. Checkpoint 1 took
32.8 ms; checkpoint 10 took 237.4 ms; checkpoint 20 took 478.5 ms; checkpoint 25
took 602.5 ms. The journal reached 23,345,156 bytes. The bounded probe stopped at
its 600 ms per-checkpoint threshold. The edit timer calls this routine on the GUI
thread, including while another workspace is active. Every append reads and
validates the entire prior chain and stores another complete document snapshot.
The same GUI thread also serializes and commits periodic autosaves every 30 seconds.

The probe also added an uncheckpointed marker and immediately loaded a different
saved timeline. The document changed, the old journal did not grow, and recovery
had no candidate containing the pending marker. `loadTimeline()` stops the edit
journal timer before validating or preserving the replacement. This differs from
`closeTimeline()`, which explicitly appends a closing checkpoint.

Other source findings at that audit:

- Save Timeline calls `saveTimelineAsync()`, which still appends the journal and
  builds the snapshot synchronously before starting its worker. Both save paths
  queue a full timeline render. The QML Save menu additionally disables saving
  while a render is active. Project persistence and publishing need separate
  actions and lifecycles.
- The only caller of the global `WorkerQueueRegistry::publishUserIntent()` is
  TimelineDocumentController. Edit does not publish interactive intent there.
  Hiding Timeline clears its latency-sensitive state and resumes deferred media
  work; the isolated hidden-Timeline proxy decision was normal/not deferred.
  This is a source-confirmed scheduling gap, not a measured CPU regression.
- `syncToDocument()` and QML's playback handler obtain detailed `currentClip()`
  maps on ticks from a 15 ms clock. For selected graded clips, this getter runs
  export capability evaluation, including a whole-sequence spatial-grade scan;
  source compatibility can invoke synchronous ffprobe on a cache miss. Grade
  compilation itself has a cache. Repeated project scans and cache-miss probing
  on an inspector getter are source-backed risks; no per-tick timings were measured.
- Opening a Recent recovery creates a fresh TimelineView whose startup path scans
  recoveries and can prompt for the same state again. This is a source-backed
  workflow risk that still needs a visible UI reproduction.

Probe source, exact results, compilation recipe, and boundaries are under
`evidence/timeline-lifecycle-2026-10-06/follow-up-audit/`. Prioritize preserving
pending edits and moving durable checkpoint work off the GUI thread, then separate
Save from render and give the current Edit workspace ownership of worker priority.


## Fix pass: durability, checkpoint cost, and Save versus Publish

Automatic recovery checkpoints now snapshot the editorial state on the GUI
thread and serialize writes on a dedicated single-worker pool. Hashing, chain
validation, JSON serialization, append, flush, and fsync execute on that worker.
The worker retains only validated journal heads, bounded to 64 paths. A changed
file size, modification time, or creation time forces full revalidation; damaged
journals are renamed with unique identities and retained byte for byte. Subsequent
checkpoints append to a fresh chain. Active writes coalesce later automatic edits
instead of queuing unbounded document snapshots. Periodic autosave writes also
run on this pool. Explicit flush, document replacement, Close, and shutdown wait
for their durable checkpoint; ordinary edits do not wait for journal I/O.

Opening another timeline validates the replacement first and checkpoints current
pending edits before clearing any state. New Timeline and Close do the same.
Checkpoint failure refuses replacement or Close and leaves the working document
open. An unavailable/damaged replacement leaves the existing debounce timer,
document, and edits intact.

Save Timeline now persists the project without enqueueing rendering. It remains
available while a gallery render is active. Publish, Save & Queue, and the separate
Render to Library menu action retain explicit rendering through the delivery
review flow. Render jobs receive immutable document snapshots. Finishing an older
render updates gallery artifacts without replacing newer canonical edits or
backup documents, and gallery navigation retains the editable canonical path.
The isolated completion regression also exposed metadata replacement clearing
catalog backups. Rust metadata upserts and list replacement now preserve omitted
JSON backups; explicitly supplied JSON still replaces a backup, and records
omitted from an explicitly replaced list retain the existing removal semantics.

Main publishes the visible photo editor's preview-processing intent. Late hidden
Timeline callbacks cannot overwrite that intent. Deferred media work resumes
when photo processing finishes. Selected grade export capability is cached across
playback ticks and invalidated by document/media changes, avoiding repeated
whole-sequence spatial export checks on unchanged clips. An already open timeline
also skips the startup recovery prompt, so opening a Recent recovery does not
immediately prompt for it again.

The exact 128-clip persistence probe now completes all 80 checkpoints. At entry
20, the same 18,665,721 journal bytes took 34.45 ms rather than 478.53 ms (13.9×
faster). Entry 80 took 33.51 ms with a 75,162,141-byte journal. This includes the
explicit synchronous durability wait; automatic checkpoint writes run in the
background. The pending-marker replacement probe now confirms the newest edit is
recoverable. Gates accepted the patch: entry 20 at least 50% faster, late checkpoint
below 100 ms, valid SHA-256 chains, damaged-byte preservation, and pending-edit
recovery. No history is deleted or compacted to obtain these results.

Validation:

- Desktop app and native workflow fixture builds passed. All three focused
  recovery/playback/frame-scheduling tests passed in 16.43 seconds.
- The dedicated `--timeline-save-render-workflow` passed with its own temporary
  PostgreSQL catalog and real generated media. It checks plain Save, explicit
  rendering, immutable input, newer canonical bytes, canonical library navigation,
  and the latest backup after render completion. It is registered in CTest as
  `timeline_save_render_workflow_test`.
- Rust workspace tests passed. Workspace formatting still reports existing
  differences outside the changed SQL methods; unrelated files were preserved.
- The native pointer workflow passed: visible separate Save/Render actions,
  pointer Save, Edit navigation, decoded paused return, and immediate Close with
  an uncheckpointed marker followed by recovery activation from Recents. The
  warm Edit sample was 61 ms, hidden playhead/frame deltas were zero, and Source
  was paused. All 21 capture dimensions matched their manifest; the menu and
  recovery rows were visually reviewed.
- The broader controller suite still stops at its shared-proxy cleanup assertion.
  Its earlier recovery fixture intentionally leaves a damaged saved project;
  cache inspection reports an incomplete document inventory and refuses cleanup.
  That guard and assertion were preserved. The earlier sandbox run additionally
  could not start PostgreSQL; the native run establishes the cleanup failure.

Artifacts and source/binary hashes are under
`evidence/timeline-lifecycle-2026-10-06/fix-pass/verification.json`. The native
shell fixture uses catalog recovery/fallback; the separate Save/render test uses
actual temporary PostgreSQL. The 61 ms switch is one fixture sample, not a
controlled route speedup. Snapshot construction, cold export-capability probing,
manual recovery verification, long camera media, and large native projects still
require qualification. These changes do not establish that every reported
large-project freeze is resolved.


## Remaining issues: second audit

This audit used the current controller objects and temporary data only. It did
not change application code or user workspaces. The isolated probe uses a local
file catalog fallback and dummy still-media records. These are controller
correctness and GUI-call timings, not decoded-frame or real-camera throughput
measurements. Source, exact results, build recipe, and hashes are in
`evidence/timeline-lifecycle-2026-10-06/second-audit/`.

Three issues reproduced:

1. **Opening recovery during Save can silently replace the newer saved project.**
   The probe explicitly saves a three-marker document asynchronously, then opens
   a two-marker autosave for the same timeline before completion is delivered.
   Save completion identifies the working document only by timeline ID; the
   changed revision triggers another automatic Save of the recovered document.
   The final canonical document has two markers and the recovered document is
   marked clean, without another user Save. The earlier journal history still
   exists; this is an unexpected canonical overwrite, not proof of permanent
   loss. Guard completion and chained saves with a working-session/load identity
   as well as timeline ID. See `timeline_document_controller.cpp:14521`.
2. **Save removes unrelated recovery rows from Recents.** Before saving, the
   cache contains one unrelated unsaved timeline's recovery row; after Save it
   contains zero. The recovery remains discoverable on disk. Save completion
   replaces the combined saved/recovery cache with saved-only asset records,
   whereas explicit refresh merges both. Use the same merged result or refresh
   after Save. See `timeline_document_controller.cpp:14513` and `:4528`.
3. **Recovery opening still blocks the caller.** With 128 clips, 40 checkpoints,
   and a 37,414,501-byte journal, direct recovery discovery takes 946.6 ms and
   opening the selected recovery takes 1882.8 ms. Candidate lookup rescans all
   recoveries, then reads and validates the selected journal again. QML invokes
   this synchronously from activation. Move validation/discovery off the GUI
   thread and expose pending/error state during opening. See
   `timeline_document_controller.cpp:10326` and `TimelineSetupPage.qml:437`.

Two additional source-backed concerns remain unqualified by runtime timing:

- Every checkpoint stores a complete document and the journal has no bounded
  rotation/compaction lifecycle. Immutable render snapshots are also created
  without a cleanup hook found in desktop code. The observed journal growth
  makes retention a workflow and storage issue; preserve durable recovery and
  in-flight render references when designing reclamation.
- Intent updates reprioritize worker queues even when state is unchanged. Resume
  paths sort again, and comparator media-path lookup scans clips. This can make
  frequent Edit processing or visibility signals expensive in large projects.
  Measure a large-queue fixture before changing ordering or scheduling. See
  `timeline_document_controller.cpp:8012`, `:8020`, and `:8178`.

Prioritize the Save/recovery session race, then Recents consistency, then
nonblocking recovery opening. These findings are not fixed by this audit.


## Superseding follow-up: 2026-10-08

The three reproduced second-audit findings above are addressed in the 2026-10-08
performance/recovery pass: same-ID Save completion is session-guarded, Save
retains unrelated Recent recovery rows, and QML recovery discovery and selected
open now verify asynchronously. Dirty replacement checkpoints the outgoing
working revision before apply and leaves it open on checkpoint failure. The
focused lifecycle receipt, exact journal/reopen invariants, and source hashes
are recorded in
[`performance-recovery.md`](evidence/app-polish-2026-10-08/performance-recovery.md).

The registered `timeline_recovery_lifecycle_test` passed 1/1 in 39.68 seconds.
Its 128-clip / 40-checkpoint fixture measured 3.46329 ms discovery admission
and 0.6345 ms selected-open admission for the 38,019,421-byte journal while
model/build work was concurrent. An earlier isolated focused run measured
0.020667 ms and 0.012 ms; these are host-load observations, not a controlled
before/after comparison with each other or the older 37,414,501-byte fixture.
They measure controller admission, not end-to-end/native large-project
qualification. Restore-as-copy and discard correctness are covered by the
focused mode; their synchronous I/O cost, large-document apply, and journal
retention remain unqualified. The existing no-argument recovery test continues
into unrelated media/editing coverage and currently fails later at its Paste
Attributes media-analysis wait; the focused recovery segment itself passes.
This update does not qualify the reported large-project freeze as resolved.
