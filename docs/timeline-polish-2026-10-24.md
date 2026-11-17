# Timeline and keyframe polish

This builds on the [interaction fixes](timeline-depth-2026-10-24.md) and
[export/cleanup follow-up](timeline-readiness-2026-10-24.md). The changes focus
on the editing surface, keyboard behavior, and usable layouts at smaller sizes.

## Editing surface

The Timeline toolbar shows six primary editing tools when space is constrained.
Track selection, trim, roll, slip, slide, and rate stretch remain available in a
precision-tools menu, with configured shortcuts and concrete disabled reasons.
The menu identifies an active precision tool by its icon and active state.
Larger toolbars retain all thirteen direct editing tools. The compact path
preserves a single row at the checked laptop widths.

The curve editor puts channel selection and graph mode first. Previous/next
keyframe, fit, zoom, add, and Actions follow in a compact toolbar. Clearing an
entire curve moves into Actions in compact panels. Time and value inputs share
one row; in the smallest panels, graph modes use icons and the labels shorten
without hiding the numeric inputs. Repeated target status disappears when the
panel is especially short. The graph minimum height also adapts.

The plot has a separate value-label gutter, major and minor time grid lines,
a subtle area fill, adaptive time ticks, and hollow versus solid keyframe
markers. Offscreen keys stay outside the visible plot. Time-range changes now
repaint the curve as well as moving its points and labels.

A small overview appears when the panel has enough height. Dragging its range
pans the graph, dragging either edge changes the time range, and double-clicking
restores the full clip. These actions do not alter the document.

The animation Inspector keeps its selected-channel summary and graph actions
on one row. Its additional controls menu fits beside the primary tabs at common
panel widths. This gives the channel list more usable vertical space.

## Precision interactions

- Drag an empty graph region to box-select keys. Shift, Control, or Command
  adds to the selection; modifier-clicking an individual key toggles it.
- Left/Right nudges the selection by one authored frame. Shift uses ten frames.
  A time nudge is rejected if it would leave the clip or collide with an
  unselected keyframe.
- Up/Down adjusts selected values. Percentage controls use one percentage
  point, gain uses 0.1 dB, rotations use one degree, and source time uses one
  frame. Other parameters use a bounded fraction of their range. Shift uses
  ten times the normal step.
- Command/Control+A selects keys. Command/Control+C, X, and V use the keyframe
  clipboard while the graph has focus; paste inserts at the playhead. Retime
  curves retain their existing clipboard restriction. Numeric text fields
  retain their text-editing shortcuts.
- Delete/Backspace deletes the key selection. Escape clears selection. Previous
  and next controls select and seek actual keys. Shift-drag constrains a point
  gesture to time or value according to its dominant movement.

Graph shortcut handling prevents global clip deletion, clip paste, or frame
stepping from taking over these focused editing operations. Pointer gestures
retain their stable owner and live publication.

The new selection check exposed a backend Undo bug: individual clip and track
key removals always pushed snapshots inside a continuous edit. They now honor
that edit scope, so deleting several keys restores as one Undo. The focused
controller regression verifies restoration of weighted tangents for both clip
and track curves, and Redo for the track group.

## Verification

Evidence is retained under [timeline-polish-2026-10-24](evidence/timeline-polish-2026-10-24/).
The app and focused targets built successfully. Two new Qt-generated keyboard
handler warnings were removed by using boolean membership checks. The existing
generated ExportView integer-conversion warning remains. The final build/run
receipts and source/executable/capture hashes are recorded
in [verification.json](evidence/timeline-polish-2026-10-24/verification.json).

Eight focused checks passed: UI consistency, tool palette, panel docking, shared
clip/track curves, keyframe parameter coverage, color-grade curves, effect
keyframe rendering, and runtime style policy. The palette check opens the actual
precision menu and activates Rate Stretch using keyboard navigation. Both source inventory/style regression checks also passed. The source
style audit reports no violations across 294 QML files.

The final native Main interaction check passed with 13 captures. It exercises live point dragging, one-step Undo,
box selection without a document change, one-frame timing changes to both keys,
coarse value changes, selection deletion/Undo, graph-local clipboard shortcuts,
previous/next destinations, and numeric controls after resizing from 1366 x 900
to 1100 x 760. In the resized fixture the actual graph panel is only 264 pixels
wide and 298 pixels tall. The larger layout then checks overview edge zoom and
range panning without editing the document, plus context menus in both bottom
and side docks. The existing crop, Color/filter Program-pixel, and panel-drag
checks remain part of the combined run.

The separate final native Inspector check passed with six captures. It covers channel/effect targeting, pin/follow,
locked/deleted targets, resizing, persistence, and detaching the graph. Its
captures include the docked and detached views.

The focused native checks can be repeated with the existing driver (use a new
output directory so earlier evidence remains intact):

```sh
QT_QPA_PLATFORM=cocoa PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1 PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1 \
  build/release-macos/tests/actual_app_workflow_benchmark_driver \
  --timeline-ui-proof-dir /tmp/looksy-keyframe-review \
  --timeline-depth-proof-only \
  --source "$PWD/tests/fixtures/creative-tools/landscape-mountain-source.jpg"
```

The Inspector variant uses `--timeline-inspector-animation-proof-only` in place
of the depth flag. The driver supplies its own isolated test runtime.

Reviewed views:

- [Compact graph](evidence/timeline-polish-2026-10-24/native-keyboard-final/timeline-keyframe-narrow-dock.png)
- [Graph overview](evidence/timeline-polish-2026-10-24/native-keyboard-final/timeline-keyframe-overview-zoom.png)
- [Box selection](evidence/timeline-polish-2026-10-24/native-keyboard-final/timeline-keyframe-box-selection.png)
- [Animation properties](evidence/timeline-polish-2026-10-24/native-keyboard-final/timeline-animation-properties.png)
- [Graph context menu](evidence/timeline-polish-2026-10-24/native-keyboard-final/timeline-keyframe-context-menu.png)

Intermediate failures remain with the evidence. They include the original
multi-key deletion Undo failure and narrow-panel overflow. One later replay
produced no rendered-frame capture and then failed an early crop interaction;
its log remains in `native-frame-timeout`. The final replay passed that path
without changing the capture timeout or interaction waits. The combined proof
now explicitly reopens the Inspector after moving the graph into its dock;
closing a tab does not promise which neighboring tab will be exposed. Popup
checks wait for the previous menu to finish dismissing before sending a new
context-menu gesture.

This is scoped work in a checkout containing substantial pre-existing changes.
It does not qualify sustained playback latency, startup speed, audible output,
every parameter/mode at every window size, or expanded spatial-filter export
placements. The existing export restrictions and broader readiness gates remain
as documented in the earlier follow-up. No project schema changed.
