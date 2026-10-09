# Performance fixes — September 28, 2026

Priority and evidence reference: [30-finding audit](performance-impact-audit-2026-09-28.md).
Preserve existing working-tree changes. Implement in priority order; implementation
and compilation do not establish measured speedups or native UI correctness.

## Acceptance boundaries

For each issue, use at most three measured optimization attempts before revisiting
the hypothesis. Compare the same workload and build before/after. A fix must retain
output correctness, recovery behavior, cancellation, and bounded resource use.
Native workflow measurements remain pending; historical audit numbers are not
baselines for the modified tree. The goal's verification work now includes isolated
checkpoint tests and a bounded benchmark comparison, recorded under finding 7.

## 1. Catalog media duration probing

Implemented; runtime acceptance pending:

- Remove `ffprobe` calls from snapshot parsing and relinking on the UI thread.
- Run at most two duration tasks concurrently, grouping assets sharing a path.
- Cache successful durations on disk against canonical path, size, modification
  time, and creation time. Read/validate/write this cache on workers.
- Cancel superseded work; reject results with an old catalog generation or path.
- Update duration roles incrementally and coalesce picker notifications.
- Keep the existing five-second probe deadline and reject nonfinite durations.

Required runtime acceptance: catalog rows available without waiting for probes;
warm unchanged refresh launches zero duration probes; stale/relinked results never
replace current metadata; process concurrency stays at two; no GUI event-loop
delay attributable to waiting for a duration probe. Offline, failed, changed-file,
duplicate-path, and shutdown cases still require runtime verification.

Compilation: the Release `photo_editor_app` catalog model object compiled
successfully, including automatic MOC and QML cache generation for the modified
Timeline/Carousel views. This was an object build, not a linked app or launch.
No tests, benchmarks, or real-library operations were run.

### Duration queue follows selection and viewport

A source/coverage review found that the initial duration queue followed the
filtered asset list, but later scrolling and selection changes did not reprioritize
it. In a large mixed-media library, a visible clip near the end could wait behind
many offscreen probes even though the UI itself was no longer blocked.

Pending duration requests now have a path-to-position index. On selection or
viewport changes, and before starting another worker, the current selection and
visible rows are promoted through indexed swaps. This visits the viewport rather
than scanning the entire queue. Already-promoted shared paths are skipped; each
request retains every grouped asset ID. Starting a request removes its index
entry and moves its payload out of the queue. Refresh rebuilds the index under a
new generation; draining the queue clears it.

Active probes are allowed to finish, preserving the two-worker cap and avoiding
repeated cancellation during scrolling. A newly visible item can still wait for
an active probe's existing timeout; this is priority scheduling, not an immediate
duration guarantee. Existing generation/path checks and persistent-cache validation
remain in place.

The existing catalog test covers duration values supplied directly to picker
rows, but it does not exercise the new worker/cache/priority path. Runtime
acceptance must include scrolling to late catalog rows while both workers are
busy, selecting an offscreen clip, duplicate paths, repeated priority changes,
filter changes, refresh/relink cancellation, and a warm zero-probe refresh.
No tests or benchmarks were run during this review.

Release catalog model and generated MOC object compilation passed:
`/private/tmp/looksy-performance-fix-1-duration-priority-build.log`.
Focused whitespace checks passed. This is additional implementation progress on
finding 1; it does not close that finding's runtime acceptance.

### Isolated duration-probe runtime verification

Added `catalog_media_duration_test` (UNIX, 30-second timeout), linked against the
current desktop controller runtime. It supplies a gated subprocess fixture through
an isolated PATH and uses temporary app-data/cache/media directories. No live
catalog, real-library records, or user media are accessed. Thumbnail work is
suppressed so the checks exercise duration scheduling specifically.

Verified on three consecutive final runs:

- Nine rows publish while two probe processes remain blocked; a model-thread
  heartbeat continues. Final runs reported 0 ms at integer-millisecond resolution
  for the nine-row model replacement (not a native frame or large-library metric).
- Exactly two requests start while both gates are held. A new selection then
  precedes visible rows, which precede the offscreen queue after a slot opens.
- Eight unique paths across nine records cause eight probes; both records sharing
  a path receive the result. An unchanged refresh starts zero additional probes.
- Changing one source revision causes one grouped cache miss. A source changed
  during probing is neither published nor cached and succeeds on a fresh retry.
- Replacing a running request with a new source preserves the new result.
  A same-path old-generation result is allowed to finish while event delivery is
  held; replacing the generation before delivery prevents it overwriting a known
  newer duration.
- Invalid output is neither published nor cached. Offline records are skipped;
  returning online reuses a valid cache. A gated process reaches the real
  five-second worker timeout while the heartbeat continues, then retries normally.
- The installed `/opt/homebrew/bin/ffprobe` reads a generated one-second PCM WAV
  through the actual controller path. Switching PATH back to the fixture for a
  warm refresh still returns the cached duration without launching a probe.
  This real-probe branch is conditional on an installed ffprobe and did execute
  in all three runs here.

Final build: `/private/tmp/looksy-performance-fix-1-duration-real-build.log`.
Final repeated run: `/private/tmp/looksy-performance-fix-1-duration-real-test.log`
(three passes, 20.21 seconds total; includes one five-second timeout per run).
Focused whitespace checks passed. Earlier fixture/extended checks also passed.

This supplies controller correctness and event-loop evidence for finding 1.
It does not establish native presentation latency on a 1,000-asset library,
shutdown with active work, all filesystem/network failures, or Windows behavior.
Those acceptance items remain open.


### Native Library and active-shutdown follow-up (2026-10-01)

Active-shutdown coverage now records both owned helper PIDs before destroying the
model. Three consecutive runs returned from destruction in 0 ms and observed both
helpers gone within 41, 42, and 33 ms. Existing duration contracts and the real
one-second WAV/ffprobe branch passed in those runs.

The native source-QML Library fixture publishes 1,000 rows with two held helpers,
600 online media paths, and 100 offline paths. Three matched baseline runs publish
in a median 24 ms; all 12 wheel events per run move the Library and produce a
frame. Exactly 600 durations complete, and warm refresh starts zero new helpers
(median warm readiness 99 ms). The fixture uses a normal QEventLoop with a 10 ms
readiness timer, suppresses thumbnail work, excludes screenshot capture from
heartbeat timing, and guards against accidental navigation away from Library.

**Overall native responsiveness is not accepted:** the baseline median maximum
cold heartbeat gap is 766 ms. Profiling attributes the main pause to synchronous
hidden Edit workspace construction. This is tracked under finding 8 below.
Source-QML synthetic rows and window-delivered Qt wheel events do not establish
packaged-app, real-thumbnail, network-filesystem, or physical input-to-photon proof.

Evidence: [library-duration-native-prewarm.json](evidence/performance-2026-09-28/library-duration-native-prewarm.json).

## 2. Batch project changes

Implemented; isolated controller recovery, real database, and warm native macOS acceptance passed in the workloads documented below:

- Selection adds/removals, individual assignments, project creation, and import
  assignment now share one asynchronous batch path. The Library removal loop is
  replaced with a single model call; creation captures selection before filtering.
- One worker at a time per model preserves ordering. Pending changes for a project
  combine by asset ID, so the latest desired membership wins. The worker owns its
  temporary payload file and process; it does not access the live model.
- New `catalog_state update-project-memberships <payload-file>` uses bound array
  parameters and one PostgreSQL transaction for creation, removals, and valid
  additions. Payload files avoid command-line length limits. Unknown asset IDs
  are reported and remain local without preventing valid members from saving.
- Version 1 `project-membership-outbox.json` stores only membership intent before
  visible mutation. Startup replays it; a failed project gets one attempt per
  revision and retries on explicit refresh, without blocking other projects.
- Catalog snapshots merge pending deltas. The journal retires an entry only after
  a snapshot confirms its memberships and no worker for it is active. Unreadable
  recovery files are preserved. Deletion changes pending additions to removals.

The small atomic journal write still runs on the UI thread; expensive process and
database waits run on workers. No database schema/environment changes. A packaged
app must ship the updated `catalog_state` helper alongside the desktop executable.

Compilation evidence:

- Release catalog model object and generated QML/MOC compiled successfully.
  Log: `/private/tmp/looksy-performance-fix-2-build.log`.
- `cargo check -p photo-catalog --bin catalog_state --offline` passed.
- `cargo build -p photo-catalog --bin catalog_state --offline` passed.
  Log: `/private/tmp/looksy-performance-fix-2-rust-build.log`.

Required runtime acceptance: a large selection causes one helper and one model
reset, with no GUI process waits; create-with-selection retains the original IDs;
rapid add/remove, slow helpers, failed helpers, stale snapshots, restart, deletion,
unknown IDs, and corrupt outbox cases preserve the final intended memberships.
The initial implementation was compiled without runtime tests or database mutations.

### Isolated membership runtime verification

Added `catalog_project_membership_test`, using the production controller with
temporary helper executables, catalog snapshots, cache, and recovery storage.
Helper resolution is checked before model construction; the fixture never invokes
the real database helper or uses the user's catalog.

The test first reproduced an identity defect: case-insensitive project lookup
accepted `bAtCh`, but persistence used that spelling instead of the existing
`Batch` name. `changeProjectMemberships` now uses the existing record's name for
the journal, queue, and helper payload.

After that fix, three consecutive Release runs passed (0.94 seconds each;
2.82 seconds total). In each run, creating a project from 1,000 selected assets
returned in 1 ms, measured at integer millisecond resolution, with one model reset
and one initial helper containing all selected IDs. A 5 ms event-loop heartbeat
continued while the helper was deliberately blocked. This is controller evidence,
not a native frame-time measurement or a database throughput benchmark.

Coverage also checks rapid remove/add/remove coalescing, stale-snapshot overlay,
retiring confirmed intent, canonical project identity, one failure attempt per
revision, explicit refresh retry, replay after controller reconstruction, and exact
preservation of an unreadable recovery file. Helpers simulate acknowledgment and
failure; PostgreSQL transaction semantics, unknown-ID handling, asset deletion,
native scrolling, and shutdown during a blocked save remain acceptance work.

Evidence:

- Reproduced failure: `/private/tmp/looksy-performance-fix-2-membership-test.log`.
- Release build: `/private/tmp/looksy-performance-fix-2-membership-fixed-build.log`.
- Three passing runs: `/private/tmp/looksy-performance-fix-2-membership-repeat-test.log`.


### Real helper and isolated PostgreSQL acceptance (2026-10-01)

`catalog_state` was rebuilt from the current source in Release mode. The new
`tests/catalog_project_membership_postgres.py` creates a fresh temporary PostgreSQL
cluster with a Unix socket and no TCP listener, migrates the full catalog through
the real helper, and seeds 1,000 synthetic assets. It stops its cluster in `finally`.
Three independent runs passed:

- Exactly 1,000 memberships added; an audit trigger records one transaction ID
  for all 1,000 inserts. Repeating the same request adds no rows.
- A single request removes 500 memberships.
- Unknown IDs produce the documented local-only failure while valid additions
  and removals commit with exact resulting membership.
- A forced insert failure rolls back earlier removals and audit writes; a newly
  created project also rolls back with its failed membership request.
- Overlapping add/remove intent is rejected without mutation.
- Deleting an asset through the real helper removes its membership. A later batch
  reports the deleted asset missing while persisting another valid addition.
- A project name containing an apostrophe and SQL punctuation remains literal.

This closes the helper/database transaction correctness gap. It does not establish
native UI responsiveness, integrated controller/database recovery, or large-catalog
throughput; request timings include schema initialization and are not a speedup
comparison. Native selection, scrolling during persistence, and active-save shutdown
remain next for finding 2.

Evidence: [project-membership-postgres.json](evidence/performance-2026-09-28/project-membership-postgres.json).


### Active-save shutdown fix and native/database proof (2026-10-01)

A new regression reproduced an uncanceled membership helper surviving model
destruction beyond the two-second shutdown budget. The model now signals an
owned shared cancellation token, and the existing worker process loop handles
termination and kill escalation. Queued canceled work exits before preparing its
request. No GUI wait was added, and the durable membership outbox is preserved.

Three final controller runs passed all earlier batching/recovery contracts plus
active-save shutdown. Destruction returned in 0 ms; a helper deliberately ignoring
SIGTERM was killed within the two-second gate. Exact outbox bytes survived and a
new model replayed the identical membership payload. Earlier cooperative-helper
runs exited in 7 ms. Per-run final timings are in the evidence JSON.

The native driver now exercises the actual Library with 1,000 distinct cached
240-pixel synthetic thumbnails. In three independent temporary PostgreSQL clusters,
it invokes the production controller save command, holds one fixture helper while
scrolling, then delegates that helper to the real Release `catalog_state` binary.
Independent SQL readback confirms all 1,000 exact IDs under the new project.

| Metric | Three integrated native/database runs |
| --- | --- |
| Save command | 20, 20, 21 ms |
| Model resets / model-dispatched saves | 1 / 1 in every run |
| Maximum heartbeat gap while save held | 59, 63, 63 ms |
| Maximum heartbeat gap during completion | 6, 7, 6 ms |
| Longest wheel-to-frame sample | 11.64, 10.79, 8.34 ms |
| Completion after release | 31, 41, 41 ms |
| Visible thumbnails ready after scrolling | 20 / 20 in every run |
| Exact memberships persisted | 1,000 in every run |

All declared <=100 ms command/heartbeat/wheel gates and exactness guards passed.
Captured native screenshots were inspected and show the selected project, 1,000
results, and decoded thumbnails. These are acceptance timings, not a measured
before/after save-speedup claim. Separate initial native fixture runs also passed.

This accepts the measured warm macOS source-QML Library/controller/helper/database
path and the isolated shutdown/replay contract. It does not establish packaged QML,
physical input-to-photon, RAW decoding, other operating systems, or cold startup.
Initial model rows are injected; recovery permutations use the controller fixture.
The separately measured hidden Edit prewarm problem remains under finding 8.

Evidence: [project-membership-native-shutdown.json](evidence/performance-2026-09-28/project-membership-native-shutdown.json),
[native screenshot](evidence/performance-2026-09-28/project-membership-native-scrolled.png).
Next unfinished implementation/acceptance area: **4, GPU preview readback**; do not
repeat the already implemented Carousel export change.

## 3. Carousel export

Implemented for the app's menu and workspace export actions; runtime acceptance
pending:

- Capture value-only document, media, recipe, variable, export-setting, and output
  plan state. Construct an independent renderer on the worker without initializing
  clipboard connections, preview timers, history, autosave, or design-library IO.
  The live controller is never captured by the worker.
- Run board rendering, encoding, read-back verification, PDF proof generation,
  manifest hashing, video subprocesses, and automatic partial cleanup in a pool
  limited to one export. The UI polls atomic progress at 50 ms intervals.
- Keep one renderer implementation for snapshot export and synchronous callers,
  preserving text, brushes, linked spreads, recipes, profiles, proof, and manifest
  semantics. Shared decoded-image cache access already uses its existing mutex.
- Cancellation is checked between rendering items, PDF boards, 1 MiB hash chunks,
  and 50 ms subprocess waits. Image decoding/encoding currently completes its
  individual operation before cancellation is observed.
- New/closed/loaded/recovered documents invalidate the previous export generation.
  Cancelled workers finish using their own state; stale completion cannot publish
  into the replacement document. Destruction signals cancellation without a GUI
  wait. A new export cannot replace an outstanding watcher result.
- Cleanup uses the captured policy/destination and excludes overwritten files.
  Files appearing after planning are rejected before writing/cleanup. This check
  does not claim atomic exclusion against unrelated external writers.

The app menu (`Main.qml`) and workspace both use the asynchronous path. Explicit
`exportCarouselToDirectory`/`exportConfiguredCarousel` APIs retain their synchronous
completion contract for existing non-UI callers. Startup preflight and output-plan
construction still run on the UI thread; further preflight work is tracked below.

