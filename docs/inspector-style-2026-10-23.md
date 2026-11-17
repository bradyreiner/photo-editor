# Inspector and input style enforcement

The [application style contract](ui-style-contract.md) now includes input fields,
slider rows, numeric typography, and inspector sections. These are enforced by
the same CI source gate and evaluated application audit used for popups.

## Changes

- Text fields, text areas, numeric steppers, and dropdowns share
  `InputFieldSurface`: neutral fill, 3 px corners, fill-based hover, and an outline
  only for focus or high contrast. Inline entries retain a transparent resting
  surface. Existing custom field backgrounds were migrated to the shared owner.
- Numeric text fields explicitly opt into shared mono typography. The source
  gate rejects numeric validators/hints without that opt-in and rejects custom
  backgrounds or use-site radius/border/palette overrides.
- Slider value/reset columns are 80/28 px at 100% scale and scale with the UI.
  Their positions remain fixed as values change. Narrow rows and long labels
  use a full-width track below a wrapping label/value header; labels retain their
  normal font size. Compact Edit rows now respond to density.
- Inspector titles wrap instead of shrinking. Section backgrounds remain flat,
  transparent, square, and borderless. Nested disclosures are keyboard
  accessible with visible focus, and their icons scale with the interface.
- Shared StudioPanel surfaces no longer draw a resting outline. High contrast
  retains a structural border. Selected compare panes use a neutral fill change
  instead of a local border-color override. Runtime and source gates cover both
  panel states, and a deliberately bordered replacement must be rejected.
- Tone Curve point fields fit the supported `255.000` precision. The existing
  curve interaction test now checks that capacity in both layouts.
- The evaluated application audit checks shared input surfaces, slider columns,
  displayed value/label fit, and flat sections. Real Edit captures execute that
  audit before accepting the frame. Source/binary checks are independent of the
  human-readable contract-description strings.

## Validation

Evidence is in `docs/evidence/inspector-style-2026-10-23`.

- The desktop application and focused test targets build successfully. An
  intermediate full app rebuild reported the existing Qt-generated integer
  conversion warning in ExportView; the final incremental builds were clean.
- `inspector_style_policy_test` passes 36 density/scale/width/contrast
  combinations. It checks readable labels and values, stable column positions,
  adequate tracks, painted input edges, visible focus without resizing, precise
  typed values, invalid-input preservation, keyboard reset, and disclosure.
  Deliberately unscaled columns and rounded input replacements are rejected.
- Eleven unique focused tests pass across their final runs, including exposure
  interactions, curve interactions, field/stepper/dropdown behavior, shared
  accessibility, both runtime style suites, the source audit, and audit
  regression cases. An initial exposure launch failed because its CTest entry
  omitted a display backend; it now explicitly uses offscreen rendering and its
  corrected run passes. The initial log is retained alongside that rerun.
- The source audit passes for 289 production QML files, covering 325 input
  declarations, 179 slider declarations, and 70 inspector section declarations
  through their shared families. Fourteen audit regression tests pass. Both
  source checks also pass through the normal headless CTest preset.
- The native Cocoa Edit/color-controls test passes on a real photographic
  fixture. It verifies Tone Curve before Levels, open mixer/grading sections,
  absence of the mixer XY pad, the compact grading wheel, actual numeric edits,
  and preview/renderer behaviors already covered by that fixture. Mixer, curve,
  and grading captures were visually inspected.
- `git diff --check` passes. The UI Primitives catalog includes the new shared
  input surface.

This qualifies the shared input/inspector rules and the exercised native Edit
flow. It does not qualify every workspace composition, arbitrary camera media,
full-resolution render settlement, Windows execution, or general release
readiness. Unrelated concurrent Timeline/Edit changes were preserved; hashes
record the exact shared dirty-workspace state used for this evidence.
