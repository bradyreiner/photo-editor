# P19 native group-transform authoring feasibility

## Finding

The editor engine already has static track-group transforms. `TimelineDocumentController` exposes `createTrackGroup`, `setTrackGroup`, `setTrackGroupParent`, and `setTrackGroupTransform` as `Q_INVOKABLE`s in `apps/desktop/src/timeline_document_controller.h` (around lines 892–903); the implementation of `setTrackGroupTransform` in `timeline_document_controller.cpp` updates scale, X/Y offsets, and rotation for every track with the given group ID and pushes an undo snapshot. `TimelineTrackHeader.qml` invokes these methods from the track-group UI. This means grouped transforms are not absent from the product.

They are not available as task-native authoring commands today: the typed `timeline.action` registry in `apps/desktop/src/agent_timeline_commands.h` has no create/assign/transform-group actions, and the generic reflected command allowlist does not include these method names. The current project also has no prepared shared poster group for the CampaignPoster child clips. So the immediate gap is stable-ID, journaled automation for the existing static group contract, plus animated group-transform authoring.

## Existing native approximation

`timeline.action.setTimelineClipPerspectiveQuadAnimation` is already registered and accepts up to512 local-time poses per clip, each with four normalized TL/TR/BR/BL corners; the controller checks that interpolated quad trajectories remain convex. A shared source perspective/rotation/scale can therefore be reproduced in principle by evaluating the source 4×4 transform on each child’s world-space corners at each authored pose, then mapping those transformed corners into the native sequence’s normalized clip-quad coordinates. This must use the shared source origin `(0,0)` and the current child affine transforms. Reusing a single rotation or offset per clip is not equivalent because each clip would rotate about a different pivot.

This is a technically supported path with the current per-clip quad action, not a missing renderer feature. It has not been rendered or A/B qualified here. A future proposal should bind every child clip and source curve to a fresh saved native snapshot, calculate frame-aligned poses only at points required by the shared curve, validate convexity and bounding boxes, and render comparison frames before acceptance. The P19 poster proposal remains unapplied until that calculation and native preview/export review are complete.

## Selected-row state and readout

The selection highlight itself is feasible with current native controls: the corrected panel fragment extends the existing three row backplates and keys their opacity to the source selection switches at local2.5s and4.6s. Static title color cannot be keyed with the current title animation schema, so the selected row’s backing plate carries the active state while the three row titles stay paper-colored. This reproduces a visible selection change and does not misstate the text-color limitation.

The numeric paint-opacity label can also be reproduced without an engine expression channel: `p19-brush-opacity-v3.commands-fragment.json` reuses the existing title ID for the first run and adds47 title runs on the same existing G32 lane. These run-length encoded text clips match the Remotion `Math.round(product*100)` value at every30fps output frame. The existing native brush vector-stroke opacity curve remains untouched. This is native timeline text, not a live binding to an engine brush control.

## Evidence and guards

- P17 planning snapshot: `native-projects/looksy-polish-pass17-photo-parity.json`, SHA-256 `5e478a67ab4d38a1315aace8f4332b8a605c94ab0b3607333cdd59baebb5f869`, document hash `d6a5023c817f0fe79109e7a6647b87032e5ed8a48de394697ac4f6a11be6c422`.
- P16 visual pairs: `native-4500.png`/`reference-4500.png`, `native-4620.png`/`reference-4620.png`, `native-4770.png`/`reference-4770.png`; these are closed-chunk frames from worker98633, binary `7630662c8d35f37bc10a1b68983bdbae4a736eac7007b236f8e73c4ff2dfa8a3`, P16 document hash `438df6c817d17155464f532f50dffb8cd718718fa4a86c83615e3fa1b342f24a`. They are evidence for the P16 rendered visual only, not a P17 output proof.
- Both P17 fragments are prepared and unbound; they require fresh native task admission, exact snapshot/target rebind, and empty-range/track-state checks before any execution. No project JSON has been directly edited and no production calls were made for this feasibility pass.
