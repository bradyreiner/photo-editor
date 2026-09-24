# Active app completion goal

User objective: complete the RAW rendering, loading, Gaussian-splat workspace,
embedded model, and editing tools. This includes the earlier requested smart
image search, 3D/4D capture-to-scene and novel-view photography/film workflow,
Photoshop-class masking/custom tools, and agentic workflows. A smaller working
search panel, splat viewer, deterministic preset, or passing synthetic test is
not completion of that objective.

Current user priority (2026-09-09): Gaussian viewer film emulation, realtime
rendering and viewer UI/UX. The user explicitly parked page-switching performance.
The focused photographic-fidelity goal and its sharpness/optics/film acceptance
gates are tracked in `gaussian-photographic-quality-goal.md`; increasing training
resolution alone does not complete that goal.
The full scope above is unchanged. New search setup controller code is parked,
unvalidated and not wired into production; do not treat it as a completed
model-manager UI.

Latest Gaussian UX requirement: a full-screen photographic 3D viewer with live
perspective manipulation, followed automatically by camera/film-emulated photo
rendering after movement stops. Higher preview resolution alone does not satisfy
the camera-emulation requirement.

Explicit emulation requirements (2026-09-09): grain, halation and accurate bokeh
are required, not optional polish. Completion requires a lens/aperture/focus
model, film-like image formation, consistent saved-shot/preview/export behavior
and photographic reference validation. Generic tone looks alone do not satisfy
this scope. See `gaussian-optics-and-film-requirements-2026-09-09.md`.

## Completion evidence required

| Area | Required end state | Authoritative proof still needed |
| --- | --- | --- |
| RAW/color rendering | Correct decode, calibration, edits, masks, working/output color spaces, preview and export; protected regions remain protected | Real JPG and diverse RAW camera fixtures; stage/output numerical comparisons; zero-mask and neutral-edit invariants; full-resolution preview/export parity; interactive runtime evidence |
| Loading | Responsive startup/import/library/asset switching, progressive previews without changing final color, bounded memory, cancellation, recovery and visible errors | Cold/warm app runs with representative catalogs and large RAW files; frame/interaction latency, memory and stale-result checks; offline/failure/relaunch tests |
| Gaussian-splat workspace | Import photographic/RAW multiview capture, reconstruct/train real 3D scenes, support temporal 4D capture, interactive camera/time control, render novel-view photos and apply the photographic/film workflow | Real capture-to-trained-scene-to-novel-view exports, temporal data playback/reconstruction, held-out-view comparisons, save/reopen/recovery, UI and measured interactive rendering; disclose unobserved/occluded coverage limits |
| Embedded model/search/agent | Managed local model lifecycle, actual image understanding/search and editable multi-step workflows with honest capability state, confirmations and rendered review | Packaged-runtime startup without manual external setup; model integrity/license checks; real corpus query/edit evaluation; cancellation/resource behavior; calibrated quality and latency; no silent preset substitution |
| Editing tools | Complete professional masking, refinement, layers/compositing, retouch, custom tools and agent-accessible reversible editing | Tool-by-tool functional inventory; real image/stylus/direct-manipulation tests; independent mask/region accuracy; project persistence, undo/redo, preview/export and agent/manual parity |

## Current evidence and unresolved defects

- Gaussian reconstruction diagnostics now support the still-export size limit
  of 8192 pixels per edge using bounded SSIM row buffers. Dense/streamed equality,
  24/67 MP synthetic fixtures and a real 24 MP JPEG decode pass; all eight SUV
  reports and their 24 crop files remain byte-identical. This enables full-size
  evaluation, not sharper reconstruction. See
  `gaussian-full-resolution-diagnostics-2026-09-09.md` for proof and boundaries.
- A native aperture precision defect is fixed: focused surfaces no longer blur
  from pupil offsets rounded inside large world-to-camera translations. The
  synthetic maximum error falls from 183/255 to zero; SH/temporal checks, thirty
  exact SUV motion hashes and seven focused tests pass. Remaining precision and
  UI-proof boundaries are in `gaussian-aperture-precision-2026-09-09.md`.
- Optional Gaussian bokeh sampling now reaches 4096, preserving old defaults and
  deterministic prefixes. Synthetic highlight variation falls 94.8% versus 128;
  actual viewer progressive output/interruption and six focused regressions pass.
  High quality is explicitly slower; full-size 4096-sample SUV final performance
  and universal convergence are not proven. See `gaussian-bokeh-quality-2026-09-09.md`.
- A physical thin-lens conversion kernel now has analytical and native-image
  proofs for full-frame gate/focal length/f-number/focus mapping. Physical-input
  persistence, viewer integration, scene-scale provenance and measured lens
  calibration remain open; this does not change current viewer controls. See
  `gaussian-physical-camera-foundation-2026-09-09.md`.
- Gaussian focus selection now supports a clicked surface, asynchronous depth
  estimation and cancellation/stale-result guards. Focused native and actual
  Main.qml SUV tests pass, including Escape/right-click cancellation and
  preservation of aperture/film settings. This is a scene-unit Gaussian-center
  heuristic, not calibrated physical autofocus. See
  `gaussian-focus-picking-2026-09-09.md`; fidelity and realtime gates remain open.
- Settled Gaussian optical previews now publish immutable film-processed aperture
  prefixes through a bounded latest-only queue. Native tests verify exact prefix
  and final-image parity plus stale-publication guards; actual Main.qml proof
  visibly progresses from 4 to 8 to 16 samples. Thirty motion hashes are unchanged.
  This does not establish realtime final rendering or calibrated optics; see
  `gaussian-progressive-optics-2026-09-09.md`.
- All eight baseline SUV held-out appearance views have independently scored
  neutral native renders and error crops. Native/source is 20.610263 dB /
  0.604230 SSIM, while native/trainer is 71.174574 dB / 0.999978 SSIM. Visible
  streaks, deformed edges and shadow mismatches remain reconstruction/capture
  problems. The higher-detail candidate is still training; see
  `gaussian-held-out-quality-2026-09-09.md` for the next comparison gate.
- Explicit Gaussian still photos now support tiled lens/grain/halation export up
  to 8192 pixels per edge. Six native tests pass, tile/full-image comparisons stay
  within 1/255 and thirty preview hashes remain exact. Actual viewer proof exports
  a 4499 × 6000 photo with visible tile progress in 11.896 seconds under training.
  This is not higher reconstruction quality, physical film calibration or realtime
  export; the new high-detail candidate is still training. See
  `gaussian-tiled-photo-export-2026-09-09.md`.
- Follow-up viewer loop retains compact static positions for depth sorting:
  CPU sort/setup is 25–34% lower in paired runs, with exact thirty-pose hashes
  and 12 MB retained for the million-splat SUV. Two shader experiments were
  reverted after image/performance gates failed. Six native tests pass and the
  actual viewer proof is foreground-eligible; final motion is about 13 fps and
  settling 1570 ms under unrelated training. Cold timing remains variable;
  GPU/Release performance and progressive lens refinement are still open.
  See `viewer-gpu-performance-2026-09-09.md`.
- Fixed-camera film edits now reuse the completed pre-film GPU source without
  entering camera draft mode; optimized full-precision depth setup/sorting.
  Twelve film-edit and thirty motion-frame hashes match their baselines exactly;
  six focused tests pass. Actual SUV viewer film feedback is 17–33 ms, with
  20 published motion drafts in two seconds and 1422 ms settling under concurrent
  GPU training. Camera motion is not realtime yet. GPU rasterization/refinement,
  clean-hardware performance and calibrated photographic fidelity remain open.
  See `viewer-film-performance-2026-09-09.md`.
- Reconstructed the user's 64 SUV JPEGs: all views align; 10,000-step training
  produces a validated one-million-splat scene and native front/rear/side previews.
  Held-out reconstruction is 20.61 dB PSNR / 0.604 SSIM with visible artifacts.
  Native-versus-trainer rendering of one held-out pose is 70.40 dB; renderer
  parity is not reconstruction fidelity. Originals are unchanged. See
  `suv-capture-2026-09-09.md`; this does not validate RAW or temporal 4D capture.
- Connected creative grain and highlight-driven red/orange halation to the
  settled Gaussian preview/export, with compact Film controls and versioned
  shot settings. Effects-free motion drafts and neutral identity are preserved.
  Six focused tests and actual native UI/export/persistence proof pass, including
  byte-identical repeated PNGs and tagged JPEG export.
  This is not a measured emulsion/spectral response or a calibrated stock;
  see `gaussian-film-effects-2026-09-09.md` for the contract and proof boundary.
