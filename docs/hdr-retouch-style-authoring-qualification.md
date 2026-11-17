# HDR retouch and style authoring qualification

This continues [APP-01](app-improvement-plan.md#app-01--hdr-safe-retouch) and
[PS-PRO-004](photoshop-professional-tools-roadmap.md#ps-pro-004--shared-layer-styles).
Native app testing remains deferred at the user's request.

## HDR healing

Done for the automated correction means a regression fails on the prior renderer
and passes after the fix. Signed and above-one scene RGB must survive separated
healing, removal reconstruction and baked patch matching. Texture/color controls,
coverage and confidence remain bounded. Alpha and unselected pixels must remain
unchanged. Exercise blend endpoints/intermediate values, empty/full/partial mattes,
and zero/partial/opaque alpha.

An asset-backed generated float EXR must retain these semantics before output
mapping in Interactive and Final renders. Compare physically cropped/full-source
output, cold/warm preview caches, undo/redo and canonical recipe reopen. Validate
ordinary controller PNG export separately from an engine-writer float EXR
round-trip. The latter does not establish EXR support in the app export dialog.
Persistent desktop preview identities must invalidate pre-fix renders.

Representative camera/HDR photographs, transformed nested retouch, native display,
large documents and Release performance remain additional gates for APP-01.

## Gradient and operation-mask authoring

Done for this inspector slice means the actual QML panel exposes solid/gradient
stroke fills and add/remove/reposition/color controls for gradient stops. Endpoints
stay at 0 and 1, stops stay strictly ordered, and stop count is bounded. Inserting
a stop interpolates the existing authored curve, including reversed gradients.
Each accepted edit has coherent undo/redo; invalid edits leave state unchanged.
Color dialogs must reject a changed layer, lock or stale stop target.

A style mask copies the current working selection into independent mask/source
identities in one undo step. Create, edit, replace and clear are visible actions.
Selecting a style or effect mask enters the actual layer-mask paint target;
painting and erasing change that mask without changing artwork or the working
selection. Layer/ancestor/source locks and shared consumers remain protected.
Operation masks never become ordinary layer coverage masks. Clearing or replacing
one reference preserves masks still used elsewhere. Reopen, ordinary export and
preset dependency checks must preserve the accepted definitions.

Exercise production QML at compact and ordinary inspector widths with a real
controller. Offscreen pointer tests are separate from native presentation proof.
The later [canvas and lighting slice](canvas-style-and-compound-mask-qualification.md)
implements canvas style handles and document-wide linked lighting with scoped
offscreen/controller evidence. Native Main interactions, Retina/tablet use and
measured responsiveness remain open PS-PRO-004 gates.

## Evidence

The [scoped Debug receipt](evidence/hdr-retouch-style-authoring-2026-11-02/README.md)
records 51 passed distinct checks, the successful desktop app build, 12 offscreen
inspector captures, the pre-fix failing regression, and final source/binary hashes.
It documents the corrected UI fixture and targeted reruns separately. A build or
a visible control alone does not close either full roadmap item.
