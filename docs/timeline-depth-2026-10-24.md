# Timeline interaction and controls pass

This continues the space/layout pass with changes to the actual editing paths.
The user's screenshots identified broken keyframe gestures, competing transform
and crop handles, ineffective thumbnail sizing, excessive panel chrome, missing
Edit filters, and File commands occupying the workspace.

## User-visible changes

- Save Timeline, Render Timeline, project settings, collect/portable packaging,
  interchange, and delivery actions are available through File. The Timeline
  header retains a passive saved/unsaved indicator.
- Docked panels use flat surfaces and underline tabs. One options menu owns
  close, float/dock, and move commands. Tabs accept right-click and can be
  dragged between docks, reordered, dropped at workspace edges, or floated.
  The existing layout persistence remains authoritative; transient drag state
  is not serialized.
- Transform handles sit outside the clipped sequence canvas. The viewer fits
  the complete rotated/scaled layer while the tool is active, then restores
  ordinary Program viewing. The fit is frozen during a gesture.
- Crop is a separate viewer tool. For ordinary video/photo clips it displays
  the uncropped original, shades discarded source areas, and supplies ratio,
  reset, and Done controls. It works independently of final Fit/Fill, scale,
  rotation, and placement. The decoder bypasses final composite caches and
  source cropping only during source-crop authoring; the document crop and
  final output remain intact. Scene-attached photos retain their existing
  scene-specific crop path.
- Keyframe points use a stable graph-level pointer owner, publish live changes
  during dragging, frame-snap timing, and group each gesture into one Undo.
  The graph retains its value range after a gesture, provides wheel zoom and
  Shift-wheel pan, Fit clip/keys commands, contextual easing/add/delete, and
  human-readable percent/degree/dB values. Narrow layouts stack controls.
- Animation properties show current values and separate diamond/graph actions.
  Search and grouping remain available; the footer wraps above its actions.
- Color task selection reveals its primary panel even if it was previously
  closed. Exposure is checked against actual decoded Program pixels.
- Effects includes a searchable catalog from the same shared photo-effect
  registry as Edit, with stack order, bypass, amount, reset, and numeric
  parameters. These filters are stored in the existing shared grade recipe on
  video/adjustment clips and use the photo engine's full-frame graph.
  Displacement-map authoring remains unavailable without an image binding;
  the catalog explains that restriction. Locked tracks reject mutations.
- Thumbnail target size now changes actual card geometry continuously between
  column-count changes.

## Evidence and limits

Final build, focused checks, native receipts, captures, and source/binary hashes
are recorded under `docs/evidence/timeline-depth-2026-10-24/`.

The release app built successfully. The final strict Main workflow passed with
nine captures in [native-final-pass](evidence/timeline-depth-2026-10-24/native-final-pass),
including the active-window gate. Its decoded Program mean absolute RGB
change was 25.32 for typed exposure and 29.62 for the shared Invert filter.
The advanced Inspector run passed with six captures in
[native-inspector-verified](evidence/timeline-depth-2026-10-24/native-inspector-verified).

Reviewed views:

- [Animation properties](evidence/timeline-depth-2026-10-24/native-final-pass/timeline-animation-properties.png)
- [Keyframe graph](evidence/timeline-depth-2026-10-24/native-final-pass/timeline-keyframe-live-edit.png)
- [Source crop](evidence/timeline-depth-2026-10-24/native-final-pass/timeline-crop-tool.png)
- [Shared Edit filter](evidence/timeline-depth-2026-10-24/native-final-pass/timeline-shared-edit-filter.png)
- [Panel context menu](evidence/timeline-depth-2026-10-24/native-final-pass/timeline-panel-context-menu.png)

All twelve focused tests passed in an earlier complete run. After the last
presentation changes, eleven passed and `timeline_playback_controller_test`
failed its inactive-workspace resource-release assertion during the rebuild.
Its diagnostics showed one release attempt and no completed release after the
grace period. This repeated-run failure remains a playback qualification limit;
it is not erased by earlier passes. An isolated final rerun passed in 11.39 s;
the inconsistency remains documented in [checks](evidence/timeline-depth-2026-10-24/checks).
The thumbnail test's fixed 180 ms assumption
was replaced with a bounded wait for actual asynchronous image readiness.

The final focused suite exercises the source contract, media browser, panel
pointer dragging/context actions, Color Console, shared filters, transform
workflow, frame scheduling, playback source selection, shared keyframe curves,
color-grade curves, keyframe coverage, and effect rendering. Filter checks use
actual engine pixels, typed amount input, bypass/Undo, invalid parameters,
locked tracks, and save/reopen. Crop checks verify uncropped source pixels
without document mutation, restoration, and cache selection.

The native Inspector run exercises the actual Main workspace with effect
controls, grouped channel search, target synchronization, pin/follow behavior,
retime targeting, locked/deleted targets, edit-scope closure, dock resizing and
persistence, and detaching the same graph. The full interaction proof checks
live publication while the pointer is held, gesture Undo, graph zoom/units,
crop pointer mapping in Fill, Color/filter Program pixels, panel drag round
trips, context menus, and animation action bounds. The 16 ms live-edit timer is
awaited while the mouse remains held; checking immediately after queued move
events was an invalid test timing assumption.

Intermediate failures remain alongside final evidence. Several CLI/native
attempts could not acquire an active macOS window; an inactive diagnostic mode
retains a failing overall receipt unless the window actually activates. A
computer-control attempt also timed out. No focus gate or performance budget
was relaxed. Earlier captures are not substituted for the final source state.

The app-wide source style audit currently reports two existing shared-panel
violations in StudioWorkspace.qml (lines 17471 and 17668), outside this pass.
The app build also retains a generated ExportView QML integer-conversion
warning. These unrelated changes were preserved.

This is a scoped interaction pass in an already dirty checkout. It does not
qualify every filter's preview/export parity, all media formats, sustained 4K
playback, audio output, or overall release readiness. The cold source-QML
Timeline startup failure from the earlier space pass is not resolved here.

Source, executable, receipt, and all fifteen final capture hashes are recorded in
[verification.json](evidence/timeline-depth-2026-10-24/verification.json).
