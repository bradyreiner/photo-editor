# Linked image source reliability

## Corrected failures

Unreadable linked layers previously compiled to empty pixel arrays and were
silently skipped. A second preview defect ignored a failed adjustment pass
unless cancellation was also active, allowing the unchanged base image to be
published and cached as success. The corrupt-source regression reproduced that
second failure before its correction.

Active missing/unreadable sources now fail preview and output explicitly;
existing but undecodable sources fail after layer compilation, including the
deferred export decode path. Failed adjustment passes return before publishing
or caching base pixels. Disabled, zero-opacity and non-solo layers do not block
the composite. Source-file errors identify the affected layer and path.

The native screenshot exposed a separate UI defect: failure presentation relied
on English words in the status message. Structured failure stage/detail now
drive the error presentation. The canvas badge and selected layer show
`Preview failed: Linked image source`; the badge tooltip carries the file path
and restore/disable advice. The existing last-good-preview and Retry mechanisms
are retained.

## Cache revisions

Decoded source caches now include a filesystem revision. On macOS/POSIX this
includes device/inode, nanosecond modification and change times, size and
readability. This invalidates normal in-place rewrites, same-size writes with a
restored modification timestamp, and atomic replacements without reading RAW
file contents on the UI thread.

Linked-source revisions also participate in adjusted pixels, rendered images,
rendered tiles, full-resolution disk preview filenames, canonical admission,
and derived layer-content identities. Cached caller-supplied recipe digests do
not override external source revisions. The independent disk-preview namespace
is now `pipeline-v10-linked-source-revision`; crop-editing paths retain their
own suffix without turning the source path into a fictitious filename.

These are filesystem revisions, **not immutable content hashes**. The non-POSIX
fallback has millisecond metadata only. Network/coarse-time filesystems and
concurrent mutation during a render are not transactional snapshot guarantees.

## Evidence

- `linked_photo_layer_source_test` exercises real decoding and preview/output
  rendering, unchanged explicit recipe digests, same-size/restored-mtime writes,
  atomic replacement, canonical plan invalidation, disk-preview pixel changes,
  missing/corrupt media, disabled/transparent/solo layers and restoration.
  Missing-source output is checked under legacy and ACES 2 models.
- Four focused regressions pass in 2.32 seconds:
  `/private/tmp/linked-source-disk-tests.log` (`render_plan_store_test`,
  `editor_preview_color_cache_test`, `photo_layer_color_input_test`,
  `linked_photo_layer_source_test`). The first corrupt-source test failed;
  `/private/tmp/linked-source-tests.log` is intermediate failure evidence.
- Three broader regressions pass in 2.38 seconds:
  `/private/tmp/linked-source-broad-tests.log` (`raw_camera_linear_decoder_test`,
  `render_parity_test`, `layer_lut_render_parity_test`). This is not every
  input/backend/output combination.
- The existing 4208x3120 DNG still matches ordinary RAW rendering versus a
  placed RAW layer with zero RGB8 differences:
  `/private/tmp/linked-source-real-raw.log`. This is app-internal consistency
  using one real camera fixture, not independent colorimetric certification.
- Native QML placement, layer export, undo/redo, source disappearance, visible
  failure badge/tooltip, last-good-preview retention and restoration pass:
  `/private/tmp/framehouse-linked-source-visible-VPYrAD/receipt.json`.
  `missing-source.png` and `startup.png` were visually inspected. The error
  clears after recovery; generation 7's full render misses both disk and
  rendered-image caches and completes in 1,634 ms. This is **not realtime** and
  was not a controlled performance benchmark. Overall proof elapsed: 20,322 ms.
  The proof only renames its own copy of the RAW fixture; originals are untouched.
  Its private runtime stops; port 62403 independently reports Connection refused.
- The earlier native run at
  `/private/tmp/framehouse-linked-source-proof-HYhGDL/` verified controller
  failure/recovery but exposed the hidden badge and remaining disk-key defect.
  It is not final UI/cache evidence. Its private port 61792 is also closed.

Final acceptance after carrying the missing source's actual layer ID into the
failure result: all seven regressions above pass in 4.82 seconds in
`/private/tmp/linked-source-acceptance-tests.log`. The final native/app/test build
passes with no compiler warnings/errors found in
`/private/tmp/linked-source-acceptance-build.log`; `git diff --check` passes.
A duplicate smoke build was stopped; its log is not acceptance evidence.

The final native run is
`/private/tmp/framehouse-linked-source-accepted-I6OqEp/receipt.json` (20,079 ms).
Both screenshots were visually inspected: the missing-source badge and layer
row are visible, then clear after restoration. The restored full CPU render
takes 1,589 ms and misses both disk and rendered-image caches. The private
runtime's port 62936 independently reports Connection refused after shutdown.

The native test uses an intentionally unavailable isolated catalog, and still
logs the existing pending `save_edit_recipe` shutdown warning. No model weights
were downloaded, and the user catalog was not changed. Healthy save/relaunch
is not established. The monolithic desktop smoke test is not a completion gate
for this slice; the new structured-failure assertion there is also exercised by
the bounded native badge proof.

## Still open

Automatic filesystem watching/re-render scheduling and relink UI are now
implemented and verified in `source-layer-relink-and-refresh-2026-09-07.md`.
Self-contained project packaging, immutable linked-content snapshots, canonical RAW layer
thumbnails, per-source RAW controls, broader camera/output/geometry coverage,
and retained-GPU realtime compositing remain open. The later monitor now
refreshes an idle document on source changes; it is not an immutable render snapshot.
This completes a reliability slice, not Photoshop parity or the full app goal.
