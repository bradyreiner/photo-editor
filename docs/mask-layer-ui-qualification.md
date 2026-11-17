# Mask and layer interaction cleanup

This follows the [canvas style and compound mask slice](canvas-style-and-compound-mask-qualification.md).
The user approved the interaction/UI cleanup before further tool expansion.
Native app testing remains deferred.

## Completion contract

- The current layer name, artwork/adjustment/mask target and independent working
  selection are readable at compact widths. A visible action returns from mask
  editing to that layer's artwork or adjustments without clearing the selection.
- Invalid or rejected layer switches retain the prior mask, selected layers, tool
  and working selection. Target changes create no artwork history transaction.
- Mask menu labels describe their effect on the working selection. Locked or
  missing owners/sources have precise disabled reasons; callbacks revalidate
  ownership and locks before editing. Removing a mask preserves the selection.
- Existing content/path/component actions remain reachable in the grouped layer
  menu, with no duplicate commands or enabled hidden actions.
- Selection tiles use shared controls, consistent selection/focus surfaces and
  recognizable registered icons. Keyboard selection remains focused and checked;
  selected labels do not change weight or size.
- Numeric fields use the shared UI family with tabular lining figures. Audits
  reject old mono/arbitrary overrides and missing numeric features rather than
  requiring the font the user rejected. Standard inspector columns remain scaled
  and aligned; compact HUD exceptions are explicit.
- Shared tooltips, inputs, choices and command surfaces replace the remaining
  audit violations without removing their behavior. The full QML source gate and
  mutation tests pass, followed by relevant evaluated component/pointer tests.

Done for this automated slice requires the actual production components and
workspace routes, exact target/selection/history assertions, responsive captures,
source/binary hashes and a named result ledger. Source presence, a mock transition
or a zero-violation inventory alone does not prove full application behavior.

## Boundaries

Whole-app native presentation, physical performance, tablet/Retina, packaged
Release and human visual acceptance remain separate. The professional-tools,
[capability](capability-verification-plan.md), [UX](user-experience-improvement-checklist.md)
and [app improvement](app-improvement-plan.md) plans retain their other open gates.
This slice does not complete text-style migration, advanced blending or the Pen tool.

## Evidence

The [scoped Debug receipt](evidence/mask-layer-ui-2026-11-02/README.md) records
the named tests, source/style inventory, rendered component captures, source and
binary hashes, fixes exposed by the workflow tests, and remaining native gates.

The final integrated run passed **31/31** checks, the source/style audit passed
with **0 violations across 349 QML files**, and the source gate mutation suite
passed **24/24**. The receipt records 47 offscreen captures and zero final drift
across 65 source files and 30 binaries. These close this automated cleanup slice;
the native and full-item gates above remain open.