Qt supports worker painting into independent images and copying its implicitly
shared values between threads; see [Qt thread support](https://doc.qt.io/qt-6/threads-modules.html).
The export worker owns each painter and paint device.

Compilation: Release Carousel controller object, automatic MOC, and generated QML
compiled successfully. Log: `/private/tmp/looksy-performance-fix-3-build.log`.
No linked app, native UI interaction, tests, or export-output comparison has run.

Required runtime acceptance: UI heartbeat/input responsiveness through a large
photo/video export; cancellation during encode/probe/hash/proof; identical pixels,
profiles, text, proof pages, and manifest metadata compared with the synchronous
path; edits/settings changes during export cannot alter captured output; safe
close/load/destruction, retry, collisions, and both cleanup policies.

## 4. GPU preview readback — partial implementation

The pixel inspector now has a native-surface sampling path:

- Add `MetalOutputSurface::copy_bgra8_region` with bounds, destination-size, producer
  completion, and cancellation checks. The existing full-image diagnostic helper
  delegates to it without changing its contract.
- Retain the current full native publication for inspection; sample one BGRA pixel
  on a worker. Pixel inspection alone no longer sets `require_cpu_preview_image`.
  CPU images are still used when already available.
- Keep one sample in flight and coalesce pointer movement to the latest position.
  A revision rejects completions after a newer frame, pointer move, asset switch,
  or inspector close. Workers own their retained surface and never touch the live
  controller. No additional dedicated thread pool is introduced.
- Clear stale CPU samples when native-only frames arrive. Partial surfaces are
  not misrepresented as a complete image. The HUD shows a pending state instead
  of displaying a previous pixel's values as current.
- Development telemetry `editor.pixel_inspector.readback` records copied payload
  bytes (4 for a successful sample), surface dimensions, and elapsed time. This
  is application-level readback accounting, not a measurement of hardware bus
  transaction granularity. Include these events when totaling transfer bytes.

Release compilation of the Metal backend and editor controller passed. The first
build reported an existing unused `copy_pixels_to_float_buffer` function warning
at Metal backend line 1011, outside this change. Logs:
`/private/tmp/looksy-performance-fix-4-build.log` and
`/private/tmp/looksy-performance-fix-4-build-final.log`.

This is not completion of issue 4. Native runtime measurements and pixel parity
are pending. Subsequent substeps below address scopes, diagnostics, comparison,
and the CPU display base used for regional paint. Rendering-upgrade reference
evidence and exact soft-proof gamut masks keep their existing CPU requirements;
no measured speedup is claimed from any new branch alone.

Required acceptance for this substep: identical sampled channel values at image
edges/interior, native sRGB and device-profile outputs, pointer coalescing and
asset-switch/clear safety, no UI wait for GPU completion, and evidence that an
inspector-only workflow remains on native publication with one-pixel sample
payloads. CPU comparison sampling and ordinary preview output must remain correct.

### Scope sampling

Source implementation and Release object compilation completed; runtime acceptance
remains pending:

- `MetalWorkingSurface::copy_sampled_pixels` waits for its producer on the render
  worker and copies only the selected float pixels from the shared Metal buffer.
  It uses the same center-nearest coordinates as the existing CPU sampler, checks
  dimensions and finite channels, and observes cancellation before publication.
- Eligible native previews can retain their output surface with scopes visible.
  Working-domain scopes sample Geometry directly. Output-domain scopes process
  those samples through the existing float photographic-output/device stages,
  avoiding an 8-bit display-texture approximation. Scene-linear scopes keep their
  existing CameraCharacterization observer. Proofing and other CPU consumers
  retain their existing eligibility restrictions.
- Samples keep the existing 1,024-pixel longest-edge limit. The reduced output
  pass bypasses stage-cache reads and writes so it cannot contaminate full-frame
  results. Interactive drafts and viewport-only updates keep deferred scopes.
- Missing scope data retries the established CPU route. Cancelled or failed
  histogram packing cannot return a successful native result without scope data.
- `editor.scopes.sampled_readback` records dimensions, copied float payload bytes,
  domain, and sampling time. Output telemetry includes the reduced pass and any
  abandoned native attempt before fallback. Copied bytes are application-level
  accounting, not measured hardware memory traffic.

Release editor-controller and Metal-backend object compilation passed, along
with generated MOC/QML dependencies. Log:
`/private/tmp/looksy-performance-fix-4-scopes-build.log`. The existing unused
`copy_pixels_to_float_buffer` warning remains. Focused `git diff --check` passed.
No linked app, native execution, new tests, or benchmark runs were performed.

Required acceptance: CPU/native scope-bin and float-sample agreement in each
supported domain, device-profile output, odd dimensions and images smaller than
the sample limit, scope toggles, drafts, cancellation, and fallback. Measure total
preview latency and all transfer counters with scopes on/off on the same native
Metal workload; the reduced pass has its own CPU/GPU cost, so fewer copied working
pixels alone do not establish a speedup. Next source priority remains the other
full-preview CPU consumers listed above.

### Diagnostic overlays

Implementation now routes app actions and preview publication through a worker:

- One active job and one replaceable pending frame bound concurrency. A single
  timer replaces recursively scheduled callbacks; work waits for preview editing
  to settle. The worker owns its images and retained surface and never accesses
  the controller or image provider.
- Eligible native previews copy the final BGRA8 surface on the worker rather than
  forcing float CPU preview materialization. This is still a full display-image
  readback (four bytes per pixel), not a zero-readback overlay implementation.
  Existing CPU previews reuse their image. Soft-proof diagnostics retain the
  exact gamut-mask/CPU-preview path.
- All six overlays use the same existing pixel algorithm and dimensions. Only
  completion publishes image-provider state on the UI thread. The synchronous
  reference renderer remains available to existing internal callers; app callers
  use the worker queue.
- New display frames cancel older jobs and remove obsolete visible overlays.
  Revision checks reject stale completion after disable, image-provider change,
  asset replacement, or clear. Cancellation is checked per overlay scanline;
  an in-progress Metal read completes on its worker before cancellation is seen.
- Failed native readback requests a CPU preview and retains that fallback until
  diagnostics are disabled or another asset loads. No automatic readback retry
  loop is introduced. Allocation failures cannot publish partial overlay sets.
- `editor.diagnostics.render` logs copied bytes, dimensions, elapsed time,
  cancellation, and readback failure, including cancelled jobs. Include these
  events in transfer totals alongside the main preview and inspector events.

Runtime acceptance remains pending: exact image agreement for all six overlays,
native sRGB/device-profile outputs, soft-proof masks, rapid edits and toggles,
asset switching, provider replacement, readback failure, and shutdown. Compare
input/frame latency and total CPU/GPU transfer costs on the same workload. GPU
overlay kernels and selected-overlay-only generation are further opportunities;
neither is claimed implemented here.

Release editor-controller object compilation and generated MOC dependencies
passed after the final lifecycle changes. Log:
`/private/tmp/looksy-performance-fix-4-diagnostics-build-final.log`.
Focused `git diff --check` passed. No new tests, benchmarks, linked application,
or native UI execution were run for this substep.

### Comparison display images

Source implementation added; runtime acceptance remains pending:

- Introduce `require_cpu_display_image` separately from the existing CPU-preview
  requirement. Ordinary comparison requests can use canonical native working and
  output stages, then copy the final BGRA8 image on the render worker. Existing
  split/wipe QML image consumers still receive complete images; side-by-side can
  also retain the native edited surface.
- The reference child request independently renders its own recipe/source/crop
  and can use the same final-display copy. Snapshot, external reference, selected
  adjustment groups, and mask comparison selection retain their existing state
  construction. Rendering-upgrade evidence keeps its CPU route.
- sRGB copies are tagged sRGB; device copies carry the resolved ICC profile.
  Existing display publication can recognize already-encoded destination pixels
  and avoid applying that transform twice. Invalid tags, allocation failure, or
  readback failure use the established CPU route. Cancellation is checked before
  publication, and fallback accounting includes abandoned native work.
- Legacy/noncanonical paths still provide CPU images; their native-only shortcut
  is excluded when a CPU display image is required. Soft proofing retains its
  existing CPU requirements; regional paint is addressed below.
- `editor.preview.display_readback` records copied payload bytes and elapsed time.
  These bytes are also included in the normal output-LUT transfer counters, so
  the event must not be added to those counters a second time. Reference child
  rendering already emits its own completion telemetry.

This changes full float readback to final four-byte-per-pixel readback on eligible
paths; it does not make comparison readback-free. Required acceptance includes
all comparison layouts, original/snapshot/external/section references, crops,
native sRGB and device profiles, scopes, cancellation/fallback, and toggling
comparison during rapid edits. Check displayed pixels and current-generation
publication, plus total edited/reference render time and transfer counts.

Release editor-controller object compilation and generated MOC/QML dependencies
passed. Log: `/private/tmp/looksy-performance-fix-4-comparison-build.log`.
Focused `git diff --check` passed. No new tests, benchmarks, linked application,
or native UI execution were run for this substep.

### Regional paint display base

Source implementation added; runtime acceptance remains pending:

- Paint-layer previews now request a CPU display image instead of forcing float
  CPU materialization. Eligible native output supplies the final BGRA8 base;
  CPU-only/full-resolution/proof paths retain their established rendering route.
- Native display images carry an internal output-backend tag through QImage
  cache entries and paint-base copies. Input-image metadata is cleared of that
  tag after decoding; only our publication path assigns native provenance.
  A subsequent display color-space conversion invalidates the tag.
- Regional rendering follows the captured base's backend. A native base requires
  a native display-copy patch with the same backend tag and color profile.
  Missing native support, an explicit CPU requirement, a changed profile, or a
  fallback mismatch rejects the regional shortcut and uses the full preview.
  CPU bases continue to use CPU patches. The existing nonlocal-effect, geometry,
  RAW, mask, and comparison restrictions remain.
- The region renders without a whole-frame scope plan. The settled frame remains
  responsible for scopes. Base cloning and region composition remain CPU work;
  this is not a fully GPU paint compositor.
- `editor.paint.region_render` records attempted patch size, base size, elapsed
  time, acceptance, and transfer bytes, including rejected attempts. Successful
  patch results also expose their GPU/output-LUT counters. Do not double-count
  those counters and the event; include rejected attempts in workload totals.

Required acceptance: first and repeated strokes, commit/cancel/undo, erase and
transparent layers, source/profile switching, odd dimensions, GPU unavailability,
and native/CPU fallback. Compare regional and full-frame pixels including patch
boundaries and check that unchanged surroundings remain exact. Measure first
feedback, settled publication, copied bytes, and whole-base copy cost.

Release editor-controller object compilation and generated MOC dependencies
passed after the final provenance and fallback changes. Log:
`/private/tmp/looksy-performance-fix-4-paint-build-final.log`.
Focused `git diff --check` passed. No new tests, benchmarks, linked application,
or native UI execution were run for this substep.


### Native settled-preview rejection and bounded stop (2026-10-01)

The new opt-in `--native-readback-proof` in the actual-app driver reaches the
native cocoa Edit viewport using isolated settings/data and a generated 1800x1200
image. It applies exposure 0.17, waits for both interactive and full rendering,
and requires a retained native output surface with no CPU inspector image before
checking pixel parity, one-pixel payloads, pointer coalescing, and clear safety.

**The unchanged app fails that first requirement.** An interactive Metal render
is followed by a settled `max_preview` CPU image. `displayPreviewNativeAvailable`
alone is insufficient: a CPU-produced image uploaded into a native texture also
satisfies it. The retained inspector surface is absent, so the inspector's new
one-pixel worker path is not exercised by this ordinary settled workflow.

Three measured hypotheses were attempted and rejected:

1. Permit native final-display publication for full previews within the existing
   2048-pixel selection-preview bound, keeping CPU working execution.
2. Also allow native full-surface adoption before a BGRA8 texture exists; retain
   the existing texture/format checks specifically for partial patch uploads.
3. Also allow the existing compiled canonical working kernels for those bounded
   full previews. This changed the full working backend to Metal, but publication
   still produced a CPU image and failed the primary gate.

All four modified product files were restored to their pre-attempt bytes. The
opt-in native regression probe, baseline CPU reference capture, logs, and receipt
remain. No pixel-parity, memory, latency, or speedup acceptance is claimed because
none of the candidates reached a native-only settled frame. No fourth optimization
hypothesis was attempted.

The diagnostic trace found that native device-profile publication requires a
retained Metal working surface, and that its cached ICC LUT was available on one
full render but absent on the next. Why LUT availability changed is not proven.
A complete repair must address settled publication and ICC-LUT availability
without weakening color/profile correctness or memory budgets. sRGB-only output,
scopes, diagnostics, comparison, and regional paint still need their original
acceptance work.

Evidence: [native-inspector-settled-blocker.json](evidence/performance-2026-09-28/native-inspector-settled-blocker.json).
Next highest unmeasured implementation area after this bounded stop: **5, CPU effect kernels**.

### Display ICC LUT identity reuse (2026-10-02)

A follow-up to the settled-preview investigation found a reproducible cache
mismatch. Prewarming and CPU publication named the linear Rec.2020-to-display
transform `preview-output:aces2-rec2020-interchange`; canonical publication named
the identical transform `preview-device:linear-carrier`. Target identity is part
of the engine's cache key. The two-entry LUT cache therefore held competing
copies of the same Interactive/Final tables.

All three routes now use shared device-transform options. Profile payloads,
intent, black-point compensation, pixel format, LUT quality and accuracy gates,
and the two-entry/32 MiB cache limits are unchanged. Export retains its separate
identity. This is one scoped cache fix; full-resolution native admission and
native-only settled publication are still unresolved.

Three alternating fresh-process policy comparisons prewarm both qualities, then
request native draft, native final, and CPU final tables three times. The old
identity policy misses, rebuilds, and evicts on all nine requests; the shared
identity has zero misses, rebuilds, or evictions. Cache residency falls from
30,975,008 to 16,095,264 bytes. Median sequence time is 2532.64 ms versus 0.037 ms,
but this fixture deliberately joins missing builds to make eviction deterministic.
Production prewarming is asynchronous; these numbers are not tool latency.

The exact ICC processor matches on 4096 RGBA samples, and both LUT tables and
unsafe-cell maps match byte for byte. Profile, intent, BPC, and carrier-route
separation checks pass. Three runtime color suites, the host Metal ICC parity
test, and the desktop build pass. An older source-text contract fails because
it requires a contiguous `Aces2OutputLutQuality::Interactive` token; recompiling
that probe against the saved pre-change sources reproduces the same failure.

This establishes a duplicate-cache mechanism, not the precise cause of the
previous actual-app missing-LUT observation. Native foreground acceptance remains
unavailable while the session is locked. Evidence:
[device-lut-identity.json](evidence/performance-2026-09-28/device-lut-identity.json).

## 5. CPU effect kernels — partial implementation

Convolution now uses the existing color-adjustment worker scheduler:

- Gaussian convolution (also used by unsharp mask and Optical Diffusion) and
  motion convolution (also used by Dragged Shutter) process independent rows.
  Box blur processes independent rows/columns, retaining its running-sum order
  within each line. Each task writes disjoint preallocated output pixels.
- Per-pixel kernel accumulation, double intermediates, alpha handling, edge
  mapping, and final mixing are unchanged. The transactional image swap remains
  after successful processing; cancellation cannot publish a partial image.
- Small workloads stay serial: fewer than 16 lines or below approximately 65,536
  estimated sample operations. This threshold is an initial heuristic, not a
  measured optimum. Large-radius Gaussian/box initialization now also checks
  cancellation within kernel loops.
- Reuse the persistent shared pool; do not create per-effect workers or extra
  full-size scratch images. Nested worker calls retain the scheduler's serial
  behavior. Tile orchestration still uses one tile's scratch at a time while
  its convolution lines can run concurrently.
- Dragged Shutter no longer constructs unused Gaussian coefficient arrays before
  executing its motion kernel.

The scheduler admission review found an existing lifetime hazard: allocation
failure while enqueueing could leave only part of a batch visible to workers,
then unwind the caller's captured buffers. Task allocation/callable copies now
finish in a private list before a nonallocating splice publishes the batch under
the queue lock. Thread-creation failure retains successfully started workers
(or serial execution) instead of unwinding joinable threads. Queue priority and
the process-wide CPU budget are still tracked separately under finding 12.

Runtime acceptance remains pending: identical pixels across edge modes, masks,
alpha/HDR values, radii, scaling, tiled/full-frame operations and nested callers;
cancellation and resource-failure lifetime checks; draft/settled/export time and
interactive responsiveness under concurrent work. GPU effect stages and broader
intermediate/scratch reuse still need work. This is not completion of finding 5.

Release compilation of the effect and scheduler objects passed. Logs:
`/private/tmp/looksy-performance-fix-5-convolution-build.log`,
`/private/tmp/looksy-performance-fix-5-scheduler-build.log`, and
`/private/tmp/looksy-performance-fix-5-convolution-final.log`.
Focused `git diff --check` passed. No new tests, benchmarks, or linked application
were run for this substep; no numerical speedup or runtime parity is claimed.

### Independent pixel passes and tile reuse

- Invert/posterize, Imperfect Print, Optical Diffusion source preparation, and
  final effect blending now distribute independent rows through the same pool.
  Expressions within each pixel remain in their original order. An atomic error
  flag gathers diffusion range failures; batches finish before error reporting
  or any transactional image swap. Finite-input validation remains intact.
- Small-workload serial admission and cancellation apply to these passes too.
  Imperfect Print uses its existing absolute document coordinates and seed, so
  scheduling does not introduce shared random state or change tile origins.
- The tiled stack retains its tile image, mask buffers, and binding vector between
  tiles. Growth releases the old allocation before requesting a new one; equal
  or smaller tiles reuse capacity. Stage admission subtracts retained capacities,
  including masks, rather than charging only the current tile's logical size.
  Added row cancellation checks cover tile extraction and stitching.
- Each tile is still processed in sequence to preserve the existing scratch
  bound. Intermediate working/output images inside each effect/stack are still
  allocated by their transactional stages; no broader buffer pool is claimed.

Release effect-object compilation and focused whitespace checks passed. Logs:
`/private/tmp/looksy-performance-fix-5-pixel-passes-build.log` and
`/private/tmp/looksy-performance-fix-5-tile-reuse-build.log`.
No new tests, benchmarks, or linked application were run. Acceptance must cover
masked/zero-amount/transparent pixels, diffusion range errors, deterministic
print noise, cancellation under concurrent workers, and tight budgets across
growing/shrinking edge tiles. Pixel equality, runtime speedup, and allocation
reductions are not yet measured.

### Matched CPU-kernel acceptance (2026-10-01)

Gaussian, unsharp-mask, and diffusion convolution now premultiply each source
pixel once and interleave four independent pixel accumulations. Each pixel keeps
its original tap order and double intermediates. Gaussian/unsharp preparation
uses the existing output buffer before vertical convolution overwrites it;
diffusion prepares its existing glow buffer. No additional image buffer is added.
Finite glow validation still precedes premultiplication. Cancellation and errors
retain the transactional publication boundary.

The baseline already contains the earlier scheduler and tile-buffer changes.
An interior-coordinate shortcut was rejected after a tiled diffusion regression.
Premultiplication alone improved the 2048-pixel probe by about 12 percent, below
the 20 percent target. The third candidate added interleaved accumulation and
passed the declared gates. No further optimization candidates were attempted.

Final measurements use three alternating independent process pairs at each width
(127, 1024, 2048), with three calls per case per process. No builds, tests, or
repository searches overlapped the comparisons. The fixture exercises actual
engine APIs with soft masks, negative/HDR RGB, varying alpha, all supported edge
modes, and whole/tiled Gaussian and diffusion. Source copies occur outside timing;
internal effect allocations remain included. Values below are medians of process
medians for 2048 × 1366 pixels, not native application latency.

| CPU workload | Before | After |
| --- | ---: | ---: |
| Optical Diffusion, 256-pixel tiles | 426.573 ms | 228.544 ms |
| Optical Diffusion, whole frame | 242.104 ms | 100.969 ms |
| Gaussian blur, Extend, whole frame | 104.365 ms | 47.248 ms |
| Unsharp mask, Extend | 30.513 ms | 16.746 ms |
| Maximum process RSS at this size | 279.875 MiB | 276.047 MiB |

Tiled diffusion improves **46.4 percent**. All 63 effect/edge/size cases have
identical before/after float-bit hashes. Every case passes the latency guard
(baseline × 1.15 + 1 ms), and each size passes the memory guard
(baseline × 1.1 + 8 MiB). The new reusable comparison runner records raw runs, executable hashes,
case inventories, primary target, and all guards.

Spatial, Gaussian, posterize, and codec contracts each passed three final runs.
New spatial coverage includes all four convolution tail lengths, nested shared
worker calls with exact output, resource rejection, and asynchronous cancellation
without source publication. Existing tests cover edge behavior, scaled/tiled
stacks, alpha/HDR, zero masks, and diffusion overflow. Cancellation coverage is
at the API boundary and does not claim injection at every internal stage.

**Accepted scope: this CPU-kernel optimization. Finding 5 remains partial.**
GPU effect execution, real-photo/native UI acceptance, concurrent interactive
responsiveness, 24/45 MP export timing, allocation-failure injection, and broader
scratch reuse remain open. The next ranked implementation gap is finding 6.

Evidence: [gates, source patch, hashes, raw runs, and contracts](evidence/performance-2026-09-28/cpu-effect-kernels.json).

### Native real-photo effect verification (2026-10-02)

Three alternating process pairs compare the previously accepted CPU kernel with
its preserved predecessor. All other objects and source QML are identical. On a
hash-verified 25.8 MP portrait, median run timings change as follows:

| Measurement | Previous kernel | Accepted kernel |
| --- | ---: | ---: |
| Warm effect edit to settled frame (superseded wait fixture) | 965.1 ms | 661.6 ms |
| Layer rendering stage | 401.0 ms | 233.0 ms |
| Full-resolution 16-bit PNG export | 23.267 s | 19.263 s |
| Maximum process RSS | 6923.3 MiB | 6043.4 MiB |

All four published CPU-preview hashes and all 16-bit export pixels match across
all six runs. Undo/redo restores exact effect parameters. The scene graph uses
Cocoa/Metal, but effect rendering still falls back to CPU. Frame measurements use
controller-injected edits and a frame-swap receipt; three warm edits per run are
not a population tail estimate or physical input/display timing. A later trace
found that this fixture manually pumped events and slept on the UI thread; its
frame timings are not accepted native responsiveness evidence. The streamed
output comparison below uses a normal Qt event loop for both binaries. Exact
pixels, completed export, and worker-stage observations remain valid. Initial runs
that hashed only an 8-bit conversion of PNG output were excluded and repeated.

The 39.7 MP panorama then exposed a separate failure in both kernel versions:
the tiled effect API allocates a 606.1 MiB full-frame output against its default
512 MiB scratch allowance. Full-resolution preview and export fail. Native
large-photo acceptance therefore remains incomplete. The next candidate streams
adjustment-effect tiles into the compositor's existing output transaction,
preserving its rollback boundary and the tile scratch ceiling.

Evidence: [effect-native-real-photo.json](evidence/performance-2026-09-28/effect-native-real-photo.json).

### Stream adjustment-effect tiles into the compositor transaction

Implemented; native performance acceptance remains open. `render_photo_effect_tiles` renders padded tiles
with the same kernels, masks, origins, and support, then lends each cropped tile
to a caller-owned sink. The sink cannot retain the borrowed view or mutate the
source. It may receive tiles before a later failure, so its destination must be
private until the whole operation succeeds. Identity delivers no tiles.

Adjustment layers consume these tiles directly into the existing private output
owned by `apply_photo_layers`. Each pixel uses the previous blend/opacity/clipping
and layer-mask formula. Cancellation or a late failure discards the enclosing
transaction. This removes the redundant full-frame effect output allocation and
keeps the 512 MiB tile scratch ceiling; it does not raise the production limit.
The public transactional tiled API still allocates and charges its own output,
validates masks before allocation, and swaps only after complete success.

Initial checks: six focused suites pass. Streamed output matches the transactional
API exactly across all edge modes, with exactly one sink visit per pixel. Tests
cover sink rejection, cancellation after tile delivery, tight scratch budgets,
identity, and a later-tile overflow that must preserve the compositor's caller.
A 32-case compositor matrix matches float bits against the old full-output path
for Gaussian/diffusion, four blend modes, soft effect and layer masks, clipping,
and partial opacity.

Native timing repetition stopped because results became unstable. An earlier
three-pair comparison failed the 26 MP frame guard (616.5 to 934.1 ms), while
exact pixels, export latency, and memory guards passed. Traces showed 387-537 ms
worker-to-UI delivery gaps while the fixture slept on the UI thread. That trial
is retained as unaccepted evidence; the fixture now uses the normal dispatcher
for both binaries, with the same declared acceptance gates. The reference-only
binary gives the old output
path a 2 GiB scratch allowance so it can produce 40 MP oracle pixels. This budget
change is absent from production. Both comparison executables use identical
frontend objects and frozen QML. The PNG checker preserves all 16 channel bits
and temporarily permits a 1 GiB decode allocation in the fixture, because the
40 MP RGBA16 verification image exceeds Qt's default 256 MiB reader limit.

Three earlier matched pairs complete the 39.7 MP full-resolution preview and
16-bit PNG export with exact pixels. Maximum process RSS falls from 7488.0 to
7021.4 MiB across those runs, but their frame guard fails and is not accepted.
The corrected Qt-event-loop comparison completes one 25.8 MP pair: frame median
675.3 to 589.3 ms, export 132.412 to 137.494 s, peak RSS 5577.2 to 5786.1 MiB.
All pixel, latency, and memory guards pass for that pair. The required three
pairs are incomplete. Export duration is about seven times the earlier trial,
and a host sample shows user Looksy and virtual-machine CPU activity; neither
was stopped. The bounded loop stops instead of treating this as an accepted
speedup. The source candidate remains provisional.

Final correctness checks pass: all six freshly linked focused suites, the
32-case float-bit compositor matrix, and a synthetic 7500 x 6000 linear-invert
adjustment with every pixel exact under the default scratch budget. Its maximum
RSS is 2146664448 bytes; this is not a 45 MP real-photo diffusion/export proof.
The first large fixture used an unregistered effect name and was corrected to
`invert_linear_rgb` before the successful run. The desktop application builds
and links. No packaged-app visual acceptance is claimed. Database helper dispatch
is suppressed during measured setters; undo/redo can still trigger retries to
the fixture's offline endpoint.

Content-layer effect preparation still uses the transactional full-output API;
this candidate does not close its large-raster or extent-expansion limits.
Evidence: [corrected event-loop comparison](evidence/performance-2026-09-28/effect-streamed-output-event-loop.json)
and [earlier unaccepted timing trial](evidence/performance-2026-09-28/effect-streamed-output.json).

### Skip unused luminance preparation for adjustment tools (2026-10-02)

The general adjustment-layer compositor prepared a full float luminance image
for every creative, detail, or optical edit. Only Texture and Clarity consume
that image. Preparation now uses the existing spatial-detail predicate, so those
tools retain the same luminance/blur inputs and other tools avoid the allocation
and conversion pass. This is a separate one-candidate loop on top of the prior
streamed-output candidate; it does not accept that earlier native timing work.

Declared acceptance: at least 80 percent of one 24 MP float plane removed from
peak RSS for diffusion, unchanged float-bit output, median layer time within
10 percent plus 5 ms, and other memory within 5 percent plus 16 MiB. Three
alternating fresh-process pairs, with three repetitions each, pass every gate:

| CPU layer workload | Median before / after | Maximum RSS before / after | Saved RSS |
| --- | ---: | ---: | ---: |
| 24 MP diffusion | 1891.52 / 1875.68 ms | 1976.83 / 1886.23 MiB | 90.59 MiB |
| 24 MP vignette plus 1% invert | 221.65 / 174.19 ms | 1940.86 / 1849.30 MiB | 91.56 MiB |
| 2.8 MP diffusion + Texture + Clarity | 352.85 / 361.34 ms | 383.14 / 383.13 MiB | 0.02 MiB |

All 22 float-bit hash comparisons match across creative effects, vignette,
dehaze, sharpening, noise reduction, Texture, Clarity, and threshold-adjacent
values, both plain and masked/clipped with partial opacity and screen blending.
Noncreative cases include a 1 percent invert effect to force this general CPU
compositor rather than bypassing the target through the retained renderer.
Inputs contain negative/HDR color and varying alpha. The desktop app builds,
and seven focused suites pass. Engine memory acceptance is complete; these
measurements do not establish physical-input latency or a universal optical-tool
speedup.

A native Cocoa/Metal integration pair on a hash-verified 1627 x 2090 photograph
passes all four CPU-preview hashes, exact 16-bit PNG pixels, and effect undo/redo.
Frame median is 724.0 to 697.1 ms and export 1889 to 1878 ms, but the sample is
not repeated native performance acceptance. Whole-process peak RSS rises from
2606137344 to 2974695424 bytes, so the engine saving must not be generalized to
app-wide memory. The after capture shows the photo and Studio controls; the
before capture has an empty viewport. Visual parity is therefore open. A
follow-up image-only readiness predicate timed out and was removed because the
viewport can use a native texture. That diagnostic failure does not establish a
new product failure. Both captures and the failed diagnostic manifest are retained.

Content-layer inspection confirms the remaining output is sampled as a complete
raster for transformed layers and sequential effects. Its large-raster limits
remain open; no content-layer product change is included in this candidate.

Evidence: [effect-detail-plane.json](evidence/performance-2026-09-28/effect-detail-plane.json).

### Stop loading full previews for inactive tool images (2026-10-02)

Native diagnostics found 60 `edited` image-provider requests during four effect
edits plus undo/redo. Hidden comparison branches and loupe images subscribed to
full-resolution preview revisions. The source bindings now load only visible
split/wipe images, visible viewport images, and visible CPU loupe images. The
active viewport continues to load its CPU fallback even when it displays a native
texture. Viewport source/reset identity and saved comparison state are unchanged.

One product candidate, seven source bindings, passes three alternating fresh
native Cocoa/Metal process pairs against identical frozen QML and one shared
driver. The hash-verified real photograph is 1627 x 2090 pixels:

| Measurement | Before | After |
| --- | ---: | ---: |
| Edited-image provider requests | 60 | 12 |
| Sum of requested QImage sizes (median) | 726.41 MiB | 145.28 MiB |
| Maximum process RSS across three runs | 2950.33 MiB | 2856.34 MiB |
| Median run effect-frame p50 | 677.64 ms | 682.13 ms |
| Median 16-bit PNG export | 1868 ms | 2004 ms |
| Median split / wipe readiness | 3044 / 144 ms | 2047 / 143 ms |
| Median first CPU-loupe readiness | 8 ms | 31 ms |

The request reduction is **80 percent**. Requested image sizes are not physical
copy or transfer bytes: QImage storage can be shared. All declared median
latency/memory guards pass, all four preview hashes and 16-bit export pixels
match across six runs, and native texture readback matches the CPU preview when
normalized to RGBA8. All 24 effect/split/wipe/CPU-loupe captures pass photo-region
content checks; representative comparison and fallback captures were visually
inspected. The app builds and the focused outline/viewport contract passes.

The initial comparison fixture selected wipe before activating Compare, which
restored the saved split layout. It was corrected to select afterward and assert
the actual mode; all confirmation pairs were rerun. The loupe fixture explicitly
clears native presentation after rendering settles, then requires its CPU image
to load and present. It does not claim full native-device failure recovery.

**Latency limits remain:** the third candidate process records 1959 ms median
effect-frame time, a 6167 ms wipe opening, and a 2036 ms CPU-loupe opening. Its
QML trace contains repeated approximately one-second frame intervals. This is
accepted work reduction with passing declared median guards, not a uniformly
fast interaction or tail-latency fix. Window exposure/activation and frame cadence
need explicit measurement before attributing those stalls.

An earlier diagnostic trio also reproduced two blank native captures despite
ready QML/controller state. Later GPU readback matches CPU pixels, and all final
confirmation captures contain the photo, but this change is not claimed to fix
that unexplained intermittent presentation issue. A suspected Qt texture-wrapper
replacement leak was ruled out by checking Qt's ownership implementation.
Linked-view acceptance and larger native photo workloads remain open.

Evidence: [hidden-tool-images.json](evidence/performance-2026-09-28/hidden-tool-images.json).

### Streamed adjustment output: repeated engine acceptance (2026-10-02)

The current streamed output implementation now has three alternating, fresh
process pairs for 24 MP Optical Diffusion and 45 MP Gaussian blur. The reference
uses the same current engine, including accepted kernel/luminance improvements,
with only the streamed adjustment block replaced by its earlier full-output
path. No new product edits were made for this comparison.

| Measurement | Full-output reference | Streamed output |
| --- | ---: | ---: |
| 24 MP diffusion, median run median | 1936.79 ms | 1981.42 ms |
| 24 MP diffusion, maximum peak RSS | 2251.41 MiB | 1885.31 MiB |
| 45 MP Gaussian, median run median | 652.17 ms | 585.64 ms |
| 45 MP Gaussian, maximum peak RSS | 4145.78 MiB | 3460.28 MiB |

Peak memory falls **366.09 MiB (16.3%)** and **685.50 MiB (16.5%)**, respectively.
The 24 MP case is 2.3% slower, within the declared 10% + 50 ms guard. All six
paired output hashes match. Twelve mask/detail benchmark cases and 32
blend/effect-mask/layer-mask/clipping cases also match. The existing spatial and
layer-effect suites pass, including cancellation and failure rollback.

The old path rejects the 45 MP fixture with its original 512 MiB budget. Its
private comparison executable alone allows 2 GiB to produce reference pixels;
the current implementation completes using the default 512 MiB tile budget.
The 45 MP timing therefore compares against an explicitly enlarged reference,
not a working old production configuration.

**This is supplemental engine acceptance.** The original native frame/export
guards remain open, and this synthetic HDR/alpha corpus does not establish
foreground tool latency, real-photo visual quality, or content-layer limits.
The locked desktop session prevents valid foreground acceptance at present.
Evidence: [effect-stream-engine.json](evidence/performance-2026-09-28/effect-stream-engine.json).

### Content effects: bounded delayed rows replace full output (2026-10-02)

Content effects now reuse the private prepared layer raster instead of allocating
another full-frame transactional output. A bounded ring stores finished rows
until later tiles no longer need their original source halo. This preserves the
complete resulting raster used by rotation, scaling, text sampling, and subsequent
effects. Pending rows and all tile scratch share the original 512 MiB budget.

The new `apply_photo_effect_stack_to_private_raster` API explicitly permits
partial private output on failure. Its only production caller owns a prepared
layer copy inside the compositor transaction; failure discards that layer and
the compositor output. Existing public transactional effect APIs are unchanged.

One candidate passes three alternating fresh process pairs for each workload:

| Measurement | Before | After |
| --- | ---: | ---: |
| 24 MP content Gaussian, median run median | 1101.17 ms | 1209.37 ms |
| 24 MP content Gaussian, maximum peak RSS | 2961.25 MiB | 2617.78 MiB |
| 24 MP content diffusion, median run median | 2877.94 ms | 2942.57 ms |
| 24 MP content diffusion, maximum peak RSS | 3029.66 MiB | 2671.48 MiB |
| 45 MP content Gaussian / Extend, median run median | 2273.85 ms | 2126.31 ms |

The two 24 MP workloads save **343.47 MiB** and **358.17 MiB** of peak memory,
clearing the declared 300 MiB target. Their latency increases by 9.8% and 2.2%,
within the declared before × 1.10 + 50 ms guard. This is primarily a memory and
large-raster completion improvement, not a claim of faster ordinary tool response.

The old 45 MP nonexpanding path rejects the original scratch budget. A private
full-output oracle alone receives a 2 GiB allowance; the new production path
completes under its original 512 MiB budget with matching output and 654.58 MiB
lower peak process memory than that oracle.

All nine paired output hashes match. Ten additional content fixtures cover
transforms, sequential effects, masks, clipping, and transparent/Extend edges.
Direct exact-pixel contracts cover three edge modes and four tile sizes, including
tiles smaller than the blur halo and ring wraparound. Scratch rejection,
asynchronous cancellation, and a later content failure preserve the required
ownership/rollback behavior. Five focused suites and the desktop build pass.

**Remaining:** transparent content expansion still has a separate 512 MiB raster
cap, so the 45 MP result does not cover expanded transparent-edge effects. Native
foreground and real-photo visual acceptance remain open while the desktop is
locked. Evidence: [content-effect-rows.json](evidence/performance-2026-09-28/content-effect-rows.json).

### Content compositing: parallel output rows (2026-10-02)

A CPU sample of the existing content workflow showed transformed sampling and
blending on the calling thread. The content compositor now assigns independent
destination rows to the existing bounded CPU worker pool. It completes each layer
before starting the next, reads immutable content/masks/text backdrops, and keeps
the same per-pixel arithmetic. Rasters below 65,536 pixels retain the serial path.
Both content paths now observe cancellation between rows and join workers before
returning. The existing worker mechanism executes nested calls inline.

One candidate passes three alternating fresh process pairs, with three renders
per process. Results are median per-run medians:

| Workload | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| 24 MP plain content | 702.86 ms | 128.43 ms | 81.7% |
| 24 MP rotated/scaled content | 761.15 ms | 124.41 ms | 83.7% |
| 24 MP Gaussian content effect | 1052.35 ms | 422.20 ms | 59.9% |
| 3 MP rotated glass text | 279.22 ms | 46.41 ms | 83.4% |
| 24 MP diffusion content effect | 2713.73 ms | 2071.77 ms | 23.7% |

All four declared primary workloads exceed the 20% improvement target. Diffusion
and four small-raster workloads pass their latency guards; every workload stays
within before × 1.05 + 32 MiB peak RSS. Every paired output hash matches, as do 20
additional content fixtures covering masks, clipping, rotation, sequential
effects, embossed text, and frosted-glass text.

A new contract compares ordinary parallel output against 96 concurrent nested
serial renders, including pixel/source/text content, transformed and untransformed
placement, masks, clipping, and glass backdrops. Five focused suites and the
desktop build pass. These results include the delayed-row memory change above,
which is retained in both sides of this comparison.

**Proof boundary:** this is CPU content composition and effect-workflow timing,
not native input-to-frame latency. The current session remains locked with
`loginwindow` frontmost, so foreground tool acceptance remains pending. Transparent
content expansion still has its separate raster cap. Evidence:
[content-compositing.json](evidence/performance-2026-09-28/content-compositing.json).

### Layer-stage mask preparation: skip unused image snapshot (2026-10-02)

Inspection found that `apply_color_layers_stage` copied the entire input image
for edge refinement on every layer render. Only its legacy local-mask branch
reads that snapshot. Mask sets prepare their own immutable source; content layers,
legacy gradients, and retouch do not consume the outer copy. It is now allocated
only for its consumer. The empty legacy-gradient loop is also skipped when both
gradient flags are off. Active edge-source pixels and refinement math are unchanged.

One candidate passes three alternating fresh process pairs per 24 MP workload:

| Workload | Peak memory saved | Median run timing, before → after |
| --- | ---: | ---: |
| Plain content | 366.23 MiB | 125.94 → 101.86 ms |
| Gaussian content effect | 366.78 MiB | 447.43 → 441.35 ms |
| Adjustment-layer diffusion | 364.75 MiB | 2633.61 → 1939.87 ms |

All three memory reductions exceed the declared 300 MiB target and pass latency
guards. Diffusion timings varied substantially across the alternating processes;
these timings support the declared median regression guard, not a uniform speedup
claim. The accepted result is removal of an unnecessary full-frame allocation.

Forty before/after output hashes match across layer masks, legacy local masks,
mask sets, both local representations present, linear/radial gradients, and clone
retouch. Thirty activity checks require the enabled local/gradient/clone fixtures
to produce different pixels from their unmasked baseline. Legacy-mask, mask-set,
and combined-representation timing/RSS guards pass in three additional pairs.

The initial clone fixture omitted its offset and canonical raster coverage and
failed the activity check; an offset-only correction also failed. The final
fixture supplies both, and the complete confirmation was rerun. These failures
are excluded from acceptance rather than counted as clone coverage.

Seven focused suites, including mask coverage and editor-preview math, pass.
The desktop build passes. This work preserves the recent content-row memory and
parallel-compositing changes in both reference and candidate builds. Native
foreground/real-photo acceptance and the separate transparent-expansion cap remain
open. Evidence: [unused-mask-source.json](evidence/performance-2026-09-28/unused-mask-source.json).

### Content effects: skip fully clipped final padding (2026-10-02)

Full-canvas content previously allocated a larger transparent raster for its
final blur even though composition clips every added pixel. The last active
Gaussian, box, unsharp, or motion effect now uses the existing private raster
when its actual kernel edge mode is Transparent, the layer covers the canvas,
and it has no transform. Fill layers qualify after rasterization. Text materials,
partially covering or transformed content, earlier effects, and other kernels
retain expansion. The production allocation and scratch limits are unchanged.

Three alternating fresh process pairs, with three renders per process, pass:

| Gaussian content workload | Peak memory saved | Median run timing, reference → candidate |
| --- | ---: | ---: |
| 6000 × 4000 (24 MP) | 369.06 MiB | 412.06 → 396.13 ms |
| 8256 × 5504 (45.44 MP) | 697.06 MiB | 774.67 → 723.99 ms |

The old production path rejects the larger raster. Its pixel oracle changes
only the expanded-raster ceiling in a private reference build, leaving its effect
scratch limit unchanged. The candidate completes at the original production
limits and matches the oracle hash. These results establish allocation removal
and the declared median timing guard, not native tool latency.

The 180-case matrix covers five effects, three edge modes, masks, clipping,
blend modes, oversized and partial content, transforms, text, effect ordering,
source/fill layers, and tile boundaries. Each candidate output matches an explicit
expanded reference and a separately linked pre-change engine. Three paired
diffusion and rotated-content guards pass with matching hashes and bounded timing
and RSS. All eight focused suites and the desktop build pass. Native foreground
and real-photo visual acceptance remain pending. Evidence:
[clipped-effect-padding.json](evidence/performance-2026-09-28/clipped-effect-padding.json).

## 6. Catalog snapshot preparation — partial implementation

`CatalogAssetModel` now moves JSON decoding and per-asset canonical/legacy recipe
projection into the existing bounded catalog worker pool. The worker captures
snapshot bytes, a recipe asset path, a generation, and an atomic cancellation
flag; it does not capture the model or mutate its live state. Only projected
assets and required Library metadata return to the model thread. The completed
asset vector moves into model storage without an additional full row copy.

A model has at most one fetch or parse stage in flight. Repeated refresh requests
invalidate the active generation and coalesce into one subsequent fetch. Killing
an obsolete helper no longer calls `waitForFinished()` from `refresh()`. A parse
already in progress checks cancellation between assets; Qt JSON decoding and an
individual recipe projection are not interruptible internally. Loading stays
active until the latest request is published or fails. Failed-to-start helpers,
invalid JSON, and worker preparation errors preserve the last published catalog.
Shutdown signals cancellation without passing a destroyed controller to workers.

Publication retains the existing recipe overrides and membership outbox overlay.
It additionally preserves ratings (including resetting to defaults), relinks,
and deletions made during fetch/preparation, even if their persistence has
already finished before the snapshot arrives. Superseded completions cannot
publish assets or clear the newer request's loading state.

Remaining work for this finding: the helper still fetches all assets and full
recipes; derived metadata rebuilding, filtering/model reset, and import-source
discovery still execute on the model thread. Paging, lightweight records/on-demand recipes, incremental publication,
and realistic large-catalog acceptance remain necessary. Sharing the bounded
catalog pool avoids adding another pool, but queue priority under concurrent
membership operations still needs measurement.

Acceptance must cover repeated refresh during fetch and parsing; process-start,
malformed-output, and projection failures; shutdown during work; edits/deletions
while a snapshot is in flight; selection and membership preservation; and
1k/10k/100k catalogs with raster-heavy recipes. Record UI input delay, worker and
publication time, payload size, peak memory, and first usable viewport. No speedup
or runtime behavior is claimed from source changes alone.

Release compilation of the catalog model and generated MOC objects passed.
The final catalog object was rebuilt successfully after preserving local-change
tracking across coalesced refreshes. Logs:
`/private/tmp/looksy-performance-fix-6-parse-build.log` and
`/private/tmp/looksy-performance-fix-6-parse-final.log`.
Focused `git diff --check` passed. No tests, benchmarks, or linked application
were run for this substep; runtime acceptance remains open.

### Library metadata preparation and indexed rebuilding

Project records/membership sets, review-state normalization, and relink-path
normalization now run in the snapshot worker. A typed `CatalogLibraryState`
replaces the JSON metadata passed to the UI thread; accepted state moves into
model storage before the existing pending-project and concurrent-edit overlays.
Absent metadata retains its previous behavior: no metadata fields means keep
local state; any metadata field means replace all three persisted collections.
Cancellation is checked between project records, membership IDs, review records,
and relinks. Worker parsing preserves project-name whitespace normalization,
created-at fallbacks, rating clamping, rejection precedence, allowed color labels,
and empty/invalid entry handling.

Two remaining UI algorithms were reduced without changing their ordering rules:

- Project labels now build an asset-to-project-name index by walking memberships
  once, sort each name list, then assign it to assets. This replaces a membership
  test for every asset/project pair with work proportional to actual memberships
  plus assets (and the per-asset name sorts).
- Camera facets now use case-folded hash keys for uniqueness, retaining the first
  spelling encountered and the existing locale-aware display sort. Capture years
  use an integer set and retain descending ordering. Previously each camera label
  searched the accumulated list, which becomes quadratic when mixed-media file
  basenames supply many unique labels.

`catalog.snapshot.publish` records payload bytes, incoming asset count,
preparation time, publication time, and generation. Preparation includes JSON,
recipe, and Library metadata decoding; it excludes helper fetch and worker queue
wait. Publication includes synchronous model callbacks and import-source refresh;
it excludes the later loading-state signal and telemetry write. These are phase
measurements for future profiling, not first-visible-frame or interaction metrics.

Acceptance additionally needs equivalent project ordering/membership, camera
facet spelling and Unicode case behavior, review defaults, relinks, and absent
metadata handling. Peak memory should include the temporary membership index.
Release compilation of the catalog model and generated MOC objects passed, with
no compiler warnings or errors in
`/private/tmp/looksy-performance-fix-6-metadata-build.log`.
Focused `git diff --check` passed. No tests, linked application, or performance
benchmarks have been run; runtime correctness and latency acceptance remain open.

### Streamed helper serialization

The snapshot helper now streams the ordered asset SELECT through SQLx and writes
one converted asset at a time to compact JSON using a 64 KiB output buffer. It
no longer collects every `PgRow`, a converted asset vector, and a pretty-printed
JSON string for this path. The existing vector-returning API remains for other
callers, sharing the same SQL constant and row conversion to preserve asset
fields, ordering, limit semantics, legacy/canonical recipes, and import dates.
This is one SELECT rather than offset pages, so concurrent inserts cannot cause
page-boundary skips or duplicates. Other metadata queries retain their existing
separate-query consistency semantics. The helper still reads every full recipe.

The output remains one JSON object with the same field names and trailing newline.
Array separators, query/row/serialization failures, broken pipes, and final flush
errors are handled explicitly. A failure returns a nonzero process status; the
model only parses a successful completed process. Snapshot stdout and stderr are
now separate, and failure messages use at most 4 KiB of stderr instead of exposing
a potentially large partial JSON document in the UI. The last accepted catalog
remains visible on failure.

`futures-util` is now a direct catalog dependency for SQLx's stream iterator. Its
version was already present in the lockfile; no package version was upgraded.
The desktop still buffers and parses a complete payload, so this does not yet
provide paged first-viewport delivery, on-demand recipes, or incremental model
publication. Helper memory now scales with a current row and driver buffers for
the asset stage, rather than application-owned vectors of all rows/assets;
metadata collections and the desktop payload still scale with catalog size.
Actual peak RSS, transport savings, and latency have not been measured.

Required acceptance includes empty/limited/unlimited catalogs, exact parsed JSON
agreement with the existing snapshot API, large recipes, slow consumers, database
failure midway through a stream, broken pipes, and refresh cancellation. Measure
helper and desktop memory separately and preserve the previous corruption and
recovery checks. No tests, live database helpers, or benchmarks were run.

Release desktop object compilation passed:
`/private/tmp/looksy-performance-fix-6-stream-desktop-build.log`.
Offline Rust checking of the snapshot binary passed:
`/private/tmp/looksy-performance-fix-6-stream-check.log`.
Offline checking of all catalog binaries, including the packaged `catalog_helper`
dispatcher, passed: `/private/tmp/looksy-performance-fix-6-stream-bins-check.log`.
Focused `git diff --check` passed. Compilation does not establish runtime snapshot
parity or measured memory/latency improvements.

### Bounded parallel recipe preparation (2026-10-01)

Snapshot recipe preparation now uses the shared CPU scheduler in 32-record
batches. With the scheduler's current eight-item task granularity, a catalog
batch admits at most four tasks. Queued image work can run between batches.
The result vector is sized and detached before admission; workers write disjoint
slots through its stable data pointer. Source JSON remains immutable. The
scheduler joins callbacks before captured storage can unwind, including exceptions
and cancellation. Original array order, projection rules, generation checks,
local-edit overlays, and the final publication boundary are preserved.

Three measured candidates were evaluated. Full-array admission improved load
time but a separate shared-pool probe waited 99 ms. Batches of 128 records retained
the speedup but failed the 1,000-record heartbeat guard (34 ms versus a 24 ms
limit). The final 32-record candidate passes all declared comparison gates.

The fixture exercises actual model fetch, parsing, recipe projection, metadata
rebuilding, and publication. A `cat` subprocess transports generated snapshots;
it excludes database work. Assets are offline, each carries a scalar legacy
recipe, and thumbnails/Timeline loading are suppressed. Three alternating process
pairs per size contain three refreshes each. No builds, tests, or repository
searches overlapped those comparisons. Readiness values are medians of process
medians; heartbeat and RSS values are maxima across all matched runs.

| Metric | Before | After |
| --- | ---: | ---: |
| 1,000 assets ready | 298.691 ms | 166.405 ms |
| 10,000 assets ready | 2,934.018 ms | 1,619.563 ms |
| 1,000 assets: maximum heartbeat gap | 7 ms | 7 ms |
| 10,000 assets: maximum heartbeat gap | 59 ms | 57 ms |
| 10,000 assets: peak process RSS | 526.781 MiB | 528.359 MiB |

The 10,000-asset readiness reduction is **44.8 percent**, exceeding the 30 percent
target. Smaller-catalog latency, heartbeat, memory, and exact IDs/recipes pass.
This improves preparation throughput; retained full-snapshot memory is essentially
unchanged, and no incremental first-viewport delivery is claimed.

The new snapshot contract and existing thumbnail-reuse contract each pass three
final runs. Coverage includes mixed canonical/legacy scalar recipes against serial
projection, ordering, coalesced refresh, stale completion, active cancellation,
invalid JSON, failed helpers retaining the last catalog, and active-parse shutdown.
Repeated tiny shared-pool tasks during preparation and model destruction both
report 0 ms at integer-millisecond resolution in all three final runs. These are
controller/scheduler checks, not native render or input-to-photon measurements.

The final normal CMake build passes. An intermediate unrelated smoke-include
`QTest` declaration failure was left untouched; the later shared-worktree build
passed. No clean/full application regression claim follows from these targets.

**The user moved focus from catalog work to editing tools on 2026-10-01.** Paging, lightweight
records/on-demand recipes, incremental publication, full-payload memory scaling,
real database/native behavior, and raster-heavy 100k catalogs still require work.
Evidence: [raw comparisons, attempts, source patch, and contracts](evidence/performance-2026-09-28/catalog-snapshot-preparation.json).

## 7. Recipe checkpoint serialization — partial implementation

The v3 journal now reuses compact serialized records for unchanged active,
rejected, and migration-retry snapshots. Lookup is scoped by asset ID and then
requires exact equality of the complete encoded JSON value, including file path,
recipe, and migration evidence. It deliberately does not use the tolerant
`EditRecipeState::operator==`, pointer identity, or an unverified revision token.
Changed values are reserialized. Pending records can reuse a matching cached
record but do not populate the retained cache, avoiding retention of a large new
pending recipe after each slider change.

The next cache retains only records still present in eligible roles in the
successfully committed checkpoint. Serialized bytes are capped at 64 MiB; the
associated JSON comparison values and container overhead consume additional
memory, and old/new caches can coexist during a write. Failed writes keep the
previous cache; successful empty checkpoints release it. Peak memory and cache
admission policy remain performance acceptance concerns.

The writer assembles the same v3 compact payload from complete escaped record
JSON and separately encoded authority/schema strings. It hashes the entire
payload, then writes envelope prefix, payload, and suffix into one `QSaveFile`
instead of allocating/copying another full-size envelope buffer. Owner-only
permissions, disabled direct-write fallback, size limits, synchronous durability,
queue ordering, rejected-record preservation, and the atomic commit remain.
Readers and migration behavior for v1/v2/v3 are unchanged. No live recovery file
was opened, migrated, modified, or discarded during implementation.

The optional checkpoint profiler now reports `records_and_payload_us` in place of
separate encoding/payload/envelope assembly stages, plus serialized cache hits,
misses, retained serialized bytes, and payload size. Digest and atomic-write
stages remain. Profile output contains no recipe or migration-proof content.

This does not yet resolve all of finding 7: building the recipe JSON for exact
comparison, hashing the entire payload, and rewriting the complete recovery file
still happen synchronously. Immutable raster/blob storage and a durable
incremental journal remain candidates for the remaining work. No durability is
deferred or relaxed by this substep.

Acceptance requires independent byte/JSON/checksum comparison with v3 output,
exact reopen for all writer-supported recipe versions, Unicode/escape handling,
changed migration evidence and sub-tolerance numeric edits, role transitions,
large rejected plus small unrelated edits, cache-budget boundaries, failed writes,
corrupt recovery preservation, and killed-writer recovery. Compare 1/4/16 MiB
recipes with identical before/after workloads and include peak RSS. Existing
historical timings are not a baseline for this implementation.

Initial journal object compilation passed:
`/private/tmp/looksy-performance-fix-7-journal-build.log`.
Final Release journal/controller/MOC object compilation and focused whitespace
checks passed: `/private/tmp/looksy-performance-fix-7-journal-final-build.log`.
No tests, linked application, or benchmarks have been run; this is not runtime
or durability acceptance.

### Verification and measured refinement

The goal's verification step exercised the current and portable SHA-256 journal
suites in temporary directories (2/2 passed). Added byte-equivalence and exact
recovery cases cover unchanged/changed cached records, tiny numeric changes,
file paths, migration evidence, role transitions, failure/retry, and clearing.
Fixed the legacy test fixture to construct actual five-field v1/v2 payloads.
No live recovery file or catalog was accessed.

The first timing attempt missed the changed-edit regression guard. The final
writer uses direct payload serialization when only pending edits exist, preserving
record reuse for active/rejected/migration-retry cases. A second comparison passed:
the 4 MiB rejected-record case improved from 31.484 to 19.537 ms conservative p95;
other measured cases stayed within their stated guards. Peak RSS increased 8.3%.
All benchmark reopens matched exactly. This establishes one isolated improvement,
not application-wide latency or completion of finding 7.

See [comparison method, all scenarios, risks, and evidence](recipe-checkpoint-performance-2026-09-28.md).
Tests: `/private/tmp/looksy-performance-fix-7-attempt2-tests.log`.
Build: `/private/tmp/looksy-performance-fix-7-attempt2-build.log`.
Artifacts: `/private/tmp/looksy-checkpoint-comparison-20260928/attempt-2/`.

### Unchanged mask encoding during tool commits (2026-10-02)

Accepted in the measured workloads: retain one journal-owned mask-encoding entry so a
changed global adjustment does not normalize and base64-encode an unchanged
raster mask again. The cache compares complete mask input recursively with exact
metatypes and values, owns copied string/binary key storage, and bypasses empty
legacy stacks, missing mask IDs, and unsupported QVariant types. Changed masks
encode normally. A conservative 128 MiB admission estimate charges raster input
at seven times its size plus string/container overhead; it is not a measured heap
allocation cap. Successful empty checkpoints release the entry. Complete payload
serialization, SHA-256, atomic QSaveFile commit, and synchronous durability remain.

The new isolated cases include changed 4 MiB brush masks and 16 MiB tone edits.
The native fixture explicitly uses Cocoa and Metal with frozen source QML and
measures endCoalescedEdit on a synthetic photo with a 4 MiB mask. Database helper
dispatch is stopped after each local checkpoint; checkpoint persistence remains
enabled. This measures synchronous gesture commit, not optical input latency or
complete database persistence. Both journal suites pass, including exact output,
sub-tolerance changes, raw-data mutation, type changes, ordering, budget rejection,
legacy fallback, missing IDs, failed writes, role transitions, and reopen.

Three alternating before/after process pairs passed the declared 20% reduction
requirement for each matched pair. Median run p95 results:

| Workload | Before | After | Reduction |
| --- | ---: | ---: | ---: |
| 4 MiB unchanged-mask tone checkpoint | 32.28 ms | 20.19 ms | 37.5% |
| 16 MiB unchanged-mask tone checkpoint | 127.03 ms | 78.97 ms | 37.8% |
| Cocoa/Metal 4 MiB gesture commit | 51.47 ms | 39.54 ms | 23.2% |

Native matched-pair reductions were 22.4%, 20.8%, and 27.6%. The most conservative
unpaired comparison (largest after p95 / smallest before p95) gives 19.988%; it
is provided as a variability bound rather than represented as an extra matched
pair. Peak RSS stayed within 2.3% even comparing the largest candidate peak with
the smallest baseline peak. Small, changed-brush, and rejected-record scenarios
passed the regression guard. All benchmark reopens were exact.

The first new legacy comparison test failed because uncached legacy serialization
generates fresh IDs. The final fixture checks fallback fields and cache bypass;
cache admission also excludes nonempty masks without stable IDs. No candidate
was accepted with a failing correctness check.

Evidence: [checkpoint-mask-encoding.json](evidence/performance-2026-09-28/checkpoint-mask-encoding.json).
The desktop `photo_editor_app` target also rebuilt and linked successfully. The
packaged application was not launched as part of this comparison.

This accepts an incremental improvement to finding 7. Large changed masks,
multiple pending assets, full-file hashing/writing, real-photo/packaged-app
latency, and optical brush/lasso feedback remain open.

### Changed-mask encoding reuse: rejected (2026-10-02)

A bounded follow-up measured reusing encoded raster/stroke values between the
compatibility mask stack and canonical mask-set/retouch representations within
one save. Three alternating fresh-process pairs preserved all nine checkpoint
byte sequences, 32 mask-fixture outputs, and exact reopen behavior.

The candidate reduced median run p95 for changed 4 MiB brush checkpoints from
32.70 to 28.30 ms (13.5%) and retouch from 32.44 to 25.88 ms (20.2%). However,
16 MiB brush checkpoints improved only 8.8% (130.40 to 118.89 ms), missing the
10% target. Maximum measured process RSS rose from 930,578,432 to 1,091,911,680
bytes (about 154 MiB), exceeding the declared memory guard. The candidate was
rejected, and the codec was restored byte for byte. No speedup from it is retained.

The initial system `time -l` wrapper failed its sandboxed clock query; that trial
was excluded. All confirmation pairs use per-child `wait4` RSS. The benchmark now
includes changed 16 MiB masks, retouch, and deterministic checkpoint/mask output
fixtures. New representation-parity checks and both journal suites pass after
restoration, and the desktop builds successfully. Only test coverage and evidence
remain from this attempt. No live recovery state was accessed.

Baseline stage profiles attribute about 95 ms of a 16 MiB changed-mask checkpoint
to record/payload construction, 15 ms to digesting, and 6 ms to atomic writing.
The next approach needs to reduce complete payload construction and duplicated
persistent raster data while bounding allocations. Repeating this JSON-sharing
candidate or optimizing only the file write would miss the dominant cost.
Evidence: [changed-mask-encoding.json](evidence/performance-2026-09-28/changed-mask-encoding.json).

### Compact checkpoint serialization: rejected after final confirmation (2026-10-02)

Added opt-in `payload_serialization_us` timing to distinguish Qt JSON serialization
from record construction inside `records_and_payload_us`. In the baseline,
serialization accounts for about 47–52 ms of a 16 MiB checkpoint. This timing
field remains; normal writes do not enable it.

One optimization hypothesis collected compact JSON fragments plus validated
ASCII string views and assembled one exactly reserved buffer. It preserved the
v3 format, complete digest, and synchronous atomic commit. A later guard using
20,000 stroke points and large Unicode text found that fallback serialized a
subtree twice. Preflight validation repaired that issue, and the complete paired
series was rerun for the corrected version.

Final three-pair median run p95 results:

| Workload | Before | Candidate | Reduction |
| --- | ---: | ---: | ---: |
| 4 MiB tone checkpoint | 21.57 ms | 12.66 ms | 41.3% |
| 4 MiB changed brush checkpoint | 32.53 ms | 26.52 ms | 18.5% |
| 16 MiB tone checkpoint | 82.69 ms | 48.52 ms | 41.3% |
| 16 MiB changed brush checkpoint | 139.74 ms | 102.96 ms | 26.3% |

The 4 MiB changed-brush result missed the declared 20% requirement. Memory and
other workload guards passed, but earlier versions' better timings cannot replace
final confirmation. The serializer was rejected and removed; Qt serialization is
restored. No performance improvement from this candidate remains in the app.

Before restoration, 331 direct Qt differential cases covered exact bytes,
Unicode/escapes, integers/floats, large strings, nesting, and size limits. Nine
checkpoint byte sequences and the 32-case mask matrix matched across all pairs;
all reopens were exact. Both recovery suites and the desktop build pass after
restoration. Rejected code and test patches are archived, not linked into the app.
Evidence: [checkpoint-json.json](evidence/performance-2026-09-28/checkpoint-json.json).

## 8. Cold workspace loading — Carousel dialog contents

Moved the contents of 31 Carousel dialogs into individually registered QML files
under `qml/workspaces/carousel/`. The lightweight dialog shells remain available
to existing commands. Each shell loads its contents synchronously in
`onAboutToShow`, passing explicit workspace, theme, controller, and dialog inputs.
Contents remain instantiated afterward, preserving controls and state on reopen.
The command palette, reading-order editor, and video preview expose their internal
focus/player objects through properties across the new component boundary.

This removes about 7,000 lines of dialog content from first-use workspace parsing
and defers creation of those controls. It does not enable background prewarming or
change Timeline loading. Closed-dialog shell bindings still exist, and an
individual dialog pays its own first-use construction cost when requested.

### Measurement and verification

Used the same existing Release `actual_app_workflow_benchmark_driver` binary,
temporary app/cache directories, unavailable test database endpoint, source QML,
offscreen rendering, and `--route-only --iterations 3 --timeout-ms 30000` for both
versions. Each run starts a fresh process and records one cold and two warm visits.
These cold values are individual observations, not population percentiles or
compiled production/native UI measurements. No live catalog was used.

The bounded loop allowed at most three attempts. The target was at least 20%
lower cold Carousel time, with at most 10% worse Timeline cold time or maximum
queued-input delay. The first attempt used inline Loader components: an initial
apparent improvement disappeared on repetition (2,556 ms reference versus
2,601 ms candidate), so that approach was rejected. The second attempt separated
the QML source files; the final comparison passed:

| Metric | Reference | Candidate |
| --- | ---: | ---: |
| Cold Carousel route | 2,443 ms | 1,182 ms (-51.6%) |
| Cold Carousel queued-input delay | 2,426 ms | 1,164 ms |
| Warm Carousel routes | 0 / 0 ms | 0 / 0 ms |
| Cold Timeline route | 3,504 ms | 3,281 ms |
| Maximum Timeline queued-input delay | 15 ms | 15 ms |
| Child process peak RSS (macOS bytes) | 976,207,872 | 884,916,224 (-9.4%) |

Both processes exited successfully and emitted workflow completion. Warm values
use integer milliseconds. RSS came from a separate Python wrapper's
`getrusage(RUSAGE_CHILDREN)` for each process. Remaining cold Carousel input delay
still exceeds the desired budget, so finding 8 is only partially resolved.

The actual Carousel responsive-layout shard now checks that all 31 dialog
contents are absent before opening, load successfully, and retain the same object
through close/reopen. Its QML warning capture found a Loader activation binding
loop during development; explicit loading in `onAboutToShow` fixed it. The final
shard passed in 6.54 seconds with no dialog QML warnings. This is offscreen runtime
coverage, not a visual review of every dialog state or native video playback.
Existing source contracts now include the extracted dialog files.

The broader source-contract shard fails on the existing visible-text ellipsis
rule. The identical failure was reproduced with the pre-change Carousel source;
it is not reported as a passing check. No unrelated text changes were made.

Evidence under `/private/tmp/`:

- `looksy-performance-fix-8-comparison.json`: final timings, RSS, exit codes, driver hash.
- `looksy-performance-fix-8-final-{reference,candidate}.log`: complete route logs.
- `looksy-performance-fix-8-final-tests.log`: lifecycle pass and source-contract failure.
- `looksy-performance-fix-8-reference-source-contract.log`: reproduced baseline failure.
- `looksy-performance-fix-8-final-build.log`: focused Release/QML generation build.
- `looksy-performance-fix-8-dialog-objects-build.log`: all 31 extracted dialog QML C++ objects compiled successfully.
- `looksy-performance-fix-8-CarouselView-{before,final}.qml`: compared source snapshots.

Remaining acceptance: compiled production/native startup and input timing, dialog
first-open latency and visual/focus review across document states, and further
Carousel/Timeline workspace construction costs. The next implementation priority
was finding 9, Timeline frame-presentation starvation, handled below.


### Hidden Edit prewarm: bounded native comparison (2026-10-01)

The Library acceptance workload exposed a separate hidden Edit prewarm stall.
Three measured hypotheses were attempted:

1. Asynchronous top-level Edit Loader: median 101 ms cold gap; warm-heartbeat
   guard also failed. One run unexpectedly navigated to Edit and is excluded from
   Library-only memory claims.
2. Asynchronous nested Edit page Loader with readiness propagation: probe 117 ms.
3. Top-level asynchronous Loader plus a retained appearance-settings reader:
   three alternating native process pairs recorded 106, 105, and 106 ms cold gaps,
   versus baseline 771, 756, and 766 ms.

The third candidate passed exact duration/probe counts, input/frame, publication,
warm-heartbeat, completion-time, memory, prewarm-completion, and Library-route
comparison guards. Peak RSS was 804,536,320 bytes versus 911,720,448 bytes. The
focused AppShell preference test passed. However, the candidate missed the
predeclared median maximum cold heartbeat target of <=100 ms. **Rejected; all
prewarm/settings product changes from this bounded attempt were reverted.**
Earlier accepted Carousel dialog deferral remains. The native fixture and shutdown
coverage remain as regression evidence. Profiles identify QML finalization,
repeated settings reads, preset thumbnail rendering, and secondary process-launch
contention; they do not prove that any one residual hotspot accounts for the full
remaining pause. No fourth candidate was attempted.

Evidence: [library-duration-native-prewarm.json](evidence/performance-2026-09-28/library-duration-native-prewarm.json).
Next highest pending acceptance work: **2, real database and native batch project changes**.

## 9. Timeline frame-presentation starvation

`handleVideoFrame` now reads the current invalidation epoch instead of incrementing
it on every decoded frame. An active worker can finish and present while newer
frames arrive. One pending slot keeps only the latest waiting frame; displaced
pending frames are dropped. The dropped-frame counter now increments only when
that slot is overwritten, rather than counting a newly queued frame as dropped.

A shared invalidation method clears pending work and cancels the active epoch for
source/grade changes, explicit seeking/frame stepping, actual backend seeks,
output changes, decoder/GPU recovery, and destruction. Completion checks source
and grade generations, synchronization state, and output availability before
publication. It checks again after synchronous notifications that could seek or
detach the output. The worker remains marked active during publication callbacks,
so reentrant frame arrival queues work instead of replacing the active watcher.

The change preserves the existing renderer, frame color settings, one-worker
limit, and image-processing algorithms. It fixes arrival-driven cancellation;
continuously changing animated grade/effect keys still invalidate old settings
and need a separate time-aware playback assessment. It does not claim that the
renderer now meets a 30/60 fps throughput target.

### Deterministic runtime verification

Added `timeline_frame_scheduling_test`, linked to the production desktop runtime.
It injects synthetic decoded frames at the sink boundary and holds the global
worker pool with a semaphore, forcing newer arrivals while frame work is pending.
Processing and image publication execute the real implementation after release.
No live catalog, media files, decoder, or user document is used.

The unchanged scheduler failed the active-frame presentation assertion. After the
fix, three consecutive Release runs passed (0.52, 0.12, and 0.12 seconds):

- Five rounds of 61 arriving frames each publish the active red frame followed by
  the newest blue frame; intermediate green frames are not processed/presented.
- Exactly 295 overwritten pending frames are counted across those rounds.
- The UI event-loop heartbeat advances while the worker is held.
- Seek, frame step, output detachment, and forced source/grade transitions reject
  active results and clear the stale pending slot; later valid frames resume.
- A frame arriving inside a publication callback is processed in order.
- Destroying the controller advances the worker's shared cancellation epoch.

Qt used CPU frame conversion because this offscreen run had no RHI backend. This
proves queue behavior and stale-result protection in the exercised controller
paths, not native decoder frame timestamps, GPU correctness, display cadence,
animated-grade behavior, or 4K playback performance.

Evidence under `/private/tmp/`:

- `looksy-performance-fix-9-before-test.log`: reproduced original starvation.
- `looksy-performance-fix-9-final-build.log`: Release controller and fixture build.
- `looksy-performance-fix-9-final-test.log`: three passing runtime runs.
- `looksy-performance-fix-9-app-object-build.log`: desktop object build attempt
  stopped at an unrelated shader dependency: `timeline_effect_mask.frag` uses
  `textureSize`, which the requested ESSL 100 shader target does not support.
  The controller compiled in the successful test-runtime build; the application
  object build is not counted as passing. No shader changes were made here.

Next implementation priority was 10, asynchronous Carousel preview generation,
handled below.

## 10. Carousel preview workers

The regular `refreshPreviews`, `refreshPreviewAt`, and selected-content preview
paths now enqueue dirty boards/items. A dedicated pool runs one preview renderer
at a time; each live controller retains one active task and combines pending
refreshes. Snapshot capture occurs after the initiating mutation has completed.
The renderer uses the existing value-only export snapshot constructor and owns
its QObject and paint devices on the worker thread. It never captures the live
controller. Existing selection-based invalidation and the 200 ms edit debounce
remain; the current board is processed first within a dirty batch.

The worker performs image decoding, adjusted-item rendering, board painting, and
PNG encoding/writes. UI completion publishes only the latest request generation.
Mutations, geometry changes, document replacement, and destruction cancel obsolete
work. Dirty sets are retained across superseded tasks so changing another board
cannot drop an earlier board's refresh. Completed-but-undelivered old results
also fail the generation check.

Each task writes to an exclusive temporary cache directory. Published board/item
URLs own their individual files and share directory ownership; in-flight
snapshots retain those owners too. Replaced files can be removed while unchanged
sibling boards keep their own files. File and directory deletion run off the UI
thread. Unchanged adjusted-item previews reuse their existing published file.
PNG URLs remain compatible with the current QML image controls. Board preview
URLs are now published only after a successful image write.

### Verification

The new `carousel_preview_worker_test` uses isolated app/cache paths, synthetic
photos, the production controller, and a semaphore-held preview pool. Three
consecutive Release runs passed in 1.34, 0.92, and 0.90 seconds (3.17 total).
Each run checks:

- Three-board creation returns while rendering is held, with an advancing UI
  heartbeat; the observed call duration was 0 ms at integer millisecond precision.
- Board PNGs and an exposure-adjusted photo-item preview match the synchronous
  renderer/decoder after conversion to the same RGBA format.
- Repeated edits to different boards coalesce without losing the latest edit;
  editing one board preserves unrelated board URLs.
- An unchanged adjusted item is reused without another decode.
- Replaced files are deleted while still-used files remain available.
- A worker that finishes before its UI callback cannot publish over a newer edit.
- Close/new-document and destruction during queued work discard old results;
  released cache files are cleaned up.

The real-QML Carousel responsive-layout/dialog shard also passed in 7.53 seconds,
including opening/retaining all 31 deferred dialog contents. Its existing host
audio/font warnings remain. The focused Release runtime and test build passed
without compiler warnings. No live catalog or user documents were used.

Evidence under `/private/tmp/`:

- `looksy-performance-fix-10-verified-build.log`: final focused build.
- `looksy-performance-fix-10-final-tests.log`: three preview-worker passes.
- `looksy-performance-fix-10-ui-test.log`: Carousel UI shard pass.

These tests establish worker scheduling, fixture pixel equality, and exercised
cache/lifecycle behavior. They do not establish native input/frame latency for
10/50/200-board real-photo documents, HDR/profile/RAW parity across all inputs, or
improved total rendering throughput. Publication still waits for a dirty batch
to finish, and PNG caching still uses disk. Further incremental publication and
an in-memory image provider remain opportunities. Specialized comparison/proof
render paths were not changed here. Next implementation priority: 11, Timeline
CPU video-frame conversion and processing passes.

## 11. Timeline CPU video conversion

The YUV converter now resolves each mapped plane pointer and stride once per
frame and writes through a resolved output buffer. Previously those Qt accessors
ran for every pixel sample. Sampling, range/matrix math, camera conversion,
rounding, output formats, map lifetime, and fallback paths retain their existing
operations. This change adds no threads or image copies. Camera/static-grade
fusion already existed and was not reimplemented.

The focused Release converter/benchmark build passed. The real frame-processing
path was measured with synthetic mapped frames in an isolated offscreen process,
using identical fixtures in a preserved pre-change executable and the candidate.
Two comparisons ran in opposite orders. All 66 cases matched exactly for pixel
SHA256, dimensions, format, and ICC profile SHA256. Cases cover ten planar,
semiplanar, and packed layouts, full/limited ranges, BT.601/709/2020 matrices,
rotation/mirroring, one P010 camera-transform case, and the five larger cases below.

Each timed case has one warmup and 20 measured frames per process. The table
compares the faster reference p95 with the slower candidate p95 across the two
runs, using the image's per-frame decode/normalize metric, not the smoothed UI value.

| Conversion | Reference p95 | Candidate p95 | Reduction |
| --- | ---: | ---: | ---: |
| 1080p NV12 | 11.300 ms | 5.891 ms | 47.9% |
| 1080p YUV420P | 16.024 ms | 5.913 ms | 63.1% |
| 1080p P010 | 12.243 ms | 6.158 ms | 49.7% |
| 1080p UYVY | 8.278 ms | 6.116 ms | 26.1% |
| 4K NV12 sampled to 1080p | 11.752 ms | 5.668 ms | 51.8% |

The geometric mean reduction is 49.0%; peak child RSS was 102,825,984 bytes for
the reference versus at most 102,858,752 bytes for the candidate (+0.032%).
One candidate passed the predeclared >=20% timing improvement, <=10% per-case
regression, exact-output, and <=10% memory-growth gates. Artifacts, method,
comparison JSON, source snapshots, and source/binary hashes are under
`/private/tmp/looksy-performance-fix-11/`; build logs are
`/private/tmp/looksy-performance-fix-11-{baseline,candidate}-build.log`.

This is measured CPU conversion progress. Native decoder textures, GPU grading,
upload bandwidth, real playback cadence, broader effects, and full-frame HDR
parity remain open. The frame-scheduling test was rebuilt with the converter
change; no new scheduling-test run is claimed here. Next priority is 12.

## 12. Shared CPU workers for image packing

RGBA16 and RGBA32 working-image packing now use the engine's persistent worker
set through `run_cpu_parallel_for`. Large conversions no longer create a fresh
hardware-sized `std::async` group per image. The existing color/effect scheduler
uses the same pool. Parallel batches execute only on that fixed worker set;
waiting callers no longer each execute an additional CPU chunk outside it.
The pool retains its hardware-thread-count-minus-one size, with a serial fallback
when no worker is available. Small batches still execute inline.

A scoped nesting flag covers worker callbacks and inline serial callbacks, so
nested CPU stages execute inline and restore the prior state even on exceptions.
Callbacks are allocated before publication and every published task completes
before the synchronous call returns or rethrows. Packing retains its small-image
serial threshold, per-row cancellation checks, and pixel conversion arithmetic.

This is a shared budget for participating parallel image stages, not a process-wide
CPU limit. Qt's pool, separate brush/catalog/Carousel pools, codec threads, and
ACES gamut-table construction still have independent scheduling. Priority lanes,
queue-wait telemetry, and native slider/brush latency under concurrent import,
export, and playback remain open. No speedup or runtime parity is claimed for
this change; no new tests or benchmarks were run for finding 12.

Compilation passed for the Release engine library and affected desktop preview
controller object, without compiler warnings. Log:
`/private/tmp/looksy-performance-fix-12-build.log`. This is compilation evidence,
not a linked application launch or runtime acceptance.
Next implementation priority: 13, asynchronous import preflight.

## 13. Asynchronous import preflight

The Import view now requests preflight asynchronously and binds to the model's
report, busy state, and scanned-file count. A dedicated single-worker pool does
recursive enumeration, file readability/size checks, catalog-path indexing,
RAW/JPEG pairing, duplicate-policy evaluation, and destination/capacity checks.
Workers capture values and an implicitly shared catalog snapshot; they do not
access the live QObject. Per model, one active request and one replacement pending
request bound queue growth. Policy setters in the same event are coalesced.

A scan stores immutable source facts once. Policy changes can reuse the most
recent matching root/selection scan for up to 30 seconds; destination checks and
catalog duplicate evaluation still run on the worker. Matching in-flight scans
may finish for the latest policy request, while obsolete reports fail their
generation check. Changing sources/selections, closing/hiding the review, storage
settings changes, and destruction invalidate work. Cancellation is checked between
files and evaluation iterations; it cannot interrupt an OS filesystem call that
is already blocked. Progress is sampled by a 100 ms UI timer.

Confirmation and folder-import entry points also use the worker. They always
rescan current source facts, reject destination changes during admission, and
hold the model's busy state until the scan has been accepted or rejected. Closing
the review cannot cancel a separately confirmed import. The resulting eligible
paths are used for the job and project/preset association; the second recursive
UI-thread collection was removed. The existing synchronous `importPreflight`
method remains a compatibility API, but the Import view and admission paths no
longer call it. Eligible path arrays are omitted from the report sent to QML.

### Verification

The focused Release controller/MOC and test-runtime builds passed. Three isolated
`catalog_import_preflight_test` runs passed in 0.51, 0.14, and 0.14 seconds. They
exercise real controller paths with temporary media/storage and an unavailable
fixture database endpoint:

- A held worker keeps the UI heartbeat advancing and publishes no early report.
- Async results match the synchronous helper for mixed media, duplicate paths,
  catalog duplicates, unsupported files, and missing files.
- Policy changes reuse file facts without another scan; confirmation detects a
  file deleted after review instead of trusting cached eligibility.
- Repeated source changes and completed-but-undelivered tasks cannot replace the
  latest report; destruction with queued work does not retain a live model.
- Destination changes during admission reject the request and release busy state.
- Successful folder admission persists the freshly scanned selection in the
  isolated job store. The fixture job is removed afterward; no real import runs.

The dedicated real-QML `desktop_shell_import_preflight_test` passed in 3.64 seconds.
It loads Main.qml, routes to Import, verifies confirmation gating, completed report
publication, policy-cache reuse, and close/reset behavior, with no ImportView QML
warnings. The older broad `desktop_shell_import_safety_test` stopped before its
Import checks at the existing generic text-contract assertion. That assertion
also treats C++ `catch (...)` as text ellipses; no passing broad-suite claim is made.
Its async wait points and source API assertion were updated for the new contract.

Evidence: `/private/tmp/looksy-performance-fix-13-{build,test-build,final-build,
final-tests,ui-test,ui-final-build,ui-focused}.log`. The first UI log records the
broad-suite failure; `ui-focused` records the passing focused UI run.

Remaining acceptance includes 10k/100k-file native timing and memory, external or
unavailable-volume behavior, and full import-worker integrity/recovery. Review
facts can be stale within their bounded cache window; confirmation always
rechecks them and the import worker remains responsible for copy/hash integrity.
Job JSON construction, selection-manifest writing, and job-store admission still
occur on the model thread after scanning. These are remaining admission costs;
this change does not claim the entire import pipeline is free of synchronous I/O.
Next implementation priority: 14, asynchronous export preflight.

## 14. Cached asynchronous export preflight

ExportView now supplies one input snapshot and reads published single/batch
reports. It no longer invokes the synchronous source/profile preflight from
presentation bindings. A single worker runs source dimensions, recipe parsing,
RAW metadata, profile resolution/compilation, destination checks, and collision
planning. Requests debounce for 150 ms, retain one active and one latest pending
request, and reject obsolete generations. Hiding or destroying the view cancels
review work. The visible view refreshes filesystem-dependent reports every five
seconds when idle; identical requests within two seconds reuse the current report.

The expensive source report is separate from destination/collision and clipping
issues, preserving issue order and report fields. A worker-owned QCache has an
approximate 32 MiB cost limit, including serialized report, recipe, and revision
costs. Its key includes media type, source/preview paths, exact recipe, and preset.
Source existence/readability, canonical path, size, modification time, and metadata
change time are checked on the worker, with a maximum report age of 30 seconds.
Source revisions are checked before and after construction. Single/batch checks
for the same source share that entry; scope and destination changes reuse it.
Custom ICC/DCP requests deliberately bypass this report cache because registry
mapping changes have no revision token. They still execute off the UI thread.

The UI batch action now also validates on a worker using fresh source/profile
checks independent of the advisory cache. Accepted requests enter the existing
catalog admission path after validation. Filenames are planned again at admission
against current files and queued output reservations. The existing source and
destination admission checks remain. Normal window close waits while batch
validation is in progress, and the shutdown helper waits for preflight work while
Qt plugins/application state are alive. Compatibility synchronous preflight and
batch-queue APIs remain available to non-UI callers.

### Verification

Focused Release controller/MOC, runtime-library, and fixture builds passed without
compiler warnings. Three final `export_preflight_worker_test` runs passed in 1.88,
1.60, and 1.60 seconds (5.09 total), covering:

- A held worker leaves the event-loop heartbeat responsive and publishes no early
  report; asynchronous single/batch maps match the synchronous helper maps.
- Identical single/batch source checks build one source report; scope/destination
  changes reuse it, while rewriting the source refreshes dimensions and the report.
- Repeated destinations coalesce; cancelled and completed-but-undelivered results
  cannot overwrite newer reports.
- Missing custom ICC profiles block export and are re-evaluated on forced refresh.
- Batch submission rejects a source deleted after review. A collision introduced
  after worker completion receives a new filename during admission.
- Destruction with queued work releases the model without waiting for that worker.

All fixtures use temporary media/storage. Admission uses an injected in-memory
writer, so the test exercises controller admission behavior without writing real
catalog jobs or exporting user files. An initial custom-profile fixture failed
because it supplied an invalid recipe reference; it was corrected to a valid
missing-profile reference before the final runs.

The dedicated real-QML `desktop_shell_export_preflight_test` passed in 4.08 seconds.
It loads Main.qml and ExportView, checks pending/ready button states, rapid
replacement of destination text, invalid destinations, hiding/cancellation, and
the normal-window-close guard during a batch request. No ExportView QML warnings
were emitted. The broader delivery suite's asynchronous wait point/source API
assertions were updated, but that entire suite was not rerun or claimed passing.

Evidence: `/private/tmp/looksy-performance-fix-14-{build,admission-build,test-build,
final-build,final-tests,final-ui-test}.log`. Earlier attempts are retained alongside
these logs. No native 1/100/1,000-RAW timing, peak-memory measurement, real catalog
admission, full export artifacts, or GPU/color-parity claim is made. Cancellation
checks occur between source checks; an individual decoder/profile/filesystem call
may finish before cancellation takes effect. Final per-job recipe construction,
source/destination checks, and filename revalidation still use the model thread;
further batch admission scheduling remains an opportunity.
Next implementation priority: 15, coordinated photo-cache memory budgets.

## 15. Shared photo-cache memory budget

The decoded, characterized/linear, adjusted, rendered preview, rendered tile,
foreground instance-map, and mask-tile caches now register retained entries with
one coordinator. The shared ceiling is one eighth of installed RAM, bounded to
256 MiB–2 GiB (1 GiB if hardware detection falls back to 8 GiB). Existing local
ceilings remain upper bounds. Entries are evicted by a shared least-recently-used
order, so adding entries to one cache can release older entries in another.
Both local and shared access-order updates use constant-time list operations.

The existing Timeline memory/disk-pressure response now reaches all seven caches:
warning reduces the shared ceiling to 65%; critical reduces it to 35% and drops
currently retained entries. Normal pressure restores the exact configured
ceiling. QImage, QByteArray, and shared working-image references held by consumers
remain valid after cache eviction. One common mutex protects cache membership
and accounting; decoding and rendering stay outside that mutex.

Photo preview diagnostics and Timeline cache-budget state expose
`sharedPhotoCacheBudget`, including aggregate/owner bytes, entries, hits, misses,
rejected shared reservations, and replacement/local/shared/pressure/clear release
counts. Accounting is explicitly labeled `cache-owned-references-not-process-rss`.
Sizes are recorded at insertion; shared backing allocations may be counted more
than once, and subsequent working-buffer materialization is not reserved here.
Releasing cache ownership does not promise an immediate RSS reduction.

### Verification

The three changed production translation units compiled. The first focused-test
build exposed a missing `photo_engine` include dependency in the new test target;
adding the target dependency resolved it. The final test target built successfully.
`photo_cache_budget_test` then passed three consecutive runs (0.41/0.09/0.09 s).
It covers hardware budget tiers, local and cross-owner LRU eviction, replacement,
oversized/zero-budget rejection, exact pressure percentages, aggregate accounting,
destructor cleanup, 6,000 concurrent insert/lookup iterations with pressure changes,
and retained QImage/QByteArray/shared-pointer validity after eviction. It exercises
the real decoded cache, editor mask cache, and Timeline pressure-response route.
All fixtures and settings are isolated in temporary storage.

Evidence: `/private/tmp/looksy-performance-fix-15-build.log`,
`/private/tmp/looksy-performance-fix-15-test-build-final.log`, and
`/private/tmp/looksy-performance-fix-15-tests.log`. The initial object build reported
an existing Timeline `selection.size()` integer-conversion warning; final build
logs also contain Ninja log-recovery warnings. These checks establish cache
ownership/accounting behavior, not native rendering performance or RSS savings.

This is a partial implementation of finding 15: live render scratch allocations,
history, other workspace caches, workflow reservations, and GPU-wide allocation
coordination remain outside the shared ceiling. Native repeated 45 MP switching,
peak RSS, pressure behavior, hit rate, decode repetition, and mutex contention
still need measurement before claiming an app-level performance improvement.

Next priority: 16, fresh first-use mask-brush measurement and acceptance gating.

## 16. First-use mask-brush gate and current native measurement

The actual-QML gate now requires both the first controller point and the first
stroke including `beginActiveBrushStroke()` setup. Both use the existing QML
feedback budget: p95 <=50 ms, maximum <=80 ms. These are controller-injected
measurements; the separate 33 ms controller-only interaction gate does not prove
pointer-event-to-visible-pixel latency. Empty sample sets, non-finite/negative
timings, missing first-use summaries, and incomplete/skipped workflows now fail.
At least two iterations are required for first-use and later-stroke samples.

The driver separately records stroke setup, point dispatch, Canvas paint/sync,
frame swap, and observed matte-worker completion. Previously it accepted any
frame swap after adding a point and reported worker completion only after that
frame. It now requires a Canvas paint containing live points, a subsequent scene
graph synchronization, and a frame swap. The QML receipt is enabled only by the
benchmark. This is a conservative presentation receipt, not hardware pointer or
display-scanout timing; it can include an extra synchronization after painting.

`--brush-only` exercises the real Main.qml/Studio workflow, including repeated
asset switching, without requiring Carousel/Timeline/Export routes. The focused
gate budgets brush stages only; it is not an overall app pass. `--brush-auto-mask`
enables auto-mask before each stroke. The report records the scenario, auto-mask
setting, and measurement contract. A capture run can save `05-brush-feedback`
after the timed receipt; captures are separate visual checks and are not timing
baselines. Unexecuted route summaries are omitted rather than reported as zero.

### Baseline and bounded attempts

The initial broad native run observed 591 ms at first brush feedback but never
completed Timeline routing (`target_events_met=false`); it is not a valid full
baseline. Two completed Cocoa/host focused runs using the stronger receipt and the
driver's original CPU preview preference gave:

| Run | First point | First stroke incl. setup | Later strokes p95 | Sampled peak RSS |
| --- | ---: | ---: | ---: | ---: |
| Before 1 | 94 ms | 95 ms | 78.15 ms | 1202.55 MiB |
| Before 2 | 91 ms | 92 ms | 82.60 ms | 1238.33 MiB |

A later visual check found that these fresh profiles still displayed the welcome
guide. These figures describe that CPU/onboarding configuration, not an
unobstructed editor with the normal Auto backend preference. The final focused
fixture completes onboarding in its temporary settings and requests Auto; those
results must be kept separate from the earlier candidate comparison.

The runs use the existing 1800x1200 synthetic PNG fixture. Each run has one
first-use sample and four later samples, each after switching asset identity. They do not establish a cold-start percentile distribution or
same-photo repeated-stroke latency. Setup and point dispatch were below 1 ms;
the first run observed matte completion at 18 ms with 8 ms worker time, versus
83 ms to Canvas paint/sync and 94 ms to swap. The presentation gate remains red.

Loop bounds: at most three narrow attempts; accept only if first-use clears the
50/80 ms gate with at least 20% reduction, later-stroke p95 and peak RSS within
10% of the baseline range, and unchanged feedback semantics. Two candidates were
measured and reverted:

| Candidate | First point | Later strokes p95 | Sampled peak RSS | Decision |
| --- | ---: | ---: | ---: | --- |
| Immediate visible-Canvas paint request | 91 ms | 76.00 ms | 1215.91 MiB | Reverted: first-use gate still fails |
| Transparent Canvas prepared when brush tool opens | 100 ms | 74.05 ms | 1189.63 MiB | Reverted: first-use regresses |

The loop stopped before a speculative rendering rewrite. No brush behavior
optimization is accepted here. The retained changes fix the acceptance gap and
provide current measurements; Canvas/scene-graph presentation work, physical
pointer timing, separate Canvas allocation/texture-upload attribution, and broader
photographic workloads remain open.

Artifacts are under `/private/tmp/looksy-performance-fix-16-{baseline,
focused-before,focused-before-2,direct-paint,prewarm}/`. Each completed focused
artifact contains its manifest, stage log, resource samples, and gate report.
`scripts/perf/test-actual-app-workflow-gate.py` has six passing focused checks for
first-use/setup requirements, invalid/empty samples, and focused-route scope.

### Final fixture verification

The final driver compiled and linked after the fixture corrections. Native
verification ran normal brush, auto-mask, and visual capture sequentially with
isolated settings. All three reached the workflow completion event; performance
gates correctly remained failed. The two timing runs had no captures:

| Auto-preference fixture | First point / first stroke | Later strokes p95 | Sampled peak RSS |
| --- | ---: | ---: | ---: |
| Normal brush | 87 / 87 ms | 76.85 ms | 1118.95 MiB |
| Auto-mask enabled | 95 / 95 ms | 77.70 ms | 1870.31 MiB |

Auto preference is not GPU execution proof: completed preview telemetry reported
CPU fallback for `Layers:photo_layers` (and `Layers:local_mask_sets` with auto-mask).
The final screenshot was inspected: the live blue mark is visible at the injected
point and the welcome guide is absent. This verifies visible feedback in that
fixture, not settled mask accuracy, real mouse/tablet latency, or GPU parity.
Capture timings are excluded from the tables.

Final evidence: `/private/tmp/looksy-performance-fix-16-verified-normal/`,
`/private/tmp/looksy-performance-fix-16-verified-auto-mask/`, and
`/private/tmp/looksy-performance-fix-16-verified-capture/`. The sequential runner
log is `/private/tmp/looksy-performance-fix-16-verification.log`; each child gate
returned 1 for latency failure even though the orchestration process completed.
Final driver compilation/link evidence is
`/private/tmp/looksy-performance-fix-16-final-driver-build.log`. Only acceptance
and diagnostic changes are retained; first-use latency remains unresolved.

### Tool-focused follow-up (2026-10-01)

The user requested leaving catalog work and focusing on editing tools. A current
offscreen QML brush baseline completes the workflow but still fails the feedback gates.
With frozen source QML, first-point/stroke feedback is 61/62 ms, later-stroke p95
is 65.25 ms, and brush HUD p95 is 334.4 ms. These are current-build measurements,
separate from the September baseline above.

The remaining bounded rendering attempt replaced the live-vector Canvas with a
QQuickPaintedItem while retaining the matte/native paths and the paint/sync/swap
receipt. First feedback improved only to 55/56 ms, later-stroke p95 was 61.25 ms,
and HUD p95 was 322.6 ms. It failed the 50 ms target and 20 percent first-use
improvement gate. The candidate was reverted byte-for-byte in both brush-preview
source files and SharedMaskOverlay.qml, and the restored driver rebuilt. No
brush latency fix or visual parity is claimed. The only retained harness change
allows a frozen QML root via `PHOTO_EDITOR_BENCHMARK_QML_ROOT`.

The separate tool-opening breakdown identifies a concrete next target: the first
selection-brush `controlsChanged` notification consumed 247 ms (273 ms cumulative,
following 26 ms through mask-state notification). Mask creation/normalization and
state preparation took only a few milliseconds. Later broad notifications also
consume 55–82 ms. Trace the required mask/layer notifications before narrowing
this update; the newly created child mask must remain visible in the layer UI.

**Next target at that point: brush/tool activation notifications.** The feedback-renderer
loop is stopped after its final failed candidate. Catalog work is out of the
current user-requested focus. Evidence: [QML reports, rejected result, restoration, and phase timings](evidence/performance-2026-09-28/brush-feedback-renderer.json).

**Correction (2026-10-02):** the original native label was incorrect; these
commands used the driver's offscreen default. Native UI acceptance remained
unproven, and the candidate had already been reverted.

### Tool activation follow-up (2026-10-01): three rejected candidates

The user moved focus from catalog work to editing tools. A separate bounded
loop targeted brush opening, where the first `controlsChanged` notification
spent roughly 247 ms refreshing QML. The fixed acceptance target was native HUD
p95 <=250 ms, at least 20 percent improvement, and protected brush-feedback/RSS
and correctness checks.

| Candidate | HUD p95 | First feedback | Later feedback p95 | Decision |
| --- | ---: | ---: | ---: | --- |
| Frozen baseline | 334.4 ms | 61 ms | 65.25 ms | Reference |
| Split mask/layer notifications | 319.8 ms | 64 ms | 117.4 ms | Reverted |
| Copy mask list once per refresh | 339.4 ms | 72 ms | 101.45 ms | Reverted |
| Instantiate closed-menu rows on open | 300.0 ms | 58 ms | 113.15 ms | Reverted |

**Measurement correction (2026-10-02):** these runs used the benchmark's
default offscreen Qt platform and software renderer. The figures above are
offscreen QML diagnostics, not native Cocoa/Metal acceptance evidence. No
candidate was accepted. Subsequent native work explicitly sets
`QT_QPA_PLATFORM=cocoa` and `QSG_RHI_BACKEND=metal`.

All workflows reached their target events. Each candidate failed the initial
primary gate; three alternating confirmation pairs and visual acceptance were
therefore not pursued. No performance improvement is accepted from this loop.
The controller, QML helpers, menu component, and notification-contract experiment
were restored to their pre-loop state, preserving earlier unrelated changes.

Split-stage diagnostics measured 18 ms in mask-control notification and 207 ms
in layer notification on first activation. Process sampling shows synchronous
QML binding cascades and Repeater regeneration. The Layers tab was closed and
its layer-row creation instrumentation did not fire. Closed-menu suppression
alone was insufficient. This loop is exhausted; a further attempt needs a
separately scoped inspector-update design with measured binding dependencies.

The notification-contract experiment also exposed existing outline and undo
notification gaps. Its experimental fixes were reverted with the rejected
signal split; they are not claimed as retained correctness repairs.

Evidence: [tool-activation.json](evidence/performance-2026-09-28/tool-activation.json),
raw reports and a process sampling excerpt in
[evidence/tool-activation](evidence/performance-2026-09-28/tool-activation/).

### Long lasso gestures (2026-10-02): diagnostic harness retained; fixes rejected

A 512-point offscreen QML gesture measured 6.4 ms p95 controller updates. At
1,024 points it measured 12.1 ms. Two normalization candidates measured 10.2
and 10.1 ms and missed the predefined <=8 ms / >=30 percent reduction target.
Both were reverted. Profiling then identified repeated full-path hashing in
`activeMaskOutlinePaths()` even on outline-cache hits.

Explicit Cocoa/Metal runs exposed a much larger real UI cost: 1,024-point
controller updates measured 75.0 ms p95 and closure 731 ms. Reusing the already
computed shape-source signature in the outline cache key measured 44.0 ms p95
and closure 286 ms, with the same closed-geometry SHA256, all raw coordinates,
one-step undo, and exact redo. That final candidate also missed the absolute
target and was reverted. No improvement is accepted; this three-attempt loop
is exhausted. No confirmation pairs or screenshot acceptance were claimed.

The benchmark now has an optional 1,024-point lasso workload driven through the
actual QML lasso activation flow and controller updates at an intended 16 ms
cadence. It checks raw coordinates, closure, and complete gesture undo/redo.
These are controller-call timings plus frame counts, not optical input-to-frame
latency. `PHOTO_EDITOR_BENCHMARK_NATIVE_SETTINGS=1` chooses a disposable native
preferences namespace. That option alone does not select native UI rendering;
use `QT_QPA_PLATFORM=cocoa QSG_RHI_BACKEND=metal` explicitly. Platform telemetry
is retained to prevent confusing offscreen measurements with native UI proof.

A separate preference-reader experiment was also reverted: native-preferences
but offscreen-rendered brush opening changed only 337.0 -> 323.8 ms p95.

Evidence: [lasso-tool.json](evidence/performance-2026-09-28/lasso-tool.json) and
[tool-preference-reads.json](evidence/performance-2026-09-28/tool-preference-reads.json).

## 17. Preserve current catalog thumbnails across refreshes

Catalog thumbnail completions now retain a provenance token plus filesystem
revisions for the original, local path/URI dependencies in the recipe, and the
generated thumbnail. Snapshot preparation validates the previous immutable row
snapshot on the existing worker. Publication merges by asset ID and retains a
thumbnail only when the validated token still matches the currently held token
and source path, media type, and recipe bytes are unchanged. This also covers
virtual copies appended during publication. Bulk validation stays off the GUI
thread; completion performs bounded dependency checks before accepting a result.

An unchanged refresh therefore keeps the existing URL/current-preview state and
does not enqueue that asset for generation. Changed inputs or missing/replaced
cache files invalidate only affected rows. Offline originals retain the previous
preview, but that fallback is not marked current for visual-search indexing.
Results whose source revisions changed during rendering or before GUI delivery
are rejected; source changes during rendering can requeue the affected asset.
`catalog.thumbnail.reuse` reports retained/invalidated/validated-candidate counts.

Photo cache keys advance to `photo-v6-source-dependencies` and include renderer
epoch, output/decode edges, canonical adjustment identity, and dependency revisions.
Video poster keys advance to `video-v3` with source revision and output edge. This
causes a one-time cache miss for old keys. Revisions include inode/change time on
Unix, so an atomic same-size replacement with restored mtime cannot reuse the old
disk thumbnail. They remain metadata observations, not immutable content hashes.
Custom ICC/DCP registry-backed recipes conservatively rerender rather than reuse
until the registries expose revision tokens; invalid recipe projections also
receive no reusable provenance.

### Verification

`catalog_thumbnail_reuse_test` passed three final consecutive runs (0.80/0.48/0.47 s).
It uses temporary media/settings and `/bin/cat` as the snapshot transport, then
exercises the production asynchronous parse/validation/publication path and real
thumbnail workers. It does not access a live catalog database.

- Ten identical snapshots: zero worker starts and zero thumbnail publications;
  the same URLs remain visible in the model.
- Forced-invalidation control: three worker starts and three publications for
  the same three assets. This is a work-count control, not a wall-time speedup.
- One recipe edit, an atomic same-size source rewrite with restored mtime, a
  deleted cached image, a linked-image rewrite, and a relink: one publication each.
- Original and relative-path linked-image replacements change decoded thumbnail pixels; stale in-flight
  dependency revisions are rejected.
- Offline preview retention, return online, and virtual-copy reuse pass.

The existing catalog test's disk-key helper was updated to the v6 namespace so
its corrupt-cache and recipe-key checks continue targeting the actual cache.
Final focused logs: `/private/tmp/looksy-performance-fix-17-final-source-build.log`
and `/private/tmp/looksy-performance-fix-17-verified-tests.log`. The existing
`catalog_incremental_timeline_model_contract` also passed three final runs
(0.41/0.06/0.06 s).

The earlier audit overstated the Timeline part of this finding:
`clear_thumbnail_source_for_item` already returned early for Timeline rows.
This change addresses photo/video runtime publication and their cache identity.
Model resets/filter rebuilding remain finding 19; this does not establish zero
QML delegate reconstruction. Native large-library refresh latency, real RAW/video
workloads, and live-database integration remain unmeasured.

The broader `catalog_asset_model_test` reached its later import tests after the
thumbnail checks, then failed because its wait helper returned before asynchronous
preflight. The helper now waits for both preflight and importing. With that repaired,
the test instead timed out at `import should eventually finish` (19.49 s total,
15 s import wait). The full suite is not passing; import service/job completion
remains separate unfinished verification. Logs:
`/private/tmp/looksy-performance-fix-17-regression-{tests,rerun}.log`.

Next implementation priority at that point: 18 (implemented below).

## 18. Timeline catalog reconciliation

Implemented with isolated measured acceptance; native Library workflow acceptance
remains open.

`CatalogAssetModel::reconcileTimelineAssets` now uses the existing asset ID map
for updates, compares normalized records, and returns without row, selection,
search, metadata, or thumbnail work when nothing changed. Project labels are
computed for incoming Timeline records. Photo burst/duplicate/sync and camera/year
metadata are rebuilt only when a photo ID changes media type or removing a Timeline
separator can join photo bursts. Changed visible records receive `dataChanged`;
contiguous insertions/removals use range notifications and preserve persistent
photo indexes. Ranked search additions retain rank order. Index maps are coherent
at each completed row notification. Structural changes still refresh thumbnail
scheduling because they can move photos into the viewport.

The common unranked merge no longer performs a full catalog search or rebuilds
the entire visible index per Timeline. There is still a linear catalog scan to
detect removals; interleaved ranked insertions still shift vector rows and update
affected suffix indexes. This does not make all ranked workloads linear.

### Measurements and acceptance

Same Release benchmark, 10,000 offline photo records, 10/100/1,000 Timelines,
seven unchanged-refresh and seven single-edit samples per case. Values below are
milliseconds: baseline → worst result across three final candidate runs. Add/remove
are single operations per case, rather than p95 values.

| Timelines | Unchanged refresh p95 | Single edit p95 | Insert all | Remove all |
| ---: | ---: | ---: | ---: | ---: |
| 10 | 42.55 → 0.20 | 55.30 → 0.29 | 45.92 → 2.22 | 39.01 → 1.07 |
| 100 | 43.78 → 0.15 | 43.20 → 0.32 | 70.15 → 2.36 | 42.37 → 1.09 |
| 1000 | 56.77 → 0.54 | 50.55 → 1.16 | 325.17 → 2.75 | 40.78 → 1.14 |

All cases exceed the predeclared 20% reduction target for refresh/edit p95.
Peak process RSS was 109.39 → 109.75 MiB
(+0.33%), within the 10% guard. At 1,000 Timelines, seven unchanged refreshes
emit zero data notifications instead of 7,000; seven single edits emit seven
instead of 7,000. Bulk add/remove each emit one range notification instead of 1,000.

Three consecutive CTest runs passed for each of:

- `catalog_timeline_reconciliation_contract`
- `catalog_incremental_timeline_model_contract`
- `catalog_thumbnail_reuse_test`

The new contract checks persistent photo indexes, selected-photo preservation,
selected-Timeline removal, ranked ordering, empty/duplicate input IDs, search/media/
rating filters, map coherence at row notifications, photo-group metadata after
separator removal, and a photo/Timeline ID collision. The fixture initially used
singular media filter strings; correcting these to the API's `photos`/`timelines`
values resolved that test failure. Final source was explicitly recompiled and
all three validation binaries relinked before the final runs.

[Raw baseline, candidate runs, and comparison](evidence/performance-2026-09-28/timeline-reconciliation.json)
are retained in the repository. Temporary build/check logs:
`/private/tmp/looksy-performance-fix-18-verified-source-build.log`,
`/private/tmp/looksy-performance-fix-18-contract-build.log`, and
`/private/tmp/looksy-performance-fix-18-final-checks.log`.

These are isolated model timings with synthetic records, without a native QML
view, real media, or live catalog database. They establish less reconciliation
work and correct tested model behavior, not total Library frame latency. The
broader catalog/import completion failure recorded under #17 remains open.

Next implementation priority at that point: 19 (bounded investigation below).

## 19. Library filter model resets — candidates rejected

No application optimization accepted. `applyFilters` and its visible-record
storage have been restored to their prior implementation. The retained changes
are an isolated benchmark target, its test-access declaration, and evidence.

### Baseline and attempted improvements

`catalog_filter_benchmark` measures seven repeated publications, single-record
updates, and alternating camera-filter narrowing/restoration at 10,000 and 100,000
synthetic offline photos. Its optional `--verify` mode also requires zero resets,
minimal data notifications, preserved persistent indexes/selection, and unchanged
QML delegate identity/scroll for a filter that retains every row. That mode is an
**unmet acceptance check**, not a passing CTest registration in the current tree.

The target was at least 20% lower unchanged-publication p95 with preserved row
identity and no significant filter-time or memory regression. Three candidates
were tried under `photo-editor-perf-loop`'s bounded acceptance procedure:

1. Indexed matching plus incremental ranges; partition/reorder full records in
   place with persistent-index remapping.
2. Move each reordered record once and avoid unnecessary visible-map rebuilds.
3. Separate visible row order into integer storage slots, reuse cleared slots,
   and trim storage when the catalog shrinks.

For 100,000 photos, milliseconds (p95):

| Operation | Baseline | Candidate 1 | Candidate 2 | Candidate 3 |
| --- | ---: | ---: | ---: | ---: |
| Unchanged publication | 25.04 | 10.20 | 12.20 | 10.63 |
| Single-record edit | 24.11 | 11.37 | 16.87 | 11.43 |
| Alternating filter: narrow | 17.35 | 76.88 | 66.27 | 50.59 |
| Alternating filter: restore | 23.32 | 79.38 | 59.74 | 57.73 |

All candidates eliminated the benchmark's 28 model resets per size and retained
the selected photo's persistent index. Candidate 3's peak RSS was 354.14 MiB versus
423.16 MiB baseline. However, even that candidate made narrowing approximately
2.9 times slower. These regressions reject all three candidates despite the
improvements elsewhere. Sub-millisecond/first-run samples at 10k showed timing
noise; no broad speedup is claimed from them.

The standalone QML GridView baseline created 49 and destroyed 56 delegates for a
membership-preserving search and moved scroll position from 5,000 to zero.
Candidates created/destroyed zero delegates and retained position 5,000. The
first candidate's fixture initially required a new frame even for unchanged
content; it was corrected to require an initialized rendered view while allowing
an idle view to skip repaint. That correction did not resolve the CPU regressions.

### Current-app boundary and next investigation

Current `StudioWorkspace.qml::libraryGalleryRows` obtains the entire
`catalogAssets.galleryAssets()` array and constructs a separate justified-row
array. `studioLibraryGalleryGrid` is a ListView over that array; its refresh
connections listen to model reset, row insertion/removal and asset count. Tiles
already listen for data changes. An accepted incremental model design also needs
to handle layout changes in this gallery and avoid rebuilding all row geometry
for unaffected changes. The standalone GridView does not prove this app path.

A future attempt should start with the actual Studio gallery's filter-to-frame
and delegate/layout measurements, and profile predicate evaluation, publication,
ID maps, and gallery geometry separately before choosing another storage design.
The remaining asynchronous filtering, immutable source/index projection, and
server paging work are still open.

Reproduce the baseline (Release build):

```sh
cmake --build build/release-macos --parallel 4 --target catalog_filter_benchmark
QT_QPA_PLATFORM=offscreen build/release-macos/tests/catalog_filter_benchmark --output /tmp/catalog-filter.json
```

[Raw baseline and rejected candidates](evidence/performance-2026-09-28/catalog-filter-attempts.json)
are retained. The reverted experimental code is not part of the application.
The restored source built successfully. The restored 100k narrowing p95 reproduced
at 17.22 ms, with the original 28 resets per size and the same standalone GridView
scroll/delegate behavior. `catalog_timeline_reconciliation_contract` and
`catalog_thumbnail_reuse_test` each passed three consecutive runs after restoration.
Temporary logs: `/private/tmp/looksy-performance-fix-19-restored-build.log`,
`/private/tmp/looksy-performance-fix-19-restored.log`, and
`/private/tmp/looksy-performance-fix-19-regression.log`. The known broader catalog
import-completion failure from #17 remains separate and was not rerun here.

Next implementation priority: 20, byte-aware photo-history budgeting. Finding 19
remains unresolved; the three-attempt limit stops this hypothesis, not the overall
performance goal.

## 20. Photo-history memory — partial integration, deferred

Storage primitives and revision handles are now integrated into undo/redo and
history reads. Integrated memory acceptance remains open; cached states and named
snapshots retain their earlier ownership/persistence paths. The component results
below predate integration. See the handoff below for current limitations.

### Implemented storage and exact-state adapter

- `HistoryPayloadStore` shares immutable binary payloads by SHA-256 content ID.
  A configured resident byte limit spills older blocks through `QSaveFile`; RAM
  ownership is released only after successful atomic file publication.
- Resident accounting includes QByteArray capacity. Borrowed `fromRawData` buffers
  are copied into owned storage. Duplicate captures share one stored block.
- Restore verifies size and SHA-256. Missing/corrupt files return an error. Failed
  spill keeps valid resident data and reports the budget overrun; it does not
  silently discard undo data to enforce a hard cap.
- Handles keep their storage session alive after the store object is destroyed.
  Explicit collection deletes only unreferenced blocks in that private session.
  Session files are removed after the last owner ends. These files are **not a
  persistent undo journal or a completed crash-recovery design**.
- `HistoryStateRecord` extracts large QByteArrays recursively from all nine
  QVariantList fields in `EditRecipeState`, including masks, paint tiles, and
  nested recipe snapshots. Qt list/map/hash types and scalar state are preserved.
  Restore materializes each shared block once and returns a complete state or an
  error. It exposes no implicit conversion to a partially restored recipe.

Both capture and restore are synchronous worker primitives. The controller
integration dispatches hashing/I/O to workers and preserves the active state/stack
position until restore succeeds; queued raw payload bytes are not yet bounded. Nonbinary metadata, active edits, returned buffers, other
owners of shared QByteArrays, queued captures, and OS file cache are outside this
store's resident budget.

### Component measurements

Same deterministic payload fixture in separate processes: 64 unique 4 MiB blocks.
The baseline retains QByteArrays in memory; the candidate uses a 32 MiB store
budget. This measures the storage mechanism, not an end-to-end editor workflow.
Worst peak RSS across three final candidate runs:

| Metric | In-memory baseline | Payload store |
| --- | ---: | ---: |
| Store-owned resident payloads | 256 MiB | 32 MiB |
| Process peak RSS | 269.92 MiB | 52.50 MiB |
| Retained logical payload data | 256 MiB | 256 MiB |
| Exact restoration | All 64 | All 64 |

Resident ownership fell 87.5%; measured peak RSS fell
80.55%. The tradeoff is I/O: capture of all 64 blocks takes
roughly 1.2 seconds and restore roughly 1.0 second, versus sub-millisecond
in-memory reference operations. No UI-latency improvement is claimed.

A separate large-mask stress run retained **100 different 45,000,000-byte blocks**
(4.5 GB logical data, equivalent to 100 single-channel 45 MP masks):

- Resident payload ownership: 90,000,000 bytes under a 128 MiB budget.
- Spilled payloads: 4,410,000,000 bytes.
- Peak process RSS: 183.88 MiB.
- Every block restored with exact expected SHA-256.
- Mean capture/restore per block: 199.85/194.04 ms.

This is a mask-payload test, not full 45 MP RGBA frames, multiple live photos,
rendered parity, or app undo/redo. The 4.5 GB in-memory baseline is deliberately not
run; no process-RSS reduction ratio is claimed for that stress case.

`history_payload_store_test` passed three consecutive CTest runs
(4.81/4.78/4.78 seconds). Coverage includes exact nested masks/paint/snapshot
restoration, shared restored buffers, borrowed-buffer ownership, handle lifetime,
concurrent retain/load, unreferenced cleanup, empty payloads, same-length
corruption, missing files, and unavailable spill storage. Error paths retain
handles/current input; controller-level recovery remains to be integrated/tested.

[Raw measurements and comparison](evidence/performance-2026-09-28/history-payload-storage.json)
are retained. Build/check logs:
`/private/tmp/looksy-performance-fix-20-final-build.log` and
`/private/tmp/looksy-performance-fix-20-final-checks.log`.

### Integration checklist from the component phase

This checklist predates the integration summarized below; it is not a statement
that every item remains unimplemented.

1. Replace history vector consumers with record handles and bounded background
   capture/restore. Keep normal recent undo fast, bound queued bytes, prefetch
   older revisions, and surface spill/read errors without advancing history.
2. Preserve coalesced edits, redo branches, asset switching, history hover/click
   restore, cancellation, labels/details, and save/recovery behavior. The current
   history-list formatter reads every state, so it must not trigger bulk disk
   materialization after this change.
3. Account for cached states and snapshots; integrate memory-pressure policy.
   Current edits/renderer/outbox ownership cannot be counted as freed merely
   because the history store releases its reference.
4. Replace whole-file named-snapshot rewriting with per-asset/revision persistence
   while retaining existing snapshots and proving migration/relaunch behavior.
5. Run real controller and native workflows: 100 raster-changing edits across
   several large photos, measured peak RSS, undo/redo latency, and exact rendered
   restoration. Component results alone do not close these gates.

### Handoff to finding 21

The user requested moving to the next ranked issue. Finding 20 remains partial.
Revision handles are now connected to `HistoryService` and controller undo/redo,
with asynchronous cold restoration and history descriptions, stale-result guards,
and a byte allowance for recent resident revisions. Five focused tests passed
before the final watcher-lifetime follow-up. The first integrated 64 x 4 MiB
measurement regressed peak RSS (baseline 304 MiB, paced 347 MiB, burst 561 MiB),
so the component memory improvement is **not accepted as an app improvement**.
The follow-up releases completed watcher results/callbacks sooner and processes
deferred deletions in the fixture; its rebuild and remeasurement remain pending.
Queued raw payload bytes, cached states, named snapshots, memory-pressure policy,
and native large-photo acceptance remain open.

## 21. Metal allocation and cache locking — measured partial improvement

### Retained change

`publish_masks_and_color_lut_surface` now releases `color_lut_mutex` after
acquiring immutable cached buffers. Per-render pixel/geometry/scratch allocation,
source upload, and command encoding occur outside the cache lock. Local ARC
owners retain buffers across concurrent cache eviction; encoded commands keep
their resources alive for GPU execution. Shader math and buffer contents are
unchanged. Cache-miss allocations/uploads still occur under the lock.

### Matched direct-Metal measurements

Apple M4 Pro, Release build, 36 publications per lane, three runs per version and
mode. Three concurrent lanes use 1600x1200, 640x480, and 2560x1707 inputs with tone,
curves, spatial/fine detail, and geometry. Values below are **medians of per-run
p95**, not pooled samples or native desktop measurements.

| Concurrent metric | Before | After | Change |
| --- | ---: | ---: | ---: |
| Preview submission p95 | 10.072 ms | 7.625 ms | -24.3% |
| Thumbnail submission p95 | 7.823 ms | 2.207 ms | -71.8% |
| Large-frame submission p95 | 15.230 ms | 15.857 ms | +4.1% |
| Preview completed/read-back p95 | 32.366 ms | 30.619 ms | -5.4% |
| Peak process RSS | 961.25 MiB | 990.98 MiB | +3.1% |

The predefined target was at least 15% lower concurrent preview submission p95,
with serial submission/completion and peak RSS regressions below 10%, plus exact
pixels. Aggregate results pass: serial latency changes range from -6.1% to +3.4%;
serial peak RSS changes +0.01%. Timing varies between runs; concurrent preview
submission improved 11–42% across the three comparisons. Concurrent total elapsed
time increased 1.7%; this is reduced submission contention, not a throughput claim.

The initial diagnostic fixture hashed every output, leaving long gaps between
renders. Its first candidate result failed the latency target. Both baseline and
candidate were rerun with full byte comparisons against retained serial reference
pixels to exercise sustained overlap. Only that matched second fixture is used
for acceptance. Process RSS includes the benchmark's input/reference vectors.

### Correctness and reproduction

All 12 timed runs produced the same nine reference SHA-256 values and exact
per-publication byte comparisons. A separate concurrency contract passed three
runs, each with 51 distinct source-plus-adjustment-layer recipes and 306 concurrent
publications. Explicit stage checks cover masks, source pixels, tone, curves,
downstream look, user LUTs, geometry, spatial and fine detail. It churns beyond
cache entry limits and checks pre-cancellation followed by a successful retry.
It compares concurrent output to serial GPU output; it does not establish CPU/GPU
or real-photo fidelity independently.

Build `metal_submission_benchmark`, then run `--serial <report.json>`,
`--concurrent <report.json>`, or `--contract <report.json>` on a host with Metal.
CTest registration: `metal_concurrent_publication_test` (returns skipped when no
Metal device is available). The sandbox reported `no_system_metal_device`; all
accepted runs used the host. The existing unused `copy_pixels_to_float_buffer`
warning remains; the benchmark compiled without new warnings.

[Raw runs, contracts, gates, and comparison](evidence/performance-2026-09-28/metal-cache-locking.json).
Build log: `/private/tmp/looksy-performance-fix-21-final-build.log`.

### Remaining scope

Scratch-buffer pooling is unimplemented. Cache-miss critical sections, full-size
export concurrency, and the native desktop preview/thumbnail/export workload
still need measurement and acceptance. Finding 21 is **partial**, and the overall
performance goal remains active. Finding 22 is addressed below; finding 23
(Carousel undo/autosave) is the next untouched ranked issue.

## 22. Timeline undo capture — measured partial improvement

### Retained change

`copyTracksForSnapshot()` reuses a previous immutable snapshot's clip array when
all clip values in that track are unchanged. Changed arrays detach into independent
clip objects; nested Qt strings, effect lists, keyframes, and maps keep normal value
sharing until mutation. C++ defaulted equality covers every clip field, including
nested effect masks, keyframes, captions, and output variants.

The live clip array is not newly shared with history. Edit methods often retain
Track/Clip pointers across capture, which would bypass a later container detach.
Reusing a separate immutable snapshot preserves those pointers and undo state.
Track properties are copied for every revision; clip-array reuse follows track ID,
so track reordering remains correct. Existing 8–50 entry limits and the 80k
clip-entry target are unchanged.

The first candidate improved ordinary capture but regressed a last-clip edit on
a single 10k-clip track by 39.3%. It was superseded. The retained candidate checks
the selected clip for a mismatch first, then performs full value comparison before
sharing. An edit outside the selection still invalidates the correct array.

### Final matched measurements

Release build, isolated offscreen controllers, 36 captures and 36 public coalesced
trim edits per run. Fixtures contain four effect names and one keyframe per clip.
Three runs per version/workload alternate baseline/candidate order. Values are
medians of per-run p95, not native Timeline frame/input timings.

| Clips / tracks / edited position | Capture p95 before → after | Trim p95 before → after | Peak RSS before → after |
| --- | ---: | ---: | ---: |
| 100 / 10 / first | 0.038 → 0.019 ms | 0.084 → 0.080 ms | 48.88 → 30.33 MiB |
| 1k / 10 / first | 0.233 → 0.144 ms | 0.716 → 0.647 ms | 121.94 → 41.25 MiB |
| 10k / 10 / first | 2.272 → 1.335 ms | 12.041 → 11.245 ms | 287.91 → 130.89 MiB |
| 10k / 10 / last | 1.989 → 1.346 ms | 11.647 → 11.737 ms | 269.67 → 132.70 MiB |
| 10k / 1 / first | 2.326 → 1.472 ms | 5.457 → 4.706 ms | 249.28 → 239.27 MiB |
| 10k / 1 / last | 2.158 → 1.508 ms | 5.651 → 5.119 ms | 249.30 → 239.27 MiB |

At 10k clips/ten tracks, capture improved **41.2%** for the first clip and **32.3%**
for the last; peak RSS fell **54.5% / 50.8%**. Unique retained clip-array capacity
fell **81.4%**. Capacity accounting excludes nested payloads and the live document;
it is not a global memory budget or a measurement of committed physical pages.

The predefined gate was at least 20% lower 10k/ten-track capture p95, with public
trim and peak RSS regressions below 10%. All eight final workloads pass. The
largest trim regression is 0.8%, while capture improves 13.7–51.5% across all
sizes. Other trim work still dominates overall edit latency. Earlier exploratory
runs are not mixed into this final matched comparison.

### Correctness and reproduction

All 48 final timing runs checked snapshot isolation through an existing clip
pointer and public undo/redo restoration. Five focused tests passed three times:

- `timeline_history_contract`: nested values, immutable reuse, changes outside the
  selection, coalesced trim, keyframes, effects, clip movement, track reordering,
  redo branching, and exact external-sequence file restoration.
- Existing color-grade curve, CUBE LUT, shared keyframe curve, and title-motion
  keyframe workflows, linked against the shared controller runtime.

Build targets: `timeline_history_benchmark`, `timeline_history_workflow_regression`.
Run `timeline_history_benchmark <clips> <tracks> <report.json> [--last]` or
`--verify`. CTest selection: `^timeline_history_.*(contract|regression)$`.

[Measurements, gates, and original/candidate capture functions](evidence/performance-2026-09-28/timeline-history-sharing.json).
Logs: `/private/tmp/looksy-performance-fix-22-final-build.log`,
`/private/tmp/looksy-performance-fix-22-candidate-2-build.log`, and
`/private/tmp/looksy-performance-fix-22-final-tests.log`.

Unrelated grouped-clip movement edits appeared concurrently in the worktree and
were preserved. They are outside the capture/trim timing path; the evidence
records this provenance boundary.

### Remaining scope

Full value comparison remains O(total clips), and changed tracks still copy their
clip objects. Operation-level undo, byte-budget/pressure policy, heavier per-clip
payload workloads, broad bulk-operation recovery, and native rendered Timeline
interaction remain open. This is accepted controller-level progress, not completion
of finding 22 or the overall performance goal. Next untouched priority: 23.

## 23. Carousel undo and autosave — measured hot-path improvements, acceptance open

### Retained change

`mediaObject()` and `itemObject()` previously probed source files unconditionally,
including SHA-256 of the first/last 64 KiB. When all persisted source-identity fields
were populated, the computed values were discarded. Serialization now probes only
when it needs a fallback size, modification time, fingerprint, or media dimensions.
Existing stored-value precedence and missing-field behavior are preserved.

This improves the document serialization shared by undo capture and autosave. It
does not introduce a filesystem cache, change recovery format, or move work to a
worker. Coalescing and current/last-good atomic recovery already existed.

### Final matched measurements

Release build, isolated offscreen controllers. Each board has two image items,
two text items, and two brush items with 48 points each. Images reference a real,
deterministic local PNG with complete captured source identity. Each process runs
20 undo captures followed by six autosave callbacks. Three runs per version and
size alternate baseline/candidate order; these are medians of per-run p95.

| Workload | Capture p95 before → after | Autosave callback p95 before → after | Peak RSS change |
| --- | ---: | ---: | ---: |
| 50 boards / 300 items | 108.49 → 29.06 ms | 129.97 → 52.48 ms | +0.19% |
| 200 boards / 1,200 items | 423.09 → 115.60 ms | 508.60 → 194.43 ms | -0.21% |

At 200 boards, capture improved **72.7%** and the autosave callback **61.8%**.
All 12 runs restored the same document through undo/redo and recovery and produced
matching normalized serialized SHA-256 values. The predefined gate (20% lower
capture p95; autosave and memory regressions under 10%; exact output) passes.

These are callback timings on one local source image, not native input/frame
latency or many independent slow-storage assets. Autosave timing repeats the
current document; changed-version recovery is covered separately by the contract.

### Correctness and reproduction

Two focused tests passed three times:

- `carousel_history_serialization_contract`: each identity fallback, complete
  identity after source replacement/removal, missing fingerprint/dimensions,
  unknown-field preservation, undo/redo, current/last-good ordering, write failure
  without losing the current document or last-good file, retry, and a new controller
  reopening the last-good document after interrupted JSON. Invalid current bytes
  remain preserved in that recovery fixture.
- `carousel_history_adjustment_regression`: existing adjustment-patch workflows
  against the shared controller runtime.

Build targets: `carousel_history_benchmark`, `carousel_history_workflow_regression`.
Run `carousel_history_benchmark <boards> <report.json>` or `--verify`.
CTest selection: `^carousel_history_.*(contract|regression)$`.

[Measurements and exact-output evidence](evidence/performance-2026-09-28/carousel-serialization-identity.json).
Logs: `/private/tmp/looksy-performance-fix-23-candidate-build.log`,
`/private/tmp/looksy-performance-fix-23-test-build.log`, and
`/private/tmp/looksy-performance-fix-23-final-tests.log`.

### Second retained change: immutable history snapshots

Undo capture now freezes document values rather than serializing every item into
JSON. Snapshot root arrays detach immediately because existing edit handlers can
hold mutable item, board, and group pointers across capture. Nested Qt values,
including cutout bytes, remain shared until mutation. Missing source identity is
resolved at capture time, including failed probes; later serialization does not
inspect a replaced, deleted, or newly created source. Metadata and unknown fields
remain captured. Undo/redo still restore through the existing document parser.

A value-only serializer accepts these snapshots without a controller or GUI object;
the contract exercises it on a worker while the live document changes. Autosave
has not yet adopted that worker path.

Matched comparison against the **first retained identity optimization**, three
alternating runs per version/size, median of per-run p95 values:

| Workload | Capture p95 before → after | Undo+redo pair p95 before → after | Autosave callback p95 before → after | Peak RSS before → after |
| --- | ---: | ---: | ---: | ---: |
| 50 boards / 300 items | 39.34 → 0.23 ms | 99.61 → 79.42 ms | 66.43 → 52.48 ms | 189.27 → 140.73 MiB |
| 200 boards / 1,200 items | 126.78 → 0.88 ms | 276.21 → 269.14 ms | 205.23 → 205.06 ms | 646.66 → 443.83 MiB |

At 200 boards, capture improved **99.3%** and peak RSS **31.4%**. The gate
(50% capture improvement, at most 10% regression in autosave/undo+redo/RSS, exact
output) passes. All 12 runs produced matching document hashes within size and
restored exact recovery/undo contents. Absolute timings varied on the shared host;
the evidence retains every run. The new fixture also measures six undo+redo pairs,
so its peak memory and callback measurements are not interchangeable with the
first-step table above.

The two focused tests passed three times after the final test additions. The
extended contract covers captured source timing, concurrent serialization, existing
mutable pointers, shared 4 MiB raster bytes and subsequent mutation, adjustment
patches, metadata/unknown fields/saved versions, no-op and transient-only
transactions, changed transactions, coalescing, and the existing 48-entry bound.
The previous recovery failure/retry/reopen checks remain in place.

[Immutable history measurements and exact-output evidence](evidence/performance-2026-09-28/carousel-immutable-history.json).
Build/test logs: `/private/tmp/looksy-23-snapshot-final-build.log`,
`/private/tmp/looksy-23-snapshot-contract-build.log`, and
`/private/tmp/looksy-23-snapshot-final-tests.log`.
An intermediate build encountered an unrelated concurrent edit in
`editor_preview_controller.cpp`; the worktree correction was already present
before the successful rebuild. This change did not edit that file.

### Third retained change: bounded worker autosave

The timer captures immutable document values and submits serialization, previous
recovery reads, last-good backup, and atomic writing to a dedicated worker pool.
Each controller retains one active write and one pending checkpoint; newer requests
replace the pending checkpoint. Completion publishes status only for the current
document generation and revision. A stale completion leaves newer edits unsaved
and schedules a later checkpoint.

Explicit save, load, create, close, discard, and destruction drain admitted writes
before changing recovery ownership or removing files. This is a synchronous
lifecycle barrier, limited to the active and pending writes; normal edits and
autosave timer callbacks do not wait. Successful explicit save now also removes the
old session-keyed recovery when it assigns a new saved-document ID. Failed explicit
save preserves that recovery and resumes the autosave timer.

A failed last-good write prevents replacing current recovery. Current-schema
checkpoints avoid redundant migration; legacy checkpoints still migrate, and an
unsupported current schema cannot replace valid last-good recovery. Atomic writes
reject short writes. Serialization/writing still processes complete documents.

Matched comparison against the **retained immutable-history implementation**:
nine runs per version/size, median of per-run p95 values, alternating order. The
measurement function is identical in both binaries. All 36 final runs are retained.

| Workload | Autosave UI callback p95 before → after | UI timer delivery p95 before → after | Persistence completion p95 before → after | Peak RSS before → after |
| --- | ---: | ---: | ---: | ---: |
| 50 boards / 300 items | 52.65 → 0.37 ms | 52.67 → 0.38 ms | 52.65 → 53.23 ms | 139.20 → 140.78 MiB |
| 200 boards / 1,200 items | 199.93 → 0.92 ms | 199.94 → 0.93 ms | 199.93 → 197.97 ms | 442.97 → 469.80 MiB |

The 200-board UI callback improved **99.5%**; peak RSS increased **6.1%**.
Completion, capture, undo+redo, and memory remained within the 10% regression caps;
UI callback improvement exceeded 90% and timer delivery stayed below the 8 ms
aggregate gate. All final runs restored identical document contents and matching
normalized hashes. These are controller/event-loop timings, not native rendered
frame or pointer-input measurements.

Candidate 1 failed the completion-time guard (roughly 45% regression at 200 boards).
Candidate 2 removes redundant current-schema migration. Its first three 50-board
runs showed large host variation, including unchanged baseline p95 from 47.88 to
100.92 ms. The comparison was extended once to nine runs, retaining the original
three and every additional run. The evidence includes both candidates and all raw
results; the final table uses only candidate 2's full nine-run comparison.

The extended serialization/recovery contract and adjustment regression passed
three times. Deterministic worker gating checks 20 coalesced requests, stale
completions, failure/retry, backup failure, explicit save and failed-save behavior,
load/create/close/discard/destruction, and the existing last-good reopen and immutable
history contracts. Save/load tests use an isolated failing helper and temporary
files; they do not access the user's catalog database.

The lifecycle fixture also exposed an existing preview-cleanup shutdown crash:
a preview worker submitted cleanup after Qt's global pool had been destroyed.
`PreviewFiles` now performs cleanup directly during application shutdown. Normal
operation keeps asynchronous cleanup; repeated contracts now exit cleanly.

[Worker autosave measurements and lifecycle evidence](evidence/performance-2026-09-28/carousel-worker-autosave.json).
Logs: `/private/tmp/looksy-23-autosave-final-tests.log`,
`/private/tmp/looksy-23-autosave-candidate-2-build.log`, and
`/private/tmp/looksy-23-autosave-comparison.log`.

### Fourth retained change: exact document comparisons

Transaction commit and saved-document dirty checks now compare immutable snapshot
inputs before serializing. Equal item/media/board/group inputs need no JSON. When
inputs differ, only those records are serialized and merged with their original
unknown fields, preserving canonicalization and optional-field behavior. Geometry
and brush coordinates compare exactly; Qt's fuzzy geometry equality is not used.
Selection fields remain excluded only from saved-content comparisons.

The saved baseline retains its JSON alongside typed values. If load/undo replaces
passthrough content, equality falls back to full serialization using that cached
baseline. This preserves exact behavior without adding a second saved-document
serialization to every undo/redo. Closing, recovery loading, and creating a document
release both saved baselines.

Matched comparison against the **retained worker-autosave version**: five runs per
version/size, median of per-run p95 values, alternating order. Each process measures
12 unchanged, first-item-change, and last-item-change checks/transactions, followed
by the existing history/autosave/undo+redo workflow with a saved baseline.

| Measured stage | 50 boards / 300 items, before → after | 200 boards / 1,200 items, before → after |
| --- | ---: | ---: |
| Dirty check, unchanged | 31.52 → 0.22 ms | 120.67 → 0.72 ms |
| Dirty check, first item changed | 30.78 → 0.70 ms | 115.66 → 1.92 ms |
| Dirty check, last item changed | 31.37 → 0.74 ms | 118.98 → 2.26 ms |
| Begin/commit transaction, unchanged | 61.24 → 0.43 ms | 235.13 → 1.39 ms |
| Begin/commit transaction, first item changed | 61.14 → 0.62 ms | 232.95 → 1.98 ms |
| Begin/commit transaction, last item changed | 60.82 → 0.77 ms | 234.07 → 2.77 ms |

All six stage comparisons at both sizes exceed the 90% improvement gate. Persistence
completion, undo+redo, and peak RSS remain within 10% regression caps; capture and
autosave UI callbacks stay below 2 ms, and timer delivery below 8 ms. Peak RSS rises
0.9% at 50 boards and 1.5% at 200 boards. The small 50-board autosave callback moves
0.23 → 0.29 ms, within its absolute guard. All 20 runs preserve exact document,
recovery, and undo/redo contents, with matching normalized hashes within size.

Three focused tests passed three times. The expanded equality oracle compares
**177 typed-field mutations**, every metadata field, exact subpixel geometry,
NaN/infinite JSON behavior, equivalent adjustment patch formatting, reordered IDs,
removed items, and optional/unknown-field merging against full serialization.
Selection-only dirty state, return-to-saved behavior, and saved-baseline release
also pass. Existing history and worker recovery/lifecycle contracts remain covered.
A Python source-coverage CTest checks that all **172 serialized input fields** occur
in the fast comparisons; it is registered when a Python 3 interpreter is available.

[Document comparison measurements and equality evidence](evidence/performance-2026-09-28/carousel-document-comparisons.json).
Logs: `/private/tmp/looksy-23-equality-final-build.log`,
`/private/tmp/looksy-23-equality-tests.log`, and
`/private/tmp/looksy-23-equality-comparison.log`.

### Remaining acceptance and priority

The recurring undo-capture, autosave, transaction, and saved dirty-check hot paths
have measured controller improvements. **Finding 23 is not fully complete.**
Undo/redo still materializes JSON and restores through the existing parser; the
saved-fixture pair remains about **505 ms**. Changed passthrough values still trigger
full comparison serialization. Root capture and comparison remain linear scans;
unknown source identity can still probe synchronously. There is no history byte
budget, and large named-version metadata can still require substantial merging.

Native 50/200-board frame/input behavior, lifecycle drain latency, large independent
or slow-storage assets, large raster history memory, disk-write volume, and actual
application crash/relaunch remain unverified. Lifecycle actions still synchronously
drain up to two admitted writes. These acceptance and residual-cost items stay open.

**Next highest untouched finding: 24, hidden workspace Library projections.**
Current Carousel catalog callbacks still rebuild picker assets on asset count,
total count, project-stat revision, and loading changes without a visibility gate;
only media-duration updates currently check picker visibility. Findings 24–30 and
the earlier recorded acceptance gaps remain in the full goal scope.

## 24. Hidden workspace Library projections

### Visibility gating and coalesced refresh

Carousel previously rebuilt its full media list on startup and on four catalog
notifications even with the workspace or picker closed. Timeline already gated its
four existing callbacks while the picker was closed; it still rebuilt once per
notification when open. Main retains both workspace loaders after their first visit.

Both workspaces now mark picker data dirty on catalog changes and schedule one
refresh per event-loop burst only while the workspace and picker are visible.
Closing or hiding cancels queued refreshes. Opening synchronously obtains current
rows once; scope changes refresh the visible picker. A tracked preferred-project
property follows catalog filters and Timeline project bindings. Metadata/model
changes update visible rows while preserving selection by asset ID; a removed
selected asset clears selection. Carousel retains its photo/video and photo-only
logo filtering. Both workspaces now use a lightweight catalog projection containing
the nine fields consumed by the dialog/card: ID, label, thumbnail, type, duration text and
seconds, file path, Timeline document path, and online state. Carousel also filters
media types before constructing rows. This avoids projecting recipes and unrelated
metadata for every unselected asset. The original full-map API remains available.
Carousel retains its JavaScript array representation. The unused Carousel startup
projection is removed.

### Isolated acceptance

`workspace_picker_refresh_test` extracts the actual source refresh functions,
catalog handlers, timers, preferred-project dependency, and visibility handlers.
It runs them with the real `GalleryPickerDialog` and real
`CatalogAssetModel` projection APIs in an offscreen Qt Quick software window.
The oracle obtains the original full maps, filters them independently, and compares
all nine picker fields and row order exactly with the lightweight result.
Other workspace behavior is stubbed. Catalog signals are emitted by a counting
adapter; synthetic records contain photo/video/audio/timeline media and two projects,
with offline paths, known durations, and empty adjustment payloads.

The lifecycle contract checks zero startup/hidden scans, one scan per visible
signal burst, fresh rows on opening, project changes, Timeline binding precedence,
selection identity after reordering, updated selected metadata, cleared selection
after removal, nonempty photo-only logo results, and cancellation on close or
ancestor visibility changes. A direct oracle covers five project scopes and six
media filters, and a source check detects new dialog/card field dependencies.
The existing full maps do not contain `recipeJson`, although Carousel insertion
reads that optional key; this change preserves the existing empty fallback and
does not claim to repair recipe transfer. A guard requires revisiting the compact
projection if the full API begins supplying that key. These checks exercise the
actual dialog but do not establish full native workspace or Library workflow acceptance.

Third candidate accepted for this isolated workload. The first candidate (gating/
coalescing) exceeded the Carousel opening guard in Debug: **407.650 → 459.548 ms**.
The second (adding media filtering before full maps) still exceeded it in Release.
Neither was accepted. The final candidate uses small picker rows.

Final matched Release comparison: three fresh processes per version, workspace,
and size, with five samples per process (**24 runs total**). Values below are
medians of per-process nearest-rank p95 values. Peak RSS uses the median process
peak. All nine picker fields and row-order hashes match in each paired scenario.

| Workspace / assets | Hidden burst before → after (ms) | Open before → after (ms) | Visible burst before → after (ms) | Peak RSS before → after (MiB) |
|---|---:|---:|---:|---:|
| Carousel / 1,000 | 43.887 → 0.025 | 12.570 → 5.192 | 96.471 → 41.475 | 99.250 → 85.969 |
| Timeline / 1,000 | 0.031 → 0.029 | 51.868 → 14.677 | 204.082 → 41.356 | 107.078 → 90.922 |
| Carousel / 10,000 | 886.988 → 0.028 | 234.925 → 23.606 | 1174.146 → 41.399 | 257.844 → 187.672 |
| Timeline / 10,000 | 0.014 → 0.026 | 1562.960 → 55.073 | 5753.131 → 52.786 | 375.109 → 237.281 |

Visible burst timing includes a minimum **40 ms** event-processing window; it is
not native input or frame latency. Across five bursts, Carousel hidden scans fall
**20 → 0** (150,000 full mapped rows avoided at 10k assets), visible scans **25 → 5**,
and startup scans **1 → 0**. Timeline hidden/startup scans stay at zero and visible
scans fall **20 → 5**. Opening makes exactly one current projection.

All four workload groups pass: exact picker contents/order, zero hidden scans, one
visible scan per burst, at least 50% less C++ projection work per visible burst,
opening and visible burst latency within a 10% + 2 ms guard, and peak RSS within
10%. Both `workspace_picker_refresh_contract` and `gallery_picker_dialog_ui_test`
pass **three consecutive times** on the final build.

Evidence: [all attempts, raw runs, hashes, gates, and test results](evidence/performance-2026-09-28/workspace-picker-refresh.json).
Reproduce a run with `workspace_picker_refresh_test Carousel 10000 report.json`
(or `Timeline`); the optional fourth argument selects a frozen workspace-source
directory for the original handlers. Run `--verify` for field/filter/lifecycle
contracts. Temporary build/runner details are under `/private/tmp/looksy-24-*`.

### Remaining cost and acceptance

Finding 24 remains partial. Opening still scans the catalog and constructs a
complete list of small rows; closed pickers retain their last list. An incremental
or paged picker model and releasing closed presentation data remain follow-ups.
Native import and culling after visiting both workspaces, real adjustment payloads, frame/input
latency, and retained-memory behavior remain unverified. Findings 25–30 and earlier
recorded acceptance gaps also remain open. Next untouched priority: 25, synchronous
diagnostic logging.

## 25. Diagnostic logging on emitting threads

### Bounded writer and delivery behavior

The original Qt handler held a global mutex while formatting, parsing JSON, writing
one file record, and flushing it. It also formatted console text even when the
console suppressed or replaced that text. Local diagnostics are enabled by default
in production builds through `PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS`; this path
was not limited to development builds.

`observability_writer.h` now owns a single writer thread and QFile. It prepares
JSONL/console output and flushes batches of up to **64 records**. Sparse routine
output can coalesce for **2 ms**; warnings, critical/fatal messages, explicit
flushes, full budgets, and shutdown bypass that wait. The pending budget includes
active work: **4,096 records / 8 MiB estimated captured data**, with one oversized
record admitted alone. Producers wait when full; capacity pressure does not drop
records. Batch buffers and Qt allocations are additional memory.

Producer timestamp, thread, file, line, function, and message data are owned before
returning from the handler. Structured `logInfo`/`logWarning` calls retain the JSON
object they already built, avoiding a parse and unused console formatting. They
still send the same message through Qt, preserving category filters and custom
handlers. Generic Qt records keep their producer-side console pattern and are
parsed on the writer. File/console batches preserve admission order.

Warnings, critical errors, and Qt fatal messages wait for delivery through their
own record. `observability::flush()` provides an explicit read/shutdown barrier;
desktop shutdown uses it after its shutdown event, and an exit hook drains and
joins the worker. The existing agent log assertion helper now flushes before
reading. Configuration stays alive for late static-destructor callbacks. File
failure reports and complete fallback records go to stderr. These are QFile
flushes, not fsync durability; abrupt process death can lose queued routine logs.

### Matched measurements

`observability_benchmark` uses the production Qt handler, JSON formatting, and
file writes. Only the AppBootstrap log-root dependency is replaced with temporary
fixture storage. Both versions use Release `-O3 -DNDEBUG`, C++20, Apple clang, and
Qt 6.10.2. The baseline compiles the frozen original logger. Each producer emits
10,000 representative structured preview events; critical delivery is included
in total drained time. RSS is captured before result-file parsing/verification.
Stderr is redirected to a regular file in both versions.

The first candidate left default console formatting on producers. The second
reused prepared fields and batched both outputs, but its 128-record batches missed
p99 guards under four producers. The final scheduling candidate uses smaller
batches and bounded sparse coalescing. Its first three pairs missed the compact
four-producer 30% p95 target by about **3 µs**, while baselines varied. Two more
pairs were collected for **every scenario**, with unchanged binaries. All five
pairs are retained and included; this was remeasurement of the third candidate.
There were **100 final process runs** across five modes and one/four producers.

Values below are medians of per-process percentiles and elapsed/RSS values.

| Mode / producers | Producer p95 before → after (µs) | Producer p99 before → after (µs) | Total drain before → after (ms) | Peak RSS before → after (MiB) |
|---|---:|---:|---:|---:|
| quiet / 1 | 151.292 → 13.167 | 169.708 → 17.125 | 1351.050 → 91.847 | 13.000 → 14.125 |
| quiet / 4 | 719.625 → 284.917 | 1241.375 → 322.666 | 6697.628 → 306.035 | 13.359 → 21.250 |
| compact / 1 | 180.375 → 14.334 | 224.083 → 18.958 | 1522.442 → 105.297 | 13.016 → 16.234 |
| compact / 4 | 524.833 → 363.083 | 736.875 → 403.542 | 5900.195 → 352.676 | 13.656 → 21.500 |
| over_budget / 1 | 149.917 → 10.125 | 166.125 → 12.917 | 1371.218 → 84.135 | 13.000 → 14.094 |
| over_budget / 4 | 492.042 → 284.041 | 731.750 → 318.208 | 5666.190 → 304.203 | 13.594 → 21.531 |
| json / 1 | 151.417 → 143.000 | 169.000 → 154.625 | 1385.133 → 1282.492 | 13.000 → 13.672 |
| json / 4 | 504.875 → 460.542 | 679.042 → 517.750 | 5840.490 → 5296.278 | 13.781 → 14.016 |
| disabled / 1 | 0.541 → 0.542 | 0.542 → 0.583 | 5.547 → 5.523 | 12.531 → 12.641 |
| disabled / 4 | 0.958 → 0.584 | 2.416 → 2.625 | 6.399 → 7.787 | 13.109 → 13.109 |

All ten groups pass the declared gates:

- At least 30% lower producer p95 in quiet, compact, and over-budget modes.
- No more than 10% p99 regression; the disabled control permits 2 µs absolute slack.
- Chunk p95 within 10% + 0.1 ms, and total drained time within 10% + 5 ms.
- Peak RSS within baseline + 32 MiB. This is a queue-allocation guard, not a promise of lower memory.
- Exact normalized records and per-producer order; no missing or duplicated events.

Normalization removes timestamps, PID/thread identity, and source-file/line/function
locations; source context and producer thread identity have separate exact checks.
The disabled mode suppresses the info category; it is not a diagnostics-disabled
build. The full JSON console mode still pays for generic Qt console formatting
on the producer and has smaller gains. None of these figures is native frame
or physical-input latency.

### Correctness and remaining acceptance

`observability_writer_test` and `app_bootstrap_test` passed three consecutive
runs. Coverage includes active-batch accounting, full-queue backpressure, oversized
records, FIFO order, explicit flush barriers, stopping with blocked admission,
normal process-exit drain, Qt fatal delivery, source-context lifetime, producer
thread identity, Unicode/escaping, malformed/plain messages, category suppression,
warning rate limiting, custom Qt message handlers, custom console patterns,
non-string event overrides, console modes, and file-open failure fallback.

Evidence: [all candidates, raw final runs, gates, hashes, and contract results](evidence/performance-2026-09-28/observability-writer.json).
Reproduce one workload with `observability_benchmark 4 10000 report.json compact`.
Temporary builds and traces are under `/private/tmp/looksy-25-*`. The actual
desktop `main.cpp` and `observability.cpp` Release objects also compiled, including
the shutdown barrier integration. This was not a linked application or UI run.

Finding 25 has isolated performance/delivery acceptance. Native render/input
behavior, interactive terminal sinks, slow-storage/full-volume/partial-write
behavior, and native application teardown remain open. Backpressure and critical
flushes can still block when a sink stalls. Sampling/aggregation policies are not
added: this implementation retains admitted records. Next untouched priority:
**26, repeated source reads during verified managed import and backup.**

## 26. Verified import source digest reuse

**Status:** accepted for the isolated local-filesystem workload; real card/external
storage, native UI, and other operating systems remain unverified.

### Existing behavior and change

Managed import already made independent verified copies, backed up originals before
replacing any managed destination, preserved different existing backups, and applied
Skip/ReuseIdentical/Replace policies. Its native copy helper rehashed the source for
every destination. For four 16 MiB files with backup, the measured baseline read
128 MiB for source SHA-256, another 128 MiB for destination SHA-256, and requested
128 MiB of native copying. These are logical byte counts, not physical device I/O.

`ImportSourceVerification` now pins each source at its first comparison/copy and
shares one checksum across eligible copies within that `discover_assets` call.
Its fingerprint includes length, modification/creation times, and Unix device,
inode, mtime, and ctime with subsecond components. Reuse requires a nonzero inode
and subsecond ctime. Coarse/unsupported fingerprints retain a fresh source hash
per copy; the checksum must still match any earlier pinned checksum. There is no
cross-import cache. Byte comparisons for duplicate/reused files remain byte
comparisons and do not acquire an unnecessary SHA pass on the normal reuse path.

Source hashes validate the opened handle and pathname before/after reading.
Source metadata is checked again before publication, and every partial destination
is independently hashed. Copy/checksum/read/publication failures clean the owned
partial and preserve the previous destination. Earlier successful backup copies
remain intact if a later operation fails. `std::fs::copy` and platform publication
semantics remain in place; streaming-copy replacement and additional I/O workers
were not needed for this measured improvement.

### Matched evidence

The bounded-loop gate was at least 10% lower p95 for 16 MiB managed+backup imports;
other groups must stay within baseline × 1.10 + 2 ms, and peak process RSS within
baseline + 16 MiB. Exact destination contents, normalized asset IDs/order/counters,
unchanged destination checksum/native-copy counts, and halved source checksum
bytes for managed+backup were also required.

The final comparison has 120 fresh processes: six scenarios × two sizes × five
alternating before/after pairs. Each process times five complete `discover_assets`
calls. Values are medians of process p95 values (with five samples, p95 is the
maximum), and medians of whole-fixture peak RSS from macOS `getrusage`. Files are
synthetic JPEG-named bytes using metadata fallback on the warm local filesystem;
this is not a real-photo/card or cold-cache measurement. Fixture creation and
independent output checks are outside the import timer, but inside RSS scope.

| MiB per file (four files) | Scenario | Import p95 before → after (ms) | Peak RSS before → after (MiB) |
|---:|---|---:|---:|
| 1 | backup | 37.291 → 28.752 | 7.50 → 7.52 |
| 1 | one | 18.610 → 18.013 | 7.50 → 7.56 |
| 1 | reference | 0.281 → 0.286 | 7.45 → 7.47 |
| 1 | reference_backup | 18.498 → 18.632 | 7.48 → 7.50 |
| 1 | reuse | 2.310 → 2.430 | 7.69 → 7.73 |
| 1 | skip | 1.050 → 1.083 | 7.72 → 7.72 |
| 16 | backup | 514.458 → 403.074 | 7.52 → 7.56 |
| 16 | one | 265.399 → 265.549 | 7.50 → 7.58 |
| 16 | reference | 0.345 → 0.346 | 7.47 → 7.52 |
| 16 | reference_backup | 271.802 → 265.730 | 7.52 → 7.55 |
| 16 | reuse | 31.861 → 30.968 | 7.70 → 7.75 |
| 16 | skip | 14.887 → 15.918 | 7.61 → 7.69 |

All twelve groups pass. In the 16 MiB managed+backup case, source checksum bytes
fall from 128 MiB to 64 MiB; destination checksum bytes remain 128 MiB and native
copy requests remain 128 MiB. The first three-pair diagnostic comparison overlapped
compiler activity and is retained in the evidence as a pilot, excluded from final
acceptance. A preliminary counter classified `/var` versus `/private/var` paths
incorrectly; source-root canonicalization corrected it before candidate acceptance.
`/usr/bin/time -l` could not read the sandbox clock-rate sysctl, so final RSS uses
one fresh Python wrapper's child `getrusage` per benchmark process.

### Correctness and remaining acceptance

The 24 focused importer contracts passed three consecutive final runs (the explicit
benchmark is ignored in normal test runs). They cover managed/reference imports,
selection order, root escape, RAW/JPEG policy, backup refusal, skip/reuse/replace,
empty files, checksum reuse, source mutation during hashing/copy/pre-publication,
same-length edits with restored mtime, path replacement, disappearance, corrupt or
unreadable partials, publication failure, preserving the previous destination and
verified backup, per-import cache lifetime, and forced uncached fallback including
checksum changes hidden from the metadata fingerprint. The production Rust library
also builds in Release without test instrumentation; no native app was launched.

Evidence: [raw comparisons, gates, source hashes, and contract results](evidence/performance-2026-09-28/verified-import-source-digest.json).
Reproduce a workload from `rust/`:

```sh
LOOKSY_IMPORT_BENCH_OUTPUT=/tmp/import.json \
LOOKSY_IMPORT_BENCH_SCENARIO=backup LOOKSY_IMPORT_BENCH_MB=16 \
cargo test --offline --release -p photo-catalog --lib \
  importer::copy_test::verified_import_benchmark -- --ignored --exact
```

Temporary baseline/candidate binaries and comparison scripts are under
`/private/tmp/looksy-26-*`.

Remaining boundaries: actual physical reads on a card/external drive, native UI
responsiveness, cold storage and filesystem-specific behavior, and Windows are
not measured. The context retains O(number of touched sources) metadata/digests,
not image buffers; large-import memory still needs measurement. Metadata checks
are not an atomic filesystem snapshot, and metadata extraction precedes pinning.
The existing predictable partial filename and POSIX CreateNew check/rename race
remain; concurrent writers to the same destination are not newly supported.
Repeated duplicate byte comparisons, streaming checksum/copy fusion, and bounded
I/O concurrency remain separate opportunities. Next untouched priority:
**27, unrelated job classes sharing a single active slot.**

## 27. Job scheduling: per-attempt execution ownership

**Ownership-phase status (superseded by 27b below):** the execution-state prerequisite
was implemented and controller-tested while admission was still serial. This phase
alone did not claim a queue-wait or throughput improvement.

The existing runner stored one shared completion watcher, job ID/kind, timer,
and cancellation token. Allowing a second job in that design would overwrite
state needed by the first job. `JobRunner::Attempt` now owns those fields for
one attempt, with a map indexed by durable job ID. Admission rejects an ID that
is already active. Start, completion, and release callbacks validate the attempt
object as well as its ID, so a late callback cannot affect a replacement retry.
Completion retains ownership through the persistent-record update, then releases
only that attempt on all return paths.

Cancellation observation captures all active IDs/tokens and performs one
read-only store request. Results apply only to matching tokens, preserve sticky
cancellation, and retain coalescing and failed-read retry. Watcher callbacks hold
weak references; completion keeps a temporary strong reference, and watcher
removal uses deferred deletion. Runner destruction detaches worker callbacks
without waiting for those workers.

### Verification

Before the change, the four existing Release runner tests passed. After the
change, `job_runner_startup_test`, `job_runner_scan_test`,
`job_runner_wake_test`, `job_runner_cancellation_test`, and the new
`job_runner_attempt_test` passed three consecutive runs each. An extended version
of the new test then passed three additional runs, checking the job IDs and error
payloads sent through the persistent upsert boundary during reversed completion.

The new test runs the actual controller against a fixture helper and controlled
worker futures. It verifies independent watchers/tokens, duplicate-ID rejection,
one cancellation read for two attempts, sticky cancellation, stale replies after
same-ID replacement, stale completion/release rejection, out-of-order completion,
correct persistent-update routing, watcher reclamation, and nonblocking shutdown.
The helper captures update requests; it does not prove real database durability.
Builds passed. An existing unused-function warning in the Metal backend and
Ninja log-recovery warnings are recorded separately in the raw build evidence.

Evidence: [source hashes, build logs, and test output](evidence/performance-2026-09-28/job-runner-attempt-ownership.json).
Reproduce with `ctest --test-dir build/release-macos -R
'^job_runner_(scan|wake|cancellation|startup|attempt)_test$'
--output-on-failure --repeat until-fail:3`.

### Work identified after the ownership phase (see 27b for subsequent changes)

- Add resource-aware admission that permits an independent lightweight job
  during heavy work, while keeping heavy render concurrency bounded.
- Account for CPU/GPU/I/O/memory use and foreground editing intent; import
  metadata extraction currently creates its own Rust thread group.
- Reserve output paths and protect shared source/destination dependencies,
  especially managed-import replacement and timeline outputs.
- Measure a small import/export queued behind a long render against the serial
  baseline, with queue wait, throughput, interactive latency, and memory guards.
- Verify cancellation, ordering, restart recovery, and outputs in real catalog
  and native workflows. Execution admission/completion still contain synchronous
  store helpers that must be considered in the responsiveness measurement.

At the end of this ownership phase, finding 27 remained the priority. Subsequent
admission work and finding 28 are recorded below.

## 27b. Bounded admission and current proof boundary

Resource admission is now implemented: at most one heavy render and one independent
light I/O job run together on hosts with at least four processors and 8 GiB RAM.
Unknown capacity, foreground interaction, and memory pressure limit new admission.
Selected-file imports are bounded by file count/size and use at most two metadata
workers (`PHOTO_EDITOR_IMPORT_METADATA_THREADS`); replacement, unbounded, and
unknown jobs remain exclusive. Rendered photo exports share the heavy lane.
Canonical paths, directory reservations, and file identities reject conflicting
reads/writes. Directory reservations deliberately serialize even distinct outputs
in one destination directory. Resource inputs are revalidated before execution.
The memory reserve is an admission estimate, not a global process memory ceiling.

Queue observation inspects only candidates that can use a free lane, stopping at
the first eligible candidate. A 100-job contract verifies one claim inspection.
Within a priority, explicit queue orders precede legacy/unordered records, then
creation time and ID break ties. This also fixes the prior non-transitive mixed
ordering comparator. Six final policy/controller targets passed three times each.
The importer worker-count contracts passed with default, one-, and two-worker limits.

Three final paired fixture processes (five jobs per process) reduced median
copy-completion p95 from **1239 to 268 ms**. This includes queue wait; queue wait
was not measured separately. However, median maximum heartbeat gap was **76 to
89 ms**, exceeding the declared +10 ms guard. A repository search overlapped this
comparison. The earlier candidate passed fixture guards, but final performance
acceptance remains open. No real GPU contention, native input, real catalog
durability, or card throughput is established. Admission/completion still invoke
synchronous store helpers. No further tuning was attempted before moving on.

Evidence: [final runs, guards, contracts, and hashes](evidence/performance-2026-09-28/job-resource-admission.json).
The user requested moving on; finding 28 is recorded below and 29–30 remain untouched.

## 28. Batched rating and flag persistence

**Status:** implemented and accepted in isolated controller/queue measurements.
Real PostgreSQL transaction and helper correctness checks pass. Native UI timing,
large real-catalog throughput, and abrupt power-loss acceptance remain open.

`CatalogAssetModel` now coalesces a burst for 25 ms and sends at most 256 review
records to one asynchronous `catalog_state set-review-states` process. Only one
batch is active. The helper validates the payload, performs a transactional bulk
upsert, and returns exact applied/missing asset IDs. A missing or deleted asset
does not prevent valid records in that batch from being saved. The legacy
single-record command remains available. No database schema migration is added.

A version-1 `culling-outbox.json` checkpoint preserves pending records, including
explicit clears, before a helper starts. Active state is retained until a complete,
valid receipt arrives. A completion retires only values matching its own batch;
newer edits stay queued. Pending values overlay catalog snapshots, and completion
protects snapshots fetched before acknowledgement. Failed or missing records stay
recoverable, with an error and explicit refresh/restart retry instead of a busy
retry loop. Unreadable recovery bytes are preserved. Deleted assets are removed
from pending intent; the database join never recreates missing asset rows.

The process has a 30-second timeout and a FailedToStart path. Orderly destruction
flushes the coalescing window, stops the child, and leaves active records for replay.
The initial 25 ms window is **not** an immediate durable acknowledgement: sudden
termination before checkpointing can still lose those newest edits. The atomic
outbox and existing projects file still serialize on the model thread; very large
pending sets and slow-disk checkpoint costs need further measurement.

### Matched measurement

Three alternating before/after process pairs exercised actual Qt selection/rating
calls and the save queue against the same temporary helper and atomic JSON store.
The baseline executable was frozen before the desktop patch. Values below are
medians of three complete runs, not population p95 estimates.

| Workload / metric | Before | After |
| --- | ---: | ---: |
| 1,000 ratings: time until all fixture writes complete | 52,875 ms | 236 ms |
| Helper launches | 1,000 | 4 |
| Time to enqueue 1,000 ratings | 4 ms | 3 ms |
| Maximum event-loop heartbeat gap | 15 ms | 8 ms |
| Peak process RSS | 39.55 MiB | 34.11 MiB |
| Single-photo save completion | 45 ms | 77 ms |

The measured bulk drain reduction is 99.55%; this is a fixture queue/store result,
not a real-catalog speedup claim. All declared latency, exactness, launch-count,
and memory gates passed. The single-photo increase reflects the coalescing window.
A final error-reporting refinement preserves checkpoint-write errors; repeated
final-candidate probes measured 234 ms bulk and 76 ms single-photo completion,
also passing all gates.

### Correctness and verification

- `catalog_culling_persistence_test`, `catalog_culling_recovery_test`, and
  `catalog_project_membership_test` each passed three final consecutive runs.
- Recovery contracts cover field parity, bounded batches, one active helper,
  heartbeat during a held helper, newer values and clears during active work,
  stale snapshots, failed helpers, restart with an active batch, partial missing
  results, malformed receipts, failed process starts, failed checkpoints,
  shutdown before initial dispatch, and preservation of corrupt recovery bytes.
- The Rust catalog library passed 51 tests; two opt-in tests were skipped by the
  general invocation. The new PostgreSQL test was then explicitly run and passed
  against a temporary cluster, including a forced mid-statement rollback.
- The compiled `catalog_state` helper passed against a fully migrated temporary
  PostgreSQL 14.17 catalog. Independent process/readback checks verified exact
  fields, missing IDs, rollback, clears, newer writes, and duplicate rejection.
  The first fixture omitted required capture dates; correcting the fixture made
  this check pass without a production schema change. Temporary clusters stopped
  afterward; the user's catalog and photos were not used.
- Release builds passed for the new/affected controller targets and Rust helper.
  The broader `catalog_asset_model_test` target was built, but its complete runtime
  suite was not run in this change. No native desktop launch is claimed.

Evidence: [gates, raw measurements, contracts, helper proof, scoped diff, and hashes](evidence/performance-2026-09-28/batched-culling-persistence.json).

The next untouched ranked issue is **29: synchronous Timeline cache diagnostics**.
Finding 30 follows it. Earlier acceptance gaps remain recorded individually.

## 29. Background Timeline cache reports and cleanup

**Implemented; isolated controller performance gates accepted. Native acceptance remains open.**

`projectCacheSummary()` and `cacheBudgetState()` now return cached reports.
A single disk worker discovers artifacts and indexes saved-document references;
unchanged documents reuse a version fingerprint including inode, size, nanosecond
mtime and ctime. Coarse or unsupported fingerprints fall back to rereading.
The retained document index has a 32 MiB cost cap. Visible cache controls refresh
on changes and periodically; stale worker generations cannot replace new state.

Cleanup runs on the same worker, always reparses saved-project references, skips
busy categories, pauses new producers for admitted categories, protects original
source paths, and refuses deletion after an incomplete reference scan. Canonical
containment handles macOS `/var` and `/private/var` aliases while rejecting links
outside the cache. Critical memory pressure requests disk work asynchronously.
QML shows report loading and cleanup progress and waits for a complete report.

### Matched evidence

Three alternating process pairs; each uses the actual controller with 200 other
saved project documents, 2,000 generated files, and eight report requests.
No builds, tests, or repository searches overlapped these comparisons.

| Metric | Before | After |
| --- | ---: | ---: |
| Median report getter pair | 342.691 ms | 0.381 ms |
| Maximum report getter pair | 371.420 ms | 3.785 ms |
| Median critical-pressure call | 174.773 ms | 0.035 ms |
| Candidate cold report ready | — | 226.881 ms |
| Candidate warm report ready | — | 77.632 ms |
| Unchanged project JSON reads per warm refresh | uninstrumented | 0 |
| Maximum process RSS | 39.20 MiB | 39.64 MiB |

File/byte/shared counts were exact. All declared getter, report-ready, pressure,
RSS, and warm-read gates passed. Baseline `ready_ms` includes an extra synchronous
validation summary, so its ratio is not reported as a user-visible readiness
speedup. Candidate cold readiness also meets the stricter bound based on the
baseline getter pair, rather than the extra validation call.

### Correctness and limits

Both cache tests passed three consecutive runs. Contracts cover restored-mtime
mutations, newly shared references before deletion, malformed projects, protected
sources, symlink escape, stale completion, nonblocking destruction/cancellation,
and critical-pressure cleanup using a sparse file above the real 1 GiB minimum.
QML parsing and compilation passed. Four focused Timeline history workflows passed.

The broader UI source contract fails because `job_runner.cpp` no longer contains
its expected `QStringLiteral("queueOrder")` token. The full Timeline controller
workflow crashes with SIGBUS in `moveSelectedForward()`/`QString::arg`; that method
is identical to the frozen pre-29 source. The cause is not proven, and full
regression acceptance remains open. Neither failure is counted as a cache pass.

Native popup/input responsiveness, slow/network disks, and simultaneous editing
and cleanup still need acceptance. Artifact enumeration/stat work remains on the
worker; transient reference memory is not covered by the retained index cap.
Fresh reference validation does not provide a cross-process filesystem transaction.

Evidence: [timeline-cache-inspection.json](evidence/performance-2026-09-28/timeline-cache-inspection.json).
Next untouched finding: **30, visual-search preparation on description cache hits**.

## 30. Reuse visual-search descriptions before preparing pixels

**Implemented; matched controller gates and cache contracts accepted. Native and
real-model acceptance remain open.**

The existing search controller now checks a persistent preparation alias before
image decode, color conversion, scaling, JPEG encoding, and base64 encoding.
Identity includes asset ID, source and thumbnail inode/device/size/nanosecond
mtime/ctime, the edit projection/current recipe hash, model identity, and the
preparation algorithm version. Versions are checked again after lookup/preparation.
Only hashes and description keys are persisted; no raw paths, catalog IDs, or
pixels are added to the local index. Aliases are capped at 10,000 entries, orphan
aliases are pruned, and Forget removes both descriptions and aliases.

Old descriptions-only indexes migrate on write and retain their descriptions.
Unconfirmed catalog previews and missing, coarse, or unsupported file versions
fall back to the existing preparation path. The existing managed-model verification,
private-store isolation, direct inspection, and 32-photo batch controls remain.

### Matched evidence and tradeoff

Three alternating process pairs, each with 1,000 distinct generated 1536×1024
edited-preview fixtures, actual controller execution, and a local protocol fixture
server. No real model inference or photographic-quality claim is made.

| Metric, median of three processes | Before | After |
| --- | ---: | ---: |
| Warm full scan | 1,339.798 ms | 264.969 ms |
| Fresh-controller warm full scan | 1,348.108 ms | 260.648 ms |
| Warm first 32-photo batch | 42.266 ms | 8.695 ms |
| Cold full scan | 3,028.301 ms | 3,542.909 ms |
| Successful image preparations per warm scan | 1,000 | 0 |
| New descriptions per warm scan | 0 | 0 |
| Maximum process RSS | 36.45 MiB | 36.45 MiB |

Cold indexing rose **17.0%**, within the declared baseline × 1.2 + 100 ms guard.
Warm scans improved **80.2%**, with exact IDs/order and zero preparation in all six
candidate warm/reopened samples. All declared latency, count, and RSS gates passed.
The initial one-off baseline was slower than the final matched baseline; only the
alternating matched comparison is used for these accepted improvement figures.

### Verification and remaining scope

The existing visual-search controller test and new preparation contracts each
passed three final runs. Coverage includes changed recipe, unconfirmed preview,
source-path changes, same-size source rewrite with restored mtime, thumbnail
mutation, changed model digest, restart reuse, forgetting, legacy-index migration,
and pruning to 10,000 aliases. Existing privacy, cancellation, detail verification,
managed runtime and private model-store contracts passed.

Cold image preprocessing remains serial. Native search UI, a real photo/RAW
corpus, real model latency, and other operating systems remain unverified.
Evidence: [visual-search-preparation.json](evidence/performance-2026-09-28/visual-search-preparation.json).

**All 30 findings now have a first-pass disposition. The overall goal remains
active: unresolved earlier findings, partial implementations, and native acceptance
listed below still require work.**

## Tool-only priorities (user scope, 2026-10-02)

The user considers Catalog done and requested focusing on tools. Historical
Catalog acceptance notes below are retained for reference; they are outside the
current work queue. Existing implementations are not new work items.

1. **Effect and comparison presentation:** foreground verification now passes
   after the user unlocked the Mac. The measured effect presentation wait is
   about 7–9 ms; the old background/locked-session stall remains a separate
   issue. Compare now reuses the existing edited image, reducing native split
   opening from 513 to 45 ms with exact pixels across three alternating pairs.
   Cache misses, linked views, and larger native workloads remain open.
2. **CPU effect rendering and large-photo memory:** retain accepted kernel and
   inactive-image improvements; finish acceptance of streamed adjustment output
   and address large-content allocation limits. Fully clipped final transparent
   padding now avoids expansion, saving 369 MiB at 24 MP and allowing a 45 MP
   Gaussian render at unchanged limits; transformed/partial content retains its
   expansion cap. Content effect
   output now uses bounded delayed rows, saving 343–358 MiB at 24 MP. Parallel
   content rows also pass matched CPU gates, reducing plain/rotated composition
   by 82–84% and Gaussian content workflow time by 60%. Retained tile convolution now lowers single-effect tiled Gaussian/diffusion time by 20–22% with exact pixels; warm application-worker Optical Diffusion improves 166 to 120 ms. Full-resolution 16-bit export parity also passes, and a separate lossless PNG compression change reduces complete photo export by 67–75% on the measured portrait, with 6–10% larger files. Multi-effect Gaussian/Unsharp/diffusion stacks now also pass retained-region gates: 32–37% lower kernel time and warm application-worker renders of 240/238 ms versus 369/365 ms, with exact preview/export pixels. GPU effect stages and native foreground acceptance remain open.
3. **Preview readback for inspection, scopes, and paint:** establish that the
   intended native branch executes with exact output before claiming transfer
   reduction. The previous three failed candidates remain reverted. A separate
   shared ICC identity fix now eliminates duplicate LUT rebuilds in a deterministic
   route-sequence probe; native settled publication remains unverified.
4. **Brush first feedback and tool opening:** both measured latency gaps remain;
   previous bounded candidates were reverted. A new approach needs a measured
   cause and foreground timing, rather than repeating those candidates.
5. **Checkpoint and history costs during edits:** mask encoding reuse is already
   accepted; changed-mask serialization and full checkpoint rewriting remain.

### Native effect-window cadence diagnosis (2026-10-02)

Added opt-in window active/exposed/application-state logging and a 16 ms GUI
heartbeat to the existing effect benchmark. Per-edit telemetry now separates
render completion from the following frame wait. An optional foreground mode
requests activation once and rejects activation failure or subsequent focus loss;
it never repeatedly takes focus. Production rendering is unchanged.

An initial Debug diagnostic reproduced roughly one-second heartbeat gaps while
the window was inactive but exposed. A two-second stack sample captured the GUI
thread waiting in Qt's scene-graph synchronization, while 440 of 457 render-thread
samples were inside Metal drawable acquisition. The export exceeded the run
limit, so this Debug run is excluded from latency comparisons.

The subsequent optimized Release probe could not activate its window within
three seconds and ended with `target_events_met=false`. The bounded comparison
stopped under the performance-loop skill's unavailable-metric rule. This evidence
locates a presentation stall but does not prove inactivity caused it, establish
foreground tool latency, or fix intermittent blank previews. Apple's documented
[drawable timeout](https://developer.apple.com/documentation/quartzcore/cametallayer/allowsnextdrawabletimeout)
is consistent with the observed cadence; it is not proof of the triggering cause.

The next native comparison must first establish foreground presentation. No
product speedup is claimed from this diagnostic change. Evidence:
[effect-window-cadence.json](evidence/performance-2026-09-28/effect-window-cadence.json).

A subsequent read-only session check reports `CGSSessionScreenIsLocked=1`,
display awake, and `loginwindow` frontmost. Foreground acceptance must wait for
an unlocked session. This current observation does not prove the lock state
during any earlier run. CPU effect measurements can continue independently.

### Gaussian tool rendering: two rejected experiments (2026-10-02)

The active photo-effect stack still prevents retained layer GPU execution. A
standalone two-buffer Metal Gaussian prototype measured actual host compute,
including allocation, uploads, both passes, completion and result copying.
Shader setup was separate (171 ms). At 1024x683, sigma 12 improved from 10.85 to
4.61 ms, but sigma 3 improved only from 4.64 to 4.05 ms and missed the declared
30% gate. More decisively, 54 of 120 synthetic edge/mask/scale/radius cases failed
the existing Gaussian RGB tolerance (2e-6) or the retained alpha tolerance (four
float epsilons). Maximum errors were 7.63e-6 RGB and 7.15e-7 alpha. The prototype
was rejected without product integration. These are compute measurements, with
no native UI or real-photo acceptance. [GPU evidence](evidence/performance-2026-09-28/gaussian-metal.json).

A separate CPU candidate specialized convolution direction and skipped edge
mapping for interior blocks while preserving double accumulation and tap order.
Three alternating before/after pairs, each with five repetitions of 21 scenarios,
retained every output hash, but the four primary tiled workloads were 37.5%
slower by geometric mean. It missed the 15% improvement gate and was reverted.
The original engine source was restored byte-for-byte, both Gaussian/spatial
suites passed, and the desktop and benchmark were rebuilt. No rendering speedup
is claimed from either experiment. [CPU evidence](evidence/performance-2026-09-28/gaussian-interior.json).

### Settled native worker: ICC accuracy blocker isolated (2026-10-02)

A new offscreen probe invokes the actual preview-render worker with a synthetic
1800x1200 source, sRGB publication and a built-in Display P3 ICC display profile.
It uses temporary runtime storage. The host session is still locked, so this
establishes worker behavior only, without native presentation or input timing.

With the accepted shared ICC identity repair already present, a complete-frame
admission candidate now reaches retained native settled output. Baseline settled
reuse copies 34,560,000 working bytes back to CPU; the candidate avoids that copy.
Its sRGB output stays within one byte of the CPU reference, but the ICC output
differs by up to ten bytes. This fails the quality gate before a matched latency
or memory series can start.

Two private accuracy diagnostics investigated the ICC interpolation map. A
quarter-byte channel-error guard alone still missed pixels. Also marking cells
whose corners cross a clipped channel boundary reduced settled image errors to
at most one byte, with exact alpha. The Final LUT remained within its existing
5% unsafe-cell cap (36,418 of 884,736 cells). Applying this stricter rule to the
Interactive cube exceeded the cap, so the integration used strict Final settings
and the same Final device quality for progressive and settled native output.

The independent 16,384-sample seeded color corpus then found a remaining failure
at sample 1039: exact 0.161627 versus approximate 0.153172, two displayed bytes.
This is the first reported failure, not a maximum over the corpus. The midpoint
validator lies on the cube's main diagonal; its tetrahedral weights omit the
two other vertices. Cell error away from that diagonal needs stronger coverage.
The candidate and its test changes were reverted byte-for-byte. The saved probe,
source patch, images and failure preserve a reproducible next step without
repeating the earlier admission-only experiments.

The restored desktop and workflow driver build successfully; both existing ICC
runtime suites and the host Metal ICC parity test pass. Those existing tests do
not establish the newly exposed stricter byte-accuracy requirement.

No product speedup is accepted from this attempt. Larger images, memory guards,
real-photo/export output and native presentation remain unverified. See
[worker and ICC evidence](evidence/performance-2026-09-28/settled-native-worker.json).

### Shared ICC transforms: concurrent application accepted (2026-10-02)

The ICC cell-coverage investigation exposed a separate bottleneck: every exact
application of a shared transform held one mutex. Pinned Little CMS 2.18's API
(page 97) supports concurrent calls using the same transform handle. The app's
RGBA-float transforms use NOCACHE, have call-local execution scratch, and finish
context/alarm setup before publication. `CompiledIccTransform::apply` now permits
independent pixel buffers to execute concurrently. No production worker pool or
rendering arithmetic changed.

Three alternating fresh-process pairs, seven measurements per case, pass all
gates on 1024x683 signed/HDR RGBA fixtures:

| Shared transform | Four-call batch before | After | Reduction |
| --- | ---: | ---: | ---: |
| sRGB | 229.26 ms | 58.81 ms | 74.3% |
| Display P3 | 230.57 ms | 60.61 ms | 73.7% |

Single-call medians remain about 57 ms and pass the regression guard. Peak RSS
rises from 69.71 to 77.86 MB, within the declared 10% plus 32 MiB limit. Every
measured result matches its single-call reference byte-for-byte, including alpha.
The new concurrency suite also covers regular, proofing and gamut-check handles,
cache churn and retained-handle lifetime. Five focused runtime suites, the host
Metal ICC parity test, and the desktop build pass. This is accepted engine
concurrency work; corresponding native tool latency remains unverified.
[Accepted evidence](evidence/performance-2026-09-28/icc-concurrency.json).

### ICC cell coverage: accurate prototype, integration pending (2026-10-02)

A private prototype checks all six tetrahedral centroids and clipped channel
boundaries in addition to the existing midpoint. A 97 cube exceeds the sRGB
unsafe-cell cap, as does a 113 cube by a small margin. A 115 cube fits the existing
32 MiB cache with the Interactive 33 cube and stays below 5% unsafe cells for
both sRGB and Display P3. It passes 1,032,768 independent uniform/shadow-weighted
samples with at most one encoded-byte error and exact alpha, including the saved
seeded regression. This is sampled coverage, not a proof over every cell point.

The original matched prototype missed its cold-build gate: roughly 1.34 seconds
versus 0.415 seconds. Its peak RSS also grew from 158 to 218 MB. After the separately
accepted concurrency fix above, a single diagnostic build falls to 919 ms for
sRGB and 895 ms for P3, retaining every quality result. That is encouraging but
still needs repeated build/memory measurements, explicit policy integration,
real settled-worker output/transfer gates, and native presentation acceptance.
The LUT and native-admission prototypes remain outside production.
[Feasibility evidence](evidence/performance-2026-09-28/icc-cell-coverage.json).

### Settled ICC integration: reverted at memory gate (2026-10-02)

The bounded integration attempt kept the previously accepted shared-transform
concurrency fix. It tried explicit Final ICC channel and interpolation-region
validation, a 115-cube table, and at most four shared-scheduler tasks. It did not
enable settled native admission. All three candidates passed the three independent
color corpora for sRGB and P3 (1,032,768 samples per run, maximum encoded RGB error
one byte, alpha exact), and the matched cold-build ceiling. The unchanged 5 percent
unsafe-cell and 32 MiB LUT-cache ceilings also held. The additional region
validator scratch was bounded at 32 MiB; this is not a claim about total LUT build
scratch or process memory.

All three candidates failed the predeclared process-memory ceiling. The final
candidate reused pixel buffers, omitted redundant center-cell indices, and
compacted perceptual samples in place. Three alternating fresh-process pairs gave:

| Metric | Baseline median | Candidate median | Gate |
| --- | ---: | ---: | --- |
| sRGB LUT build | 415.292 ms | 858.834 ms | <= 930.584 ms: pass |
| P3 LUT build | 416.859 ms | 870.930 ms | <= 933.718 ms: pass |
| Process peak RSS | 133,873,664 bytes | 183,091,200 bytes | <= 180,815,462 bytes: fail |

The full candidate, scheduler API extension, policy fields, and registered
accuracy test were reverted to the pre-attempt snapshots. The rejected patch,
standalone accuracy probe, three sets of paired receipts, and plan are archived
under [settled integration evidence](evidence/performance-2026-09-28/settled-integration.json).
The baseline accuracy probe exits 3 because it reproduces the known color errors;
all candidate accuracy runs exit 0. Initial sandbox resource-counter runs were
invalid and replaced with host measurements. No settled-worker or native UI
speedup is claimed. The session remained locked; native foreground proof remains
open. Stop this integration hypothesis after the three-candidate limit. A future
attempt needs streamed validation or another materially different memory design. Restored source matches all seven saved engine, scheduler, test, and native-suffix snapshots byte for byte; the three focused targets rebuilt and all three runtime suites passed.

### Retained tile convolution: accepted engine and worker improvement (2026-10-02)

The prior turn made progress by ruling out the settled ICC integration on memory.
A fresh read-only session check still reports `CGSSessionScreenIsLocked=Yes`, so
foreground presentation timing remains unavailable. This turn addressed the next
independent CPU-effect bottleneck: a padded tile convolved and mixed halo pixels
that its sink immediately discarded.

The engine now carries a private retained rectangle through single-effect tiled
Gaussian, Unsharp Mask, and Optical Diffusion rendering. Horizontal convolution
computes retained columns across every source row; vertical convolution and
mixing compute only the retained rectangle. This preserves the original tap order,
double accumulation, full input/mask validation, source halos, transaction and
cancellation behavior, and all memory admission limits. Whole-image processing
and multi-effect stacks use the full rectangle. No public API or color tolerance
changed. One candidate was required.

Three alternating before/after process pairs at each image size, with five
repetitions per case, passed the predeclared gates. Across four primary tiled
Gaussian/diffusion cases, geometric-mean time fell **20.00% at 1024 pixels wide**
and **21.53% at 2048 pixels wide**. Every primary case improved. The other cases
improved 4.52% at 1024 and regressed 1.05% at 2048, within the 5% guard. All 21
case hashes matched at both sizes across all pairs; maximum candidate RSS stayed
below the baseline-median * 1.05 + 4 MiB ceiling. The preliminary standalone
baseline is archived separately; the reported comparison uses the matched pairs.

Actual application-worker verification used a 1800x1200 image and identical
objects except `photo_effects.cpp.o` (plus the archive symbol index). In three
alternating host Metal pairs, excluding the first request per effect/process:

| Actual render-worker measurement | Before median | After median |
| --- | ---: | ---: |
| Gaussian complete render | 79 ms | 77 ms |
| Gaussian layer stage | 64 ms | 59 ms |
| Optical Diffusion complete render | 166 ms | 120 ms |
| Optical Diffusion layer stage | 152 ms | 107 ms |

All 48 output images have exactly matching decoded RGB and alpha bytes. Requests
served by the adjusted-image cache were controls and were excluded from effect
latency figures. Worker peak RSS remained about 1.05 GB. A separate pair using
the hash-verified portrait fixture, scaled to 1800 pixels wide, produced 16 more
exact image matches, including fresh full-resolution worker renders. That pair
is pixel evidence only; it overlapped focused tests and is not a latency result.
Portrait provenance and attribution remain in `effect-native-real-photo.json`.

The added contract covers 84 edge/radius/anisotropic-scale/tile combinations,
comparing both transactional and private delayed-row paths with whole-image
output: all RGB and alpha floats match exactly. Existing multi-effect, invalid
input, cancellation, mask, and memory-limit contracts also pass. Four focused
suites (`photo_effect_spatial_test`, `photo_effect_gaussian_test`,
`photo_layer_color_input_test`, `photo_layer_content_padding_test`) and the
Release desktop build pass. The accepted change is engine/render-worker progress;
foreground frame latency, full-resolution real-photo export, multi-effect
acceleration, and packaged visual acceptance remain open.

Evidence: [effect-retained-crop.json](evidence/performance-2026-09-28/effect-retained-crop.json)
with raw paired timings, exact-output receipts, source probe, scoped patch, build
and test logs. No Catalog work was added.

### Full-resolution retained-crop export verification (2026-10-02)

The previously accepted retained-tile convolution now passes three alternating
full-resolution export pairs per effect using the existing checksum-verified
4150x6218 portrait. The probe calls the production `render_flattened_output_file`
and `run_photo_export` functions used by the desktop controller, including
canonical Export admission and atomic PNG publication. All 12 decoded 16-bit
RGBA hashes match exactly, dimensions and bit depth are unchanged, and both
effects differ from the no-effect control. The only differing engine archive
member is `photo_effects.cpp.o` plus the archive index.

| Full-resolution metric | Before crop change | After crop change |
| --- | ---: | ---: |
| Gaussian render median | 1518 ms | 1356 ms |
| Gaussian complete PNG export median | 17794 ms | 17651 ms |
| Optical Diffusion render median | 2855 ms | 2008 ms |
| Optical Diffusion complete PNG export median | 16107 ms | 15255 ms |

All per-effect latency and RSS guards pass. Peak export RSS remains approximately
1.93 GB for Gaussian and 1.95 GB for diffusion. This establishes one real-photo,
full-resolution PNG export path; it does not prove other formats, every recipe,
export-dialog behavior, or native frame responsiveness. A fresh session check
still reports the screen locked. The dominant remaining cost is PNG encoding and
publication: 13.2-16.3 seconds compared with 1.4-2.0 seconds of rendering. This
observation prompted the separate PNG encoding hypothesis below.

Evidence: [effect-retained-crop-export.json](evidence/performance-2026-09-28/effect-retained-crop-export.json).

### PNG export compression: accepted production improvement (2026-10-02)

Full-resolution effect verification exposed PNG encoding/publication as the
largest export cost. Existing preview-cache writers already choose fast lossless
compression; `run_photo_export` and the buffered standard export encoder still
used the default. Both export writers now share `kPngExportCompression = 12`.
The installed Qt 6.10.2 [PNG implementation](https://github.com/qt/qtbase/blob/v6.10.2/src/gui/image/qpnghandler.cpp#L953-L967)
maps that value to zlib level 1. JPEG and TIFF configuration is unchanged.
No pixel conversion, profile handling, metadata policy, or publication/cancellation
logic changed. One candidate was required.

Three alternating fresh-process pairs per effect ran the complete production
flattened export renderer and atomic PNG writer on the original 4150x6218 portrait.
Both binaries include the previously accepted tile optimization; only export
compression differs. Each metric below is the median of three runs.

| Full-resolution export metric | Default PNG compression | Fast lossless PNG compression |
| --- | ---: | ---: |
| Gaussian encoding/publication | 16249 ms | 2927 ms |
| Gaussian complete export | 17643 ms | 4326 ms |
| Gaussian file size | 110,631,669 bytes | 121,838,789 bytes |
| Optical Diffusion encoding/publication | 13219 ms | 2929 ms |
| Optical Diffusion complete export | 15235 ms | 4978 ms |
| Optical Diffusion file size | 114,395,968 bytes | 121,085,622 bytes |

Encoding is 78-82% faster and complete export is 67-75% faster on these cases.
Files are 5.85-10.13% larger, below the predeclared baseline * 1.15 + 1 MiB limit.
Peak export RSS remains about 1.93-1.95 GB and passes baseline * 1.05 + 32 MiB.
All 12 decoded 16-bit RGBA hashes are exact. An independent PNG chunk reader also
verifies every non-IDAT chunk byte-for-byte across all six pairs, including
IHDR and profile metadata. Both effects still change the no-effect control.

A new 48-case contract covers the shared buffered writer and atomic file writer
(96 roundtrips): RGB/alpha, premultiplication, 8/16-bit, unprofiled grayscale,
sRGB/P3, flat/gradient/line/noise patterns, ICC bytes, text, DPI, and offsets match
the original Qt encoding behavior. `png_export_encoding_test` and
`photo_export_recovery_test` pass, including snapshot ownership and preservation
of an existing destination on cancellation or injected failure. The Release
desktop and actual-app driver build. Existing atomicity was preserved.

This is measured full-resolution photo-export work, not a native-dialog or
whole-app latency claim. Standard queued export uses the same policy and passes
the encoder contract; its end-to-end job throughput was not measured. Other
photos may have different size/speed tradeoffs. Foreground acceptance remains
open because the Mac session is locked. No Catalog work was performed.

Evidence: [png-export-encoding.json](evidence/performance-2026-09-28/png-export-encoding.json),
including the declared gates, exact source delta, raw paired export receipts,
metadata checks, test logs, and source probes. Large temporary PNGs are outside
the repository; their decoded hashes and encoded byte counts are recorded.

### Multi-effect retained regions: accepted engine and worker improvement (2026-10-02)

The previous goal turn made progress with full-resolution single-effect export
verification and faster lossless PNG encoding. The current session is still
locked. The next measured tool gap was multi-effect tiling: the existing retained
rectangle was used only when the stack contained one effect.

For Gaussian, Unsharp Mask, and Optical Diffusion stacks, the renderer now expands
each stage's retained output by the sum of all following support. Later stages
therefore receive every contributing input sample, while earlier stages can skip
results that cannot reach the final tile. Disabled entries contribute no support.
Other operator stacks retain their previous route; box running sums, for example,
have numerical dependence on their prefix. No new allocation or memory ceiling
was introduced. Full-image execution retains its complete rectangle.

The first candidate exposed an HDR correctness failure: unfiltered scratch values
outside an earlier stage's valid rectangle could overflow later diffusion
preparation. It was rejected. The second candidate clears those discarded samples
to transparent black before subsequent stages. A regression using values up to
3e38 now matches the whole-image reference exactly. This was a two-candidate loop.

Three alternating processes per side, five repetitions per case, passed the
predeclared kernel gates:

| Metric | 1024 pixels wide | 2048 pixels wide |
| --- | ---: | ---: |
| Four targeted tiled stacks, geometric-mean time reduction | 32.09% | 36.58% |
| Other whole-image/control cases, after/before time ratio | 0.963 | 0.975 |
| Largest candidate process RSS | 167,100,416 bytes | 380,813,312 bytes |

Every primary case improves, all 16 scenario hashes match across all pairs at both
sizes, and peak memory stays below baseline median * 1.05 + 4 MiB. The benchmark
covers both Gaussian/diffusion orders, Unsharp/diffusion, a three-effect stack,
unsupported-operator controls, and a disabled trailing effect.

The actual 1800x1200 application worker also passes three alternating pairs.
Warm medians exclude the first request per order/process; adjusted-cache requests
remain controls and are excluded from render latency:

| Worker metric | Before | After |
| --- | ---: | ---: |
| Gaussian then Optical Diffusion, complete render | 369 ms | 240 ms |
| Same stack, layer stage | 350 ms | 223 ms |
| Optical Diffusion then Gaussian, complete render | 365 ms | 238 ms |
| Same stack, layer stage | 352 ms | 223 ms |

All 48 preview comparisons are byte-exact, including alpha. Worker peak RSS remains
about 1.06 GB. The linked objects differ only in `photo_effects.cpp.o` and the engine
archive index. The current PNG export optimization is present on both sides.

On the original 4150x6218 portrait, all 12 full-resolution 16-bit PNG exports
match their paired decoded-pixel hashes; every non-IDAT PNG chunk also matches.
Export memory guards pass. Export **timing is inconclusive**: for example,
Gaussian/diffusion baseline rendering varies from 5.5 to 19.6 seconds, and a
reverse-order candidate run varies from about 3.4 to 13.3 seconds. No export speedup
or tail-latency acceptance is claimed, and no further timing retries were made.

The expanded contracts cover 108 multi-effect edge/scale/tile combinations on
transactional and private delayed-row paths, the prior 84 single-effect cases,
extreme HDR scratch, invalid later-tile masks, budgets, cancellation, and an
unsupported later operator. Four focused suites and the Release desktop build
pass. Native presentation, export timing under stable conditions, other operator
acceleration, and GPU effect execution remain open.

Evidence: [effect-stack-retained-regions.json](evidence/performance-2026-09-28/effect-stack-retained-regions.json)
with source probes, scoped patch, failed HDR diagnosis, raw paired measurements,
image comparisons, and build/test logs. Catalog remains outside the active queue.

## Comparison edited-image reuse — accepted (2026-10-02)

After the user unlocked the Mac, three current-code foreground runs completed
with visible photo captures and exact native/CPU preview hashes. The old
approximately one-second presentation wait did not reproduce. Current effect
work was dominated by rendering; opening Compare unnecessarily repeated the
edited photo's effect and output stages for another roughly 500 ms.

The comparison request can now read an existing complete CPU-rendered edited
image using the normal source/recipe/profile/size identity. The reference still
renders independently, including original, named snapshot, external, and selected
adjustment comparisons. Native-output-tagged entries, partial updates, scopes,
proof-mask requests, and overlay requests retain their existing paths. No cache
budget or separate comparison cache was added.

### Matched native acceptance

Three alternating fresh baseline/candidate process pairs used identical frozen
QML, the same Release benchmark driver except the controller object, native
Cocoa/Metal, isolated data/settings, and the 1627×2090 glass photograph. All runs
maintained active/exposed foreground presentation. Medians:

| Metric | Before | After |
| --- | ---: | ---: |
| Open split comparison to ready frame | 513 ms | 45 ms |
| Comparison render worker | 470 ms | 2 ms |
| Effect edit to settled frame | 567.7 ms | 553.9 ms |
| Wipe switch | 133 ms | 134 ms |
| CPU loupe fallback | 17 ms | 17 ms |
| 16-bit export | 766 ms | 775 ms |

Split opening improves **91.2%**, exceeding the predeclared 40% target. All
candidate comparison renders report a cache hit. Every preview/export hash
matches, all 12 paired comparison photo regions are exact, and all 24 native
captures pass photo visibility checks. Complete UI screenshots contain small
edge/animation differences; they are not claimed to be byte-identical.

Peak process memory varies from 2.43–2.90 GB before and 2.47–2.86 GB after. The
predeclared maximum-RSS guard passes; no memory reduction is claimed. Timing
regression guards pass. An initial candidate overlapped compilation and is
excluded, as are preliminary runs. The wrapper terminates after the workflow
completion event, so these runs do not establish graceful shutdown.

The color-cache suite covers 16 exact cold/warm image/profile cases across two
rendering models, two monitor profiles, and four reference modes, plus changed
recipes/references, missing references, guarded requests, and cancellation. It
and the before/after recipe suite pass; the desktop builds. The first test fixture
omitted the canonical source ID and assumed missing references always failed at
decode rather than plan validation; those fixture assumptions were corrected.

This closes the measured warm comparison rerender gap. Native linked views,
large/RAW photos, physical-pointer latency, and historical intermittent blank or
background presentation stalls remain unverified. The tool-only goal remains
active; Catalog remains outside the queue.

Evidence: [comparison-edited-cache.json](evidence/performance-2026-09-28/comparison-edited-cache.json)
contains declared gates, six run manifests/logs, captures, pixel comparisons,
scoped accepted patch, fixture hashes, test and build results.

## Remaining priorities

1 has source changes, isolated controller/real-probe verification, and passing active
shutdown coverage. Native duration counts, cache reuse, publication, and held-probe
scrolling pass in a synthetic 1,000-row Library; full responsiveness remains open
because hidden Edit prewarm stalls (finding 8). 2 has isolated controller/recovery
verification, a fixed and tested active-save shutdown path, real PostgreSQL contracts,
and three accepted integrated native/controller/helper/database runs with cached
synthetic thumbnails. Packaged/cold/other-platform acceptance remains unverified. 3 has source
implementation and compilation evidence, with runtime acceptance still open.
4 has source changes for inspector, scopes, diagnostics, comparison,
and regional paint. Native inspection exposed CPU publication replacing the
settled preview; three bounded repair candidates failed and were reverted. Native
parity and performance acceptance remain open. 5 has
CPU parallelism and scratch reuse plus accepted matched kernel measurements
(46.4 percent faster tiled diffusion with exact hashes). Real-photo CPU/export
pixels now match, and provisional streamed adjustment output completes 39.7 MP
preview/export and a synthetic 45 MP contract. Repeated native timing acceptance
stopped under unstable host timings; GPU effects and transparent content expansion
limits remain open. Bounded delayed rows now remove the separate content-effect
output allocation, with 343–358 MiB lower 24 MP peak memory, passing exact-output
and latency gates, five focused suites, and a successful desktop build. An additional engine-only memory fix skips inactive Texture/Clarity
luminance preparation, saving 90.6 MiB for 24 MP diffusion with matching output
and seven focused suites. Native pixel parity passes on a small real photo, but
visual and overall process-memory acceptance remain open. 6 has asynchronous snapshot preparation,
streamed helper output, and accepted bounded parallel projection (44.8 percent
faster 10k controller readiness); paging, memory scaling, and incremental publication remain open. 7 has partial
serialization reuse with isolated measurement/recovery evidence. 8 has measured
Carousel dialog deferral with remaining native and workspace readiness work.
9 has an arrival-starvation fix with deterministic controller coverage; native
playback acceptance remains open. 10 has asynchronous regular preview generation
with isolated pixel/lifecycle and QML coverage; large native workloads remain
unverified. 11 has measured CPU-conversion progress with exact fixture parity;
native GPU/playback work remains open. 12 shares persistent image-stage workers;
compilation passed and runtime acceptance remains open. 13 moves import review and
confirmation scans off the UI thread with isolated controller/QML coverage; large
native workloads and remaining job-admission I/O are open. 14 adds asynchronous
review and fresh batch validation with source-report reuse and controller/QML
coverage; native large-RAW workloads and full export acceptance remain open.
15 coordinates seven retained photo caches; in-flight/global memory management
and native workload acceptance remain open. 16 adds first-use acceptance and
native measurements; the three feedback-rendering candidates and three separate
tool-activation candidates were reverted after missing their gates. 17 preserves validated thumbnail publication with isolated worker/model
coverage; native large-library acceptance remains open. 18 has measured isolated
reconciliation improvements and model contracts; native workflow acceptance remains
open. 19 has a baseline and three rejected candidates; application filtering is
restored and the issue remains unresolved. 20 has a tested payload store and
state-record adapter and partial controller integration; integrated memory acceptance
and named-snapshot persistence remain open. 21 has measured shorter cache locking
with exact concurrent GPU output; scratch pooling and native acceptance remain
open. 22 shares unchanged immutable history clip arrays with measured capture/RSS
improvements and focused restoration coverage; byte budgets and native acceptance
remain open. 23 removes unused identity probes, captures immutable history, performs bounded
worker autosave, and compares transaction/saved-content inputs with exact record
fallbacks. Controller gates and equality/recovery/lifecycle contracts pass; restore
latency, broader memory work, lifecycle latency, and native acceptance remain open.
24 now gates and coalesces picker refreshes and supplies small rows, with matched
Qt fixture improvements and field/selection/lifecycle contracts. Incremental
publication, retained presentation memory, and native workflow acceptance remain
open. 25 has a bounded writer, prepared-event reuse, matched producer/drain metrics,
and ordering/lifecycle/error delivery contracts. Native input/render behavior,
interactive/slow sinks, and native teardown remain open. 26 shares verified source
digests within an import, with matched local-file read/latency/RSS and mutation
contracts; physical card I/O and native acceptance remain open. 27 has per-attempt
ownership and bounded resource admission; final responsiveness/native acceptance
remains open. 28 has measured batched culling persistence, recovery contracts, and
isolated real-PostgreSQL correctness proof; native/large-catalog throughput and
checkpoint scaling remain open. 29 has cached asynchronous reports, an incremental
saved-document index, asynchronous cleanup and pressure handling, matched controller
gates, and recovery contracts; native/full regression acceptance remains open.
30 now reuses persistent preparation aliases before decoding, with matched warm
latency/count/RSS gates and revision/migration/retention contracts; cold indexing
rose 17 percent and native/real-model/other-platform acceptance remains open.
