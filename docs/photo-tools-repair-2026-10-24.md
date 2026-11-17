# Photo tool repairs

Remove/Heal/Patch now receive brush input in the photo viewer. Previously the
viewport accepted only Selection brush input. Applying a generated repair also
now requests a photograph render: the old overlay-only draft deliberately
discarded retouch pixels, leaving the original object visible despite a saved
patch. Committed retouch changes, including replacement and deletion, use the
same corrected route. Failed generation keeps the staged selection editable.
Remove defaults to full brush flow, and completed operations clear their busy
chip.

Crop, Horizon and Transform overlays have explicit stacking above viewer input.
Crop handles accept their entire hit area, including the half outside an image
edge. Board framing does not clip the source while using geometry tools.
Horizon endpoints are measured using the displayed crop's pixel aspect, rather
than treating normalized width and height as equal units. Outside clicks,
cancelled drags and tiny horizon gestures do not reuse an old guide.

Compare is a child of the existing image viewer. Split, wipe and linked views
share that stage; Done returns to editing. The editor's other toolbar does not
overlap Compare, and passive comparison surfaces do not show a full-resolution
loading indicator for work they cannot request.

## Verification

`desktop_shell_photo_tools_test` loads production Main.qml and StudioWorkspace
with real controllers and an isolated generated 800 x 400 image. Qt mouse events
exercise crop and horizon gestures, then paint a Remove area. The test requires
a generated patch, changed settled image pixels, removal of the red object,
undo/redo, a cleared busy indicator, three embedded comparison layouts, and Done.
The original aspect constraint is retained during crop dragging. Native Cocoa
and offscreen runs use the same assertions; native screenshots verify visible
layout. These are Qt pointer events in an exposed native window, not an OS input
latency measurement.

Focused supporting checks are `editor_crop_zoom_test` (exact crop pixel and
flattened-output parity), `photo_retouch_quality_test`, and
`retouch_cancel_ui_test`. The real `ai_model_runtime_inference_test` separately
verified LaMa ONNX inference, cancellation and the pinned model contract. An
initial timing run during compilation missed the latency gate; an idle rerun
passed in Debug at 4,196 ms cold and 2,421 ms warm. The final Release
rerun passed at 4,687 ms cold and 2,450 ms warm, with cancellation acknowledged
in 1,763 ms and a real photographic 800 x 523, 16-bit-per-channel LaMa patch. The uniform-color workflow fixture
uses the runtime's Local CPU Fill spot path; it does not substitute for the real
LaMa check.

Two existing checks stopped outside these repairs: the older crop/retouch shell
shards hit the color-domain disclosure source assertion before reaching their
workflows, and `crop_loading_qml_lifecycle_test` reaches an unrelated missing
AdditiveLightWaves instrumentation anchor. Their assertions were retained.

## Manual check

1. Open a disposable photograph in Edit. Choose Crop, drag a box, resize an image
   edge handle from its outer half, and choose Done. Undo and redo the crop.
2. Choose Straighten and draw along a visible horizon. Check the corrected angle
   on both landscape and portrait photos. Click without dragging, then click
   outside the image; neither should apply another correction.
3. Choose Remove, paint an object, Apply, then Done. Confirm the image changes,
   the activity chip clears, and undo/redo restores the appropriate result.
4. Open Compare. Check split, wipe and linked views without another window or
   overlapping toolbar. Choose Done and continue editing the same viewer.

Native receipts and screenshots are under `build/photo-tools-2026-10-24/`.
The final native and offscreen workflow runs both pass; the removal fixture's
center equals its background exactly after Apply. The three supporting focused
CTest checks pass. `release-model-final.log` and
`bundled-model-verification.log` record the real model and packaged artifact
checks. Source and binary SHA-256 snapshots identify the verified build.

The release cache originally had `PHOTO_EDITOR_ENABLE_AI_INFERENCE=OFF`, while
Debug had the verified runtime enabled. Release was configured with inference ON,
`.photo-editor/ai/onnxruntime`, and `.photo-editor/ai/model-bundle`, with CoreML
remaining OFF as required by the pinned installation. The app bundle includes
the installed LaMa/SAM2 models and ONNX library; no model download is required.

The changes were built into the debug and release app bundles. The running app
must be restarted to load the rebuilt executable and QML. Existing sessions were
not closed. This evidence does not qualify all photographic removal quality,
combined crop/perspective/retouch cases, large RAW images, or export/reopen of
generated patches.