- Implemented finite-aperture Gaussian rendering with scene-unit focus/radius,
  circular/rounded polygon apertures, deterministic sample integration and
  cancellation of superseded refinements. Native mathematical/occlusion/texture
  parity checks and actual Scene UI/repeat-export proof pass. Six focused native
  tests pass; compact optical controls persist across page switches, repeated
  PNGs match exactly and sRGB JPEG export measures 47.80 dB PSNR against PNG.
  This is generic
  thin-lens geometry, not calibrated 50mm/Portra emulation. Measured grain/halation,
  physical-scale calibration and real photographic reference validation remain
  open. See `gaussian-thin-lens-2026-09-09.md`.
- Connected lens framing, linearized-sRGB exposure compensation and generic
  photographic looks to the native Gaussian preview and export. Saved shots and
  paths carry validated settings; accepted exports freeze them. Nine focused
  tests pass, including independent GPU color checks and exact preview-texture /
  export parity. Actual Main.qml controls and repeat-export checks pass; the
  final run is foreground-eligible and verifies panel cleanup across page changes.
  No navigation speedup or sustained framerate improvement is claimed.
  Physical aperture/focus/shutter/ISO and measured film response remain open.
  See `gaussian-photo-controls-2026-09-09.md`.
- Scene now fills the app window, moves setup controls into an overlay, and
  switches from bounded live navigation to a settled display-resolution photo
  preview. Actual native UI proof preserves the camera and exact exported bytes;
  five focused checks pass, including new live/settled/hidden/stale-frame cases.
  At that checkpoint physical camera and film emulation were not connected;
  newer thin-lens/film work above supersedes that specific gap, not the realtime
  or complete photographic-simulation gates. See `immersive-scene-viewer-2026-09-09.md`.
- Previously prioritized Gaussian Scene and page switching (now parked). Added an actual six-page
  navigation proof with a real 753,038-splat scene, foreground gating, cold/warm
  separation and RSS; fixed superseded/canceled rapid navigation. Native UI proof
  preserves the scene camera and byte-identical novel-view exports. The speed
  candidate was reverted after inconsistent guards and invalidated v1 setup;
  final v2 baseline still has ~185 ms warm switches, 4-5 s first Carousel/Timeline
  visits and 6.1 s scene loading. Seven focused checks pass. See
  `page-switch-and-scene-baseline-2026-09-09.md`; no speed, reconstruction-quality
  or broad completion claim is made.
- Added the native search-model setup backend: explicit download consent or local
  copy, owned/locked store, bounded resumable transfers, cancellation, pinned
  hashes, disk checks and recoverable repair. A real isolated 1.54 GB local copy
  verifies all eight files and reuses them on retry without changing the source;
  the signed runtime performs real GPU search from that copy. A real HTTPS repair
  fetches only the 253-byte config and reuses seven files. This backend is NOT yet
  connected to model setup UI or production readiness gating. Full-size download,
  managed startup/shutdown and packaged setup proof remain open. See
  `search-model-install-backend-2026-09-09.md`.
- Fixed the search runtime signing-order defect: prepare/sign a new runtime,
  compile its final digest into the app and plist, then verify/preserve it during
  outer app signing. A separate desktop build, real ad-hoc app seal, independent
  native integrity check and post-sign Library search proof pass. Repeats take
  218/209 ms on five inputs; the first UI query takes 9,854 ms. Real execution
  rejected hardened ad-hoc signing despite valid signatures; that unsupported
  combination now fails before output creation. Developer ID/hardened release,
  notarization, macOS compatibility and managed setup remain open. No user app
  process was stopped or replaced. See `search-runtime-signing-2026-09-09.md`.
- Packaged the approved search worker with its Python/native dependencies and
  integrity-checked app resource copy. Actual GPU inference and the native
  Library Find photos proof pass using that copy without Python on PATH, with
  five-image vector reuse, filter invalidation/recovery and owned shutdown.
  UI first search is 9,987 ms, repeats 235/204 ms; four focused tests pass in
  3.08 s; twelve expanded search contracts pass in 32.56 s with localhost fixture
  access. The 511 MB bundle has no non-system absolute native dependencies but
  inherits a macOS 26 minimum. Production auto-discovery/model setup, clean-machine
  compatibility and signing order remain unverified or unfinished; this is a
  development package, not embedded-model completion. See
  `search-runtime-package-2026-09-09.md`. Earlier statements below about missing
  similarity UI/ownership/packaging describe their historical checkpoints;
  the newer evidence supersedes those specific gaps, not the full-goal gates.
- Connected similarity search to the production Library under explicit local
  runtime configuration. The real Find photos UI invokes the approved encoder,
  ranks/projects candidates and reuses vectors; a native catalog binding handles
  filter changes, source/recipe/deletion changes and hidden top-K candidate edits
  without canceling for unrelated background previews. Eleven focused contracts
  pass in 30.90 s; the actual Main.qml proof retrieves the expected dog,
  verifies clear/filter invalidation/recovery and captures wide/compact layouts.
  Final five-photo search takes 6.1 s; repeats take 214 and 269 ms, not whole-library
  or general-quality proof. Final wide/compact captures are inspected, with no
  binding errors in the final proof log. Packaged setup, larger-corpus calibration
  and derived deletion pruning remain open. See `similarity-search-ui-2026-09-08.md`.
- Added a production-compiled asynchronous similarity coordinator connecting the
  approved encoder to the persistent index for explicit catalog/view snapshots.
  Real MPS proof ranks all ten queries as expected over five inputs/four scenes,
  reuses all five vectors on later queries and after worker restart, and stops
  both owned workers. First loading/indexing/query takes 5.7 s with event-loop
  heartbeats; later tiny-scope calls take 54-65 ms. Catalog thumbnails now expose
  current-preview provenance separately from historical stale-result diagnostics,
  excluding transient edits and permitting newly validated previews. This is NOT
  a full-app completion claim: ten focused suites pass in 25.81 s and the desktop
  builds, but the coordinator is not
  connected to Find photos or automatic catalog change tracking; callers still
  must invalidate generations. Large-library/UI/packaging and deletion pruning
  remain open. The full catalog test later times out in import, so it is not a
  whole-catalog pass. See `background-similarity-coordinator-2026-09-08.md`.
- Added a native-owned persistent encoder using the approved SigLIP 2 model.
  Real MPS proof encodes five inputs once, ranks all ten repeated queries as
  expected, suppresses a canceled real reply, restarts with stable model identity
  and reuses all five persisted vectors; both owned processes stop. Startup takes
  4.4 s while the native event loop ticks; start/cancel admission are below 1 ms
  and warm request plus tiny-index retrieval is 11-12 ms. Eight focused suites
  and the production app build pass; shared-runtime CPU/native ranking proof
  also passes. This is NOT a connected Find photos or catalog-indexing feature.
  Background authority/invalidation, packaged runtime, UI and broader corpus
  quality remain open. No additional model was downloaded or copied. See
  `owned-search-encoder-2026-09-08.md`.
- The user approved the isolated SigLIP 2 search-model download; all pinned files
  are now downloaded and verified. Offline CPU/MPS inference feeds real vectors
  through the native persistent index, with all five labeled queries ranking an
  expected image first across five inputs/four scenes, including an existing
  RAW-derived preview. Full native ranking matches the encoder dot products.
  Warm MPS text encoding is 5.4-8.5 ms, but initial verification/loading/warm-up
  cost seconds; this is NOT measured whole-library or UI latency. The encoder
  remains an isolated proof, not a packaged or connected Find photos feature.
  Background catalog indexing, owned long-lived inference and larger-corpus
  accuracy remain open. Existing 20.9 GB chat-model weights were not copied.
  See `siglip-local-retrieval-proof-2026-09-08.md`. This supersedes the pending
  SigLIP-download statements below, not the other managed-chat proof gates.
- Added a production-compiled local vector-store foundation for genuine image/text
  retrieval, with current-content scope checks, stale-worker/delete/recreate
  protection, exact cosine top-K and atomic corruption/cancellation handling.
  Five focused suites pass in 8.63 s. Six 20,000-vector/768-dimension synthetic
  top-32 runs take 573-607 ms and match a full-sort oracle; cancellation is below
  1 ms in the final uncontended run after fixing a 144 ms validation delay. This
  is NOT an enabled fast-search feature or photographic/model-quality proof.
  Encoder integration and the background catalog lifecycle remain to be built;
  approval for a separate 1.54 GB SigLIP 2 test download is pending. No model
  weights were copied/downloaded. See `visual-embedding-index-2026-09-08.md`.
