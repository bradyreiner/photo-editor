# Import preview, sources, review, and recovery

Import now supports Fit, 100%, incremental zoom, and drag panning in the large
photo preview. At 100%, a source image pixel maps to a physical display pixel.
File details load on a bounded background worker and show format, size,
dimensions, capture time, camera, and video duration where available. Unknown
capture times are labeled unavailable; file modification times are not presented
as capture times. Video playback remains an explicit native-player action.

RAW/JPEG grouping combines matching stems in the same directory into one capture
tile while retaining both exact file paths. A group can be selected together;
partial selection has a visible dash. Opening a group previews the JPEG first
and allows navigation to each member. Selection counts distinguish captures
from the actual files admitted for import.

The source rail has Locations, Recent, and Favorites modes. Recent and favorite
folders persist in settings, with each list bounded to eight entries. Unavailable
saved folders report a readable message without clearing the current batch.
Completed and canceled import receipts can be dismissed without deleting the
job record or preventing a later job from appearing.

Review provides access to the existing global library and backup settings.
Changing a populated library uses the existing verified migration path. These
are application storage settings for future imports, not per-batch overrides.
Storage changes invalidate confirmation and rerun preflight; confirmation is
disabled while a library migration runs.

Existing path matches have a side-by-side comparison with the actual incoming
and existing previews, their full paths, loading/unavailable feedback, and batch
Skip/Replace controls. The dialog explicitly describes path matching; it does
not claim content-hash or perceptual duplicate detection. Up to 128 matches can
be reviewed. Video matches offer explicit Open video actions.

An interrupted reviewed Skip job now uses the production helper's
`reuse-identical` policy on resume/retry. This lets an identical copy published
before interruption be included in catalog recovery. Changed destination files
remain skipped. The switch requires the exact nonempty reviewed manifest and
matching admitted counts; initial jobs, changed manifests, and Replace jobs
retain their requested policy.

## Validation

Evidence is in `docs/evidence/import-detail-2026-10-23`.

- The macOS desktop application and focused test targets build successfully.
- The real Main.qml native Cocoa import test passes, including decoded photo
  previews, 100% scale, panning, asynchronous photo/video metadata, grouped and
  partial selection, recent/favorite source state, destination/backup changes
  invalidating preflight, real path-match comparison previews, keyboard
  navigation, and receipt dismissal. Responsive screenshots cover 1440x900,
  1280x760, and 900x700. Native UI confirmation does not dispatch a real import;
  completed receipts use injected job records.
- The focused Import source/safety contract and job admission policy tests pass.
  Replay tests include a changed manifest remaining on Skip.
- `scripts/prove-import-ingestion.py` exercises the production Rust helper with
  disposable PostgreSQL and 1,200 synthetic media files: 400 JPEG, 400 DNG, and
  400 MP4. It terminates an actual managed copy after one file is published and
  another has a partial file, then resumes with `reuse-identical`. SHA-256
  verification covers all 3,600 original, managed, and backup files. All 1,200
  catalog links remain readable after PostgreSQL restarts, repeated Skip import
  preserves row counts, and no partial files remain.

These are separate native UI and production-helper proofs. Full GUI JobRunner
interruption/relaunch recovery, arbitrary camera media, large real shoots, and
release qualification are not established by these fixtures. Source and binary
hashes record the validated state of the shared dirty workspace; unrelated
Timeline and Edit changes are outside this pass.
