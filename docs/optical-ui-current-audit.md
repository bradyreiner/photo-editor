# Current redesign audit

The objective remains a professional precision creative tool: restrained neutral structure, coherent typography and controls, efficient layouts, and deliberate RGB/CMY optical moments. This audit supersedes historical next-step lists in the implementation log; it does not replace the original scope.

Evidence inspected: current QML, `native-panel-geometry/receipt.json` (25 captures), `native-current-photo/receipt.json` (23 captures, authorized RAW), `native-optical-facet-aligned/receipt.json` (24 captures plus motion frames), and the focused interaction results recorded in the implementation log. Paths below are relative to `docs/evidence/optical-ui-2026-09-19/`.

| Requirement | Current evidence and assessment | Remaining action |
|---|---|---|
| Cohesive neutral surfaces, typography, control states | Shared Theme, ActionButton, MiniButton, fields, and GlassSurface now use neutral colors, compact geometry, consistent primary states, and UI typography. Native compact screenshots confirm the shared appearance. | Audit remaining custom containers in the named workspaces; Export empty and completed-job surfaces now follow the shared neutral hierarchy. |
| Edit hierarchy and photograph | `native-current-photo/01-edit.png` shows the large image stage, one Adjust heading, ordered Light controls, compact White Balance, and neutral surround. Later panel/primary changes are covered by generated-fixture captures. | Include current styling in final real-photo review after remaining changes. |
| Filmstrip density and navigation | `native-panel-geometry` verifies mixed-aspect fixture thumbnails, positioning to the last item, loading and pointer selection. Tight spacing is visually confirmed. | Verify wheel-driven scrolling; programmatic positioning is not wheel-input evidence. Large-catalog benchmarking is not inferred from 24 fixtures. |
| Selection workflow | Current real-photo selection capture shows one contextual toolbar, bounded tool palette, organized refinement, and clear mask enable/inversion states. Native fixture checks verify mask enable and lock behavior. | Verify commit/cancel/history using rendered selection state. Imported synthetic alpha with reset refinement is not default AI refinement proof. |
| Compare layout and titles | Real-photo side-by-side and linked comparison captures; driver verifies automatic snapshot title/active source and keyboard bypass menu. Section comparison gesture test passes. | Verify wipe dragging and external-reference labels in the current UI. |
| Histogram and scopes | Shared dock content, readable chart grid, explicit domain/status, settings popovers, and logarithmic toggle are covered by native captures and driver checks. | Final visual review with photograph; do not conflate this UI scope with numerical engine validation. |
| Library hierarchy | Native captures show one rating row, compact filters, grouped metadata, and actions. Metadata no longer uses invented camera/lens fallbacks. | Final cross-screen review; no separate catalog migration or tagging feature is required by this redesign. |
| Export delivery workflow | Native captures show preview, resolved format summary, Color/Crop row, destination, preflight, and distinct primary action. `native-delivery-current/04d-real-export-completion.png` verifies a current actual generated-fixture export and one completion effect. | Empty guidance is flat and wraps; current actual delivery passes. Completed-job progress styling is corrected in `native-delivery-row-final`. Collision preservation was not re-tested by this fresh-destination export. |
| Optical completion | Facet animation replaces bars/waves; recorded frames show separation, convergence, and light/dark resolution. Native receipts verify finite completion, reduced motion, no idle paints, and drawing-surface release. | Check frame timing without screenshot capture overhead. Final real-photo placement review. Physical caustic/ink simulation is not claimed. |
| Remaining workspace consistency | Current Carousel and Timeline captures confirm compact controls, reserved Carousel navigation, corrected Source monitor layout, and shared primary actions. Scene entry follows shared styling. | Review representative open menus/dialogs and populated Scene; entry view alone is insufficient for the latter. |
| Verification | Production and driver builds, native captures, focused source checks, adjustment gestures, comparison gestures, retouch action cancellation, and tool-exit handler checks pass. | Close the specific remaining interaction/rendering gaps above. Do not rerun unrelated suites without a relevant change or failure. |

## Next implementation batch

1. Export empty/completed states and current generated-media delivery are verified; retain their behavior during the remaining interaction checks.
2. Add native pointer-driven wipe and wheel-driven filmstrip checks to the workflow driver, preserving existing checks.
3. Exercise actual generated-fixture delivery and rendered selection history, then run one final current RAW visual review.
4. Review representative dialogs and a populated Scene fixture; measure the finite motion separately from capture overhead.

Completion is not yet established. This is a bounded list of implementation and proof gaps, not a request for another preference decision or an invented approval gate.