- Search now checks actual local previews when saved descriptions omit a query
  detail, fixing the reproduced hat miss recorded below. Explicit contradictions
  can retain the text-only path; the final landscape-negative run correctly
  rejects the portrait without a photo check in 17.9 s, versus 54.3 s with an
  unnecessary check in the preceding run. Missing-eyeglasses and recovered-hat
  checks are correct on this same photo, but take roughly 50-60 s; this is not
  realtime or corpus-quality proof. Four final focused suites pass in 9.79 s,
  the desktop builds, and the actual native Find photos action, visible evidence,
  result filtering and edit-dialog target scope pass with wide/compact captures.
  No agent edit was generated by that dialog-scope proof. See
  `visual-search-detail-verification-2026-09-08.md`. Fast whole-library image/text
  retrieval, labeled photographic quality evaluation and actual managed-weight
  inference remain open; no model weights were copied or downloaded. The final
  priority instruction and positive UI path have now both been revalidated.
- Fixed a reproduced managed-search cache defect: changing the owned server's
  port after restart no longer forces new descriptions for an unchanged verified
  model/private store. Search now validates current runtime ownership, artifact
  revision and stale replies itself; four focused tests and the desktop build
  pass. Real external-local inference reuses a saved description and distinguishes
  a portrait from a landscape query, but still misses its visible hat and takes
  7.6-108.4 seconds across these different smoke queries. Actual managed-weight
  restart proof awaits explicit permission to make a private copy-on-write model
  test store; no model was copied/downloaded. See
  `managed-search-restart-2026-09-08.md`. This is not proof of high-quality,
  fast whole-library search or completion of embedded inference.
- Export admission is a provisional asynchronous candidate: delayed-helper
  submission is 21-43 ms versus 919 ms with active UI heartbeats, explicit Saving
  and unconfirmed states, bounded pending requests, same-ID retry and agent
  durable receipts. Native screenshots verify pending rows and the close guard,
  but the nine-copy proof times out with one job still queued. The full export
  regression now passes all assertions in 227.80 s, missing the +10% total-time
  comparison cap against 177.60 s; it is NOT accepted as an overall performance
  improvement. A live sample identifies remaining foreground completion writes.
  Worker shutdown ordering also remains incomplete. See
  `export-admission-responsiveness-2026-09-08.md`; the prior synchronous full pass
  below is a baseline, not proof that this candidate meets all loading gates.
- The full native export controller regression now passes (177.60 s) after
  correcting its restart fixture to retire the old runner and await replacement
  recovery before reading history. Existing color/format, mask, recovery,
  19-image delivery/collision/integrity and failed-only retry assertions pass;
  phase logs and retained artifacts are documented in
  `export-regression-timeout-2026-09-08.md`. This supersedes the earlier bounded
  timeout as the latest functional result, not as a realtime/CI latency claim.
  A live stack sample identifies synchronous export admission/history helper
  waits on the main thread as the next loading target. Full RAW/camera, 3D/4D,
  embedded-model and editing-tool completion requirements remain unchanged.
- Known unresolved recovery now disables and guards history mutations in both
  queue controllers, including direct calls that bypass disabled UI. The new
  regression fails before the fix, verifies zero helper calls for 15 guarded
  actions afterward, and checks controls restore after recovery success. Six
  focused tests, real PostgreSQL unchanged-history/native Export proof, two
  export handoff/recovery tests and exact-image Scene/Edit proof pass. See
  `job-recovery-control-guards-2026-09-08.md`. Backend-wide stale-update and
  deletion protection, explicit conflict reconciliation and all broader goal
  requirements remain open; these guards do not establish realtime behavior.
  The larger export controller test had omitted runner recovery initialization;
  the corrected fixture passes that former failure point but times out at the
  bounded 180-second full-run limit. It is not a passing full export regression.
- Job-recovery conflicts no longer hide a healthy Library or offer catalog
  repair for a job-only failure. Recovery has a separate durable UI error;
  readable history remains visible, and onboarding defers without completing
  itself while recovery needs attention. Six focused tests, real PostgreSQL
  conflict/unchanged-history checks, native Library/Export and genuine catalog
  outage screenshots, and exact-image Scene/import/Edit regression pass. See
  `job-recovery-library-isolation-2026-09-08.md`. Explicit conflict resolution,
  stale worker writes, deletion/recreation, recovery-time mutation safety and
  the full goal remain open; the native whole-proof gap still reaches 1,629 ms.
- Fixed real-catalog clobbering by legacy migration. A dedicated insert-only
  helper command accepts new or exactly matching payloads and rejects conflicting
  catalog state without clearing local history. Real PostgreSQL verifies
  preserved newer state and exactly one winner for competing imports; 42 Rust
  tests, five native tests and exact-image Scene/import/Edit proof pass. The
  concurrency suite also exposed a bounded-lock acquisition timeout, repaired
  with short retries inside the same limit and five repeated passing runs. See
  `legacy-job-catalog-conflicts-2026-09-08.md`. Ordinary stale worker updates,
  conflict-resolution UI, deletion/recreation handling and all
  broader completion requirements remain open.
- Fixed reproduced legacy job-history data loss during concurrent migration and
  fallback mutation. Migration now serializes uploads, prunes only acknowledged
  unchanged versions, preserves late work for retry, and shares a cross-process
  mutation lock with fallback upsert/delete. Malformed history cannot be silently
  replaced with an empty queue. Five focused tests, real PostgreSQL migration and
  exact-image native Scene/import/Edit proof pass. See
  `job-history-concurrency-2026-09-08.md`. Catalog-level stale-update conflicts,
  crash/lock-timeout behavior and the full goal remain open; this is not proof
  that all job persistence is concurrency-safe or nonblocking.
- Startup recovery now runs in the background with dispatch closed until its
  selected-catalog reads and commits succeed. Failures retry without allowing
  fallback writes to count as durable recovery. Delayed-helper admission changes
  from 445 ms to below 1 ms; real PostgreSQL recovery preserves export/import
  pause and timeline cancel states with 120 UI ticks during 1201 ms. Six focused
  tests and the exact-image native Scene/import/Edit proof pass; three separate
  history/model assertions remain failing. See
  `background-job-startup-recovery-2026-09-08.md`. Job commits, concurrent legacy
  migration/user mutation safety, failure UI and the full completion scope remain
  open; observation-only stalls still reach 1206 ms.
- Idle job-queue observations now run off the UI thread, coalesce notifications,
  discard superseded snapshots and retry failures without dispatching fallback
  work. Delayed-helper admission improves from 464 ms to below 1 ms; the native
  Scene/import/Edit proof retains exact output files, catalog save, final outline
  and undo with time/memory guards passing. Six of nine broader checks pass;
  three separate history/model checks fail and remain open. See
  `idle-job-queue-responsiveness-2026-09-08.md`. Startup recovery, job commits and
  catalog/photo opening still cause observation gaps up to 1196 ms. The prior
  1565 ms whole-proof gap belongs to the harness's after-capture AI self-check,
  not a measured editing action. The full completion scope remains open.
- Small-image sampled outlines no longer decode/color-process/rasterize from
  foreground property getters. The existing worker now handles all sampled
  final outlines, with source-free fast paths retained. New tests exposed and
  fixed unstable legacy-mask reconstruction inside global-color identity and
  cached fallback mislabeling. Native add-mask/exposure calls improve from
  1442/1422 ms to 74/53 ms, with exact exported files, completed final outline,
  save/undo and memory gates passing; six focused tests pass. See
  `small-mask-outline-responsiveness-2026-09-08.md`. Later phase attribution and
  remaining observation-only stalls are recorded above; the other app-wide
  completion requirements remain open.
- Scene now has an actual render-to-Library-to-Edit handoff with frozen camera,
  complete-file publication, normal persistent import and guarded navigation.
  Native proof exposed double photographic rendering of the sRGB scene export;
  the handoff now saves canonical Relative Colorimetric intent without changing
  RAW defaults or existing photo recipes. All 476,800 neutral RGBA pixels and
  profile match, masked exposure/P3 edits autosave, and undo restores exact output.
  Fresh catalog readback retains the intent. See
  `scene-photo-edit-handoff-2026-09-08.md`. The observed 3362 ms event-loop gap
  remains a foreground responsiveness defect; broader JPEG/temporal/reopen proof,
  true 4D training, embedded-model quality and the other goal gates remain open.
- Real catalog autosave/fresh-process reopen exposed two further losses:
  recipe history was omitted from canonical metadata, and Display P3 delivery
  intent reopened as sRGB. Provenance v2/migration 0014 now preserves history
  with v1 read compatibility; an explicit non-sRGB v13.3 delivery-space field
  preserves output intent and participates in output cache identity while
  retaining existing sRGB serialized bytes. Three focused native tests, all 41
  Rust workspace tests, real PostgreSQL constraint checks and real imported
  DNG/JPEG save/reopen tests pass. Both 4208x3120 reopened exports match their
  saved-process references exactly in pixels/profile, with complete recipe JSON.
  The desktop app and catalog helpers rebuilt. See
  `catalog-recipe-reopen-2026-09-08.md`. Full-window relaunch/crash-during-save,
  older already-lost metadata recovery and all-device/color calibration remain
  open; six separate architecture-boundary failures also remain. Only isolated
  test catalogs were migrated, and their servers are stopped.
