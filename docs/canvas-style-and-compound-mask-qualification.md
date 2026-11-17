# Canvas styles, document light and compound masking

This is the next authorized slice of
[PS-PRO-004](photoshop-professional-tools-roadmap.md#ps-pro-004--shared-layer-styles)
and the existing selection/layer workflow contract. Native app testing remains
deferred. Offscreen gestures, CPU output and persistence are separate from native
presentation, tablet/Retina use and measured performance.

## Canvas authoring

Done means the actual inspector exposes Edit on canvas for drop/inner shadows,
gradient overlays and gradient strokes. Handles correspond to renderer content
pixels, document-pixel style scales and the full parent/final geometry chain.
Cropping, parent transforms and anisotropic source-area scaling must not turn
screen movement into incorrect authored distances or angles.

Dragging provides a live preview without changing the saved recipe or history.
Release commits one undo step; Escape, pointer cancellation, panning, disabled or
locked targets, document/target changes and stale accepted edits discard the
draft. Cancellation preserves independently accepted changes. A shadow that uses
shared light keeps that policy during a drag. Global and local lighting changes
must match the visible result and obey every affected consumer's locks.

Done requires pointer/keyboard tests through production QML and a real controller,
exact recipe/history assertions, preview/committed pixel changes, numeric geometry
round-trips and undo/redo. Pure mock hooks are additional coordinate tests. Source
presence or a fake gesture callback alone does not prove the integration.

## Document lighting

Existing layers remain local until explicitly linked. Linked layers share a
per-document angle; unlinking preserves the current appearance. Independently
edited nested component documents retain their own light. Changing a document
angle validates all affected consumers and commits atomically in one undo step.
Own/ancestor locks, invalid geometry and resource bounds must leave the accepted
state unchanged.

Recipe, adjustment patch, canonical provenance and nested document formats must
explicitly negotiate these new semantics. Configured document lighting survives
save/reopen even before any layer links to it. Legacy local documents retain their
existing meaning; unsupported linked formats fail closed. Preview, export,
copy/independent components and scoped nested Apply/Cancel must retain the same
accepted definitions.

## Compound journeys

Exercise deterministic subject/object/brush-compatible sources through add,
subtract, intersect and invert, then switch between working selection, ordinary
layer mask, style/effect mask and artwork targets. Verify clipped painting and
erasing on transformed nested owners; cancellation; shared-mask moves and
independent copies; inherited locks and rejected-edit redo preservation; undo,
reopen and ordinary flattened export. Every step names its stable target IDs and
checks unintended artwork/selection mutation.

Synthetic source rasters prove composition and persistence, not semantic AI
segmentation quality. Provider/model/native and large-document stress gates remain
separate. Existing broad interaction checks must continue to pass.

## Implemented contract

The inspector now offers Edit on canvas for drop/inner shadows, gradient overlays
and gradient strokes. Shadow drags retain their shared-light policy. Drafts enter
preview rendering only; saved recipes and history change on accepted release.
Per-document lighting is explicitly linked, and unlinking retains appearance.
Converting linked artwork to a component captures its angle in a separate nested
scope rather than losing the link or changing the look.

New semantics use desktop recipe 20, adjustment patch 4, canonical provenance v7
with `document-layer-style-light-v1`, and nested document v2. Legacy local-only
formats retain their meaning. Canonical render keys include a consumed root
light angle; unused configured angles can change without changing pixel identity,
while saves still persist their updated provenance. Editor and library photo
preview namespaces also advance for the corrected tiled-style crop frame.

Compound qualification exposed and repaired effect-mask creation reading the
inspected mask instead of the independent working selection. Creation now clones
working sources, remaps their identities, and checks source and inherited locks.
Active tiled styles retain their full authored paint frame through a crop.

## Qualification record

The [final scoped Debug receipt](evidence/canvas-style-light-compound-masks-2026-11-02/README.md)
records 57 passed checks, 56 built app/test targets, 77 passed Rust tests with four
explicit integration skips, 21 offscreen captures, exact commands/environments,
named compound journeys and zero drift across 80 source files and 59 binaries.
At that snapshot, the broader UI inventory reported 23 findings; the
[interaction cleanup](mask-layer-ui-qualification.md) addresses those findings
and records the current source/style gate separately. Focused proof remains
separate from native/Release/human acceptance. Full APP-01 and PS-PRO-004 completion
still requires their remaining owner checklist gates.
