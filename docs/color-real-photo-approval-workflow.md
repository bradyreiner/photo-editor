# Real Photo Color Approval Workflow

Mathematical color tests are required, but they do not replace editorial review.
Use this workflow when adding representative real-photo fixtures or approving
major color-pipeline changes.

## Fixture Requirements

- Cover at least portrait skin, landscape sky/foliage, night recovery,
  monochrome/high-contrast, saturated color, RAW/input color, and wide-gamut
  export cases.
- Record source license, redistribution permission, author/source URL when
  applicable, camera/profile metadata, and checksum before using the image in a
  repeatable gate.
- Store approved rendered outputs in the repository only when the source license
  permits redistribution of both the source and derivative output.
- If redistribution is not permitted, keep the fixture as a local approval asset
  and store only the manifest entry, checksum, recipe, reviewer, and pass/fail
  notes.

## Approval Gate

1. Render the candidate recipe through preview and final export paths.
2. Compare against the existing math gates: chart tolerance, profile transform,
   preview/export parity, gamut diagnostics, and output-path parity.
3. Review the rendered output for plausible neutral balance, skin, sky,
   saturated colors, shadow recovery, highlight rolloff, and unwanted hue shifts.
4. Record the approved recipe, output profile, bit depth, backend/fallback
   evidence, reviewer, date, and license status.
5. Promote the fixture to CI only after licensing and deterministic thresholds
   are reviewed together.

## CI Promotion

Promoted real-photo fixtures must have deterministic thresholds. Prefer a
compact golden hash plus targeted behavior checks, mirroring
`render_golden_test`, instead of storing large unbounded reference images.