- The normal Edit-to-Export handoff now preserves the complete asset-bound
  recipe, fixing omitted layers/preset provenance/history and stripped source
  profile bindings. The editor also retains loaded recipe-level history through
  state snapshots/undo. Four focused regressions pass; real 4208x3120 DNG and
  JPEG handoffs preserve exact rendered pixels/profiles with two layers and a
  mask. Native RAW-base/JPEG-layer and JPEG-base/RAW-layer runs reach the real
  Export panel and persistent job runner with unchanged recipes and exact
  delivered/reference PNG pixels/profiles. See
  `export-recipe-handoff-2026-09-08.md` for proof artifacts
  and fixture limits. The separate source-contract test fails on shared
  menu/split-button requirements. The later catalog/controller persistence proof
  above covers real imports; full-window relaunch, independent color ground
  truth and full-resolution realtime remain open.
- A native profile traced the source-layer redo stall to repeated QML layer
  presentation reads. Studio now uses a reactive copy snapshot, with same-count
  edit/thumbnail refresh and detached-value tests. Two matched foreground app
  proofs reduce redo from 1079ms to 56/55ms, preserve exact exported bytes and
  inspector undo/redo, and pass whole-proof time/memory guards. Three focused
  tests pass; see `layer-inspector-snapshot-2026-09-08.md`. One inactive candidate
  observation failed time guards and remains explicitly excluded as nonmatching.
  Other whole-proof observation gaps still reach 1020ms; large stacks, all-tool
  responsiveness, background QoS and full-resolution realtime remain unproven.
  The normal Export panel already uses persistent background jobs; replacing it
  with the standalone single-flight export API is not required integration.
- Photo-export APIs now offer cancellable worker execution from captured edit
  state, preserve JPEG ICC metadata, protect current/linked original paths and
  atomically publish output without direct-write fallback. Fault/cancellation
  and lifecycle tests pass, with 18 focused regressions green; real 4208x3120
  DNG/JPEG flattened worker exports
  exactly match the synchronous reference. The native placed-layer proof now
  uses async export: admission is below millisecond timer resolution, with a
  239ms maximum gap on a 200ms observation timer during a 3.73-second export.
  That turn's whole-proof maximum observation gap was 1.17 seconds and redo
  admission 1.11 seconds; the later snapshot improvement is recorded above.
  See `async-photo-export-2026-09-08.md`. Normal Export-panel routing
  and legacy synchronous callers are unchanged; complete UI integration,
  independent color ground truth and full-resolution realtime remain open.
- Global-slider drafts now retain existing masks/retouch and no longer mistake
  downstream mask-rasterization invalidation for a mask-only preview request.
  Canonical/working-image ACES publication also uses the same final output LUT
  for draft and settled rendering. Eight model/working-profile cases are exact;
  native real RAW/JPEG drafts and full-export comparisons pass within 1/255,
  with protected pixels intact. Seventeen focused regressions pass. See
  `global-slider-mask-fidelity-2026-09-08.md`. The broad controller passes the
  repaired global/local-mask blocks but still fails at a later advanced-tone
  scheduling assertion; noncanonical and monitor-ICC output parity is still open.
  That turn's app proof localized its 4.66-second stall to synchronous layer
  export (3.52 seconds) and redo admission (1.08 seconds). Async export progress
  and the subsequent redo improvement are recorded above; complete normal
  export-panel behavior still needs separate end-to-end verification.
  Other tools, camera/HDR/device calibration and full-resolution realtime remain
  unproven; this does not close the reconstruction or embedded-model requirements.
- Selection-start during a warm-cache load now replaces render work invalidated
  by the new mask/layer revision, preventing a blank canvas while retaining
  stale-result rejection and the loaded-idle fast path. The display-image cache
  is now lossless PNG: an unchanged retry previously altered pixels through JPEG
  compression. Cold and warm recovery tests and real RAW/JPEG readbacks preserve
  exact pixels; a bounded lossless-codec comparison also cuts 13MP cache encoding
  median from 4.65 to 2.57 seconds with protected decode/memory/size guards.
  Fourteen focused regressions and native RAW/JPEG app smoke pass; see
  `lossless-preview-recovery-2026-09-08.md`. The broad controller
  test gets past its retry failure but still stops at a later slider-scheduling
  expectation. The smoke still records a 4.7-second event-loop gap; whole-app
  loading and full-resolution realtime remain unproven.
- Forty coverage/refinement operations now request full-quality restoration and
  retain local effects/layers in mask-commit drafts. An opacity release failure
  was reproduced before the fix; native opacity/expansion checks now require
  changed pixels, protected regions and flattened-export agreement. Real RAW
  opacity and cropped expansion pass within 1/255 of export. See
  `mask-coverage-release-2026-09-08.md` for the full evidence and limits.
  Broader mask-stack/retouch behavior and actual realtime performance remain open.
- Local color edits now retain their effects in drafts and schedule full-quality
  work; native-only previews now report preview/crop dimensions rather than full
  source dimensions. The real RAW probe first reproduced missing native handoff,
  then exposed the separate omitted-effect/no-full-render routing defect. Actual
  RAW/JPEG native texture readbacks now show distinct edits, protected pixels and
  full-generation completion. See `local-mask-native-preview-2026-09-08.md`.
  A separate luminance micro-optimization failed its speed gate and was reverted.
  Correct masked foreground realtime and the full goal remain unproven.
- Film publication now stages both files, uses exclusive complete-file renames
  and rolls back its receipt on ordinary movie-publication failure. Focused
  collision/preservation, actual static/timed encoding, a 48-frame app export
  and all eighteen Gaussian regression tests pass; see
  `gaussian-film-publication-2026-09-08.md`. Crash-atomic pair recovery remains
  open. A real-trainer handoff investigation also identified configuration,
  rasterization and licensing gaps; see `gaussian-checkpoint-handoff-2026-09-08.md`.
  No genuine checkpoint was imported or trainer declared compatible.
- Scene Lab now exports timed 4D MP4s with independent scene-time intervals and
  fixed or saved-path camera motion, including reverse and frozen time. Six
  synthetic films/95 decoded frames, wrong-time negative controls, a 48-frame
  app-generated film, frozen-plan/cancellation checks and seventeen regressions
  pass. See `gaussian-timed-film-2026-09-08.md`. This completes the artifact-to-
  timed-film path, not genuine dynamic training, real reconstruction quality,
  mastering formats, robust final publication or large-scene realtime proof.
- Scene Lab now opens pinned 4D artifact projects, scrubs/plays explicit scene
  time and exports frozen-time PNG/JPEG photos. Queue bookkeeping now preserves
  newer preview requests behind a frozen export. Native controller/viewport
  checks cover 100 scrubs, exact photo pixels, cancellation/reopening and queued
  export after teardown; sixteen regressions and packaged QML/photo proof pass.
  See `gaussian-spacetime-workspace-2026-09-07.md`. This uses synthetic temporal
  data, not genuine dynamic training. Checkpoint conversion, trained moving-scene
  quality and large-scene foreground performance remain open. Timed film export
  subsequently gained the bounded workflow described above.
- A bounded data-only space-time artifact API now preserves temporal geometry,
  appearance and camera/time metadata under a pinned hash. Twelve parameter and
  independent SafeTensors roundtrips, 27 malformed-file cases, streaming-boundary
  checks, 144 pixel-exact native save/reload frames and fifteen Gaussian tests
  pass. See `gaussian-spacetime-artifact-2026-09-07.md`. Workspace integration
  followed as described above; checkpoint conversion, genuine dynamic training,
  real quality and foreground performance remain unproven.
- Native space-time Gaussian infrastructure now evaluates correlated XYZ/time
  geometry, temporal opacity and SH/cosine appearance at an explicit scene time.
  Square-root covariance projection preserves thin trained ellipsoids that lose
  SPD when stored as float covariance. Four thousand independent density checks,
  144 time/view/appearance image comparisons, fourteen regressions, static pixel
  parity and native app reopening pass. Large probes use synthetic motion on
  an existing static scene, not trained dynamic data. See
  `gaussian-spacetime-rendering-2026-09-07.md`. Checkpoint adapters, genuine 4D
  training, real dynamic quality and foreground
  performance remain open. A one-attempt static shader specialization was
  reverted after its confirmation gain missed the declared performance gate.
