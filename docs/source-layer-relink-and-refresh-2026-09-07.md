# Source-layer relinking and automatic refresh

## Behavior

The linked-image layer menu offers Relink Image. The dialog holds the stable
layer ID, not its row number, so reordering or deleting layers while it is open
cannot redirect the replacement to another layer. Invalid files, non-source
layers and locked layers are rejected before undo/state mutation.

Relinking changes the source path while preserving the layer ID, name, canvas
footprint, transform, masks, blending, visibility and adjustments. Replacement
images fit the stored layer dimensions; they do not silently resize the layout.
Undo/redo restores the old/new link as one edit.

The document controller owns a linked-file monitor. It coalesces filesystem
events at 150 ms, watches files and parent directories, reattaches after atomic
replacement, and polls revisions every 1.5 seconds while links exist. A missing
directory is tracked through the closest existing ancestor. Polling covers
missed or unsupported notifications and reattaches watches after recovery.
Unrelated directory activity does not render the document. Switching documents
or removing all links releases the old watches and stops the poll timer.

An external change invalidates layer thumbnails and schedules a new full
pipeline preview generation, without saving a changed recipe or adding history.
Missing/corrupt media uses the existing visible error and last-good-preview
behavior. Restoring the source schedules recovery automatically, with no slider
change or manual Retry required.

## Verification

`linked_image_source_monitor_test` covers burst coalescing, atomic replacement,
unrelated directory events, missing/restored files, whole-directory removal and
restoration, initially absent nested directories, and discarding old-document
events. Initial focused run passed in 9.71 seconds:
`/private/tmp/source-monitor-test.log`. This is correctness evidence including
fallback polling, not an interactive latency benchmark.

The controller regression compares persisted layer content through the recipe
codec, excluding refreshed thumbnail cache fields. It verifies validation,
locks, identity targeting, complete persisted-state preservation, undo/redo,
changed full-resolution preview pixels after relinking and external editing,
automatic missing-file recovery, unchanged recipe/history after external writes,
and release of old-document watches. Its initial map comparison caught the
expected refreshed `thumbnailSource`; the persisted-content comparison is the
relevant invariant, not preserving a stale thumbnail.

All four acceptance regressions pass in 21.01 seconds:
`/private/tmp/source-relink-acceptance-tests.log` (`source_layer_relink_test`,
`linked_image_source_monitor_test`, `linked_photo_layer_source_test`,
`photo_layer_color_input_test`). This includes actual decoded preview-pixel
changes, not just cache-key changes. Builds pass, including the app and shared
controller runtime: `/private/tmp/source-relink-full-build.log` and
`/private/tmp/source-relink-menu-build.log`. No compiler warnings/errors were
found in those successful logs; `git diff --check` passes.

The first native watch proof exposed that relinking used an interactive-only
stack change. Relinking now explicitly schedules the full-resolution settled
preview. That intermediate proof, `/private/tmp/framehouse-source-watch-proof-YNnvMR/`,
is not acceptance evidence. Its bundled runtime was confirmed absent afterward.

The corrected native run at `/private/tmp/framehouse-source-watch-verified-jerS3b/`
passes in 32,162 ms: RAW placement/export, automatic missing-source detection,
visible error with retained preview, automatic restoration, QML relinking and
relink undo/redo. It renames only a private copy of the RAW fixture; neither
removal nor restoration is accompanied by a slider change or manual Retry.
Its private runtime stops and port 64847 independently reports Connection refused.
An attempt at
`/private/tmp/framehouse-source-relink-ui-jWq4JN/` hit the 45-second proof deadline
while controller tests ran concurrently; it is not accepted UI evidence. No
bundled runtime remained afterward. These are not controlled performance runs.

The standalone rerun at `/private/tmp/framehouse-source-relink-menu-cXehyx/`
completed the workflow in 34,385 ms, but visual inspection exposed an existing
shared-menu defect: the popup extended beyond the window. Port 49182 was
independently closed. That screenshot is not accepted as finished menu layout.

`MenuPopover` now uses positive window margins, caps its dimensions to the
overlay, and scrolls overflowing rows. Keyboard focus brings the selected row
into view. Qt documents the popup margin/overlay behavior in its
[Popup reference](https://doc.qt.io/qt-6/qml-qtquick-controls-popup.html).
`menu_popover_bounds_test` passes in 1.83 seconds with 30 commands, 990x620 and
360x260 windows, opposite anchor corners, first/last focus and disabled-row
navigation. Log: `/private/tmp/menu-popover-bounds-test.log`.

Final native acceptance:
`/private/tmp/framehouse-relink-contained-menu-U6XhVo/receipt.json` (35,186 ms).
It passes the automatic RAW disappearance/restoration, relink, undo/redo and
visible-menu workflow, and asserts the menu content lies inside the viewport.
`relink-menu.png` was visually inspected: the Relink command and full menu fit
inside the app window. The final private runtime stops and port 49833
independently reports Connection refused. Successful final menu/app build:
`/private/tmp/source-relink-menu-verified-build.log`. Five focused regressions
pass in total; no all-camera, all-filesystem or realtime claim is made.

The native proof invokes the dialog's shared QML accepted-file handler; it does
not automate navigating the operating-system file picker. The intentionally
unavailable isolated catalog still emits the pending save-helper shutdown
warning. That run also emitted a generic TIFF-decoder error for RAW thumbnails;
the later `source-layer-thumbnails-2026-09-07.md` slice fixes this separately.

## Boundaries

This is live linked-file refresh, not immutable project snapshots or portable
project packaging. Concurrent external writes may temporarily fail decoding;
later revisions trigger recovery. File metadata calls can depend on filesystem
responsiveness, and this is not a network-filesystem realtime guarantee.
The later thumbnail slice provides the RAW/color path. Relinking does not
introduce per-layer RAW develop controls or prove GPU compositing performance.
The existing isolated native proof cannot establish healthy catalog persistence.
