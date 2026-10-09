# Cutout crash and neutral adjustment creation

Creating a cutout above existing adjustments could crash the layer renderer. Adding an adjustment layer also moved Base Image edits into the new layer, changing which content those edits affected. The renderer now prepares each adjustment from the actual layer being rendered, and Add Layer creates a neutral adjustment while preserving Base Image edits.

The macOS crash report `Looksy-2026-10-06-231014.ips` records `EXC_BAD_ACCESS` in the standard photo-layer callback immediately after `copy_pixels_from_selection`. Its loaded app UUID is `8D00B0CC-0C9C-31A3-9E54-A96869A74AB3`. The callback subtracted the original vector's data pointer from a copied layer's address and indexed the compiled adjustment vector with the result. Linked base-image cutouts copy the layer vector; group rendering also synthesizes adjustment layers. Both invalidate that assumption. Preparation now occurs once per rendered adjustment inside the compositor, retaining immutable preparation and parallel row execution without pointer-derived indexing.

`addAdjustmentLayer` previously called `createAdjustmentLayerFromCurrentGlobalEdits`. The default action now inserts a neutral layer through the existing layer-stack transaction. The explicit conversion command remains available, and tests that intentionally convert Base Image edits call it directly.

Evidence lives in [the receipt](evidence/cutout-adjustments-2026-10-06/receipt.json). The copied-layer fixture exited with signal 11 before the renderer fix. After the fix, linked cutouts, graded groups, and their combination match the scalar compositor bit for bit across repeated renders. The controller fixture reproduced a pixel change before the neutral-add fix and covers unchanged pixels and substantive recipe fields, undo, redo, independent materialization, and recipe reopen. Row positions and generated display names are excluded from recipe equality because insertion changes these presentation fields.

The native proof loads the source Main.qml and StudioWorkspace with real controllers, an isolated catalog, settings, cache, and app-data directory. It creates linked and independent cutouts through the production QML menu handler and sends three QTest pointer clicks to the visible Add Layer button. Both a generated image and the same RAW photo referenced by the crash report completed safely and retained exact CPU-rendered pixels before and after each addition. Native screenshots were inspected; RAW-photo captures remain in `/tmp/looksy-cutout-regression-2026-10-06/native-raw`.

These are correctness checks. They do not measure interaction latency: the fixture deliberately performs synchronous CPU reference renders to compare pixels. The RAW proof uses a rectangle selection and isolated edits, rather than replaying the user's saved subject mask or document. GPU pixel equality and broader photographic-quality qualification are separate checks. The user's originals, saved recipes, outbox, and recovery records were not modified.

Run the focused checks:

```sh
ctest --test-dir build/release-macos \
  -R '^(neutral_layer_elision_test|editor_preview_cutout_adjustment_test|editor_preview_photo_layer_group_test|editor_preview_coalesced_undo_test)$' \
  --output-on-failure
```

Run the native correctness proof in a disposable macOS window:

```sh
QT_QPA_PLATFORM=cocoa PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1 \
  build/release-macos/tests/actual_app_workflow_benchmark_driver \
  --cutout-adjustment-proof --timeout-ms 60000 \
  --capture-dir /tmp/looksy-cutout-native
```

An optional `--source` path selects a real image while retaining the isolated catalog and edits.