- Scene Lab now exposes timed-capture preparation and background verified
  reopening, including standalone committed datasets. Saved projects pin the
  manifest hash; prepared images must pass hashes, PNG decoding, calibration
  dimensions and sRGB checks. The real RAW fixture, twenty source rejection cases,
  eight prepared-data cases, five regressions, native QML creation, fresh-process
  reopening and existing static rendering pass. See
  `gaussian-timed-workspace-2026-09-07.md`. This remains prepared input, not 4D
  reconstruction: temporal training/rendering/export, real dynamic evidence,
  broader calibration/input UX and resumable partial preparation remain open.
- A calibrated timed-capture preparation API/driver now preserves synchronized
  camera IDs/times, validates fixed-rig pinhole poses and contained inputs,
  rescales intrinsics to actual prepared dimensions, and hashes canonical sRGB
  observations. Known timed folders are rejected by the static trainer. Twenty
  malformed-input cases, real RAW decode with explicitly synthetic timing/poses,
  five regressions and existing native static-scene reopening pass. See
  `gaussian-timed-capture-2026-09-07.md`. This artifact is explicitly prepared,
  not trained: genuine 4D training/rendering and real dynamic
  capture evidence remain required. No static-frame/animation substitute closes
  the original 4D goal.
- Optional RAW decode prewarming is now latest-selection-only, with one active
  task and one pending replacement instead of one uncancelable job per distinct
  source. A held-worker burst replaces 99 of 100 pending requests, executes only
  the latest, and passes cancellation/deduplication/exception/lifetime checks.
  Real RAW pyramid population, twelve unchanged full-resolution controller
  hashes, seven regressions and native editing smoke pass. See
  `raw-prewarm-latest-selection-2026-09-07.md`. This bounds speculative work;
  synchronous metadata/recipe admission, foreground FPS and other goal areas
  remain open, with no whole-app memory or cold-load speed claim.
- Edited and comparison preview caches now require a successful worker-side
  pixel decode at the expected dimensions before reuse. Header-valid but
  undecodable JPEGs previously reported success in all eight baseline cases;
  they now regenerate through the atomic cache writer. Twenty-four synthetic/
  full-resolution RAW/JPEG cases across legacy and ACES2 pass exact regenerated
  pixels, cancellation and wrong-size repair, alongside six regressions and
  native editing smoke. See `preview-cache-decode-recovery-2026-09-07.md`.
  This does not detect every silent corruption or complete cold loading,
  all-camera fidelity or the other goal dimensions.
- Warm asset selection/retry now dispatches cached image decoding through the
  existing worker instead of running the full renderer inline on the GUI thread.
  The accepted real 4208x3120 loop reduces selection admission from 122 to 2.4ms
  RAW and 60 to 0.86ms JPEG, preserving exact cache pixels, ready latency and
  memory gates. Held-worker switching, completed-old-result rejection and retry
  after cache eviction pass, alongside seven regressions and native editing
  smoke. See `warm-asset-loading-2026-09-07.md`. Preview readiness remains about
  125/60ms; cold loading, heavy recipe admission and foreground frame proof remain.
- Preview tile assembly now drains submitted copy/adjustment workers before
  releasing borrowed buffers on cancellation, early return or exceptions. The
  failing-before lifetime regression reproduces three unsafe exit cases; all
  guarded cases, six focused regressions, full-resolution RAW/JPEG reference/locality
  checks and a native RAW/JPEG editing smoke pass. See
  `preview-tile-cancellation-lifetime-2026-09-07.md`. This closes a worker-buffer
  lifetime defect, not realtime loading or complete asset-switching proof.
- Repeated refined-mask coverage is now reused behind exact input-pixel and
  complete recipe/context/geometry checks, with cancellation-safe publication,
  two-use admission and a shared 384 MiB payload budget. The accepted loop
  improves 24-edit RAW warm median from 1183 to 663ms (44%) at 4208x3120 and JPEG
  from 168 to 71ms (58%) at 1600x1186. All output hashes match; peak RSS grows
  4.2%/2.7%. Six regressions, real-image controller tests and a native editing
  smoke test pass. See `refined-mask-coverage-reuse-2026-09-07.md`. First-mask
  and cache-building edits are still expensive; this is not foreground
  realtime, cold import or completion of other goal dimensions.
- Layer admission now avoids copying/zeroing full raster-bearing edit state,
  validates actual detail controls, and uses a reduction-based finite-pixel
  check. All 60 tested nonfinite detail cases are rejected (28 were previously
  admitted), while 80 valid cases preserve their rendered checksum. The bounded
  two-attempt loop improves warm publication from 31.9 to 22.8 ms with identical
  RAW output hashes and unchanged peak memory. Eleven focused regressions,
  native Metal tests and full-source JPEG checks pass. See
  `layer-admission-2026-09-07.md`. Cold publication remains about 140 ms; this
  is not whole-app realtime or completion of the other goal dimensions.
- Retained-layer compilation now reuses lifetime-pinned GPU pixel storage only
  after exact comparison of all active source bytes, avoiding redundant payload
  copying/hashing on warm edits. A bounded native RAW loop improves median
  publication from 117 ms to 37 ms (32 ms confirmation), with all twelve output
  hashes unchanged and about 0.5% peak-memory growth. Mutation/lifetime tests,
  ten focused regressions, native sampling/alpha/color tests and full-resolution
  JPEG-source checks pass. See `layer-payload-reuse-2026-09-07.md`. This is a warm
  layer-stage gain, not full editor realtime; cold loading, foreground scheduling
  and all other goal dimensions remain open.
- Layer minification now widens its alpha-aware filter footprint with reduction
  scale, on CPU and Metal, instead of undersampling with four taps. The expanded
  3,240-case regression passes; rotated checker error drops from 0.131 RMS to
  zero on the analytic fixture. Full 4208x3120 RAW/JPEG sources reduced to 12%
  pass all four working spaces, native readback and visible 8/16-bit output
  checks; nine focused regressions and broader Metal parity pass. See
  `layer-minification-2026-09-07.md`. Cold upload/readback still measures
  134–159 ms; higher-order filtering, resource reuse and foreground realtime
  remain open, alongside all other completion dimensions.
- Rotated/scaled image, pixel and text layers now use alpha-aware bilinear
  sampling on CPU and Metal instead of nearest-neighbor, with transparent
  padding and no hidden-color contribution. A failing-before 2,160-case
  independent regression, native real RAW/JPEG transforms in four working
  spaces, 8/16-bit output checks and nine focused tests pass. See
  `layer-transform-sampling-2026-09-07.md`. Heavy minification, full-resolution
  transform precision and foreground realtime still need proof/improvement;
  this does not complete professional editing or the other goal dimensions.
- Straight-alpha layer compositing now preserves foreground RGB over transparent
  backgrounds, blends only overlapping coverage and keeps adjustment-layer alpha
  unchanged on CPU and Metal. The failing-before regression now passes 4,096
  cases plus blend/HDR invariants; native GPU and seven broader regressions pass.
  Real DNG/JPEG masked layers pass four working profiles and both rendering
  models at preview sizes, plus full 4208x3120 ProPhoto/ACES 2 output, with exact
  16-bit color/alpha invariants. See `layer-straight-alpha-2026-09-07.md`.
  This is not independent color calibration, full Photoshop tool coverage,
  foreground realtime or completion of the other goal dimensions.
- Gaussian viewport destruction no longer waits on the GUI thread for load or
  render work, and accepted queued photos survive teardown with their frozen
  scene/camera and serialized renderer ownership. A failing-before held-worker
  regression, actual 753,038-splat load/Metal lifetime proof and ten Gaussian
  regressions pass. See `gaussian-viewport-teardown-2026-09-07.md`. This does not
  eliminate reconstruction-controller/global-pool shutdown waits or add durable
  post-crash export recovery; the full loading and product goal remain open.
- Gaussian training now sizes refinement cadence to the registered capture and
  stops growth before short runs end. Fixed-camera/image comparisons improve
  every held-out view (21.37 to 22.82 dB mean PSNR; SSIM 0.587 to 0.711), at
  roughly triple measured training time and peak footprint. A fresh integrated
  32-view run reaches 22.94 dB / 0.713 SSIM; its 753,038-splat artifact validates.
  Independent image metrics, ten native Gaussian tests and packaged workspace
  proof pass. The native renderer reproduces the denser candidate at 70.03 dB
  against its trainer image; isolated 800-edge motion renders take 28ms median.
  See `gaussian-training-schedule-2026-09-07.md`. Ghosting, full-resolution and
  foreground realtime, broader captures, 4D and film fidelity remain open.
