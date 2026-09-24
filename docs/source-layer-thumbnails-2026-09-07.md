# Color-managed linked-image thumbnails

## Behavior and scope

Source-layer normalization no longer reads image files on the UI thread.
`rendered_photo_layer_thumbnail_source` returns an empty pending source for linked
image layers; the existing asynchronous queue owns their decode and publication.
It cannot mark the old generic teal placeholder ready after a RAW decode failure.

The worker uses `decode_source_image` at a bounded 128-pixel edge, retaining the
linked file's own decoded metadata. RAW/EXR use the canonical CPU output renderer;
already-rendered JPEG/PNG use ICC conversion without another photographic tone
transform. The result is a tagged sRGB 64x64 PNG. RAW normalization and camera characterization
are performed by the canonical path, not by the generic Qt TIFF image reader.
Embedded PNG data retains precedence over a source path.

This is explicitly a neutral source-content thumbnail, not a flattened document
preview: RAW uses default ACES2 rendering and Linear ProPhoto working space; JPEG
preserves its existing rendered appearance. Neither inherits the document
grade, masks, transform, base-camera settings or document output-profile change.
The metadata records `source_neutral_srgb` and this separate contract. Adjustment
thumbnails retain their existing shared-preview behavior; other raster/icon
thumbnail behavior is unchanged.

The linked-file filesystem revision participates in the source thumbnail key.
The worker rejects results if the revision changes during rendering. Cancellation
is passed through decoding and output rendering and checked before publication.
Missing/corrupt images and absent worker dependencies fail closed. The shared
thumbnail color-pipeline cache version is now v6.

## Verification

The new `source_layer_thumbnail_test` exercises tagged sRGB and Display P3 JPEGs,
canonical neutral output parity, independent neutral JPEG ICC conversion,
background-thread execution, document-grade isolation, cancellation, revision
invalidation with changed pixels, external replacement during rendering,
missing/corrupt files and missing renderer dependencies. An optional DNG argument
exercises a real RAW decode rather than an embedded camera JPEG.

Native proof: `/private/tmp/framehouse-source-thumbnail-proof-vjLb2o/receipt.json`.
The app completed RAW placement, a ready color-managed source thumbnail, layer
export, undo/redo, automatic missing-source detection/restoration, QML relinking
and a contained layer menu in 26,993 ms. `relink-menu.png` was visually inspected:
the layer thumbnail contains the portrait rather than the old outline placeholder.
The previous `PhotometricInterpretation=32803` TIFF error is absent from the log.
The private model runtime stops; port 58373 independently reports Connection
refused. No user catalog, model store or original RAW fixture was modified.

The independent JPEG oracle exposed an intermediate defect: applying the RAW
output transform to a display-referred JPEG changed RGB8 values by up to 52.
The implementation now separates photographic development from display-referred
ICC conversion. Final real-RAW test log: `/private/tmp/source-thumbnail-real-raw.log`.
Both tagged JPEGs match independent ICC conversion with zero RGB8 difference;
the DNG matches canonical neutral RAW output with zero RGB8 difference. The
same test verifies worker thread separation and the cancellation, file revision,
failure and embedded-pixel precedence cases described above.

All five focused acceptance tests pass in 42.08 seconds:
`thumbnail_refresh_queue_service_test`, `editor_preview_layer_adjustment_test`,
`photo_layer_color_input_test`, `source_layer_thumbnail_test` and
`source_layer_relink_test`. Log: `/private/tmp/source-thumbnail-acceptance-tests.log`.
The final app and test builds succeed without compiler warnings/errors in
`/private/tmp/source-thumbnail-final-build.log`; scoped `git diff --check` passes.

Final native acceptance after the JPEG correction:
`/private/tmp/framehouse-source-thumbnail-final-gBDTJs/receipt.json` (26,375 ms).
All thumbnail, RAW placement/export, automatic recovery, relink/undo and contained
menu gates pass again. `startup.png` was visually inspected with the menu closed:
the source portrait thumbnail is visible in the layer panel. The TIFF error is
absent. The private runtime stops, and port 59950 independently reports Connection
refused. The isolated unavailable-catalog save-helper shutdown warning remains;
it is not claimed repaired by this work.

## Boundaries

Canonical RAW parity checks integration, not independent camera calibration.
The real fixture is one Nexus 6 DNG, not an all-camera or all-RAW-format corpus.
Cancellation is cooperative; decoder internals may not stop immediately.
Filesystem revision checks are not immutable source snapshots. Non-source pixel
and text thumbnails are not promoted to a new color-management contract here.
The native proof uses an intentionally unavailable isolated catalog and cannot
prove healthy save/relaunch. These are correctness checks, not a foreground
realtime latency benchmark or retained-GPU compositing implementation.
