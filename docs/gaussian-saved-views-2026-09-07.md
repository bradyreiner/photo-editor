# Durable camera views in Scene Lab

Implemented: name and save the current camera, recall it, continue navigating,
reset to the capture camera, and reopen the scene without losing saved shots.
This is a static-3D shot workflow, not temporal 4D or film output.

## Behavior and storage contract

- Scene Lab has a focused Save view popup and a saved-shot selector. Navigation
  clears the active-shot indicator; reset returns to the original capture camera.
- The full camera pose, dimensions, intrinsics and capture-camera index are stored
  in `camera-views.json` beside the project's `scene.json`. Viewport resizing does
  not reduce the stored camera resolution. Restoring an unchanged camera returns
  its saved pose directly, avoiding floating-point drift from zero-delta orbit math.
- The sidecar is atomically replaced under a writer lock. Names are trimmed and
  Unicode-normalized; case-insensitive duplicate names are rejected, never
  overwritten. Limits are 80 name characters, 128 views and 256 KiB per file.
- The fingerprint binds views to the trained artifact hash, all capture-camera
  calibrations and navigation focus distance. Invalid schema, malformed IDs,
  duplicate entries, nonfinite/invalid camera values, non-orthonormal or reflected
  bases, changed calibration, oversized files and sidecar symlinks fail closed.
- Invalid saved views produce a visible error without blocking scene rendering
  or replacing the existing sidecar. Opening an unsaved scene creates no sidecar.
  Explicit saves do not rewrite the training manifest, captures or trained PLY.
- Existing PNG/JPG export still freezes its own scene and camera, independently
  of subsequent navigation, and writes the camera provenance receipt.

## Verification

Build: `photo_editor_app`, `gaussian_saved_views_test` and
`gaussian_scene_render_test` in `build/release-macos`. Final incremental logs:
`/private/tmp/gaussian-saved-views-final-build.log` and
`/private/tmp/gaussian-saved-views-ui-build.log`; no compiler warnings/errors.

Four focused tests passed: saved-view storage, scene controller, Metal rendering,
and interaction-proof eligibility. Storage coverage includes exact JSON camera
roundtrip, invalid/duplicate names, concurrent writer, changed artifact/calibration,
invalid camera data, corrupt entries/schema/JSON, oversized data and symlinks.
Log: `/private/tmp/gaussian-saved-views-tests.log`.

Packaged native proof uses a copy-on-write temporary copy of the real
380,065-Gaussian scene, an isolated unavailable catalog, and an empty private
model store. It invokes the actual Save view popup and its name-field acceptance,
then resets/recalls the view, exports JPG, drops the loaded scene and transient
camera state, reloads from disk, recalls the saved ID and exports another PNG.

Proof root: `/private/tmp/framehouse-saved-views-native-oQjM5X/`.
`proof/receipt.json` reports `ok: true`, `sceneSavedViewProof: true`, stage 7,
and 12,739 ms total. Both PNGs are 800×596 and byte-identical, SHA-256
`a0a977d82a682042f10757211a3cc52e5f7dc7b57fd9f1743d62fa41da9e5981`.
PNG, JPG and reopened-PNG camera receipts agree exactly. The saved-shot selector
and Save popup screenshots were visually inspected (`proof/startup.png` and
`proof/save-view-dialog.png`). The catalog-recovery banner is deliberate isolation,
not an alteration of the user's live catalog. The model supervisor stopped its
owned process; no model weights were installed or copied.

## Remaining boundaries

No saved-shot rename/delete/reordering or camera-path interpolation/video export
yet. Disk persistence is tested through scene unload/reload in the same app run;
an OS crash/power-loss fault-injection campaign has not been performed. Save/read
operations are synchronous but size-bounded; large-calibration save latency still
needs measurement. Viewport shutdown still waits for outstanding work.

The native motion timer's 19 ms maximum gap is **not** realtime interaction proof:
the window was inactive, and the eligibility check correctly fails. Broader
foreground/hardware timing, reconstruction quality, RAW multiview training,
temporal 4D and photographic/film editing remain open.