- Gaussian preparation now preserves optical metadata as explicit reconstruction
  priors, with conservative camera identity grouping and visible fallback counts.
  A fresh real JPEG run registers 32/32 views with 32 verified prior-bearing
  cameras; all prepared PNGs remain byte-identical. Real RAW preparation correctly
  retains heuristic estimation when equivalent focal metadata is missing. Eight
  native Gaussian tests, real RAW/JPEG mask checks and packaged Scene visual
  proof pass. Held-out quality remains poor (21.37 dB PSNR); this is not a
  controlled reconstruction-quality improvement, 4D or realtime completion.
  See `gaussian-capture-optics-2026-09-07.md`.
- Gaussian viewport load/render ownership now includes queued completions,
  preventing camera movement from losing a completed photo export's UI status.
  A failing-before regression, 100-change coalescing/reentrant/source-switch
  checks, native Metal testing on the 380,065-splat scene, and seven Gaussian
  regressions pass. See `gaussian-viewport-handoff-2026-09-07.md`. This remains
  static 3D; temporal 4D, foreground realtime, reconstruction/film quality and
  the other app completion requirements are not established.
- Managed download setup now credits bounded cached bytes in its storage estimate
  instead of requiring full-model space for every resume. The worker independently
  hashes cache data and checks actual replacement/streaming/commit space; estimates
  never grant model readiness. Native simulated-budget regressions, controller
  handoff and rendered cached/cold setup-card checks pass. See
  `model-resume-storage-2026-09-07.md`; seven focused tests pass in 44.05 seconds.
  Physical disk-failure/multi-GB proof,
  full model provisioning/inference and all other goal dimensions remain open.
- Managed model download now validates the manifest pin before any blob request,
  fetches digest-addressed blobs with bounded streaming/hashing and resume,
  aborts owned transfers on pause, and publishes the manifest last. Controller
  confirmation/readiness gates and visible setup-card errors are covered; small
  real-registry metadata/range verification read 12,881 bytes without weights.
  The desktop build and seven focused tests pass, including the full agent
  controller and rendered setup-card regression.
  See `pinned-model-download-2026-09-07.md`. Full provisioning/inference,
  physical disk-failure proof, foreground contention and all other goal dimensions
  remain open.
- Managed model verification now rechecks artifact identity at UI delivery,
  preventing a completed worker from publishing stale readiness after files
  change. Explicit active lifetime also protects queued completions and resumes
  canceled-generation replacement verification. A failing-before regression,
  five focused tests and actual packaged-runtime startup/shutdown pass; no model
  weights were installed or loaded. See `model-verification-handoff-2026-09-07.md`.
  Provisioning, actual inference and the other goal dimensions remain open.
- EXR decode no longer relabels unsupported wide-gamut primaries as sRGB without
  converting pixels. Float conversion preserves HDR/negative RGB and alpha;
  cache/provenance changes isolate the new interpretation. Seven linear RGB
  spaces, metadata precedence and independent official XYZ/Rec.709 examples
  pass; six focused regressions, real RAW/JPEG masks and controller scope mode
  pass. See `exr-source-color-2026-09-07.md`. Untagged/unsupported EXRs now fail
  closed and need a source-color assignment/recovery UI; broader EXR workflows,
  color calibration, realtime and the other goal dimensions remain open.
- Scope sampling no longer darkens ARGB32-premultiplied pixels. A failing-before,
  passing-after test covers straight/premultiplied byte, 16-bit, half-float and
  float formats. RAW diagnostics and high-precision scope test references are
  corrected; four focused tests and the controller scope-domain run pass.
  The broad test now reaches a slider-scheduling assertion. ProPhoto-like EXR
  warnings also trace to a potentially incorrect linear-sRGB tagging fallback;
  that source-color follow-up is now recorded above. See
  `color-scope-contracts-2026-09-07.md`. Full app completion remains unproven.
- Active-mask raster inspection is now asynchronous: immutable snapshots,
  one active/newest pending request, compact keys without segmentation payload
  serialization, freshness checks and nonblocking callback-safe destruction.
  Restored recipes also advance mask revision, preventing same-ID stale fills.
  Real DNG/JPEG and synthetic tests pass exact recipe-aware mask output across
  four working spaces/crop states, seven inspection modes, original/refined
  switching and a 16 MiB admission check. Final verification receipts and limits
  are tracked in `async-mask-preview-2026-09-07.md`; foreground realtime and
  other synchronous editor paths remain open, as do all other goal dimensions.
- Settled live outlines and selection/range raster previews now use recipe-aware
  RAW/working-space sampling, including earlier local edits and the immutable
  edge reference. Global color changes invalidate outlines and stale workers
  cannot publish over the new recipe. Straight-alpha encoding preserves raster
  overlay colors; evaluated empty masks do not acquire fallback outlines.
  Six focused regressions and real RAW/JPEG controller comparisons pass across
  four working spaces and crop states. See
  `live-mask-sampling-consistency-2026-09-07.md`. Async raster-fill follow-up is
  recorded above; full-resolution/foreground, picker, vector-fill and broader
  mask-refinement proof remains open; the other goal dimensions are unchanged.
- Stacked mask thumbnails now sample earlier local edits in recipe order while
  retaining the original edge-refinement reference; group refinement settings
  are shared with the renderer compiler. Independent rendered-pixel coverage
  and context-preserving continuation agree exactly on the numerical fixture.
  Real RAW/JPEG tests cover four working spaces, crop, disabled/bypassed/soloed
  masks and partial/reversed row refreshes; five focused regressions pass.
  See `stacked-mask-thumbnail-consistency-2026-09-07.md`. Live overlays, broader
  full-resolution/camera proof and foreground latency remain open, along with
  the other goal dimensions below.
- Mask-thumbnail range sampling now retains RAW metadata, floating-point values
  and the configured working space through the engine's global pre-layer look.
  Mask evaluation precedes square resizing, with final-alpha cache/freshness
  metadata preserved. Synthetic sRGB/P3 JPEGs and a real camera-linear DNG/JPEG
  pair pass exact alpha comparisons in four working RGB spaces and two crop
  states; three focused tests pass. See
  `mask-thumbnail-color-consistency-2026-09-07.md`. This is thumbnail integration
  proof, not independent mask/color calibration: sequential local-mask context,
  legacy overlay/outline samplers, full-resolution edges and realtime remain open.
- Canonical previews now carry pre-crop RAW highlight analysis through the
  executor; the shared adjustment-thumbnail and tested tiled paths inherit it.
  A separate cache-quality defect let draft/settled and full/interactive images
  share entries at equal decoded sizes. Their keys are now distinct. A saturated
  gradient's 6,223-pixel, max-two-level discrepancy is eliminated exactly.
  Real DNG and JPEG pass full-resolution lossless preview/crop and
  preview/flattened-output equality, both thumbnail resolutions and fresh
  20-tile publications. Four focused render/layer/thumbnail/crop tests pass.
  See `crop-highlight-preview-consistency-2026-09-07.md` for current receipts and
  limits: legacy/catalog/mask thumbnails, RAW region overlays, cross-resolution
  color, broader cameras/spaces and foreground realtime remain unproven.
- Crop zoom no longer applies its first change twice: 2x selects the central
  half of each image dimension, with exact undo/redo and bounded off-center zoom.
  A real RAW output comparison also exposed crop-dependent highlight recovery;
  the file-backed flattened-output path now carries full-source analysis into
  the cropped render. Real 4208x3120 DNG/JPEG runs produce 2104x1560 crops with
  exact central-region output pixels and exact return-to-full-frame output.
  See `crop-zoom-and-raw-highlight-export-2026-09-07.md`. Canonical preview and
  shared adjustment-thumbnail follow-up is recorded above. Other render paths, canonical crop-intent
  preservation, diverse cameras and realtime memory/latency remain open.
- Editor recipe close coordination now guards window close and Quit, blocks
  unfinished commits or failed checkpoints, and offers Keep editing / Retry
  close. Active save helpers finish or stop asynchronously; pending snapshots
  remain recoverable. An acknowledged save with failed outbox cleanup also
  blocks closing until repaired. Seven focused tests and large JPEG/real DNG
  private-PostgreSQL recovery/pixel checks pass, with packaged normal/blocked
  close evidence in `editor-close-coordination-2026-09-07.md`. This fixes the
  running-save-helper teardown gap below, not universal shutdown of timeline,
  export, agent and training work, unfinished-gesture recovery or realtime I/O.
