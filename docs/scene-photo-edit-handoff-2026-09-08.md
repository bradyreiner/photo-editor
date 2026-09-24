# Scene photo to editable catalog asset

## Delivered workflow

Scene's **Edit photo…** saves a PNG/JPEG at the selected export size, using the
viewport's frozen camera/time snapshot. The completed image and camera receipt
are published before the normal referenced import into the `Scene renders`
Library project starts. The editor then opens the actual imported catalog asset.
It does not use an in-memory stand-in or represent the rendered photograph as RAW.

The handoff permits one request at a time, waits for Library work, reports export,
import and recipe-preparation errors, and times out after 180 seconds while
retaining any completed files/import. Changing the scene or leaving Scene during
the operation suppresses automatic editor navigation. File lookup excludes
virtual copies. Existing import-preset adjustments are retained.

Photo encoding now uses same-directory staging and the existing exclusive media
publication helper. Final image and `.camera.json` names are not overwritten;
ordinary publication failure rolls back the receipt by identity. Film retains
its `.camera-path.json` behavior. This is not a crash-atomic two-file transaction.

## Color correction

The first native run exposed a second photographic rendering: the editor's
default Perceptual/ACES path changed 475,873 of 476,800 pixels in the already
display-rendered sRGB Scene photo. Mean RGB errors were approximately 28.37,
29.76 and 26.68 eight-bit code values; embedded profiles already matched.

The explicit Scene edit handoff now sets and persists the existing canonical
`Relative Colorimetric` output intent, retaining `aces2-ocio-v1` and all other
recipe settings. This selects the colorimetric output path instead of applying
another photographic tone rendering. It does not switch to the legacy renderer,
alter source pixels, or change RAW defaults and existing imported photo recipes.
Import presets can still intentionally change the image; the neutral test uses
isolated default preferences. This is not a general fix for all raster defaults.

## Evidence

Native debug-app proof:
`/private/tmp/scene-photo-edit-color-proof-VPHKtu`.

- Existing real static reconstruction: 753,038 splats. No new training performed.
- Actual Scene QML action, completed export, ordinary persistent import, and
  real catalog asset `asset-a4196d132d71e8cd` opened in Edit.
- 800 x 596 PNG: all 476,800 RGBA pixels match exactly after neutral editor
  export when compared in eight-bit RGBA; embedded sRGB profiles match exactly.
  The editor's higher-bit-depth PNG need not have identical encoded bytes.
- Linear mask with +0.75 exposure and Display P3 output produces changed pixels,
  the requested profile and successful catalog autosave. Three undo operations
  restore the original recipe and exact neutral exported pixels/profile.
- A fresh `catalog_snapshot` helper reads the saved Relative Colorimetric intent
  after the app exits (`persisted-catalog.json`). This is a catalog readback,
  not a second native-editor relaunch/render test.
- `scene-edit-action.png` and `startup.png` were visually inspected. The new
  action is visible, and the imported image opens in the regular Edit workspace.
- Native app exits zero with `receipt.json` / `scenePhotoEditProof.ok: true`.
  Maximum observation event-loop gap is **3362 ms**. Realtime remains unproven.
- Both isolated proof PostgreSQL servers were stopped; their data and artifacts
  remain. The user catalog was not changed by these proof runs.

The failed pre-correction proof remains at
`/private/tmp/scene-photo-edit-proof-HmVNI1`.

Focused handoff coverage includes duplicate requests, rendering-before-import,
actual imported-ID routing, changed-scene navigation, early/late busy Library,
failed/stale exports, import failure, recipe failure and timeout. Publication
coverage runs collision/rollback scenarios for both photo and film suffixes.
Native Metal regressions cover rendering, handoff, saved views, temporal workspace
and film paths. Logs: `/private/tmp/scene-photo-edit-final-native-regressions.log`;
QML component lint: `/private/tmp/scene-photo-edit-final-qmllint.log` (zero diagnostics).
The sandboxed Metal run fails because the GPU is unavailable; use the native run.

Reproduce only with a fresh isolated destination:

```sh
build/dev-macos-debug/apps/desktop/Framehouse.app/Contents/MacOS/Framehouse \
  --managed-runtime-proof-dir NEW_PROOF_DIRECTORY \
  --gaussian-workspace-proof --gaussian-project-proof EXISTING_SCENE_DIRECTORY \
  --gaussian-photo-edit-proof
```

## Remaining work

Profile the foreground stall during this workflow. Add JPEG handoff and broader
resolution/profile/preset cases, native fresh-process editor reopen, temporal
photo handoff, and file-dialog/pointer interaction coverage. This proof does not
establish novel-view quality for unobserved surfaces, genuine 4D reconstruction,
embedded-model quality, Photoshop parity, or complete app readiness.
