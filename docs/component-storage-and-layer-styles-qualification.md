# Component storage, source fidelity and shared styles

This follows the [professional tools roadmap](photoshop-professional-tools-roadmap.md)
and the [selection and component retouch evidence](evidence/color-range-component-retouch-2026-11-01/README.md).
Native testing is deferred at the user's request. Automated evidence does not
establish native pointer behavior, presentation latency or release qualification.

## Shared component storage

Done means one stored body per content identity and revision, including nested
documents and both legacy recipe aliases. Ordinary duplicates share content;
Make Independent retains a distinct identity. Each placement keeps its own ID,
transform, opacity, masks and authored properties. JSON recipes, adjustment
patches and canonical asset storage must all reopen the same exact bytes.

Conflicting bodies with the same identity/revision, unsupported schemas, missing
objects, cycles and oversized expansion must fail explicitly and atomically.
Existing inline recipes and canonical assets remain readable; new references
carry a version that older readers cannot silently accept. Compare serialized
sizes at 1, 3 and 30 instances; this measures storage growth, not UI performance.

## Embedded RAW and HDR

Done for the automated slice means exact embedded source bytes, authoritative
decoder dimensions/orientation, and float highlight, negative-color and alpha
fidelity through component conversion, Cancel/Apply, one-step undo/redo, reopen
and flattened export. Use authored DNG and float EXR fixtures. Missing decoders,
unavailable sources and document budgets return actionable errors without
replacing the accepted document. Real camera variety, native display and large
project performance remain separate gates.

## Shared layer styles

Done means an ordered, editable style stack for strokes, inner/drop shadows,
inner/outer glow, color/gradient overlays and bevel/emboss on image, text, shape
and group content. Styles have explicit document-pixel scaling, global light,
soft alpha, working-space color and mask/opacity/transform order. Bounds include
generated pixels. Unsupported enabled operations fail rather than disappear.

The inspector must expose add, edit, bypass, reorder, remove and copy/reuse.
Locked layers and inherited locks apply. Changes form coherent undo steps and
preserve original sources. Resize, undo/redo, reopen and export must retain the
stack and match rendered output. Existing text styling remains visually intact;
any migration needs an equivalence check before replacing its old renderer.

## Evidence status

The [final Debug receipt](evidence/component-storage-layer-styles-2026-11-02/README.md)
records **45 passed scoped checks**, a successful desktop app build, source/binary
hashes with no drift, storage-growth measurements and rendered inspector captures.

- Shared object tables and canonical asset storage: automated contract passed at
  1, 3 and 30 placements, including conflicts, nested/group references, legacy
  reads and exact authored payloads.
- RAW/HDR: authored DNG and float EXR subcases passed without skips, including
  source decode, component history/reopen and export.
- Layer Styles first slice: eight operation types, the actual inspector component,
  preset validation, mask ownership, duplicate policies, cache-safe authoritative
  reload, persistence and CPU/export checks passed.
- The [next authoring slice](hdr-retouch-style-authoring-qualification.md) implements
  gradient-stop color/position/add/remove, solid/gradient strokes and independent
  style masks with actual paint/erase targeting, locks and undo ownership.
- The [canvas and lighting slice](canvas-style-and-compound-mask-qualification.md)
  implements shadow/gradient handles, preview-only drafts, document-linked light,
  nested scope preservation and compound mask/layer journeys. See its
  [scoped Debug receipt](evidence/canvas-style-light-compound-masks-2026-11-02/README.md).
- Full-item gates remain open: mounted Main/native style authoring, legacy
  style migration equivalence, large-document performance, tablet/Retina, camera
  variety, packaged Release and human quality review. Native testing is deferred.

The complete monolithic controller harness is separate from these bounded checks;
its final full-harness status remains unqualified. See the receipt for exact scope.