- Synchronous checkpoint overhead is now measured and reduced: macOS SHA-256 plus
  a single-pass v2 JSON envelope cut large-recipe p95 by 66–72% and isolated
  benchmark peak RSS by 62.5%. Every iteration changes an edit; exact reopen is
  required. Existing v1 checkpoints and the portable digest path pass recovery
  tests. Seven focused tests and real large JPEG/DNG killed-process, retry,
  migration and canonical-pixel checks pass; actual DNG checkpoints measured 7 ms
  versus the prior 25–26 ms. A visually inspected packaged RAW-layer run also
  passes with a retained v2 pending checkpoint; its running-save-helper shutdown
  warning led to the editor close coordination documented above.
  See `recipe-checkpoint-performance-2026-09-07.md`.
  This is checkpoint performance, not foreground realtime rendering. Larger
  document scaling, incremental storage and the cross-cutting gates remain open.
- Completed editor recipes now have an atomic, catalog-scoped local save outbox
  and independent pending/recovery-error UI. Killed-process large JPEG and real
  DNG tests recover before-debounce edits from a stale catalog payload, commit
  canonically and preserve exact state/pixels on two reopens. Migration replay
  now preserves later edits and the original rollback record; conflicting,
  rolled-back and stale decisions are rejected. Six focused tests, the Rust
  workspace and a visually inspected packaged-app save-error proof pass.
  See `recipe-crash-recovery-2026-09-07.md`. That initial format's large
  checkpoints measured 25–26 ms (optimized above); incremental/async saving, multiwriter conflict
  handling and broader recovery remain; editor checkpoint close coordination is
  addressed above.
  The macOS session is still locked, so foreground/realtime proof is unavailable.
- Large recipe saves and rendering-migration messages now use bounded, versioned
  stdin transport instead of argv. Real 2.8 MB raster-mask recipes fail the old
  OS argument limit but pass JPEG/DNG PostgreSQL save, failure/retry and canonical
  reopen with identical state/pixels through stdin. Rust workspace tests and
  focused pipe tests pass. See `recipe-stdin-transport-2026-09-07.md`.
  An instrumented enabled-save native run also passed without a performance
  optimization (15 ms preparation; 1,645 ms settled relink), so the earlier
  save/render contention hypothesis remains unproven. Variable native timing
  needs controlled foreground measurements; no realtime or memory promotion.
- Delayed recipe saves now retain immutable asset-bound snapshots instead of
  reading the current document after a rapid switch. Newest edits coalesce per
  asset, failed saves retry, and approved migration transactions remain ordered.
  Real isolated PostgreSQL runs with JPEG and DNG documents, a linked RAW layer,
  transforms and masks pass save rejection/retry, canonical readback, fresh-
  controller state equality and rendered-pixel parity. Neutral canonical geometry
  no longer reopens as an invented crop. Three focused regressions pass. See
  `asset-bound-recipe-saving-2026-09-07.md`. Completed-edit crash recovery and save
  status UI and editor checkpoint close coordination are addressed above; very
  large document handling and non-neutral crop/UI-intent preservation remain open.
  Two enabled-save native RAW/relink
  checks exceeded their deadline with 10–11-second settled relink renders; the
  same diagnostic workflow with persistence disabled passed with a 1.6-second
  settled render. Persistence/render contention needs diagnosis and correction;
  broader enabled-save native responsiveness is not accepted. The newer
  instrumented run above passed, preventing a causal attribution from that
  earlier comparison alone.
- Linked RAW source thumbnails now decode asynchronously through their own
  camera metadata and canonical neutral rendering; JPEG/PNG thumbnails preserve
  rendered appearance through ICC conversion. Failed decodes cannot become ready
  placeholders, and source revisions invalidate thumbnail entries. Five focused
  regressions pass; tagged JPEGs match independent ICC conversion and the real
  DNG matches canonical RAW output with zero RGB8 difference. Native RAW thumbnail,
  recovery and relink proof is recorded in `source-layer-thumbnails-2026-09-07.md`.
  This does not establish independent all-camera calibration or realtime rendering.
- Source layers now relink through the layer menu with stable ID targeting,
  complete persisted layer-state preservation and undo/redo. File/directory
  watching plus fallback polling automatically refreshes external edits and
  recovers missing/restored sources without recipe/history changes. Five focused
  regressions and a native RAW watcher/relink/menu proof pass, including actual
  changed preview pixels and a window-contained, keyboard-scrollable menu. See
  `source-layer-relink-and-refresh-2026-09-07.md`. Immutable project
  assets, healthy persistence and realtime compositing remain open.
- Linked image layers now fail explicitly on missing/corrupt sources instead of
  silently omitting content. Preview edit failures cannot publish the unchanged
  base image as success. Filesystem revisions invalidate decoded, adjusted,
  rendered, tiled, disk-preview and canonical-plan caches. Focused actual-render
  tests and native RAW disappearance/restoration pass, including a visible error
  badge, last-good-preview retention and recovery. See
  `linked-image-source-reliability-2026-09-07.md`. Immutable source snapshots and
  healthy persistence remain open; this is not
  a realtime-performance claim.
- The previously disabled Source / Image Layer menu now places linked JPG/RAW
  images through a file picker. Source decode uses per-file RAW characterization,
  float32 linear layer pixels and the selected working profile; fill/pixel layer
  colors no longer assume the wrong transfer or gamut. Source layers are admitted
  by the canonical renderer, scale with preview canvas size, and have an accurate
  Source Image label. Five regressions passed; the real 4208x3120 DNG matches
  ordinary RAW rendering with zero RGB8 differences. Native QML placement,
  rendering, PNG layer export and undo/redo passed with a visually inspected
  unobscured layer panel. See `placed-image-layers-2026-09-07.md`. Source thumbnails
  are addressed in the newer evidence above;
  immutable linked-file snapshots, healthy-catalog save/relaunch, broader camera/
  output/geometry coverage and retained-GPU realtime compositing remain open.
- Active-job cancellation reads now run asynchronously and reject failed or
  malformed catalog responses as cancellation authority. The regression improved
  from 445 ms plus a false cancellation to 0 ms with active work preserved.
  Retry, same-ID attempt supersession, read-only legacy access, bounded coalescing
  and destruction are tested; all seven export/loading/recovery/scheduling
  regressions and the native error-state proof pass. Native timing now separates
  capture overhead and window activity: the roughly 1.1-second gaps were recorded
  after QML setup with an inactive window, including a 1.1-second screenshot.
  They are not foreground realtime proof or a diagnosed startup-only stall.
  See `job-cancellation-loading-2026-09-07.md`. Idle dispatch, recovery and
  mutations still have synchronous paths; the broader completion gates remain.
- Export-controller startup and notification history reads now run asynchronously.
  The real isolated-outage request improved from 5,004 ms to 0 ms, with 500 UI
  heartbeat ticks during the helper timeout. Coalescing, stale-result rejection,
  error preservation, retry, controller destruction and read-only legacy access
  are tested. Runner notification bursts also coalesce: 300 wakeups produce three
  scans while preserving cancellation and expedited dispatch. All five focused/
  export/recovery regressions and the packaged Export error-state proof pass.
  A separate 1,145 ms native startup/presentation gap and synchronous runner/
  command paths remain.
  See `export-history-loading-2026-09-07.md`.
- Scene Lab now persists ordered multi-shot camera paths, supports scrub/playback,
  and asynchronously exports cancellable H.264 MP4s with camera provenance.
  Six focused tests and a real-scene native path/export/reopen run pass. All 24
  decoded frames agree with timestamp-matched native renders at a minimum
  36.2 dB RGB PSNR. See `gaussian-camera-film-2026-09-07.md`. This is static-scene
  camera motion, not temporal 4D or film-stock emulation; encoder packaging,
  broader output/color/long-run validation and foreground realtime remain open.
- Scene Lab now saves named camera shots in a validated, atomic project sidecar
  and recalls the exact pose after scene reload. The native Save popup, reset/
  recall, PNG/JPG export and reopen/export path passed on the real 380,065-splat
  scene; original and reopened PNGs are byte-identical. Four focused tests pass.
  See `gaussian-saved-views-2026-09-07.md`. This does not complete camera-path film,
  temporal 4D, reconstruction quality or realtime foreground interaction.
- Connected search → visible target confirmation → frozen-scope local edit →
  review/undo is implemented. Detailed attempts and boundaries are recorded in
  `search-edit-workflow-2026-09-06.md`.
- Real DNG and JPG workflow attempts produced native semantic masks and rendered
  review. Model review still times out; this is not reliable autonomous or
  realtime model verification.
- The model-free transparent-mask RAW reproduction was isolated to a legacy
  renderer backend mismatch: no-mask rendering used a sampled Metal LUT, while
  masked rendering used the exact pipeline. Camera-characterized RAW now avoids
  that unverified LUT route. All 1,897,600 pixels in the real DNG preview are
  unchanged with a zero-alpha mask. ACES 2 also passes the invariant. This is an
  interim correctness fix, not completion of accelerated/realtime RAW rendering.
