# Precision trim tools and inspector polish

This pass continues the desktop Timeline and square-control refinements. Existing unrelated work and user catalogs are preserved.

## Changes

- The visible Trim menu exposes In/Out edge selection, earlier/later adjustments by one or five sequence frames, trimming to the playhead, Roll, and returning to Select. It uses the existing controller editing operations rather than a second trim implementation. Existing keyboard shortcuts remain available.
- The toolbar names the active Trim edge. Returning to another tool clears the armed keyboard edge so later comma/period input cannot accidentally continue a prior trim.
- Selected-track and linked-audio lock checks disable inappropriate menu actions. Controller checks also protect edits made directly through keyboard and timecode paths. Rejected edits must preserve media, linkage, selection, and undo history.
- Moving clips earlier/later or to another track, deleting a clip, and reusing cached proxies no longer read track/clip pointers after track synchronization replaces their storage. Bulk proxy generation snapshots candidate IDs before queuing. These fixes came from allocator-assisted regression failures.
- Filter chips, inspector tabs, and color swatches follow the shared shallow square corners. Inspector selection uses fill and text weight; keyboard focus retains a complete outline.
- Timeline input-color stream metadata and camera-match criteria move into a collapsed Input details disclosure. The selected transform and warnings stay visible, making more space for grading.
- UI-test fixtures retain linked stereo AAC using silent audio instead of a generated tone. User-app and operating-system volume settings are not changed.

## Verification

The production app and native helpers built. Fifteen QML tests, both 49-view density matrices, and the 18-capture native editing workflow passed. PNG dimensions match each manifest and device scale. The dedicated `timeline_precision_trim_workflow_test` passed under allocator diagnostics with a real silent 25 fps A/V fixture; it verifies linked frame edits, asymmetric picture-only editing, lock rejection, Undo preservation, and Trim mode save/reopen. Native pointer proof also verifies the new menu at 30 fps and trim-to-playhead alongside adding media, source preview, edge dragging, linked movement, Undo/Redo, cancellation, changing playback pictures, paused seeks, and save/reopen.

A sample of final rendered views was reviewed, including correcting the clipped Trim Out indicator:

- [Precision Trim menu](evidence/timeline-precision-tools-2026-10-04/native-workflow/02e-trim-out-to-playhead-1440x900.png)
- [Desktop Timeline](evidence/timeline-precision-tools-2026-10-04/density-native/timeline-edit-balanced-1440-1440x900.png)
- [Color inspector](evidence/timeline-precision-tools-2026-10-04/density-native/timeline-color-compact-1440-1440x900.png)
- [Edit controls](evidence/timeline-precision-tools-2026-10-04/density-offscreen/edit-balanced-1440-1440x900.png)
- [Verification receipt and logs](evidence/timeline-precision-tools-2026-10-04/verification.json)

The broader `timeline_document_controller_test` still fails its shared-proxy cleanup assertion. Diagnostics show cleanup safely refuses incomplete saved-timeline inspection; it removes zero files and retains both proxy and original. A fresh explicit cache reproduces the failure, so cache isolation alone does not resolve it. This pass leaves cleanup safety unchanged and records the unresolved fixture/index issue separately from the passing focused trim test.

Native proof uses source application QML and canonical controllers with temporary documents, media, and settings. It does not qualify a packaged release, all app-wide tests, long-sequence or camera-codec playback, audio synchronization, full-resolution color/export parity, physical trackpads, or VoiceOver.
