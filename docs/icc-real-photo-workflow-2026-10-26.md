# Native ICC and real-photo workflow validation — 2026-10-26

The native monitor-profile gap is closed for the exercised paint/document paths. The real Canon RAW workflow also passes, after fixing a recipe-readiness race that could replace the open batch-sync source and discard its refined mask. A separate selection stress case exposed and fixed unnecessary recipe parsing in the Metadata panel.

## Changes

- `Main.qml` now completes a deferred photo load only after explicit activation. Selecting another thumbnail and finishing its asynchronous recipe load leaves the open editor document intact. Pending activation is retained while either the source or destination recipe loads, then cleared after successful activation.
- The native paint/document tests have explicit `--require-native-device-icc` cases. They require a configured system monitor profile and verify the full render uses `canonical-metal-device8`; the regional path must match it. General offscreen tests retain their ordinary regional/full parity checks without pretending to exercise a physical display profile.
- The real-shoot child-process proof waits for all three visible gallery images before capturing and accepting reopen. A ready selected image alone no longer satisfies that proof.
- `LibraryMetadataPanel.qml` reads capture time through `galleryAssetAt`, avoiding asset-bound recipe projection during selection.

## Native ICC results

Both `photo_document_device_icc_workflow_test` and `photo_pixel_paint_device_icc_test` passed on Cocoa with Metal and the actual system monitor profile:

```text
profile_sha256=ac32af073eef9c4f23702661db46697315488c33d336c342b8865bb29a86cc14
profile_source=system-monitor
base_backend=canonical-metal-device8
regional_backend=paint-region
full_backend=canonical-metal-device8
regional/full maximum channel error=0
outside-region maximum channel error=0
```

The four supporting profile/reference tests passed: `color_profile_asset_test`, `preview_device_icc_identity_test`, `color_cmm_comparison_test`, and `system_monitor_profile_state_test`. Generic offscreen paint/document cases also pass. This validates the exercised conversion and regional-render consistency; it does not establish monitor calibration, printer matching, multi-camera color quality, or human artistic approval.

## Real RAW workflow

The native proof used private copies of `IMG_3665.CR2`, `IMG_3666.CR2`, and `IMG_3860.CR2`, with a managed PostgreSQL catalog under a temporary app-data root. Originals and the user's catalog were not modified.

The first run failed selective batch sync: selecting the second target while its recipe loaded replaced the editor's source with the target's neutral recipe. Sync then changed both selected photos and lost the mask. After the explicit-activation fix, the workflow passed:

1. Import three referenced Canon CR2 files through the production GUI import job; exercise RAW decoding.
2. Rate/pick the first, rate the second, and reject the third.
3. Refine a mask, selectively sync tone and masks to one other photo, and preserve the source and unselected third photo. Verify Undo, Redo, and no additional history for a no-op sync. Serialized mask exposure remains `0.12`.
4. Exercise a real unwritable-destination export failure, retry successfully, and decode the resulting 6024 × 4020 JPEG (6,776,445 bytes).
5. Move the copied source directory, observe three offline assets, and relink all three.
6. Start a separate process, reload `Main.qml` and the persisted catalog, verify culling, recipes, relinked paths, and the exported file's SHA-256, and wait for all three gallery thumbnails to render.

The visible edit preview and reopened gallery captures were inspected. This is an automated native GUI/controller workflow; it does not prove every step through pointer-only interaction. Personal-media captures remain in ignored local artifacts, not checked-in documentation. The child process reopened persisted state while the parent kept the isolated catalog alive; abrupt-crash recovery and clean-machine qualification were not assessed.

Reproduction:

```sh
cmake --build build/release-macos --target photo_editor_app desktop_shell_real_shoot_workflow_test photo_pixel_paint_test actual_app_workflow_benchmark_driver
ctest --test-dir build/release-macos -R '^(photo_document_device_icc_workflow_test|photo_pixel_paint_device_icc_test)$' -V
QT_QPA_PLATFORM=cocoa PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1 \
  PHOTO_EDITOR_REAL_SHOOT_PROOF_DIR=/private/tmp/looksy-real-workflow-final \
  PHOTO_EDITOR_REAL_SHOOT_SOURCE_DIR="$PWD/build/app-audit-fixes-2026-10-25/real-shoot/source" \
  build/release-macos/tests/desktop_shell_real_shoot_workflow_test
```

## Selection performance guard

The existing `catalog_large_recipe_selection_test` selects five times among photos containing a 64 MB raster-edit recipe and requires each click to dispatch in under 50 ms without opening the editor. The isolated baseline peaked at **673.508 ms**. Sampling identified `LibraryMetadataPanel.loadDraft → assetById → adjustmentPatchProjection` on the UI thread, only to obtain capture time. Using the metadata projection reduced the same test's maximum to **2.835 ms** (99.6% lower); it passes with zero editor opens.

The loop allowed at most three narrow hypotheses. A preliminary label-read change missed the gate and was reverted. The traced Metadata-panel fix was accepted on the second hypothesis. No memory or native paint-latency claim is made by this offscreen dispatch metric.

## Evidence and limits

Detailed local logs, failed/passing receipts, captures, and source/binary hashes are collected under ignored `build/icc-real-photo-workflow-2026-10-26/`. The tree contains unrelated concurrent work, so this report qualifies these paths and artifacts, not the entire checkout or a release. Long-project Timeline qualification, broader first-search quality, physical color review, and distribution readiness remain separate work.

The application built successfully earlier with the deferred-activation fix. The final rebuild, after the Metadata-panel fix, stopped in concurrently edited Timeline/agent code outside this pass: `QDir::isWritable`, a six-argument `register_agent_timeline_commands` call against a five-argument declaration, and `QVariant::toInt(-1)`. Those edits were preserved. The Metadata fix passes in the actual-app benchmark loading the source QML; the packaged executable has not been refreshed with that final fix. Two additional metadata persistence/sidecar cases were requested but were not run because their executables were not built; they are not counted as passes.
