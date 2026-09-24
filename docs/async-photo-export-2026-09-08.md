# Cancellable photo export and safe publication

The preceding goal turn made verified preview-fidelity progress and localized
the app proof's 4.66-second UI stall: layer export 3521ms, undo 49ms and redo
1084ms. This turn addresses the synchronous photo-export APIs. It does not close
redo responsiveness, normal batch-export UX, reconstruction, model or complete
editing-tool requirements.

## Baseline

`photo_export_recovery_test` first exercised the existing layer-export API with
a tagged Display P3 image containing alpha. PNG preserved the profile; JPEG did
not: /private/tmp/photo-export-profile-before.log. The JPEG flattening helper
created an untagged opaque image. It also constructed QImageWriter directly on
the final destination, so failed encoding could damage an existing export.

## Implementation

- `startPhotoLayerExport` and `startFlattenedPhotoExport` capture layer/recipe
  values on the controller thread, then render, prepare and encode on a worker.
  They do not capture the controller or consult later edit state on that worker.
- One export may be active per controller. Read-only busy/path/error properties
  and a completion signal expose the result. Controller destruction requests
  cancellation without waiting for the export worker.
- Cancellation atomically wins or loses against the transition into file
  publication. If accepted before that transition, it cannot publish the new
  file. Encoding/rendering already in progress are not forcibly interrupted;
  busy retires when the worker returns. Cancellation is not falsely reported
  accepted once publication has started.
- Both synchronous compatibility APIs and async APIs use a common QSaveFile
  writer with direct-write fallback disabled. JPEG alpha preparation reuses the
  existing standard export encoder, preserving ICC metadata. PNG retains its
  original image format/precision; no lossy cache substitution or PNG compression
  tuning is introduced here.
- Current base and linked source paths, including canonical symlink aliases,
  are rejected as destinations by both API variants. This is not a global
  catalog-original lookup or external-writer conflict protocol.
- The existing placed-layer app proof now awaits the async API and records its
  admission time, total worker phase and event-loop tick gaps separately from
  undo/redo. Its 45-second deadline is unchanged.

[Qt documents QSaveFile's temporary-file/commit behavior](https://doc.qt.io/qt-6/qsavefile.html),
including the loss of atomicity if direct-write fallback is enabled. The tests
exercise actual cancellation and exceptions after encoding, before commit.

## Verification contract

The focused test checks tagged PNG/JPEG output, exact PNG pixels, white-matte
JPEG alpha, snapshot preservation when the layer changes immediately after
dispatch, cancellation/exception/unsupported-format preservation of an existing
destination, a held worker with a live GUI event loop, single-flight rejection,
original-file protection, flattened sync/worker equality and nonblocking
controller destruction. An optional photographic source argument covers the
same flattened comparison on real RAW/JPEG data.

Full source-file byte
snapshots, persistent resumable export jobs, encoder-internal cancellation and
new layer-export UI controls are not provided by these APIs. Existing individual
layer export semantics remain raster content plus placement/opacity, not a new
claim of complete adjustment/mask/clipping-stack isolation. Normal Export-panel
job routing is unchanged.

## Executed evidence

- The focused recovery executable passes on the tagged 64x48 Display P3/alpha
  fixture. PNG pixels/profile and JPEG profile/white-matte samples pass; so do
  snapshot mutation, held-worker event-loop activity, single-flight rejection,
  cancellation/fault destination preservation, source/symlink protection and
  nonblocking destruction. Source-symlink coverage is Unix-specific.
- Real HDR+ Nexus 6 DNG and its reference JPEG both pass exact flattened
  synchronous-versus-worker image/profile comparisons at **4208x3120**. DNG
  reference/worker elapsed times are 13976/12889ms; JPEG 9845/9765ms. Logs:
  `/private/tmp/photo-export-hdrplus-nexus6-skin-0218.dng.log` and
  `/private/tmp/photo-export-hdrplus-nexus6-skin-0218-reference.jpg.log`.
  These compare dispatch paths, not independent color-science ground truth.
- The initial real-RAW attempt failed the test's generic 10-second wait while
  its worker was still busy without an error. Diagnostics measured the reference
  itself at 13888ms. Only the flattened correctness wait now allows 45 seconds;
  small-fixture waits remain 10 seconds and the CTest timeout remains 90 seconds.
  This budget change is not a rendering optimization or realtime acceptance.
- The rebuilt native app passes the placed-layer proof with that DNG base and
  JPEG source layer: `/private/tmp/async-photo-export-app-proof-s2NC0z/receipt.json`.
  Export admission reports 0ms at millisecond timer resolution, observed export
  phase 3733ms, 19 event-loop ticks and maximum export-phase tick gap 239ms.
  The observer runs every 200ms: these numbers are not frame latency or FPS.
  Earlier synchronous export admission occupied 3521ms on the UI thread.
- Whole-proof observation gap is still 1171ms; undo takes 50ms and redo 1113ms.
  Overall proof duration is 35510ms against the unchanged 45-second deadline.
  Redo admission remains a separate responsiveness defect. This is a single
  smoke observation, not a distribution or whole-app realtime certification.
- `startup.png` was visually inspected: photo and source-layer UI are visible.
  The catalog recovery banner is expected from the deliberately closed test DB
  port, not a statement about the user's catalog. Proof appdata/preferences/cache
  are isolated; no model weights were downloaded.

The final focused native suite passes **18/18 in 50.47 seconds**:
`/private/tmp/photo-export-final-regressions.log`. It includes photo-export
recovery, the eight-case global-mask fidelity check, native global/local mask
and opacity/expansion checks, initial-selection cold/warm recovery, corrupt-cache
recovery, preview feedback, mask coverage, local adjustment execution, edit
scheduling policy and Metal ACES output-LUT parity. The rebuilt focused targets
report no compiler warnings/errors; scoped `git diff --check` passes.

This is not the complete test suite. The previously documented broad controller
failure at the advanced-tone settled-render scheduling assertion was not repaired
or rerun in this slice. No TIFF-specific codec roundtrip, all-camera corpus,
monitor-ICC/HDR-device calibration or whole-application realtime claim follows
from these results.
