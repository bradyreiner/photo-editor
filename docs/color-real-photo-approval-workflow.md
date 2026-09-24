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

For agent-edit approval, also review the generated before/after proof pair for
each manifest `agent_cases` entry. Record `human_review.status`, reviewer,
ISO-8601 review time, 1–5 rating, and notes. A passing pixel-direction check only
proves that the requested measurable direction occurred; it does not prove the
edit is attractive, subtle, locally appropriate, or ready to become learned
taste evidence.

### Rendering-migration packet return

An independent rendering-migration reviewer extracts the supplied ZIP, opens
`index.html`, completes every decision, note, and five-score group, attests to
independence, and returns the downloaded `ratings.json`. Do not replace the
packet's pending template manually. Validate and immutably record the returned
file with:

```sh
python3 scripts/color/rendering-migration-review-packet.py record-ratings \
  --packet <packet-directory> \
  --executable <exact-release-executable> \
  --ratings <returned-ratings.json> \
  --output <new-completed-ratings.json>
```

A completed negative review is valid evidence but does not authorize migration.
The release gate requires unanimous approval:

```sh
python3 scripts/color/rendering-migration-review-packet.py validate \
  --packet <packet-directory> \
  --executable <exact-release-executable> \
  --ratings <new-completed-ratings.json> \
  --require-approved
```

`--require-approved` implies completeness, independent-review attestation,
reviewer identity, timezone-aware review time, non-empty notes, and all five
integer scores for every asset. It fails if any decision is `pending`, `revise`,
or `reject`.

### Refresh-chunk evidence gate

After unanimous calibration approval creates a v2 refresh plan, create an
immutable authorization for exactly one named chunk:

```sh
python3 scripts/color/authorize-rendering-migration-refresh-chunk.py \
  --plan <approved-v2-refresh-plan.json> \
  --chunk-index <one-based-index> \
  --executable <exact-release-executable> \
  --output <new-chunk-authorization.json>
```

The native render command requires that authorization and filters the complete
PostgreSQL inventory to its exact asset set before assessment:

```sh
Framehouse \
  --rendering-migration-report <new-chunk-report.json> \
  --rendering-migration-recipe-assets <recipe-assets-directory> \
  --rendering-migration-chunk-authorization <chunk-authorization.json>
```

Unapproved direct rendering is rejected before inventory or pixel work. The
plan-only inventory command remains available without authorization and cannot
render evidence.

Before merging the resulting component report, bind it back to the plan:

```sh
python3 scripts/color/validate-rendering-migration-refresh-chunk.py \
  --plan <approved-v2-refresh-plan.json> \
  --chunk-index <one-based-index> \
  --executable <exact-release-executable> \
  --component-report <chunk-report.json> \
  --output <new-chunk-receipt.json>
```

The validator rejects pre-approval/v1 plans, non-unanimous approval, a different
producer, missing or unexpected assets, blocked/incomplete evidence, artifact
tampering, and missing or extra artifact files. The receipt is owner-only and
create-only. A component report without a valid receipt must not enter the merged
catalog report.

The native merge command enforces that rule. Supply the complete progress/base
report first, then each overlay component, with one receipt for every overlay in
the same order:

```sh
Framehouse \
  --merge-rendering-migration-report <new-merged-report.json> \
  --rendering-migration-component-report <base-progress-report.json> \
  --rendering-migration-component-report <chunk-1-report.json> \
  --rendering-migration-component-receipt <chunk-1-receipt.json>
```

The merge rejects missing/extra receipts, altered receipt identity, a receipt
for another report, producer or asset-set differences, duplicate chunk indices
or assets, and receipts from different plan identities.

## CI Promotion

Promoted real-photo fixtures must have deterministic thresholds. Prefer a
compact golden hash plus targeted behavior checks, mirroring
`render_golden_test`, instead of storing large unbounded reference images.
