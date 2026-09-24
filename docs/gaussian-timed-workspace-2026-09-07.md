# Timed capture preparation in Scene Lab

Status: calibrated timed-input preparation and verified reopening now work in
the native workspace. This is **not** 4D reconstruction, temporal splat rendering,
or completion of the broad app goal.

## User workflow

1. Open Scene, choose **Timed 4D input**, and select a calibrated
   `capture-4d.json` manifest. The [input contract and example](gaussian-timed-capture-2026-09-07.md)
   still require a synchronized, fixed, already-undistorted multi-camera rig.
2. Choose a new project folder and preparation resolution. **Prepare timed
   capture** runs canonical image preparation and verification on a worker.
3. The workspace shows **prepared not trained**, camera/time/image counts,
   duration and project location. It explicitly says temporal training and
   novel-view/4D export are unavailable. Static training controls are not used.
4. **Open scene** reopens the prepared project, including a standalone committed
   dataset produced by the earlier pipeline driver. Verification runs in the
   background, without executing persisted commands or modifying the project.

## Persistence and integrity

- Successful UI preparation verifies its output and atomically saves `scene.json`
  with the prepared manifest's SHA-256. Reopening compares this receipt against
  the actual manifest, ignoring persisted absolute artifact paths.
- The verifier reconstructs the timed input contract from the prepared
  observations: contiguous integer time indices, matching camera timestamps,
  complete calibrated views, camera matrices, image paths and timing summaries.
- Each image must remain inside the project with no linked path components,
  match its stored hash, decode as PNG, match calibrated dimensions, and carry
  the canonical sRGB profile. Hash checks alone do not admit malformed pixels.
- Source files are not reread on reopen. Source paths and hashes are provenance,
  not proof that originals are currently online or unchanged. Opening a standalone
  dataset checks internal consistency; without an earlier saved receipt, it
  cannot establish historical authenticity. These are not cryptographic signatures.
- Prepared manifests cannot exceed the reader's 16 MiB limit. Output claims its
  new leaf directory using `mkdir` after validation, instead of accepting an
  existing directory created by another operation during that validation.
- A busy controller rejects conflicting creation/open requests. Cancellation and
  controller destruction signal the worker without joining it on the GUI thread.
  Hash verification checks cancellation between 4 MiB reads; an individual image
  decoder or filesystem operation can still take time to return.
- No status file is written before preparation claims and commits its dataset.
  Failure/cancellation can retain generated partial images, with an in-memory
  error. Uncommitted partial captures do not yet have resumable journals. A fully
  committed `timed-capture.json` remains reopenable even if saving `scene.json`
  was interrupted. Opening never silently resumes training.

## Verification

- Expanded `gaussian_timed_capture_test` covers controller admission, conflicting
  requests, successful preparation, saved and standalone reopening, unchanged
  saved receipts, changed manifests/images, and queued cancellation/destruction.
- Twenty malformed source manifests and eight prepared-data/image cases are
  rejected. Prepared cases include bad time indices, inconsistent timestamps,
  escaping paths, false timing summaries, wrong dimensions/profiles, and a
  truncated PNG whose recorded hash was recomputed. The deliberate truncated
  fixture emits the expected libpng read error.
- Real RAW integration passes using temporary copies of one Nexus 6 DNG with
  **synthetic** camera poses and timing. This proves RAW decode, preparation,
  controller persistence and verification—not a real calibrated dynamic capture.
  Final receipt: `/private/tmp/gaussian-timed-workspace-proof-34bWh7/raw-final/receipt.json`.
- Five focused Gaussian regressions pass in 3.87s. Log:
  `/private/tmp/gaussian-timed-workspace-final-regressions.log`.
- Final desktop/driver/test builds pass with no new C++ warnings. `qmllint` for
  `SceneWorkspace.qml` exits zero with no diagnostics. Final build log:
  `/private/tmp/gaussian-timed-workspace-verified-build.log`.
- Native packaged app proof invokes the actual QML prepare action with the RAW
  fixture; final receipt is `ok`, with `imagesVerified: true`, two cameras, two
  capture times, four images, and no trained viewport. Screenshot inspected:
  `/private/tmp/gaussian-timed-workspace-proof-34bWh7/native-final/startup.png`.
- Fresh-process reopening passes in `native-reopen/receipt.json` under the same
  proof root. The existing 753038-splat static scene still opens and renders in
  `native-static/receipt.json`; its screenshot was inspected too.
- App proofs use isolated preferences/catalog/cache. The visible catalog recovery
  banner is expected for the closed-port test profile. No user catalog, model
  weights, backend installations, or training jobs were changed.

## Still required

Genuine time-aware training, native temporal primitives/rendering, photo and film
export over scene time, supported runtime/hardware/license integration, calibrated
moving-capture evidence with held-out views/times, interactive performance, and
training/partial-preparation recovery remain unfinished. The input UI still
requires an explicit manifest: video/ordinary-capture calibration, synchronization,
thumbnail/time inspection and arbitrary moving-camera input are not implemented.
The remaining RAW/loading, embedded model/search/agent and full editing-tool
requirements remain active independently of this workspace step.
