# Gaussian freehand cleanup — October 25, 2026

Scene's Clean scene panel now offers Rectangle and Lasso shapes for removing an
area or retaining its contents. Lasso closes the traced outline on release,
previews an even-odd fill, and supports concave and self-crossing outlines.
Escape cancels a held stroke. Drawing uses the displayed image's letterboxing
and current camera/lens framing. Camera-path playback stops when cleanup is
armed; changing the viewport size cancels an active gesture.

By default selection operates on projected Gaussian centers at every depth. It does not
select only visible surfaces or trim the tails of individual Gaussians. Existing
hidden points stay hidden. Lasso uses the same asynchronous cleanup, persistent
artifact-bound visibility mask, twenty-state session undo/redo, restore-all,
exact original-row PLY export and frozen photo/film cleanup as rectangle editing.
The shape choice itself is not restored across app launches.

The outline is bounded to 512 vertices, with pointer sampling decimated as
needed. The native API rejects malformed, nonnumeric, nonfinite, out-of-image
and collinear outlines. The worker freezes the camera and outline before
selection and checks cancellation while scanning the original points.

## Verification

`gaussian_scene_cleanup_test` covers concavity, winding/closure, outline points,
self-crossings, degenerate input, focal scaling, existing hidden points and
cancellation. `gaussian_scene_lasso_ui_test` uses actual native shape selection
and pointer strokes, including Escape cancellation and compact-window keep
input, then verifies undo/redo, PLY export, photo provenance, reopening and
restore-all.

The small native fixture passes under
`/private/tmp/looksy-gaussian-lasso-ui-v3/`. Earlier v1/v2 failures remain: QML
point value references initially caused earlier vertices to move with the
current pointer. Storing plain numeric coordinate snapshots fixes the gesture.

The dense Cocoa/Metal run also passes:
`/private/tmp/looksy-gaussian-lasso-dense-ui-v1/receipt.json`. It begins with
1,244,819 original splats and retains 1,173,194 after the tested concave removal.
Native shape choice, malformed rejection, Escape, undo/redo, exact export,
reopening, compact keep-lasso and restore-all all pass. The dense marquee image
was inspected and shows the intended concave outline and translucent fill.

This proves the SceneWorkspace cleanup workflow. It does not certify integrated
Main lifecycle, sustained interaction performance, depth-aware selection,
component transforms, external renderer interchange or photographic quality.

## Camera-relative depth ranges

Both shapes now offer All depths (the default) and Depth range. Near and Far
specify camera-forward distances in reconstruction units. Remove affects only
centers inside both the outline and interval. Keep retains only that volume;
centers outside the depth interval are removed along with centers outside the
outline. Interval boundaries are included, points behind the camera are never
inside, and previously hidden points cannot be resurrected.

Invalid or reversed intervals disable the draw buttons and are rejected by the
native API. Camera and range are frozen for the asynchronous operation. The
result uses the existing mask, history, persistence and exact export path. Range
settings themselves are session controls, not a persisted camera-space clip.
Moving the camera after cleanup does not reevaluate or change the saved mask.
This is explicit volume selection, not automatic visible-surface segmentation.

The unit test separates coincident near/middle/far centers, checks inclusive
boundaries, camera translation/rotation and invalid ranges. The native depth
workflow passes with an independent four-layer fixture: native depth mode and
field input, invalid interval/API rejection, rectangle removal preserving the
far layers, undo/redo, reopening, compact lasso keep preserving only the near
layers, and PLY export. Both saved masks are checked against the explicit
expected layer identities. Compact controls and drawing captures were inspected.

Evidence: `/private/tmp/looksy-depth-cleanup-ui-final-v1/receipt.json` and
`/private/tmp/looksy-capture-depth-final-ctest-v1.log` (11/11 focused capture and
cleanup checks). Earlier depth UI failures remain under
`/private/tmp/looksy-depth-cleanup-ui-v1` through `looksy-depth-cleanup-ui-v5`:
the fixture needed to explicitly reopen the panel after project restoration
before resizing, and to wait for native menu activation. Export completion now
waits for the actual cleanup worker and published receipt, rather than a
nonexistent status property. The final app builds successfully. These fixtures
do not establish dense depth-range performance or automatic surface selection.

After the depth controls were added, the all-depth dense lasso workflow was
repeated successfully under `/private/tmp/looksy-depth-lasso-dense-ui-v1/`,
retaining the same 1,173,194 of 1,244,819 splats. The final depth overlay shows
the active Near/Far values while drawing, including in the inspected compact
capture. Its bounded selection still requires an explicit range rather than
inferring the visible surface.

## Dense depth-range workflow

`gaussian_scene_cleanup_ui_test --depth-lasso --project ... --proof-dir ...`
adds a real-scan depth-range proof to the existing native workflow. A private
copy of the 1,244,819-splat reference project uses a far bound of 5.002651 scene
units, selected from the positive original-center depths. Native menu/field
input arms the actual concave lasso. The removal retains 1,203,946 originals;
the test checks every original index and confirms all out-of-range and
behind-camera points remain visible. Compact keep-lasso then retains 54,753
points and verifies that every retained center lies within the range.

Escape cancellation, undo/redo, exact PLY export, photo provenance, reopening
and restore-all also pass. Evidence:
`/private/tmp/looksy-dense-depth-lasso-ui-v2/receipt.json`. Dense drawing and
compact result captures were inspected. The first proof attempt failed because
the test loader omitted its mandatory artifact hash; the second uses the hash
from the actual source-project validation, retaining the integrity check.

This closes a dense functional depth-range check, not sustained foreground
latency, automatic visible-surface segmentation or external-renderer interchange.

An independent binary audit also checks the actual dense depth export, using
its published mask rather than reconstructing the selection algorithm. All
1,203,946 retained 248-byte rows equal their original rows, including every SH
coefficient and geometry property. The header changes only the vertex count;
source/export/mask hashes match their receipts and the source remains intact.
Evidence: `looksy-dense-depth-lasso-ui-v2/export-byte-audit.json` under
`/private/tmp`. The four existing cleanup regressions pass after the added dense
proof mode: `/private/tmp/looksy-dense-depth-regressions-v1.log`.