- A separate actual-preview audit exposed a neutral legacy RAW bypass: decoded
  camera RGB was displayed without characterization until the first mask/edit.
  Metadata-driven mandatory characterization now fixes neutral previews and
  same-image Before/neutral snapshot references. Preview LUT admission is also
  aligned with export without disabling canonical post-characterization Metal
  fusion. Real DNG/JPG 1600x1186 checks show zero changed pixels for transparent
  masks and neutral references. RAW remains CPU-bound (393 ms neutral in this
  run). See `raw-neutral-preview-2026-09-07.md`; the earlier export proof did not
  cover this preview entry point.
- Real ACES 2 RAW and JPG connected workflows now pass visible confirmation,
  native semantic mask generation, rendered review and exact recipe undo. On
  each fixture, 37,920 independently selected cheek pixels are unchanged and
  the background sample shifts warmer. Review remains explicitly human-required
  after model timeout. Broader full-resolution/camera/color-space/edge and
  preview/export parity is still incomplete.
- The actual-preview audit now also fixes independent comparison references:
  external RAW images and snapshots no longer reuse the current image's RAW
  decode contract, and reference cache identities distinguish complete recipes.
  A second neutral bug affected ACES 2 JPGs: adding a transparent mask enabled
  the photographic output transform that the baseline had skipped. Neutral
  ACES 2 rendering now applies the selected transform consistently. Four real
  DNG/JPG legacy/ACES 2 previews pass at 1600x1186; ACES 2 DNG and JPG also pass
  at 4208x3120. All seven image passes, missing-reference rejection and disk
  snapshot cache isolation pass, with zero baseline/reference RGBA8 differences.
  Ten focused tests pass across two runs. This remains one-camera consistency
  evidence, not all-space accuracy or realtime: full-resolution RAW worker
  renders range from 664 to 2243 ms on the CPU. Evidence and broader comparison
  gaps are in `raw-neutral-preview-2026-09-07.md`.
- RAW selection input now goes through canonical color rendering instead of
  treating camera-linear data as a display image. This did not resolve the
  transparent-mask render failure and is not claimed as its fix.
- Scene Lab now connects canonical JPG/RAW capture preparation, fresh COLMAP
  camera reconstruction and actual Brush Gaussian training. A real 32-photo JPG
  capture registered 32/32 views and saved a fully validated 18,944-Gaussian PLY;
  a separate real DNG preparation proof passed. Held-out quality remains visibly
  insufficient. Native Metal scene viewing, camera navigation and novel-view
  PNG/JPG export are now implemented and exercised on the 380,065-splat scene.
  Deferring unrelated workspace preloading reduced the measured motion timer gap
  from 308 to 18 ms in an earlier native test. A later real-scene audit found and
  fixed negative SH color subtracting light during compositing: four held-out
  views now agree with trainer replay at 68–70 dB, with rare residual RGB8
  differences (maximum five levels). This is not an independent photographic
  quality pass. Post-fix UI repeats also exposed roughly one-second presentation
  stalls, traced to Qt waiting for a Metal drawable; realtime remains open.
  Follow-up sampling found all motion windows inactive and macOS screen lock
  confirmed. Foreground latency is therefore unverified, not a demonstrated
  renderer regression or resolved stall. QA now explicitly rejects inactive
  windows as interaction evidence, even when their timing appears fast.
  Film/temporal 4D, packaged backends and broader RAW capture proof
  are still required. See `gaussian-native-viewport-2026-09-07.md` and
  `gaussian-scene-pipeline-2026-09-07.md`; this is not a completed splat workspace.
- Existing local model and editing-tool documents are implementation leads, not
  blanket completion evidence. Their claims must be checked against the current
  build, packaged runtime and real-image behavior.
- The Sept 7 review replay reproduced the isolated 30-second timeout during
  prompt prefill; unchanged warm-cache runs completed in roughly 18 seconds.
  No latency optimization is claimed. A real Qt report-property benchmark
  ruled out ordinary QML field reads as the 58 MB report's bottleneck. See
  `model-review-performance-2026-09-07.md` for configuration boundaries.
- Review result parsing now fails closed for malformed, missing, unfinished or
  truncated model evidence and preserves deterministic review requirements.
  New server timing/cache evidence distinguishes prefill from generation. This
  improves review correctness, not embedded-runtime packaging or cold latency.
- Automatic repair no longer pastes machine-generated reports into the user
  instruction field. This restores the scored repair workflow and routes large
  feedback through the existing bounded serializer. All eight focused/native
  regressions, including the full agent controller, pass on Sept 7. A real-QML
  JPG recheck still passes semantic masking, independent protected-region checks
  and exact undo; cold model review and the observed UI stalls remain open.
- A pinned macOS model-runtime bundle is now staged and copied into the app with
  archive/license/file-inventory verification. Its bundled executable starts and
  stops successfully on an isolated local port with cloud disabled. Model
  installation now targets the selected endpoint instead of inheriting the
  shell's server. App-owned asynchronous startup, readiness, private-store
  ownership, explicit retry and graceful shutdown are now implemented. An actual
  packaged/AOT app proof passes with an empty private model store; setup correctly
  distinguishes a running server from missing model weights. Model provisioning,
  loaded-runner/parent-crash behavior and clean packaged inference remain required;
  see `embedded-agent-runtime-2026-09-07.md`.
- Packaged startup with an unavailable isolated catalog exposed five consecutive
  five-second job-history waits on the UI thread. A background readiness guard
  now prevents that fan-out and explicit startup/error state preserves assets and
  shows the failure. The final native screenshot verifies the visible library
  error. Its largest event-loop timer gap is still 1,191 ms; this is a focused
  outage fix, not realtime loading or removal of all synchronous catalog APIs.
- Nine focused runtime/agent/catalog tests pass, including the real managed-import
  backup receipt. That test exposed discarded backup verification fields, now
  preserved from the worker result. A new transient-startup-health regression
  also fails before its fix and passes afterward: initial probes honor the
  existing startup deadline; health shutdown still applies after readiness. The
  final rebuilt app again passes the isolated lifecycle and visible-outage proof.
- Ordinary job-history refresh now runs off the UI thread with single-flight
  coalescing, stale-snapshot rejection and failure-preserved rows. The isolated
  outage reproduction improved from 5,006 ms blocking to a 0 ms request return,
  with 500 heartbeat ticks during the backend timeout. Queue/recovery tests and
  packaged Export visual proof pass. Startup recovery remains owned by the
  runner rather than duplicated by the model worker. The native proof still
  records a 1,031 ms maximum event-loop gap; export/runner and mutation-side
  synchronous store calls remain. See `job-history-loading-2026-09-07.md`.

## Execution order

The first measured RAW local-edit optimization attempt was rejected and reverted:
its median gain was 4.35%, below the predeclared 15% gate. Actual half-image local
temperature editing now has stage timings and an exact untouched-region guard;
48 scalar mask-refinement combinations are also covered. The real DNG warm local
edit baseline is 161 ms median (137 ms pipeline) at 1600x1186 on the CPU-image
route. See `raw-local-edit-performance-2026-09-07.md` for measured artifacts,
memory/pixel guards. A second attempt now compiles immutable adjustment setup
once per mask set/render: refreshed real DNG median local-edit time fell from
152 to 98 ms (35.53%) with no memory regression and byte-identical output.
33,600 direct kernel comparisons across four working spaces and both rendering
models are bit-exact; real RAW/JPG preview and 4208x3120 protected-region checks
pass. Full-resolution local edits still take 734 ms RAW / 718 ms JPG in these
individual CPU runs. This is an accepted scoped speedup, not realtime or
all-tool/all-camera completion.

Managed model provisioning now has exact artifact pins, full-file verification
before readiness, explicit download confirmation/storage checks, and asynchronous
CLI pause/retry. A read-only full hash of the existing 20.9 GB model passed.
Real private-store provisioning and packaged inference are not yet proven; tagged
transport and multi-GB resume remain open. See
`managed-model-provisioning-2026-09-07.md` for evidence and boundaries.

1. Isolate and fix the RAW zero-mask/locality defect, retain an executable
   regression, then re-run real protected-region and preview/export proofs.
2. Measure and repair loading/interaction stalls and oversized draft evidence.
3. Complete the managed embedded-model lifecycle and real search/edit evaluation.
4. Implement and verify the full Gaussian-splat capture/reconstruction/3D/4D/
   novel-view/film workflow, not just file viewing.
5. Close the professional editing-tool inventory and cross-workflow gaps.
6. Audit every row above against current evidence. Keep the goal active until
   all requested end states are demonstrated; no promotion based on intent.

This ordering is sequencing, not a reduction of scope. Preserve unrelated
worktree changes and user originals throughout.
